#include "restoration_cover.h"
#include <d3d11.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <wrl/client.h>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"dcomp.lib")
#pragma comment(lib,"dwmapi.lib")
using Microsoft::WRL::ComPtr;

namespace eotf {
namespace {
void Check(HRESULT result, const char* operation) { if (FAILED(result)) throw Error(operation); }
constexpr wchar_t CoverClass[] = L"LumaVista.SdrEotf.RestorationCover";
LRESULT CALLBACK CoverProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_DISPLAYCHANGE) { ShowWindow(window, SW_HIDE); return 0; }
    return DefWindowProcW(window,message,wp,lp);
}
ATOM RegisterCoverClass() {
    WNDCLASSEXW cls = {}; cls.cbSize = sizeof(cls); cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = CoverProc; cls.lpszClassName = CoverClass;
    return RegisterClassExW(&cls);
}
void WaitForGpuCopy(ID3D11Device* device, ID3D11DeviceContext* context) {
    D3D11_QUERY_DESC description = {D3D11_QUERY_EVENT,0};
    ComPtr<ID3D11Query> completed;
    Check(device->CreateQuery(&description,&completed),"Create restoration copy completion query");
    context->End(completed.Get()); context->Flush();
    auto deadline = GetTickCount64()+500;
    HRESULT result;
    while ((result = context->GetData(completed.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
        if (GetTickCount64() >= deadline) throw Error("Restoration copy timed out");
        Sleep(1);
    }
    Check(result,"Complete restoration copy");
}
struct DpiScope {
    DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~DpiScope() { if (previous) SetThreadDpiAwarenessContext(previous); }
};
struct Surface {
    HWND window = nullptr;
    RECT bounds = {};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionSurface> bitmap;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;
    ComPtr<IDCompositionEffectGroup> opacity;
    ~Surface() {
        if (window) { ShowWindow(window,SW_HIDE); DestroyWindow(window); }
    }
    void Prepare(IDXGIFactory1* factory, const OutputConfig& output) {
        bounds = output.bounds;
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<IDXGIOutput5> monitor;
        for (UINT i = 0; !monitor; ++i) {
            ComPtr<IDXGIAdapter1> candidate;
            HRESULT result = factory->EnumAdapters1(i,&candidate);
            if (result == DXGI_ERROR_NOT_FOUND) break;
            Check(result,"Enumerate restoration adapter");
            DXGI_ADAPTER_DESC1 description = {}; Check(candidate->GetDesc1(&description),"Read restoration adapter");
            if (description.AdapterLuid.HighPart != output.adapter.HighPart || description.AdapterLuid.LowPart != output.adapter.LowPart) continue;
            for (UINT j = 0; ; ++j) {
                ComPtr<IDXGIOutput> raw; result = candidate->EnumOutputs(j,&raw);
                if (result == DXGI_ERROR_NOT_FOUND) break;
                Check(result,"Enumerate restoration output");
                DXGI_OUTPUT_DESC desc = {}; Check(raw->GetDesc(&desc),"Read restoration output");
                if (desc.AttachedToDesktop && EqualRect(&desc.DesktopCoordinates,&bounds)) {
                    Check(raw.As(&monitor),"HDR duplication unavailable"); adapter = candidate; break;
                }
            }
        }
        if (!monitor) throw Error("Restoration output unavailable");
        Check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Create restoration device");
        const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_B8G8R8A8_UNORM};
        ComPtr<IDXGIOutputDuplication> duplicate;
        Check(monitor->DuplicateOutput1(device.Get(),0,_countof(formats),formats,&duplicate),"Duplicate restoration output");
        DXGI_OUTDUPL_DESC duplicateDesc = {}; duplicate->GetDesc(&duplicateDesc);
        DXGI_OUTDUPL_FRAME_INFO info = {}; ComPtr<IDXGIResource> frame;
        auto acquireDeadline = GetTickCount64()+500;
        for (;;) {
            auto now = GetTickCount64();
            if (now >= acquireDeadline) throw Error("No updated restoration desktop image");
            Check(duplicate->AcquireNextFrame((UINT)(acquireDeadline-now),&info,&frame),"Acquire restoration image");
            // A pointer-only notification is not evidence of a valid first desktop image.
            if (info.LastPresentTime.QuadPart && info.AccumulatedFrames) break;
            duplicate->ReleaseFrame(); frame.Reset();
            if (GetTickCount64() >= acquireDeadline) throw Error("No updated restoration desktop image");
        }
        struct FrameRelease { IDXGIOutputDuplication* value; ~FrameRelease() { value->ReleaseFrame(); } } release{duplicate.Get()};
        ComPtr<ID3D11Texture2D> input; Check(frame.As(&input),"Read restoration image");
        D3D11_TEXTURE2D_DESC image = {}; input->GetDesc(&image);
        SnapshotDescription description = {image.Width,image.Height,image.Format == DXGI_FORMAT_R16G16B16A16_FLOAT,
            duplicateDesc.Rotation == DXGI_MODE_ROTATION_IDENTITY,info.ProtectedContentMaskedOut != FALSE,
            info.LastPresentTime.QuadPart != 0 && info.AccumulatedFrames != 0};
        if (!UsableRestorationSnapshot(bounds,description) || image.MipLevels != 1 || image.ArraySize != 1 || image.SampleDesc.Count != 1)
            throw Error("Restoration image cannot be reproduced unchanged");
        // All pixels stay in FP16 GPU memory. No tone mapping, shader conversion,
        // image file, video recording or display-mode change is performed here.
        window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
            CoverClass,L"",WS_POPUP,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
            nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if (!window || !SetPropW(window,L"NonRudeHWND",reinterpret_cast<HANDLE>(TRUE))) throw Error("Create restoration cover");
        BOOL disabled = TRUE;
        Check(DwmSetWindowAttribute(window,DWMWA_TRANSITIONS_FORCEDISABLED,&disabled,sizeof(disabled)),"Disable restoration animation");
        ComPtr<IDXGIDevice> dxgi; Check(device.As(&dxgi),"Query restoration DXGI device");
        Check(DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&composition)),"Create restoration composition");
        // An uninitialized DirectComposition surface is transparent. There is no
        // independent swap chain or empty flip buffer that can appear as black.
        Check(composition->CreateSurface(image.Width,image.Height,image.Format,DXGI_ALPHA_MODE_IGNORE,&bitmap),"Create restoration bitmap");
        ComPtr<IDXGISurface> update; POINT offset = {};
        Check(bitmap->BeginDraw(nullptr,IID_PPV_ARGS(&update),&offset),"Begin restoration bitmap update");
        struct EndUpdate {
            IDCompositionSurface* surface; bool pending = true;
            HRESULT End() { pending = false; return surface->EndDraw(); }
            ~EndUpdate() { if (pending) surface->EndDraw(); }
        } updateScope{bitmap.Get()};
        ComPtr<IDXGISurface2> subresource; Check(update.As(&subresource),"Query restoration update resource");
        ComPtr<ID3D11Texture2D> destination; UINT index = 0;
        Check(subresource->GetResource(IID_PPV_ARGS(&destination),&index),"Get restoration update texture");
        DXGI_SURFACE_DESC targetDesc = {}; Check(update->GetDesc(&targetDesc),"Read restoration update bounds");
        if (targetDesc.Format != image.Format || targetDesc.SampleDesc.Count != 1 ||
            !RestorationCopyFits(image.Width,image.Height,offset,targetDesc.Width,targetDesc.Height))
            throw Error("Restoration bitmap update does not fit");
        D3D11_BOX source = {0,0,0,image.Width,image.Height,1};
        context->CopySubresourceRegion(destination.Get(),index,(UINT)offset.x,(UINT)offset.y,0,input.Get(),0,&source);
        WaitForGpuCopy(device.Get(),context.Get());
        destination.Reset(); subresource.Reset(); update.Reset();
        Check(updateScope.End(),"Finish restoration bitmap update");
        Check(composition->CreateTargetForHwnd(window,TRUE,&target),"Create restoration target");
        Check(composition->CreateVisual(&visual),"Create restoration visual");
        Check(visual->SetContent(bitmap.Get()),"Set restoration content");
        Check(composition->CreateEffectGroup(&opacity),"Create restoration opacity effect");
        Check(opacity->SetOpacity(RestorationCoverOpacity),"Keep underlying windows composing");
        Check(visual->SetEffect(opacity.Get()),"Set restoration opacity effect");
        Check(target->SetRoot(visual.Get()),"Set restoration root");
        // Pixel copy completion, EndDraw, and the visual tree are all ready before
        // showing the HWND. WaitForCommitCompletion alone is not a pixel-copy fence.
        Check(composition->Commit(),"Commit restoration image");
        Check(composition->WaitForCommitCompletion(),"Wait for restoration content");
    }
};
}
struct RestorationCover::Impl {
    IsolationConfig config;
    unsigned mask;
    std::function<void()> heartbeat;
    std::vector<std::unique_ptr<Surface>> surfaces;
    ULONGLONG deadline = 0;
    Impl(const IsolationConfig& value,unsigned outputs,std::function<void()> tick)
        : config(value),mask(outputs),heartbeat(std::move(tick)) {}
};
RestorationCover::RestorationCover(const IsolationConfig& config,unsigned mask,std::function<void()> heartbeat)
    : impl(std::make_unique<Impl>(config,mask,std::move(heartbeat))) {}
