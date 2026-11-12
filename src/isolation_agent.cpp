#include "isolation.h"
#include "isolation_profile.h"
#include "third_party/minhook/include/MinHook.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
using namespace eotf;
using Microsoft::WRL::ComPtr;

namespace {
using RenderFn = HRESULT(__fastcall*)(void*, void*);
using ShaderFn = HRESULT(__fastcall*)(void*, void*, UINT*, ID3D11PixelShader**);
using CommonFn = HRESULT(__fastcall*)(void*, void*, const void*, UINT*, ID3D11PixelShader**);
RenderFn displayOriginal = nullptr, legacyOriginal = nullptr;
ShaderFn shaderOriginal = nullptr;
CommonFn commonOriginal = nullptr;
BYTE* commonCache = nullptr;
IsolationShared* volatile shared = nullptr;
SRWLOCK contextLock = SRWLOCK_INIT, shaderLock = SRWLOCK_INIT;
ULONG_PTR owners[MaxOutputs] = {};
volatile LONG ownerSequence = -1, activeCalls = 0, agentRunning = 0;
bool initialized = false;
struct Scope { int output = -1; LONG sequence = -1; };
thread_local Scope currentScope;
const GUID VariantGuid = {0xa5b47922,0xa14f,0x4179,{0x91,0x64,0x15,0x12,0x44,0x61,0x06,0x31}};
const GUID CheckedGuid = {0x269e37d7,0x4a70,0x4bbb,{0xb8,0x52,0x71,0xb5,0x6c,0x67,0xf4,0xa8}};
GUID CurveGuid(GUID guid, TransferCurve curve) {
    // Include protocol and curve so old DLLs and other modes cannot share a variant.
    guid.Data1 ^= IsolationProtocol; guid.Data4[7] ^= (BYTE)curve; return guid;
}
struct Checked { DWORD protocol; BOOL supported; };
struct Active { Active() { InterlockedIncrement(&activeCalls); } ~Active() { InterlockedDecrement(&activeCalls); } };
bool SafeRead(const void* source, void* dest, size_t n) noexcept {
    __try { memcpy(dest, source, n); return true; } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadShaderBlob(void* linked, const BYTE** p, size_t* size) noexcept {
    __try {
        auto owner = *(BYTE**)((BYTE*)linked + 0x28);
        auto blob = *(ID3DBlob**)(owner + 0x10);
        *size = blob->GetBufferSize(); *p = (BYTE*)blob->GetBufferPointer();
        return *p && *size >= 32 && *size <= 4 * 1024 * 1024;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadConfig(IsolationShared* s, IsolationConfig& c, LONG& sequence) {
    sequence = InterlockedCompareExchange(&s->configSequence, 0, 0);
    if (sequence & 1) return false;
    memcpy(&c, &s->config, sizeof(c)); MemoryBarrier();
    return sequence == InterlockedCompareExchange(&s->configSequence,0,0) && ValidOutputConfig(c);
}
Scope Identify(void* context, size_t offset) {
    auto s = shared; Scope result; if (!s) return result;
    IsolationConfig config; LONG sequence;
    if (!ReadConfig(s, config, sequence)) return result;
    float bounds[4]; if (!SafeRead((BYTE*)context + offset, bounds, sizeof(bounds))) return result;
    int index = MatchOutput(config, bounds); if (index < 0) return result;
    AcquireSRWLockExclusive(&contextLock);
    if (ownerSequence != sequence) {
        ZeroMemory(owners, sizeof(owners)); ownerSequence = sequence;
        InterlockedExchange(&s->stats.contextsMask,0); InterlockedExchange(&s->stats.mappingConflict,0);
    }
    if (owners[index] && owners[index] != (ULONG_PTR)context) {
        InterlockedExchange(&s->stats.mappingConflict,1); InterlockedExchange(&s->enabled,0);
    } else {
        owners[index] = (ULONG_PTR)context; InterlockedOr(&s->stats.contextsMask,1L << index);
        result.output = index; result.sequence = sequence; InterlockedIncrement64(&s->stats.frames[index]);
    }
    ReleaseSRWLockExclusive(&contextLock); return result;
}
ID3D11PixelShader* Variant(void* linked, ID3D11PixelShader* original, IsolationShared* s, TransferCurve curve) {
    const GUID variantGuid = CurveGuid(VariantGuid,curve), checkedGuid = CurveGuid(CheckedGuid,curve);
    ID3D11PixelShader* result = nullptr; UINT size = sizeof(result);
    if (SUCCEEDED(original->GetPrivateData(variantGuid,&size,&result)) && result) { result->Release(); return result; }
    Checked checked = {}; size = sizeof(checked);
    if (SUCCEEDED(original->GetPrivateData(checkedGuid,&size,&checked)) && checked.protocol == IsolationProtocol && !checked.supported) return nullptr;
    if (!TryAcquireSRWLockExclusive(&shaderLock)) return nullptr;
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&shaderLock); } } unlock;
    size = sizeof(result);
    if (SUCCEEDED(original->GetPrivateData(variantGuid,&size,&result)) && result) { result->Release(); return result; }
    const BYTE* source = nullptr; size_t length = 0;
    Bytes data, variant;
    bool good = ReadShaderBlob(linked,&source,&length);
    if (good) { data.resize(length); good = SafeRead(source,data.data(),length) && MakeIsolatedShader(data.data(),data.size(),variant,curve); }
    checked = {IsolationProtocol,FALSE};
    if (!good) { original->SetPrivateData(checkedGuid,sizeof(checked),&checked); return nullptr; }
    ComPtr<ID3D11Device> device; original->GetDevice(&device);
    ComPtr<ID3D11PixelShader> replacement;
    if (!device || FAILED(device->CreatePixelShader(variant.data(),variant.size(),nullptr,&replacement))) return nullptr;
    if (FAILED(original->SetPrivateDataInterface(variantGuid,replacement.Get()))) return nullptr;
    // The original DWM shader owns the variant. We retain no device references outside DWM's lifecycle.
    checked.supported = TRUE; original->SetPrivateData(checkedGuid,sizeof(checked),&checked);
    InterlockedIncrement64(&s->stats.variants); return replacement.Get();
}
void* FindLinkedByShader(ID3D11PixelShader* shader) {
    if(!commonCache)return nullptr;
    void** entries=nullptr;LONG count=0;
    if(!SafeRead(commonCache+8,&entries,sizeof(entries))||!SafeRead(commonCache+0x10,&count,sizeof(count))||count<0||count>4096)return nullptr;
    for(LONG i=0;i<count;++i){
        BYTE* linked=nullptr;if(!SafeRead(entries+i,&linked,sizeof(linked))||!linked)continue;
        void** begin=nullptr;void** end=nullptr;
        if(!SafeRead(linked+0x30,&begin,sizeof(begin))||!SafeRead(linked+0x38,&end,sizeof(end)))continue;
        uintptr_t first=(uintptr_t)begin,last=(uintptr_t)end;
        if(last<first||(last-first)%sizeof(void*)||last-first>1024*sizeof(void*))continue;
        for(uintptr_t entry=first;entry<last;entry+=sizeof(void*)){
            BYTE* wrapper=nullptr;ID3D11PixelShader* pixel=nullptr;
            if(SafeRead((void*)entry,&wrapper,sizeof(wrapper))&&wrapper&&SafeRead(wrapper+0x78,&pixel,sizeof(pixel))&&pixel==shader)return linked;
        }
    }
    return nullptr;
}
bool InspectCandidate(void* linked,ID3D11PixelShader* shader,TransferCurve curve) {
    const GUID checkedGuid = CurveGuid(CheckedGuid,curve);
    Checked checked={};UINT size=sizeof(checked);
    if(SUCCEEDED(shader->GetPrivateData(checkedGuid,&size,&checked))&&checked.protocol==IsolationProtocol)return checked.supported!=FALSE;
    const BYTE* source=nullptr;size_t length=0;if(!ReadShaderBlob(linked,&source,&length))return false;
    Bytes raw(length),changed;bool ok=SafeRead(source,raw.data(),length)&&MakeIsolatedShader(raw.data(),raw.size(),changed,curve);
    checked={IsolationProtocol,ok?TRUE:FALSE};shader->SetPrivateData(checkedGuid,sizeof(checked),&checked);return ok;
}
void SetMessage(IsolationShared* s, const wchar_t* message, LONG state) {
    wcsncpy_s(s->detail,message,_TRUNCATE); MemoryBarrier(); InterlockedExchange(&s->state,state);
}
}
extern "C" HRESULT DisplayRenderHook(void* self, void* frame) {
    Active active; Scope previous = currentScope; currentScope = Identify(self,DisplayRectOffset);
    HRESULT hr = displayOriginal(self,frame); currentScope = previous; return hr;
}
extern "C" HRESULT LegacyRenderHook(void* self, void* frame) {
    Active active; Scope previous = currentScope; currentScope = Identify(self,LegacyRectOffset);
    HRESULT hr = legacyOriginal(self,frame); currentScope = previous; return hr;
}
extern "C" HRESULT LinkedShaderHook(void* self, void* device, UINT* vs, ID3D11PixelShader** output) {
    Active active; HRESULT hr = shaderOriginal(self,device,vs,output);
    auto s = shared; if (FAILED(hr) || !s || !output || !*output) return hr;
    int i = currentScope.output;
    if (i < 0 || i >= (int)MaxOutputs) { InterlockedIncrement64(&s->stats.unscoped); return hr; }
    InterlockedIncrement64(&s->stats.shaderCalls[i]);
    IsolationConfig config; LONG sequence;
    if (!ReadConfig(s,config,sequence) || sequence != currentScope.sequence) return hr;
    bool complete = !s->stats.mappingConflict && s->stats.contextsMask == ((1L << config.count) - 1);
    if (!ShouldUseGamma(config,i,complete,s->enabled != 0)) {
        if (!config.outputs[i].hdr) InterlockedIncrement64(&s->stats.sdrBypasses[i]);
        return hr;
    }
    try {
        auto variant = Variant(self,*output,s,config.curve);
        if (variant && s->enabled && sequence == s->configSequence) { *output = variant; InterlockedIncrement64(&s->stats.replacements[i]); InterlockedExchange(&s->stats.appliedSequence,sequence); }
        else InterlockedIncrement64(&s->stats.unsupported[i]);
    } catch (...) { InterlockedExchange(&s->enabled,0); SetMessage(s,L"着色器变体创建失败，已恢复原路径",3); }
    return hr;
}
extern "C" HRESULT CommonShaderHook(void* self,void* device,const void* desc,UINT* vertex,ID3D11PixelShader** output){
    Active active;HRESULT hr=commonOriginal(self,device,desc,vertex,output);
    auto s=shared;if(FAILED(hr)||!s||!output||!*output)return hr;
    int i=currentScope.output;if(i<0||i>=(int)MaxOutputs){InterlockedIncrement64(&s->stats.unscoped);return hr;}
    InterlockedIncrement64(&s->stats.shaderCalls[i]);IsolationConfig config;LONG sequence;
    if(!ReadConfig(s,config,sequence)||sequence!=currentScope.sequence)return hr;
    bool complete=!s->stats.mappingConflict&&s->stats.contextsMask==((1L<<config.count)-1);
    if(!config.outputs[i].hdr){InterlockedIncrement64(&s->stats.sdrBypasses[i]);return hr;}
    if(!ShouldUseGamma(config,i,complete,s->enabled!=0)&&!ShouldUseGamma(config,i,complete,s->inspect!=0))return hr;
    try{
        auto linked=FindLinkedByShader(*output);if(!linked){InterlockedIncrement64(&s->stats.unsupported[i]);return hr;}
        if(!s->enabled){if(InspectCandidate(linked,*output,config.curve))InterlockedIncrement64(&s->stats.candidates[i]);return hr;}
        auto replacement=Variant(linked,*output,s,config.curve);
        if(replacement&&s->enabled&&sequence==s->configSequence){*output=replacement;InterlockedIncrement64(&s->stats.replacements[i]);InterlockedExchange(&s->stats.appliedSequence,sequence);}
        else InterlockedIncrement64(&s->stats.unsupported[i]);
    }catch(...){InterlockedExchange(&s->enabled,0);SetMessage(s,L"缓存变体检查失败，已保持原始路径",3);}
    return hr;
}
extern "C" void DisplayRenderThunk();
extern "C" void LegacyRenderThunk();
extern "C" void LinkedShaderThunk();
extern "C" void CommonShaderThunk();

