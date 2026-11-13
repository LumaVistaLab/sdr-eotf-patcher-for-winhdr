#include "../src/display_refresh.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <atomic>
#include <iostream>
#include <thread>
#pragma comment(lib, "shell32.lib")

using namespace eotf;
static void Require(bool value, const char* message) { if (!value) throw Error(message); }
static bool shown = false, validWindow = false;
static unsigned showCount = 0;
static RECT expectedBounds = {};
static LRESULT CALLBACK ObserveWindow(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0) {
        auto message = (CWPSTRUCT*)lp;
        if (message->message == WM_WINDOWPOSCHANGED && (((WINDOWPOS*)message->lParam)->flags & SWP_SHOWWINDOW)) {
            wchar_t name[128] = {}; GetClassNameW(message->hwnd, name, _countof(name));
            if (!wcscmp(name, L"LumaVista.SdrEotf.CompositionRefresh")) {
                // Hooks run in their registration DPI context. Measure physical
                // bounds even when this console test starts DPI-unaware.
                auto hookDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
                shown = true;
                ++showCount;
                LONG_PTR style = GetWindowLongPtrW(message->hwnd, GWL_EXSTYLE);
                constexpr LONG_PTR required = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
                BYTE alpha = 0; DWORD flags = 0; COLORREF color = 0; RECT bounds = {};
                bool valid = (style & required) == required &&
                    GetPropW(message->hwnd, L"NonRudeHWND") == reinterpret_cast<HANDLE>(TRUE) &&
                    GetLayeredWindowAttributes(message->hwnd, &color, &alpha, &flags) && alpha == 1 && flags == LWA_ALPHA &&
                    GetWindowRect(message->hwnd, &bounds) && EqualRect(&bounds, &expectedBounds) &&
                    SendMessageW(message->hwnd, WM_NCHITTEST, 0, 0) == HTTRANSPARENT;
                validWindow = showCount == 1 ? valid : validWindow && valid;
                if (!valid) {
                    GetLayeredWindowAttributes(message->hwnd, &color, &alpha, &flags); GetWindowRect(message->hwnd, &bounds);
                    std::cerr << "Refresh attributes: style=0x" << std::hex << style << " required=0x" << required << std::dec
                        << " alpha=" << (unsigned)alpha << " flags=" << flags << " bounds=" << bounds.left << ',' << bounds.top << ',' << bounds.right << ',' << bounds.bottom
                        << " expected=" << expectedBounds.left << ',' << expectedBounds.top << ',' << expectedBounds.right << ',' << expectedBounds.bottom
                        << " hit=" << SendMessageW(message->hwnd, WM_NCHITTEST, 0, 0) << "\n";
                }
                if (hookDpi) SetThreadDpiAwarenessContext(hookDpi);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

int main() {
    try {
        CompositionRefreshPlan plan;
        Require(plan.Next(2, TransferCurve::Gamma22, false, TransferCurve::SRGB, 1000), "Mapping did not request a frame");
        Require(!plan.Next(2, TransferCurve::Gamma22, false, TransferCurve::SRGB, 1001), "Refresh retry was not throttled");
        Require(plan.Next(2, TransferCurve::Gamma22, false, TransferCurve::SRGB, 1125), "Mapping retry was lost");
        Require(plan.Next(2, TransferCurve::Gamma22, true, TransferCurve::SRGB, 1126), "Arming did not redraw the static desktop");
        Require(plan.Next(2, TransferCurve::Gamma22, true, TransferCurve::Gamma22, 1127), "Confirmed hit did not request a final composition wake");
        Require(!plan.Pending() && !plan.Next(2, TransferCurve::Gamma22, true, TransferCurve::Gamma22, 5000), "Idle desktop kept refreshing");
        Require(plan.Next(2, TransferCurve::Gamma24, true, TransferCurve::Gamma22, 5001), "Curve change did not refresh");
        Require(plan.Next(2, TransferCurve::Gamma24, true, TransferCurve::Gamma24, 5002), "New curve confirmation did not redraw");
        Require(!plan.Pending(), "Confirmed curve left periodic refreshes");
        Require(plan.Next(4, TransferCurve::Gamma24, false, TransferCurve::SRGB, 6000), "Target change did not restart mapping refresh");
        for (unsigned i = 1; i < CompositionRefreshPlan::MaxAttempts; ++i)
            Require(plan.Next(4, TransferCurve::Gamma24, false, TransferCurve::SRGB, 6000 + i * CompositionRefreshPlan::RetryInterval), "Retry burst ended early");
        Require(!plan.Pending() && !plan.Next(4, TransferCurve::Gamma24, false, TransferCurve::SRGB, 20000), "Missing shader caused endless refreshes");

        auto priorDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        expectedBounds.left = GetSystemMetrics(SM_XVIRTUALSCREEN); expectedBounds.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        expectedBounds.right = expectedBounds.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        expectedBounds.bottom = expectedBounds.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (priorDpi) SetThreadDpiAwarenessContext(priorDpi);
        HWND foreground = GetForegroundWindow(); POINT before = {}, after = {}; GetCursorPos(&before);
        HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, ObserveWindow, nullptr, GetCurrentThreadId());
        Require(hook != nullptr, "Could not observe native refresh window");
        QUERY_USER_NOTIFICATION_STATE notification = QUNS_ACCEPTS_NOTIFICATIONS;
        HRESULT notificationResult = SHQueryUserNotificationState(&notification);
        HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr); RECT taskbarBounds = {};
        GetWindowRect(taskbar, &taskbarBounds);
        bool taskbarVisible = IsWindowVisible(taskbar) != FALSE;
        LONG_PTR taskbarTopmost = GetWindowLongPtrW(taskbar, GWL_EXSTYLE) & WS_EX_TOPMOST;
        std::atomic<bool> finished{false}, notificationChanged{false}, taskbarChanged{false};
        std::thread observer([&]() {
            // Match the baseline DPI context when checking Explorer's bounds.
            auto observerDpi = SetThreadDpiAwarenessContext(priorDpi);
            do {
                QUERY_USER_NOTIFICATION_STATE current = QUNS_ACCEPTS_NOTIFICATIONS;
                if (SUCCEEDED(notificationResult) && SUCCEEDED(SHQueryUserNotificationState(&current)) && current != notification)
                    notificationChanged = true;
                if (taskbar) {
                    RECT bounds = {}; GetWindowRect(taskbar, &bounds);
                    if (!EqualRect(&bounds, &taskbarBounds) || (IsWindowVisible(taskbar) != FALSE) != taskbarVisible ||
                        (GetWindowLongPtrW(taskbar, GWL_EXSTYLE) & WS_EX_TOPMOST) != taskbarTopmost) taskbarChanged = true;
                }
                Sleep(1);
            } while (!finished);
            if (observerDpi) SetThreadDpiAwarenessContext(observerDpi);
        });
        bool refreshed = RefreshComposedDesktop();
        Sleep(100); finished = true; observer.join();
        UnhookWindowsHookEx(hook); GetCursorPos(&after);
        if (!refreshed || !shown || !validWindow) {
            BOOL composition = FALSE; HRESULT query = DwmIsCompositionEnabled(&composition);
            std::cerr << "Native refresh: completed=" << refreshed << " shown=" << shown << " attributes=" << validWindow
                << " composition=" << composition << " query=0x" << std::hex << query << " flush=0x" << DwmFlush() << std::dec << "\n";
        }
        Require(refreshed && shown && validWindow, "Full-desktop nonactivating composition refresh failed");
        Require(showCount == 1, "Composition wake unexpectedly repeated");
        Require(!FindWindowW(L"LumaVista.SdrEotf.CompositionRefresh", nullptr), "Refresh window was left behind");
        Require(GetForegroundWindow() == foreground && before.x == after.x && before.y == after.y, "Refresh changed focus or cursor position");
        Require(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), priorDpi), "Refresh did not restore thread DPI awareness");
        Require(!notificationChanged, "Refresh changed the Shell notification state");
        Require(!taskbarChanged, "Refresh moved, hid or restacked the taskbar");
        std::cout << "PASS: bounded mapping/arming retries, curve and target changes, confirmed-curve composition wake, idle cleanup; native full-desktop alpha=1 refresh preserves focus, cursor and DPI.\n";
        std::cout << "Shell observation: notification query=0x" << std::hex << notificationResult << std::dec
            << " state=" << notification << " taskbar=" << (taskbar ? "observed" : "unavailable")
            << "; NonRudeHWND set before show; notification state=" << (SUCCEEDED(notificationResult) ? "unchanged" : "unavailable")
            << "; taskbar state=" << (taskbar ? "unchanged" : "unavailable") << ".\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << "\n"; return 1; }
}
