#include "../src/core.h"
#include <shellapi.h>
static BOOL WINAPI TestNotifyIcon(DWORD action, PNOTIFYICONDATAW icon);
#define Shell_NotifyIconW TestNotifyIcon
#define wWinMain ResidentEntryPoint
#include "../src/main.cpp"
#undef wWinMain
#undef Shell_NotifyIconW
#include <iostream>

struct MockShell {
    bool present = false;
    unsigned failModify = 0, failAdd = 0, modifications = 0, additions = 0, versions = 0;
    HICON shown = nullptr;
    std::wstring tip;
} shell;
static BOOL WINAPI TestNotifyIcon(DWORD action, PNOTIFYICONDATAW icon) {
    if (action == NIM_ADD) {
        ++shell.additions;
        if (shell.failAdd) { --shell.failAdd; return FALSE; }
        if (shell.present || !(icon->uFlags & NIF_MESSAGE) || !(icon->uFlags & NIF_ICON) ||
            !(icon->uFlags & NIF_TIP) || icon->uCallbackMessage != TrayMessage) return FALSE;
        shell.present = true;
    } else if (action == NIM_MODIFY) {
        ++shell.modifications;
        if (shell.failModify) { --shell.failModify; return FALSE; }
        if (!shell.present) return FALSE;
    } else if (action == NIM_SETVERSION) {
        ++shell.versions; return shell.present && icon->uVersion == NOTIFYICON_VERSION_4;
    } else if (action == NIM_DELETE) {
        shell.present = false; return TRUE;
    } else return FALSE;
    if (icon->uFlags & NIF_ICON) shell.shown = icon->hIcon;
    if (icon->uFlags & NIF_TIP) shell.tip = icon->szTip;
    return TRUE;
}
static void Require(bool condition, const char* message) { if (!condition) throw Error(message); }
static void State(Shared& shared, Status status, TransferCurve curve, const wchar_t* detail) {
    shared.status = status; shared.activeCurve = (LONG)curve; wcscpy_s(shared.detail, detail);
}
static void Pump() {
    MSG message;
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
}
static void AwaitIcon(HICON expected) {
    auto end=GetTickCount64()+2000;
    while(shell.shown!=expected&&GetTickCount64()<end){
        MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);Pump();
    }
    Require(shell.shown==expected,"State event did not update the tray without a polling timer");
}
struct TestTray {
    Shared shared = {};
    Tray app;
    TestTray() {
        shell = {};
        app.channel.s = &shared;
        for (int mode = 0; mode < 4; ++mode) app.curveIcons[mode] = MakeTrayIcon((TransferCurve)mode, 16);
        app.sdrIcon=MakeSdrTrayIcon(16);
        shared.outputCount=2;shared.outputHdr[0]=TRUE;
        WNDCLASSW cls = {}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = WindowProc;
        cls.lpszClassName = L"SDREotf.Tray.StateTest";
        Require(RegisterClassW(&cls) != 0, "Register tray test window");
        Require(CreateWindowW(cls.lpszClassName, L"Tray test", WS_OVERLAPPED, 0, 0, 0, 0,
            nullptr, nullptr, cls.hInstance, &app) != nullptr, "Create tray test window");
        app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        State(shared, Patched, TransferCurve::Gamma22, L"Gamma 2.2 active");
        app.AddIcon();
    }
    ~TestTray() { app.channel.s = nullptr; if (app.window) DestroyWindow(app.window); }
};
int main() {
    try {
        TestTray test; auto& app = test.app; auto& shared = test.shared;
        auto srgb = app.curveIcons[(int)TransferCurve::SRGB];
        auto sdr = app.sdrIcon;
        auto gamma = app.curveIcons[(int)TransferCurve::Gamma22];
        Require(shell.shown == gamma, "Initial applied curve did not reach the tray");
        shared.outputHdr[0]=FALSE;
        State(shared, WaitingHDR, TransferCurve::SRGB, L"Waiting for HDR target"); app.Refresh();
        Require(shell.shown == sdr, "SDR display mode did not show the SDR icon");
        shared.outputHdr[0]=TRUE;
        State(shared, Patched, TransferCurve::Gamma22, L"Gamma 2.2 active");
        shell.failModify = 1; shell.failAdd = 1; app.Refresh();
        Require(shell.shown == sdr && app.lastStatus != Patched,
            "Failed reconnect update was cached as displayed");
        app.Refresh();
        Require(shell.shown == gamma && shell.tip == L"SDR EOTF · Gamma 2.2 active",
            "Unchanged applied state did not retry after the shell recovered");
        shell.shown = srgb;
        SendMessageW(app.window, WM_DISPLAYCHANGE, 32, 0);
        Require(shell.shown == gamma, "Display change did not resync an unchanged curve");
        shell.present = false;
        SendMessageW(app.window, WM_DISPLAYCHANGE, 32, 0);
        Require(shell.present && shell.shown == gamma && shell.versions >= 2,
            "Missing notification icon was not recreated with its callback version");
        shell.present = false;
        SendMessageW(app.window, app.taskbarCreated, 0, 0);
        Require(shell.present && shell.shown == gamma, "Explorer restart did not restore the active curve");
        auto now = GetTickCount64() + 10000; app.Refresh(now);
        auto modifications = shell.modifications;
        app.Refresh(now + 4999);
        Require(shell.modifications == modifications, "Unchanged tray state refreshed before the retry interval");
        // A successful call can still be lost later during Explorer's display rebuild.
        shell.shown = srgb; app.Refresh(now + 5000);
        Require(shell.shown == gamma, "Accepted but lost icon update was not repaired periodically");
        // While the helper prepares a curve, the worker reports the still-active old curve.
        app.selected=Gamma24;
        State(shared,Patched,TransferCurve::Gamma22,L"Gamma 2.2 · 正在切换到 Gamma 2.4");app.Refresh();
        Require(shell.shown==gamma,"Pending curve switch flashed the original-path icon");
        State(shared,Patched,TransferCurve::Gamma24,L"Gamma 2.4 active");app.Refresh();
        Require(shell.shown==app.curveIcons[(int)TransferCurve::Gamma24],"Confirmed switch did not update the active icon");
        for (int mode = 0; mode < 3; ++mode) {
            State(shared, Patched, (TransferCurve)mode, CurveName((TransferCurve)mode)); app.Refresh();
            Require(shell.shown == app.curveIcons[mode], "Applied curve selected the wrong icon");
        }
        for (auto status : {Starting, Working, Original, WaitingHDR, Failed, Stopped, BlockedMixedOutputs, WaitingTopology}) {
            State(shared, status, TransferCurve::Gamma22, L"Original or waiting"); app.Refresh();
            Require(shell.shown == srgb, "Inactive status displayed an applied curve");
        }
        State(shared, Patched, (TransferCurve)99, L"Invalid curve"); app.Refresh();
        Require(shell.shown == srgb, "Invalid active curve was not guarded");
        State(shared,Original,TransferCurve::SRGB,L"sRGB active");app.Refresh();
        Require(shell.shown==srgb,"HDR original path lost its sRGB icon");
        // HDR mode can change without changing the curve, status or detail.
        shared.outputHdr[0]=FALSE;shell.failModify=1;shell.failAdd=1;app.Refresh();
        Require(shell.shown==srgb&&app.lastIcon==srgb,"Failed SDR icon update was cached as displayed");
        app.Refresh();Require(shell.shown==sdr,"SDR mode change did not retry without a curve/status change");
        shared.outputHdr[1]=TRUE;app.Refresh();
        Require(shell.shown==srgb,"Mixed HDR/SDR desktop replaced the HDR sRGB label");
        shared.outputHdr[1]=FALSE;app.Refresh();Require(shell.shown==sdr,"Returning to all-SDR outputs kept sRGB");
        shared.outputCount=0;app.Refresh();Require(shell.shown==srgb,"Unknown display mode was labeled as confirmed SDR");
        shared.outputCount=2;shared.outputHdr[0]=TRUE;app.Refresh();
        // No WM_TIMER is installed in this test. Only the worker's auto-reset
        // event and ordinary message dispatch can deliver these changes.
        app.channel.response=Handle(CreateEventW(nullptr,FALSE,FALSE,nullptr));
        Require(app.channel.response.h!=nullptr,"Create state notification test event");
        State(shared,Patched,TransferCurve::Gamma24,L"Gamma 2.4 active");
        SetEvent(app.channel.response);app.channel.Watch(app.window);
        AwaitIcon(app.curveIcons[(int)TransferCurve::Gamma24]);
        for(auto curve:{TransferCurve::Gamma22,TransferCurve::Rec709,TransferCurve::Gamma24}){
            State(shared,Patched,curve,CurveName(curve));SetEvent(app.channel.response);
            AwaitIcon(app.curveIcons[(int)curve]);
        }
        State(shared,Original,TransferCurve::SRGB,L"sRGB active");SetEvent(app.channel.response);AwaitIcon(srgb);
        shared.outputHdr[0]=FALSE;SetEvent(app.channel.response);AwaitIcon(sdr);
        shared.outputHdr[0]=TRUE;SetEvent(app.channel.response);AwaitIcon(srgb);
        app.channel.StopWatching();Pump();
        State(shared,Patched,TransferCurve::Gamma22,L"Gamma 2.2 active");SetEvent(app.channel.response);
        Require(WaitForSingleObject(app.channel.response,0)==WAIT_OBJECT_0,"Stopped observer still consumed state events");
        Pump();Require(shell.shown==srgb,"Stopped observer still updated the tray");
        app.channel.Watch(app.window);SetEvent(app.channel.response);AwaitIcon(gamma);
        std::cout << "PASS: SDR display label, preserved HDR sRGB and mixed-output labels, unchanged-state HDR toggles and failed-update retry; event-driven curve/restoration updates without polling, observer restart/cleanup, HDR reconnect notification retry, immediate display-change resync, missing icon recovery, Explorer restart, periodic repair and curve/status icons.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << "\n"; return 1; }
}