extern "C" __declspec(dllexport) DWORD WINAPI StartIsolatedAgent(AgentStart* incoming) {
    AgentStart args = {}; if (!SafeRead(incoming,&args,sizeof(args))) return 1;
    if (InterlockedCompareExchange(&agentRunning,1,0)) return 2;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,args.channel);
    auto s = (IsolationShared*)MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(IsolationShared));
    if (!s || s->protocol != IsolationProtocol) { if (s) UnmapViewOfFile(s); CloseHandle(mapping); InterlockedExchange(&agentRunning,0); return 3; }
    auto module = (BYTE*)GetModuleHandleW(L"dwmcore.dll");
    commonCache=module?module+CommonCacheRva:nullptr;
    const HookSite* sites[] = {&DisplayRenderSite,&LegacyRenderSite,&LinkedShaderSite,&CommonShaderSite};
    void* entries[] = {module+DisplayRenderSite.rva,module+LegacyRenderSite.rva,module+LinkedShaderSite.rva,module+CommonShaderSite.rva};
    void* hooks[] = {(void*)DisplayRenderThunk,(void*)LegacyRenderThunk,(void*)LinkedShaderThunk,(void*)CommonShaderThunk};
    void** originals[] = {(void**)&displayOriginal,(void**)&legacyOriginal,(void**)&shaderOriginal,(void**)&commonOriginal};
    bool enabled = false;
    AcquireSRWLockExclusive(&contextLock);ZeroMemory(owners,sizeof(owners));ownerSequence=-1;ReleaseSRWLockExclusive(&contextLock);
    if (!module) SetMessage(s,L"未加载 dwmcore.dll",3);
    else {
        bool intact = true;
        for (int i=0;i<4;++i) if (memcmp(entries[i],sites[i]->original,sizeof(sites[i]->original))) intact=false;
        if (!intact) SetMessage(s,L"DWM 入口字节不匹配，未启用隔离",3);
        else {
            MH_STATUS status = initialized ? MH_OK : MH_Initialize();
            if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
                bool ok = true;
                for (int i=0;i<4;++i) {
                    auto rc = MH_CreateHook(entries[i],hooks[i],originals[i]);
                    if (rc != MH_OK && rc != MH_ERROR_ALREADY_CREATED) ok=false;
                }
                initialized = ok;
                shared = s; MemoryBarrier();
                if (ok && MH_EnableHook(MH_ALL_HOOKS) == MH_OK) { enabled=true; SetMessage(s,L"隔离入口已安装，正在核对显示器归属",1); }
                else { MH_DisableHook(MH_ALL_HOOKS); SetMessage(s,L"无法安装隔离入口；保持原始着色器",3); }
            } else SetMessage(s,L"无法初始化内存挂钩；保持原始着色器",3);
        }
    }
    InterlockedExchange(&s->ready,1);
    while (enabled && !s->stop) {
        if (GetTickCount64()-(ULONGLONG)InterlockedCompareExchange64(&s->heartbeat,0,0)>6000) break;
        Sleep(100);
    }
    InterlockedExchange(&s->enabled,0);
    if (enabled) {
        if (MH_DisableHook(MH_ALL_HOOKS) != MH_OK) { SetMessage(s,L"转换已停用，但入口恢复失败",3); enabled=false; }
        else {
            while (InterlockedCompareExchange(&activeCalls,0,0)) Sleep(1);
            shared = nullptr; MemoryBarrier();
            // Keep inert DLL/trampoline allocations until DWM exits. This avoids unloading code
            // beneath a thread preempted in a thunk. All DWM entry bytes are already restored.
            SetMessage(s,L"隔离已停用，DWM 入口已还原",2);
        }
    }
    shared = nullptr; UnmapViewOfFile(s); CloseHandle(mapping);
    InterlockedExchange(&agentRunning,0); return enabled ? 0 : 1;
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

