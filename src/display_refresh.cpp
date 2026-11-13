#include "display_refresh.h"
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")

namespace eotf {
bool CompositionRefreshPlan::Next(LONG configSequence, TransferCurve curve,
    bool conversionEnabled, TransferCurve applied, ULONGLONG now) {
    if (sequence != configSequence || requested != curve || enabled != conversionEnabled) {
        sequence = configSequence; requested = curve; enabled = conversionEnabled;
        confirmed = TransferCurve::SRGB; remaining = MaxAttempts; next = now;
    }
    if (enabled && applied == requested && applied != confirmed) {
        confirmed = applied; remaining = 1; next = now;
    }
    if (!remaining || now < next) return false;
    --remaining; next = now + RetryInterval; return true;
}

namespace {
constexpr wchar_t RefreshClass[] = L"LumaVista.SdrEotf.CompositionRefresh";
LRESULT CALLBACK RefreshProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wp, lp);
}
ATOM RegisterRefreshClass() {
    WNDCLASSEXW cls = {}; cls.cbSize = sizeof(cls);
    cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = RefreshProc;
    cls.lpszClassName = RefreshClass; cls.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    return RegisterClassExW(&cls);
}
}

bool RefreshComposedDesktop() {
    static const ATOM registered = RegisterRefreshClass();
    if (!registered) return false;
    // Display configuration uses physical pixels, including negative origins.
    struct DpiScope {
        DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~DpiScope() { if (previous) SetThreadDpiAwarenessContext(previous); }
    } dpi;
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN), height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) return false;
    struct Window {
        HWND handle = nullptr;
        ~Window() { if (handle) DestroyWindow(handle); }
    } window;
    window.handle = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        RefreshClass, L"", WS_POPUP, x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window.handle) return false;
    // Opt out before the first show: Shell's fullscreen detection otherwise
    // toggles notification suppression and drops/restores the taskbar's z-order.
    // https://learn.microsoft.com/windows/win32/api/shobjidl_core/nf-shobjidl_core-itaskbarlist2-markfullscreenwindow
    if (!SetPropW(window.handle, L"NonRudeHWND", reinterpret_cast<HANDLE>(TRUE))) return false;
    BOOL disabled = TRUE;
    DwmSetWindowAttribute(window.handle, DWMWA_TRANSITIONS_FORCEDISABLED, &disabled, sizeof(disabled));
    // Alpha zero can be discarded by the compositor. The minimum nonzero alpha
    // wakes composition, without guaranteeing one-frame source recomposition.
    if (!SetLayeredWindowAttributes(window.handle, 0, 1, LWA_ALPHA)) return false;
    if (!SetWindowPos(window.handle, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW)) return false;
    RedrawWindow(window.handle, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    HRESULT shown = DwmFlush();
    ShowWindow(window.handle, SW_HIDE);
    // Wait for both changes: otherwise DWM may coalesce show/hide into no damage.
    HRESULT hidden = DwmFlush();
    return SUCCEEDED(shown) && SUCCEEDED(hidden);
}

bool RefreshRestoredDesktop() {
    bool completed = true;
    for (unsigned pass = 0; pass < 3; ++pass) {
        if (pass) Sleep((DWORD)CompositionRefreshPlan::RetryInterval);
        if (!RefreshComposedDesktop()) completed = false;
    }
    return completed;
}

}
