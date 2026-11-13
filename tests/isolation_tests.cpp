#include "../src/isolation.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <iostream>
#include <cmath>
#include <cstring>
#include <array>
using namespace eotf;
using Microsoft::WRL::ComPtr;
static void Check(bool v, const char* why) { if (!v) throw Error(why); }
static void HR(HRESULT hr, const char* why, ID3DBlob* error = nullptr) {
    if (FAILED(hr)) throw Error(std::string(why) + " HRESULT=" + std::to_string(hr) + (error ? std::string((char*)error->GetBufferPointer(), error->GetBufferSize()) : ""));
}
static ComPtr<ID3DBlob> LinkSDR(const ShaderContainer& root,const char* function="RGB_FULL_G22_NONE_P709_To_scRGB") {
    ComPtr<ID3D11Module> library; HR(D3DLoadModule(root.original.data(), root.original.size(), &library), "D3DLoadModule");
    ComPtr<ID3D11ModuleInstance> libInstance; HR(library->CreateInstance(nullptr, &libInstance), "CreateInstance");
    ComPtr<ID3D11FunctionLinkingGraph> graph; HR(D3DCreateFunctionLinkingGraph(0, &graph), "Create graph");
    D3D11_PARAMETER_DESC in = {"color", "COLOR", D3D_SVT_FLOAT, D3D_SVC_VECTOR, 1, 4, D3D_INTERPOLATION_UNDEFINED, D3D_PF_IN, 0, 0};
    D3D11_PARAMETER_DESC out = {"target", "SV_TARGET", D3D_SVT_FLOAT, D3D_SVC_VECTOR, 1, 4, D3D_INTERPOLATION_UNDEFINED, D3D_PF_OUT, 0, 0};
    ComPtr<ID3D11LinkingNode> input, output, call;
    HR(graph->SetInputSignature(&in, 1, &input), "Input signature");
    HRESULT callResult = graph->CallFunction(nullptr, library.Get(), function, &call);
    if (FAILED(callResult)) { ComPtr<ID3DBlob> details; graph->GetLastError(&details); HR(callResult, "SDR function call", details.Get()); }
    HR(graph->PassValue(input.Get(), 0, call.Get(), 0), "Pass input");
    HR(graph->SetOutputSignature(&out, 1, &output), "Output signature");
    HR(graph->PassValue(call.Get(), D3D_RETURN_PARAMETER_INDEX, output.Get(), 0), "Pass result");
    ComPtr<ID3D11ModuleInstance> instance; ComPtr<ID3DBlob> error;
    HR(graph->CreateModuleInstance(&instance, &error), "Create graph instance", error.Get());
    ComPtr<ID3D11Linker> linker; HR(D3DCreateLinker(&linker), "Create linker"); HR(linker->UseLibrary(libInstance.Get()), "Use library");
    ComPtr<ID3DBlob> shader; HRESULT rc = linker->Link(instance.Get(), "main", "ps_4_0", 0, &shader, &error);
    HR(rc, "Link SDR pixel shader", error.Get()); return shader;
}
int main() {
    try {
        IsolationConfig c = {}; c.protocol = IsolationProtocol; c.count = 3;
        c.outputs[0] = {{0, 0, 2560, 1440}, {}, 0, TRUE, TRUE, L"HDR target"};
        c.outputs[1] = {{-1920, 0, 0, 1080}, {}, 1, FALSE, FALSE, L"SDR left"};
        c.outputs[2] = {{2560, 0, 4480, 1080}, {}, 2, FALSE, FALSE, L"SDR right"};
        Check(ValidOutputConfig(c), "Mixed output configuration rejected");
        const float right[] = {2560, 0, 4480, 1080}; Check(MatchOutput(c, right) == 2, "Monitor origin ignored");
        Check(ShouldUseGamma(c, 0, true, true), "HDR target did not select variant");
        Check(!ShouldUseGamma(c, 1, true, true) && !ShouldUseGamma(c, 2, true, true), "SDR output was selected");
        Check(!ShouldUseGamma(c, 0, false, true) && !ShouldUseGamma(c, -1, true, true), "Unverified context was selected");
        Check(!ShouldUseGamma(c, 0, true, false), "Disabled mode selected variant");
        auto mirror = c; mirror.outputs[2].bounds = mirror.outputs[0].bounds; Check(!ValidOutputConfig(mirror), "Ambiguous mirrored output accepted");
        c.outputs[1].selected = TRUE; Check(!ValidOutputConfig(c), "An SDR target was permitted"); c.outputs[1].selected = FALSE;
        Check(DisplaySourceNumber(L"\\\\.\\DISPLAY10")==10&&DisplaySourceNumber(L"\\\\.\\DISPLAY2")==2&&DisplaySourceNumber(L"Unknown")==MAXDWORD,"Windows display number was not parsed naturally");
        DWORD sourceNumbers[MaxOutputs]={5,6,1};auto sorted=c;SortOutputs(sorted,sourceNumbers);
        Check(sorted.outputs[0].target==2&&sorted.outputs[1].target==0&&sorted.outputs[2].target==1,"Output order did not follow Windows display numbers");
        TopologyStability topology;
        Check(!topology.Observe(c,0)&&!topology.Observe(c,999)&&topology.Observe(c,1000),"Initial topology did not wait for a stable snapshot");
        auto reordered=c;std::swap(reordered.outputs[0],reordered.outputs[1]);reordered.curve=TransferCurve::Gamma24;
        Check(topology.Observe(reordered,1001),"A curve change or enumeration reorder was treated as disconnect");
        auto fewer=c;fewer.count=2;
        Check(!topology.Observe(fewer,1100)&&!topology.Observe(fewer,2099)&&topology.Observe(fewer,2100),"SDR disconnect did not pause until the remaining topology stabilized");
        IsolationConfig noHdr=c;noHdr.count=1;noHdr.outputs[0]=c.outputs[1];
        Check(!topology.Observe(noHdr,2200)&&topology.Observe(noHdr,3200)&&!ShouldUseGamma(noHdr,0,true,true),"Removing all HDR targets enabled a variant");
        IsolationConfig empty={};empty.protocol=IsolationProtocol;
        Check(!topology.Observe(empty,3300)&&topology.Observe(empty,4300)&&!ShouldUseGamma(empty,0,true,true),"No-monitor state enabled a variant");
        topology.Reset();Check(!topology.Observe(c,4400)&&topology.Observe(c,5400),"Reconnect after transient enumeration error did not stabilize");

        IsolationShared switching={};switching.stats.appliedCurve=(LONG)TransferCurve::SRGB;
        UpdateIsolationConfig(switching,c);LONG sequence=switching.configSequence;
        switching.enabled=1;switching.stats.contextsMask=7;
        switching.stats.appliedSequence=sequence;switching.stats.appliedCurve=(LONG)TransferCurve::Gamma22;
        auto next=c;next.curve=TransferCurve::Gamma24;UpdateIsolationConfig(switching,next);
        IsolationConfig snapshot={};LONG observed=0;
        Check(ReadIsolationConfig(switching,snapshot,observed)&&snapshot.curve==TransferCurve::Gamma24&&observed==sequence,
            "Curve request invalidated the output snapshot");
        Check(switching.enabled&&switching.stats.contextsMask==7&&AppliedIsolationCurve(switching)==TransferCurve::Gamma22&&
            switching.fallbackCurve==(LONG)TransferCurve::Gamma22,"Curve switch interrupted conversion or discarded the confirmed curve");
        switching.stats.appliedCurve=(LONG)TransferCurve::Gamma24;
        Check(AppliedIsolationCurve(switching)==TransferCurve::Gamma24,"New curve hit was not reported");
        next.outputs[0].selected=FALSE;UpdateIsolationConfig(switching,next);
        Check(!switching.enabled&&switching.configSequence!=sequence&&AppliedIsolationCurve(switching)==TransferCurve::SRGB,
            "Target change reused a stale curve or output mapping");

        auto inventory = InspectShaders(ReadFileBytes(SystemDwmPath()));
        auto pixel = LinkSDR(inventory.containers[0]); std::array<Bytes,3> variants;
        ComPtr<ID3DBlob> asmCode;
        HR(D3DDisassemble(pixel->GetBufferPointer(), pixel->GetBufferSize(),0,nullptr,&asmCode), "Disassemble linked test");
        WriteText(L"build\\linked-sdr.txt",std::string((char*)asmCode->GetBufferPointer(),asmCode->GetBufferSize()));
        auto hdrPixel=LinkSDR(inventory.containers[0],"RGB_FULL_G2084_NONE_P2020_To_scRGB");Bytes hdrVariant;
        for (int mode=0;mode<3;++mode) {
            unsigned groups=0;
            Check(MakeIsolatedShader((BYTE*)pixel->GetBufferPointer(),pixel->GetBufferSize(),variants[mode],(TransferCurve)mode,&groups),"Linked Windows SDR shader pattern unsupported");
            Check(groups==1&&ValidChecksum(variants[mode].data(),variants[mode].size()),"Variant checksum/group validation failed");
            Check(!MakeIsolatedShader((BYTE*)hdrPixel->GetBufferPointer(),hdrPixel->GetBufferSize(),hdrVariant,(TransferCurve)mode)&&hdrVariant.empty(),"Actual Windows PQ decoder was changed");
        }
        Bytes identity;Check(!MakeIsolatedShader((BYTE*)pixel->GetBufferPointer(),pixel->GetBufferSize(),identity,TransferCurve::SRGB)&&identity.empty(),"sRGB created an unnecessary variant");
        WriteText(L"build\\isolated-original.dxbc", std::string((char*)pixel->GetBufferPointer(), pixel->GetBufferSize()));
        WriteText(L"build\\isolated-gamma.dxbc", std::string((char*)variants[0].data(), variants[0].size()));
        const char* nativeSource = "float4 main(float4 p:SV_Position):SV_Target { float3 x=float3(.1,.5,1); float3 y=pow(max(x,0),1.0/78.84375); return float4(pow(max(y-.8359375,0)/(18.8515625-18.6875*y),1.0/.1593017578125),1); }";
        ComPtr<ID3DBlob> native, error;
        HR(D3DCompile(nativeSource, strlen(nativeSource), nullptr, nullptr, nullptr, "main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &native, &error), "Compile HDR test");
        for(int mode=0;mode<3;++mode){Bytes refused;Check(!MakeIsolatedShader((BYTE*)native->GetBufferPointer(),native->GetBufferSize(),refused,(TransferCurve)mode)&&refused.empty(),"Native HDR shader changed");}

        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> ctx; D3D_FEATURE_LEVEL level;
        HR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &ctx), "Create isolated WARP test device");
        ComPtr<ID3D11PixelShader> originalShader;std::array<ComPtr<ID3D11PixelShader>,3> curveShaders;
        HR(device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &originalShader), "Create original shader");
        for(int mode=0;mode<3;++mode)HR(device->CreatePixelShader(variants[mode].data(),variants[mode].size(),nullptr,&curveShaders[mode]),"Create curve shader");
        const char* vertexSource = "cbuffer C:register(b0){float4 value;} struct V {float4 c:COLOR; float4 p:SV_Position;}; V main(uint i:SV_VertexID){V o; o.p=float4(i==2?3:-1,i==1?3:-1,0,1); o.c=value; return o;}";
        ComPtr<ID3DBlob> vsCode; HR(D3DCompile(vertexSource, strlen(vertexSource), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsCode, &error), "Compile VS");
        ComPtr<ID3D11VertexShader> vs; HR(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs), "Create VS");
        D3D11_BUFFER_DESC cbDesc = {16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        ComPtr<ID3D11Buffer> cb; HR(device->CreateBuffer(&cbDesc, nullptr, &cb), "Create constant buffer");
        D3D11_TEXTURE2D_DESC desc = {1, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1,0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
        ComPtr<ID3D11Texture2D> target, staging; HR(device->CreateTexture2D(&desc, nullptr, &target), "Create target");
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        HR(device->CreateTexture2D(&desc, nullptr, &staging), "Create staging");
        ComPtr<ID3D11RenderTargetView> rtv; HR(device->CreateRenderTargetView(target.Get(), nullptr, &rtv), "Create RTV");
        D3D11_RASTERIZER_DESC raster = {}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
        ComPtr<ID3D11RasterizerState> rs; HR(device->CreateRasterizerState(&raster,&rs), "Create rasterizer"); ctx->RSSetState(rs.Get());
        D3D11_VIEWPORT viewport = {0,0,1,1,0,1}; ctx->RSSetViewports(1, &viewport); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs.Get(), nullptr, 0); auto rawCB = cb.Get(); ctx->VSSetConstantBuffers(0,1,&rawCB); auto rawRTV = rtv.Get(); ctx->OMSetRenderTargets(1,&rawRTV,nullptr);
        std::vector<float> values={.001f,.04044f,.04045f,.04046f,.08099f,.081f,.08101f};
        for(int step=0;step<=128;++step)values.push_back(step/128.0f);
        for(int mode=0;mode<4;++mode)for(float alpha:{0.0f,.37f,1.0f})for (float x : values) for (unsigned monitor = 0; monitor < c.count; ++monitor) {
            c.curve=(TransferCurve)mode;
            bool corrected = ShouldUseGamma(c, (int)monitor, true, true);
            ctx->PSSetShader(corrected ? curveShaders[mode].Get() : originalShader.Get(), nullptr, 0);
            float v[4] = {x*alpha,x*alpha,x*alpha,alpha}; ctx->UpdateSubresource(cb.Get(),0,nullptr,v,0,0); ctx->Draw(3,0); ctx->CopyResource(staging.Get(),target.Get());
            D3D11_MAPPED_SUBRESOURCE map; HR(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map), "Read back test pixel");
            float y = ((float*)map.pData)[0],outputAlpha=((float*)map.pData)[3]; ctx->Unmap(staging.Get(),0);
            double expected = !corrected ? (x < .04045f ? x / 12.92 : std::pow((x+.055)/1.055,2.4)) :
                mode==0?std::pow(x,2.2):mode==1?std::pow(x,2.4):x<.081f?x/4.5:std::pow((x+.099)/1.099,1/.45);
            expected*=alpha;
            if (std::abs(y-expected)>=2e-5) {
                std::cout << "mode="<<mode<<" monitor=" << monitor << " x=" << x << " y=" << y << " expected=" << expected << "\n";
                ComPtr<ID3D11InfoQueue> queue; device.As(&queue);
                if (queue) for (UINT64 mi=0;mi<queue->GetNumStoredMessagesAllowedByRetrievalFilter();++mi) { SIZE_T size=0; queue->GetMessage(mi,nullptr,&size); std::vector<BYTE> bytes(size); auto msg=(D3D11_MESSAGE*)bytes.data(); queue->GetMessage(mi,msg,&size); std::cout << msg->pDescription << "\n"; }
            }
            Check(std::abs(y-expected)<2e-5, "GPU output did not match per-output EOTF");
            Check(std::abs(outputAlpha-alpha)<1e-6,"Transfer curve changed alpha");
        }
        ctx->ClearState(); ctx->Flush();
        std::cout << "PASS: mixed-monitor identity mapping; both SDR outputs keep original shader; unverified/mirrored outputs blocked.\n";
        std::cout << "PASS: Gamma 2.2, Gamma 2.4, Rec.709 and sRGB WARP GPU readback, including breakpoints and premultiplied alpha; native HDR rejected in every mode.\n";
        std::cout << "PASS: continuous curve requests and confirmed active curve; target-change invalidation; hotplug topology stability, SDR disconnect, all HDR removed, no monitors, transient enumeration reset and reconnect.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << "\n"; return 1; }
}