RestorationCover::~RestorationCover() { Release(); }
bool RestorationCover::Show() {
    if (!impl->mask || !ValidOutputConfig(impl->config)) return false;
    try {
        DpiScope dpi; static const ATOM registered = RegisterCoverClass();
        if (!registered) return false;
        if (!SameOutputTopology(impl->config,OutputConfiguration())) return false;
        ComPtr<IDXGIFactory1> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create restoration factory");
        auto start = GetTickCount64();
        for (unsigned i = 0; i < impl->config.count; ++i) if (impl->mask & (1u << i)) {
            if (GetTickCount64()-start > 2000) throw Error("Restoration preparation timed out");
            if (impl->heartbeat) impl->heartbeat();
            auto surface = std::make_unique<Surface>(); surface->Prepare(factory.Get(),impl->config.outputs[i]);
            impl->surfaces.push_back(std::move(surface));
        }
        if (!SameOutputTopology(impl->config,OutputConfiguration()) || GetTickCount64()-start > 2000)
            throw Error("Restoration topology changed or preparation expired");
        if (impl->heartbeat) impl->heartbeat();
        HDWP windows = BeginDeferWindowPos((int)impl->surfaces.size());
        if (!windows) throw Error("Prepare restoration windows");
        for (auto& surface : impl->surfaces) {
            windows = DeferWindowPos(windows,surface->window,HWND_TOPMOST,0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
            if (!windows) throw Error("Prepare restoration window placement");
        }
        if (!EndDeferWindowPos(windows)) throw Error("Show restoration windows");
        Check(DwmFlush(),"Wait for restoration cover");
        impl->deadline = GetTickCount64()+1000;
        if (!Active()) { Release(); return false; }
        return true;
    } catch (...) { Release(); return false; }
}
bool RestorationCover::Active() const {
    if (impl->surfaces.empty()) return false;
    for (const auto& surface : impl->surfaces) if (!IsWindowVisible(surface->window)) return false;
    return true;
}
void RestorationCover::PrepareReveal() {
    Poll();
    if (!Active()) return;
    // Invalidate the whole non-occluding visual while the old picture still
    // dominates, allowing the original path underneath to compose before hiding.
    for (const auto& surface : impl->surfaces) {
        Check(surface->opacity->SetOpacity(RestorationRevealOpacity),"Prepare restored desktop reveal");
        Check(surface->composition->Commit(),"Commit restored desktop reveal");
    }
    for (const auto& surface : impl->surfaces)
        Check(surface->composition->WaitForCommitCompletion(),"Wait for restored desktop reveal");
    Check(DwmFlush(),"Compose before uncovering the restored desktop");
    Poll();
}
void RestorationCover::Poll() {
    bool lost = false;
    for (const auto& surface : impl->surfaces) {
        MSG message;
        while (PeekMessageW(&message,surface->window,0,0,PM_REMOVE)) {
            if (message.message == WM_QUIT) { PostQuitMessage((int)message.wParam); break; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (FAILED(surface->device->GetDeviceRemovedReason())) lost = true;
    }
    if (lost || !Active() || (impl->deadline && GetTickCount64() >= impl->deadline)) Release();
}
void RestorationCover::Release() noexcept {
    bool visible = false;
    HDWP windows = impl->surfaces.empty() ? nullptr : BeginDeferWindowPos((int)impl->surfaces.size());
    for (const auto& surface : impl->surfaces) {
        visible |= IsWindowVisible(surface->window) != FALSE;
        if (windows) windows = DeferWindowPos(windows,surface->window,nullptr,0,0,0,0,
            SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_HIDEWINDOW);
    }
    if (!windows || !EndDeferWindowPos(windows))
        for (const auto& surface : impl->surfaces) ShowWindow(surface->window,SW_HIDE);
    if (visible) DwmFlush();
    impl->surfaces.clear(); impl->deadline = 0;
}
}
