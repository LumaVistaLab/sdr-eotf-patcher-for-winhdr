#include "isolation.h"
#include "isolation_profile.h"
#include "agent_filename.h"
#include <tlhelp32.h>
#include <bcrypt.h>
#include <aclapi.h>
#include <setupapi.h>
#include <shlwapi.h>
#include <cstring>
#include <sstream>
#include <memory>
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"shlwapi.lib")
namespace eotf {
static std::wstring MonitorDisplayName(const DISPLAYCONFIG_TARGET_DEVICE_NAME& target,const wchar_t* source) {
    if(target.monitorFriendlyDeviceName[0])return target.monitorFriendlyDeviceName;
    // EDID names can be empty on internal panels. Resolve the exact monitor interface
    // to its PnP friendly name, not the adapter name or a hard-coded panel label.
    if(target.monitorDevicePath[0]) {
        struct DeviceList { HDEVINFO handle=SetupDiCreateDeviceInfoList(nullptr,nullptr);~DeviceList(){if(handle!=INVALID_HANDLE_VALUE)SetupDiDestroyDeviceInfoList(handle);} } devices;
        SP_DEVICE_INTERFACE_DATA iface={};iface.cbSize=sizeof(iface);
        if(devices.handle!=INVALID_HANDLE_VALUE&&SetupDiOpenDeviceInterfaceW(devices.handle,target.monitorDevicePath,0,&iface)) {
            DWORD needed=0;SetupDiGetDeviceInterfaceDetailW(devices.handle,&iface,nullptr,0,&needed,nullptr);
            if(needed>=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)&&needed<=65536) {
                std::vector<BYTE> buffer(needed);auto detail=(SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buffer.data();detail->cbSize=sizeof(*detail);
                SP_DEVINFO_DATA device={};device.cbSize=sizeof(device);
                if(SetupDiGetDeviceInterfaceDetailW(devices.handle,&iface,detail,needed,nullptr,&device)) {
                    wchar_t friendly[1024]={};DWORD type=0;
                    if(SetupDiGetDeviceRegistryPropertyW(devices.handle,&device,SPDRP_FRIENDLYNAME,&type,(PBYTE)friendly,sizeof(friendly)-sizeof(wchar_t),nullptr)&&type==REG_SZ&&friendly[0]) {
                        if(friendly[0]!=L'@')return friendly;
                        wchar_t resolved[1024]={};if(SUCCEEDED(SHLoadIndirectString(friendly,resolved,1024,nullptr))&&resolved[0])return resolved;
                    }
                }
            }
        }
    }
    return source&&source[0]?source:L"Monitor";
}
static std::wstring AgentPath() {
    wchar_t file[32768]; DWORD n=GetModuleFileNameW(nullptr,file,32768); if (!n || n>=32768) throw Error("Executable path unavailable");
    std::wstring p=file; return p.substr(0,p.find_last_of(L"\\/"))+L"\\"+AgentFileName;
}
bool MatchesIsolationProfile() {
    auto data=ReadFileBytes(SystemDwmPath()); BCRYPT_ALG_HANDLE algorithm=nullptr; BYTE digest[32];
    if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    auto rc=BCryptHash(algorithm,nullptr,0,data.data(),(ULONG)data.size(),digest,sizeof(digest)); BCryptCloseAlgorithmProvider(algorithm,0);
    return rc>=0 && !memcmp(digest,ProfileDigest,sizeof(digest));
}
IsolationConfig OutputConfiguration() {
    for (int attempt=0;attempt<4;++attempt) {
        UINT32 pc=0,mc=0; LONG rc=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pc,&mc); if(rc) throw DisplayTransition(WinError("Display buffer sizes",rc));
        if (pc>MaxOutputs) throw Error("Too many active outputs for isolation");
        if(!pc){IsolationConfig empty={};empty.protocol=IsolationProtocol;return empty;}
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc); std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);
        rc=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pc,paths.data(),&mc,modes.data(),nullptr);
        if(rc==ERROR_INSUFFICIENT_BUFFER) continue; if(rc) throw DisplayTransition(WinError("Display paths",rc));
        IsolationConfig config={}; config.protocol=IsolationProtocol; config.count=pc;DWORD sourceNumbers[MaxOutputs]={};
        for(UINT32 i=0;i<pc;++i) {
            auto& out=config.outputs[i]; const auto& path=paths[i]; out.adapter=path.targetInfo.adapterId; out.target=path.targetInfo.id;
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source={}; source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
            rc=DisplayConfigGetDeviceInfo(&source.header); if(rc) throw DisplayTransition(WinError("Source display name",rc));
            sourceNumbers[i]=DisplaySourceNumber(source.viewGdiDeviceName);
            DEVMODEW mode={}; mode.dmSize=sizeof(mode); if(!EnumDisplaySettingsW(source.viewGdiDeviceName,ENUM_CURRENT_SETTINGS,&mode)) throw DisplayTransition(WinError("Physical output bounds"));
            out.bounds={mode.dmPosition.x,mode.dmPosition.y,mode.dmPosition.x+(LONG)mode.dmPelsWidth,mode.dmPosition.y+(LONG)mode.dmPelsHeight};
            DISPLAYCONFIG_TARGET_DEVICE_NAME name={}; name.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(name),out.adapter,out.target};
            rc=DisplayConfigGetDeviceInfo(&name.header);if(rc)throw DisplayTransition(WinError("Target display name",rc));
            wcsncpy_s(out.name,MonitorDisplayName(name,source.viewGdiDeviceName).c_str(),_TRUNCATE);
            struct ColorInfo2 { DISPLAYCONFIG_DEVICE_INFO_HEADER header; UINT32 flags,encoding,bits,mode; } info={};
            info.header={(DISPLAYCONFIG_DEVICE_INFO_TYPE)15,sizeof(info),out.adapter,out.target}; rc=DisplayConfigGetDeviceInfo(&info.header);
            if(!rc) out.hdr=info.mode==2;
            else { DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO old={}; old.header={DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO,sizeof(old),out.adapter,out.target};
                rc=DisplayConfigGetDeviceInfo(&old.header); if(rc) throw DisplayTransition(WinError("Output HDR mode",rc)); out.hdr=old.advancedColorEnabled&&!old.wideColorEnforced; }
        }
        SortOutputs(config,sourceNumbers);return config;
    }
    throw DisplayTransition("Display topology did not settle");
}
static uintptr_t RemoteModule(DWORD pid,const wchar_t* name) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid)); if(snapshot.h==INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W m={};m.dwSize=sizeof(m); if(Module32FirstW(snapshot,&m)) do { if(!_wcsicmp(m.szModule,name)) return (uintptr_t)m.modBaseAddr; } while(Module32NextW(snapshot,&m)); return 0;
}
static Handle OpenAgentDwm(DWORD pid) {
    Handle p(OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_READ|PROCESS_VM_WRITE|SYNCHRONIZE,FALSE,pid));
    if(!p.h) throw Error(WinError("Open DWM for isolated agent")); return p;
}
struct AgentReadPermission {
    std::wstring path; PSECURITY_DESCRIPTOR descriptor=nullptr; PACL original=nullptr; bool changed=false;
    AgentReadPermission(HANDLE process,const std::wstring& file):path(file) {
        // DWM runs under its own virtual account. Grant only that account read/execute
        // on this newly built agent, preserving the original ACL and restoring it after loading.
        HANDLE raw=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&raw))throw Error(WinError("Query DWM account for agent read access"));Handle token(raw);
        DWORD n=0;GetTokenInformation(token,TokenUser,nullptr,0,&n);std::vector<BYTE> info(n);
        if(!GetTokenInformation(token,TokenUser,info.data(),n,&n))throw Error(WinError("Read DWM virtual account SID"));
        DWORD rc=GetNamedSecurityInfoW((LPWSTR)path.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&original,nullptr,&descriptor);
        if(rc)throw Error(WinError("Read agent file ACL",rc));
        EXPLICIT_ACCESSW access={};access.grfAccessPermissions=FILE_GENERIC_READ|FILE_GENERIC_EXECUTE;access.grfAccessMode=GRANT_ACCESS;access.grfInheritance=NO_INHERITANCE;
        BuildTrusteeWithSidW(&access.Trustee,((TOKEN_USER*)info.data())->User.Sid);PACL updated=nullptr;
        rc=SetEntriesInAclW(1,&access,original,&updated);if(rc){LocalFree(descriptor);descriptor=nullptr;throw Error(WinError("Build agent read ACL",rc));}
        rc=SetNamedSecurityInfoW((LPWSTR)path.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,updated,nullptr);LocalFree(updated);
        if(rc){LocalFree(descriptor);descriptor=nullptr;throw Error(WinError("Allow DWM to read this agent",rc));}changed=true;
    }
    ~AgentReadPermission(){if(changed)SetNamedSecurityInfoW((LPWSTR)path.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,original,nullptr);if(descriptor)LocalFree(descriptor);}
};
static void AllowAgentMapping(HANDLE process,HANDLE mapping) {
    HANDLE raw=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&raw))throw Error(WinError("Query agent mapping account"));Handle token(raw);
    DWORD n=0;GetTokenInformation(token,TokenUser,nullptr,0,&n);std::vector<BYTE> data(n);
    if(!GetTokenInformation(token,TokenUser,data.data(),n,&n))throw Error(WinError("Read mapping account SID"));
    PACL original=nullptr,updated=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
    DWORD rc=GetSecurityInfo(mapping,SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&original,nullptr,&descriptor);
    if(rc)throw Error(WinError("Read new mapping ACL",rc));
    EXPLICIT_ACCESSW access={};access.grfAccessPermissions=SECTION_MAP_READ|SECTION_MAP_WRITE;access.grfAccessMode=GRANT_ACCESS;BuildTrusteeWithSidW(&access.Trustee,((TOKEN_USER*)data.data())->User.Sid);
    rc=SetEntriesInAclW(1,&access,original,&updated);
    if(!rc)rc=SetSecurityInfo(mapping,SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,updated,nullptr);
    if(updated)LocalFree(updated);LocalFree(descriptor);if(rc)throw Error(WinError("Allow DWM mapping read/write",rc));
}
static bool SameProcess(const Shared& s) {
    Handle p(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,s.dwmPid));
    return p.h&&WaitForSingleObject(p,0)==WAIT_TIMEOUT&&CreationTime(p)==s.dwmCreated;
}
static void CheckRestored(HANDLE p,uintptr_t base) {
    const HookSite* sites[]={&DisplayRenderSite,&LegacyRenderSite,&LinkedShaderSite,&CommonShaderSite};
    for(auto site:sites) { BYTE actual[16];SIZE_T n=0; if(!ReadProcessMemory(p,(void*)(base+site->rva),actual,sizeof(actual),&n)||n!=sizeof(actual)||memcmp(actual,site->original,sizeof(actual))) throw Error("DWM isolation entry bytes were not restored"); }
}
void RestoreIsolated(Shared& s) {
    if(!s.dirty) return;
    if(!SameProcess(s)) { InterlockedExchange(&s.dirty,0);return; }
    Handle mapping(OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,s.isolationChannel)); if(!mapping.h) throw Error(WinError("Open isolation recovery channel"));
    auto agent=(IsolationShared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(IsolationShared)); if(!agent) throw Error(WinError("Map isolation recovery channel"));
    InterlockedExchange(&agent->enabled,0);InterlockedExchange(&agent->stop,1);
    auto end=GetTickCount64()+15000; while(GetTickCount64()<end&&agent->state!=2&&agent->state!=3&&SameProcess(s)) Sleep(20);
    LONG state=agent->state; UnmapViewOfFile(agent);
    if(!SameProcess(s)) { InterlockedExchange(&s.dirty,0);return; }
    if(state!=2&&state!=3) throw Error("Isolated agent has not confirmed restoration");
    EnableDebugPrivilege(); auto process=OpenAgentDwm(s.dwmPid);CheckRestored(process,RemoteModule(s.dwmPid,L"dwmcore.dll"));
    InterlockedExchange(&s.dirty,0);
    RedrawWindow(nullptr,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
struct RemoteBuffer {
    HANDLE process;void* address;
    RemoteBuffer(HANDLE p,const void* data,size_t n):process(p),address(VirtualAllocEx(p,nullptr,n,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)) {
        SIZE_T written=0;if(!address||!WriteProcessMemory(p,address,data,n,&written)||written!=n) {if(address)VirtualFreeEx(process,address,0,MEM_RELEASE);address=nullptr;throw Error(WinError("Write isolated agent arguments"));}
    }
    ~RemoteBuffer(){if(address)VirtualFreeEx(process,address,0,MEM_RELEASE);}
};
struct AgentSession {
    Shared& owner;Handle process,mapping,thread;IsolationShared* state=nullptr;uintptr_t base=0;
    explicit AgentSession(Shared& s):owner(s){}
    ~AgentSession(){if(state){InterlockedExchange(&state->enabled,0);InterlockedExchange(&state->stop,1);if(thread.h)WaitForSingleObject(thread,3000);UnmapViewOfFile(state);}}
    void Update(const IsolationConfig& config) {
        if(state)UpdateIsolationConfig(*state,config);
    }
    void Start(const IsolationConfig& config) {
        if(!MatchesIsolationProfile()) throw Error("Installed dwmcore.dll has no exact isolation profile; nothing was injected");
        EnableDebugPrivilege();DWORD pid=FindDwm();if(!pid)throw Error("No compositor in this session");
        process=OpenAgentDwm(pid);base=RemoteModule(pid,L"dwmcore.dll");if(!base)throw Error("DWM module unavailable"); CheckRestored(process,base);
        owner.dwmPid=pid;owner.dwmCreated=CreationTime(process);
        swprintf_s(owner.isolationChannel,L"Local\\SDREotf.Isolated.%lu.%lu.%llu",GetCurrentProcessId(),pid,GetTickCount64());
        mapping=Handle(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(IsolationShared),owner.isolationChannel));if(!mapping.h)throw Error(WinError("Create isolation channel"));
        AllowAgentMapping(process,mapping);
        state=(IsolationShared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(IsolationShared));if(!state)throw Error(WinError("Map isolation channel"));
        ZeroMemory(state,sizeof(*state));state->protocol=IsolationProtocol;
        state->stats.appliedCurve=(LONG)TransferCurve::SRGB;
        InterlockedExchange64(&state->heartbeat,(LONG64)GetTickCount64());Update(config);
        auto path=AgentPath();AgentReadPermission permission(process,path);RemoteBuffer remotePath(process,path.c_str(),(path.size()+1)*sizeof(wchar_t));
        auto loader=(BYTE*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");HMODULE containing=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)loader,&containing))throw Error(WinError("Find loader module"));
        wchar_t loaderPath[MAX_PATH];GetModuleFileNameW(containing,loaderPath,MAX_PATH);const wchar_t* file=wcsrchr(loaderPath,L'\\');file=file?file+1:loaderPath;
        uintptr_t loaderBase=RemoteModule(pid,file);if(!loaderBase)throw Error("Remote loader missing");
        uintptr_t remoteLoader=loaderBase+(loader-(BYTE*)containing);
        Handle load(CreateRemoteThread(process,nullptr,0,(LPTHREAD_START_ROUTINE)remoteLoader,remotePath.address,0,nullptr));if(!load.h)throw Error(WinError("Load isolated agent"));
        if(WaitForSingleObject(load,10000)!=WAIT_OBJECT_0) {remotePath.address=nullptr;throw Error("Agent load timed out; its path allocation was retained for the pending loader");}
        uintptr_t agentModule=RemoteModule(pid,AgentFileName);
        if(!agentModule){
            DWORD exitCode=0;GetExitCodeThread(load,&exitCode);
            PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY signature={};PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamic={};PROCESS_MITIGATION_IMAGE_LOAD_POLICY images={};
            GetProcessMitigationPolicy(process,ProcessSignaturePolicy,&signature,sizeof(signature));
            GetProcessMitigationPolicy(process,ProcessDynamicCodePolicy,&dynamic,sizeof(dynamic));
            GetProcessMitigationPolicy(process,ProcessImageLoadPolicy,&images,sizeof(images));
            throw Error("Isolated agent load failed: loader result="+std::to_string(exitCode)+", signature policy="+std::to_string(signature.Flags)+", dynamic code policy="+std::to_string(dynamic.Flags)+", image load policy="+std::to_string(images.Flags));
        }
        HMODULE local=LoadLibraryExW(path.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!local)throw Error(WinError("Read agent export"));
        auto entry=GetProcAddress(local,"StartIsolatedAgent");uintptr_t entryRva=(uintptr_t)entry-(uintptr_t)local;FreeLibrary(local);if(!entry)throw Error("Agent export missing");
        AgentStart args={};
        wcscpy_s(args.channel,owner.isolationChannel);
        RemoteBuffer arguments(process,&args,sizeof(args));InterlockedExchange(&owner.dirty,1);
        thread=Handle(CreateRemoteThread(process,nullptr,0,(LPTHREAD_START_ROUTINE)(agentModule+entryRva),arguments.address,0,nullptr));if(!thread.h)throw Error(WinError("Start isolated agent"));
        auto end=GetTickCount64()+10000;while(GetTickCount64()<end&&!state->ready&&WaitForSingleObject(thread,0)==WAIT_TIMEOUT)Sleep(10);
        if(!state->ready){arguments.address=nullptr;throw Error("Isolated agent did not acknowledge initialization");}
        if(state->state!=1)throw Error(Utf8(state->detail));
    }
    void Stop(){
        if(!owner.dirty)return;
        if(!thread.h||WaitForSingleObject(thread,0)==WAIT_OBJECT_0){
            if(SameProcess(owner))CheckRestored(process,base);
            InterlockedExchange(&owner.dirty,0);return;
        }
        RestoreIsolated(owner);
    }
    bool Alive() const{return process.h&&WaitForSingleObject(process,0)==WAIT_TIMEOUT&&thread.h&&WaitForSingleObject(thread,0)==WAIT_TIMEOUT;}
};
static void PublishIsolation(Shared& s,Status status,const std::wstring& message,TransferCurve curve=TransferCurve::SRGB) {
    wcsncpy_s(s.detail,message.c_str(),_TRUNCATE);InterlockedExchange(&s.activeCurve,(LONG)curve);MemoryBarrier();InterlockedExchange(&s.status,status);
}
static void PublishOutputs(Shared& s,const IsolationConfig& config) {
    s.outputCount=config.count;s.selectedOutput=-1;
    for(unsigned i=0;i<config.count;++i){s.outputHdr[i]=config.outputs[i].hdr;wcsncpy_s(s.outputNames[i],config.outputs[i].name,_TRUNCATE);if(config.outputs[i].selected)s.selectedOutput=(LONG)i;}
}
int IsolatedWorker(Shared& s,HANDLE parent,HANDLE request,HANDLE response) {
    Command wanted=SRGB; bool chosen=false;LUID adapter={};UINT32 target=0;
    std::unique_ptr<AgentSession> agent;TopologyStability topology;
    PublishIsolation(s,Original,L"sRGB · 所有输出保持原始路径");SetEvent(response);HANDLE waits[]={parent,request};
    for(;;){
        DWORD wait=WaitForMultipleObjects(2,waits,FALSE,1000);auto cmd=(Command)InterlockedExchange(&s.command,None);
        try{
            if(wait==WAIT_OBJECT_0||cmd==Stop){if(agent)agent->Stop();PublishIsolation(s,Stopped,L"已恢复原始着色器，未重启 DWM");SetEvent(response);return 0;}
            if(wait==WAIT_FAILED)throw Error(WinError("Isolated worker wait"));
            if(CurveCommand(cmd))wanted=cmd;
            IsolationConfig config=OutputConfiguration();config.curve=CommandCurve(wanted);
            bool changed=!topology.observed||!SameOutputTopology(topology.previous,config);
            bool stable=topology.Observe(config,GetTickCount64());
            if(changed&&agent){agent->Stop();agent.reset();}
            if((LONG)cmd>=100&&(LONG)cmd<100+(LONG)config.count){auto& o=config.outputs[(LONG)cmd-100];if(o.hdr){chosen=true;adapter=o.adapter;target=o.target;}}
            if(!chosen)for(unsigned i=0;i<config.count;++i)if(config.outputs[i].hdr){chosen=true;adapter=config.outputs[i].adapter;target=config.outputs[i].target;break;}
            bool available=false;for(unsigned i=0;i<config.count;++i){auto& o=config.outputs[i];o.selected=chosen&&o.hdr&&!memcmp(&o.adapter,&adapter,sizeof(adapter))&&o.target==target;available|=o.selected!=FALSE;}
            PublishOutputs(s,config);
            if(wanted==SRGB||!available||!ValidOutputConfig(config)){
                if(agent){agent->Stop();agent.reset();}
                PublishIsolation(s,wanted==SRGB?Original:!available?WaitingHDR:BlockedMixedOutputs,
                    wanted==SRGB?L"sRGB · 所有输出保持原始路径":!available?L"等待所选显示器进入 HDR / 杜比视界模式":L"输出归属不明确（镜像或重叠），保持原始路径");
            }else if(!stable){PublishIsolation(s,Working,L"显示器配置正在更新 · 保持原始路径");
            }else{
                if(agent&&!agent->Alive()){agent.reset();InterlockedExchange(&s.dirty,0);wanted=SRGB;throw Error("Compositor or isolation agent exited; automatic reapplication stopped");}
                if(!agent){PublishIsolation(s,Working,L"正在安装按显示器隔离入口…");agent=std::make_unique<AgentSession>(s);agent->Start(config);}
                agent->Update(config);auto& a=*agent->state;InterlockedExchange64(&a.heartbeat,(LONG64)GetTickCount64());
                if(a.state==3)throw Error(Utf8(a.detail));
                if(a.stats.mappingConflict){InterlockedExchange(&a.enabled,0);PublishIsolation(s,BlockedMixedOutputs,L"检测到显示器上下文冲突，所有输出保持原始路径");}
                else if(a.stats.contextsMask!=((1L<<config.count)-1)){InterlockedExchange(&a.enabled,0);PublishIsolation(s,Working,L"正在核对显示器归属 · "+std::to_wstring(a.stats.contextsMask)+L" / "+std::to_wstring((1L<<config.count)-1));}
                else{InterlockedExchange(&a.enabled,1);auto active=AppliedIsolationCurve(a);bool hit=active!=TransferCurve::SRGB;
                    std::wstring detail=hit?std::wstring(CurveName(active))+L" · 仅 "+std::wstring(config.outputs[s.selectedOutput].name)+L" · SDR 副屏旁路":L"显示器隔离已确认 · 等待 SDR 着色器命中";
                    if(hit&&active!=config.curve)detail+=L" · 正在切换到 "+std::wstring(CurveName(config.curve));
                    PublishIsolation(s,hit?Patched:Working,detail,active);}
            }
        }catch(const DisplayTransition&){
            topology.Reset();s.outputCount=0;s.selectedOutput=-1;
            bool restored=true;if(agent){try{agent->Stop();agent.reset();}catch(...){restored=false;}}
            PublishIsolation(s,restored?WaitingTopology:Failed,restored?L"显示器配置正在更新 · 等待重新连接":L"正在重试显示器切换后的恢复");
            // Transient display enumeration failures never clear the requested curve.
        }catch(const std::exception& error){if(agent){try{agent->Stop();}catch(...){}agent.reset();}wanted=SRGB;PublishIsolation(s,Failed,Wide(error.what()));}
        SetEvent(response);
    }
}
int DiagnoseIsolation(const std::wstring& report,bool enable,bool simulateGuardianLoss,TransferCurve curve) {
    Shared owner={};AgentSession agent(owner);std::wostringstream result;
    try{auto config=OutputConfiguration();config.curve=curve;result<<L"Transfer curve: "<<CurveName(curve)<<L"\r\n";bool picked=false;for(unsigned i=0;i<config.count;++i)if(config.outputs[i].hdr&&!picked){config.outputs[i].selected=TRUE;picked=true;}
        if(!ValidOutputConfig(config)||!picked)throw Error("No unambiguous HDR target");
        agent.Start(config);InterlockedExchange(&agent.state->inspect,enable?0:1);InterlockedExchange(&agent.state->enabled,enable?1:0);
        auto end=GetTickCount64()+12000;while(GetTickCount64()<end&&agent.Alive()){InterlockedExchange64(&agent.state->heartbeat,(LONG64)GetTickCount64());Sleep(100);}
        if(simulateGuardianLoss){
            if(WaitForSingleObject(agent.thread,8500)!=WAIT_OBJECT_0||agent.state->state!=2)throw Error("Heartbeat-loss watchdog did not restore the compositor");
            result<<L"PASS: guardian heartbeat intentionally stopped; in-DWM watchdog restored original entries automatically.\r\n";
        }
        auto& stats=agent.state->stats;result<<L"Isolation probe; rendering modification="<<enable<<L"\r\nMask="<<stats.contextsMask<<L", conflict="<<stats.mappingConflict<<L", unscoped="<<stats.unscoped<<L", variants="<<stats.variants<<L"\r\n";
        for(unsigned i=0;i<config.count;++i){auto& o=config.outputs[i];result<<i<<L" "<<o.name<<L" HDR="<<o.hdr<<L" selected="<<o.selected<<L" rect="<<o.bounds.left<<L","<<o.bounds.top<<L","<<o.bounds.right<<L","<<o.bounds.bottom
            <<L" frames="<<stats.frames[i]<<L" shaders="<<stats.shaderCalls[i]<<L" replacements="<<stats.replacements[i]<<L" bypass="<<stats.sdrBypasses[i]<<L" unsupported="<<stats.unsupported[i]<<L" candidates="<<stats.candidates[i]<<L"\r\n";
            if(!o.hdr&&stats.replacements[i])throw Error("SDR output invariant failed");}
        bool complete=!stats.mappingConflict&&stats.contextsMask==((1L<<config.count)-1);LONG64 hits=0,candidates=0;for(unsigned i=0;i<config.count;++i){hits+=stats.replacements[i];candidates+=stats.candidates[i];}
        VerifyShaderLibraries();result<<L"PASS: shared DWM shader libraries remain byte-identical.\r\n";
        agent.Stop();result<<L"PASS: DWM entry bytes restored; compositor was not restarted.\r\n";
        if(!complete)throw Error("Not all monitor contexts were mapped");if(enable&&!hits)throw Error("No SDR variants selected on HDR target");if(!enable&&!candidates)throw Error("No eligible SDR shader found on the HDR target");
        result<<L"PASS: output-isolation invariants.\r\n";WriteText(report,Utf8(result.str()));return 0;
    }catch(const std::exception& e){try{agent.Stop();}catch(const std::exception& r){result<<L"RESTORE: "<<Wide(r.what())<<L"\r\n";}result<<L"FAIL: "<<Wide(e.what())<<L"\r\n";WriteText(report,Utf8(result.str()));return 1;}
}
}