#ifdef EOTF_AGENT_TEST
namespace {
ID3D11PixelShader* testShader = nullptr;
void* testLinked = nullptr;
UINT* testVertex = nullptr;
ID3D11PixelShader** testOutput = nullptr;
bool testFailed = false;
ID3D11PixelShader* testVariants[3] = {};
__declspec(noinline) HRESULT TestShaderOriginal(void*, void*, UINT*, ID3D11PixelShader** out) { *out=testShader;return S_OK; }
__declspec(noinline) HRESULT TestCommonOriginal(void*,void*,const void* desc,UINT* vertex,ID3D11PixelShader** out){
    if(desc!=(const void*)0x1234||vertex!=testVertex||out!=testOutput)return E_UNEXPECTED;
    *out=testShader;return S_OK;
}
__declspec(noinline) HRESULT TestRenderOriginal(void*, void*) {
    ID3D11PixelShader* out=nullptr; UINT vertex=0;
    ShaderFn volatile invoke=TestShaderOriginal;invoke(testLinked,nullptr,&vertex,&out);
    bool complete=shared->stats.contextsMask==7;
    auto curve=shared->config.curve;
    bool gamma=complete&&shared->enabled&&currentScope.output==0&&curve!=TransferCurve::SRGB;
    if(gamma ? out==testShader : out!=testShader) testFailed=true;
    auto linkedResult=out;
    if(gamma){auto& cached=testVariants[(int)curve];if(cached&&cached!=out)testFailed=true;cached=out;}
    out=nullptr;testVertex=&vertex;testOutput=&out;
    CommonFn volatile common=TestCommonOriginal;
    if(FAILED(common(nullptr,nullptr,(const void*)0x1234,&vertex,&out))||out!=linkedResult)testFailed=true;
    return S_OK;
}
}
// Compiled only into the test DLL. Exercises the actual assembly thunks, hooks,
// ownership guard, original-shader lifetime and private-data variant cache in our own process.
extern "C" __declspec(dllexport) DWORD WINAPI TestAgentHooks(const BYTE* data,size_t size) {
    try {
        IsolationShared local={};local.protocol=IsolationProtocol;local.enabled=1;local.config.protocol=IsolationProtocol;local.config.count=3;
        local.config.outputs[0]={{0,0,2560,1440},{},0,TRUE,TRUE,L"HDR"};
        local.config.outputs[1]={{-1920,0,0,1080},{},1,FALSE,FALSE,L"SDR1"};
        local.config.outputs[2]={{2560,0,4480,1080},{},2,FALSE,FALSE,L"SDR2"};
        shared=&local; ownerSequence=-1;testFailed=false;ZeroMemory(testVariants,sizeof(testVariants));
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))return 1;
        ComPtr<ID3D11PixelShader> pixel;if(FAILED(device->CreatePixelShader(data,size,nullptr,&pixel)))return 2;
        ComPtr<ID3DBlob> blob;if(FAILED(D3DCreateBlob(size,&blob)))return 3;memcpy(blob->GetBufferPointer(),data,size);
        BYTE linked[0x48]={},storage[0x20]={},wrapper[0x80]={},cache[0x20]={};
        *(BYTE**)(linked+0x28)=storage;*(ID3DBlob**)(storage+0x10)=blob.Get();
        *(ID3D11PixelShader**)(wrapper+0x78)=pixel.Get();void* wrappers[]={wrapper};*(void***)(linked+0x30)=wrappers;*(void***)(linked+0x38)=wrappers+1;
        void* entries[]={linked};*(void***)(cache+8)=entries;*(LONG*)(cache+0x10)=1;commonCache=cache;
        testLinked=linked;testShader=pixel.Get();
        BYTE originalRender[16],originalShader[16],originalCommon[16];memcpy(originalRender,(void*)TestRenderOriginal,16);memcpy(originalShader,(void*)TestShaderOriginal,16);memcpy(originalCommon,(void*)TestCommonOriginal,16);
        if(MH_Initialize()!=MH_OK||MH_CreateHook((void*)TestRenderOriginal,(void*)DisplayRenderThunk,(void**)&displayOriginal)!=MH_OK||MH_CreateHook((void*)TestShaderOriginal,(void*)LinkedShaderThunk,(void**)&shaderOriginal)!=MH_OK||MH_CreateHook((void*)TestCommonOriginal,(void*)CommonShaderThunk,(void**)&commonOriginal)!=MH_OK||MH_EnableHook(MH_ALL_HOOKS)!=MH_OK)return 6;
        std::vector<BYTE> contexts[3];for(unsigned i=0;i<3;++i){contexts[i].resize(0x7800);auto r=local.config.outputs[i].bounds;float b[4]={(float)r.left,(float)r.top,(float)r.right,(float)r.bottom};memcpy(contexts[i].data()+DisplayRectOffset,b,sizeof(b));}
        RenderFn volatile render=TestRenderOriginal;
        for(int mode:{0,1,2,0,3,2,1,0}){
            local.configSequence+=2;local.config.curve=(TransferCurve)mode;
            for(unsigned frame=0;frame<20;++frame)for(unsigned i=0;i<3;++i)render(contexts[i].data(),nullptr);
        }
        if(local.stats.variants!=3||!testVariants[0]||!testVariants[1]||!testVariants[2]||testVariants[0]==testVariants[1]||testVariants[0]==testVariants[2]||testVariants[1]==testVariants[2])return 8;
        if(testFailed||!local.stats.replacements[0]||local.stats.replacements[1]||local.stats.replacements[2]||!local.stats.sdrBypasses[1]||!local.stats.sdrBypasses[2])return 4;
        local.enabled=0;for(unsigned i=0;i<3;++i)render(contexts[i].data(),nullptr);
        ID3D11PixelShader* output=nullptr;UINT vertex=0;((ShaderFn)LinkedShaderThunk)(linked,nullptr,&vertex,&output);
        if(testFailed||output!=pixel.Get()||local.stats.unscoped!=1)return 5;
        if(MH_DisableHook(MH_ALL_HOOKS)!=MH_OK||memcmp(originalRender,(void*)TestRenderOriginal,16)||memcmp(originalShader,(void*)TestShaderOriginal,16)||memcmp(originalCommon,(void*)TestCommonOriginal,16))return 7;
        MH_RemoveHook(MH_ALL_HOOKS);MH_Uninitialize();
        shared=nullptr;testShader=nullptr;testLinked=nullptr;context->ClearState();return 0;
    }catch(...){shared=nullptr;return 9;}
}
#endif
