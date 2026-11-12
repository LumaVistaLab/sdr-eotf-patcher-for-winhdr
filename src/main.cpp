#include "core.h"
#include "isolation.h"
#include "tray_icon.h"
#include "info_window.h"
#include <shellapi.h>
#include <sstream>
#include <algorithm>
using namespace eotf;

static const wchar_t* ClassName = L"LumaVista.SdrEotfPatcher.v1";
static constexpr const wchar_t* ProductName = L"SDR EOTF Patcher for Windows HDR";
static constexpr UINT TrayMessage = WM_APP + 1, ControlMessage = WM_APP + 2, StateMessage = WM_APP + 3;
static constexpr UINT IdGamma = 100, IdSRGB = 101, IdInfo = 102, IdExit = 103;
static constexpr UINT IdGamma24 = 104, IdRec709 = 105;
static constexpr UINT IdOutputBase = 1000;
static constexpr UINT TimerId = 1;
static std::wstring ExePath() { wchar_t p[32768]; DWORD n = GetModuleFileNameW(nullptr, p, 32768); if (!n || n >= 32768) throw Error("Cannot locate executable"); return p; }
static std::wstring Directory() { auto p = ExePath(); return p.substr(0, p.find_last_of(L"\\/")); }
static bool Admin() {
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY; PSID group = nullptr; BOOL member = FALSE;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) { CheckTokenMembership(nullptr, group, &member); FreeSid(group); }
    return member != FALSE;
}

struct Channel {
    Handle mapping, request, response, parent, child;
    Shared* s = nullptr;
    ~Channel() { if (s) UnmapViewOfFile(s); }
    void Start() {
        SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
        if (!mapping.h) {
            mapping = Handle(CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(Shared), nullptr));
            request = Handle(CreateEventW(&sa, FALSE, FALSE, nullptr)); response = Handle(CreateEventW(&sa, FALSE, FALSE, nullptr));
            HANDLE raw = nullptr;
            if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &raw, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0)) throw Error(WinError("Duplicate parent handle"));
            parent = Handle(raw);
            if (!mapping.h || !request.h || !response.h) throw Error(WinError("Create guardian channel"));
            s = (Shared*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
            if (!s) throw Error(WinError("Map guardian channel"));
            ZeroMemory(s, sizeof(*s));
        }
        ResetEvent(request); ResetEvent(response); InterlockedExchange(&s->command, None);
        InterlockedExchange(&s->status, Starting);
        std::wostringstream args;
        args << L'"' << ExePath() << L"\" --worker " << (uintptr_t)mapping.h << L' ' << (uintptr_t)request.h << L' ' << (uintptr_t)response.h << L' ' << (uintptr_t)parent.h;
        std::wstring line = args.str();
        SIZE_T n = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &n);
        std::vector<BYTE> memory(n);
        auto attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)memory.data();
        if (!InitializeProcThreadAttributeList(attrs, 1, 0, &n)) throw Error(WinError("Initialize guardian attributes"));
        struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Cleanup() { DeleteProcThreadAttributeList(p); } } cleanup{attrs};
        HANDLE inherit[] = {mapping.h, request.h, response.h, parent.h};
        if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr)) throw Error(WinError("Set guardian handle list"));
        STARTUPINFOEXW startup = {}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attrs;
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(ExePath().c_str(), &line[0], nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                            nullptr, Directory().c_str(), &startup.StartupInfo, &pi)) throw Error(WinError("Start restoration guardian"));
        child = Handle(pi.hProcess); CloseHandle(pi.hThread);
        HANDLE waits[] = {child.h, response.h};
        if (WaitForMultipleObjects(2, waits, FALSE, 5000) != WAIT_OBJECT_0 + 1) throw Error("Guardian did not become ready");
    }
    void Send(Command c) { InterlockedExchange(&s->command, c); if (!SetEvent(request)) throw Error(WinError("Signal guardian")); }
    bool Alive() const { return child.h && WaitForSingleObject(child, 0) == WAIT_TIMEOUT; }
    bool StopAndWait() {
        if (!s) return true;
        if (Alive()) { Send(Stop); if (WaitForSingleObject(child, 55000) != WAIT_OBJECT_0) return false; }
        if (s->dirty) RestoreIsolated(*s);
        return !s->dirty;
    }
};

struct Tray {
    Channel channel;
    HWND window = nullptr;
    NOTIFYICONDATAW icon = {};
    HICON curveIcons[4] = {};
    UINT taskbarCreated = 0;
    Command selected = Gamma22;
    LONG lastStatus = -1;
    TransferCurve lastCurve = TransferCurve::SRGB;
    std::wstring lastDetail;
    std::wstring lastError;
    InfoWindow info;
    bool closing = false;

    void AddIcon() {
        icon = {}; icon.cbSize = sizeof(icon); icon.hWnd = window; icon.uID = 1;
        icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP; icon.uCallbackMessage = TrayMessage;
        icon.hIcon = curveIcons[(int)TransferCurve::SRGB]; wcscpy_s(icon.szTip, L"SDR EOTF · 初始化中");
        if (!Shell_NotifyIconW(NIM_ADD, &icon)) throw Error("Notification icon could not be added");
        icon.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &icon); lastStatus = -1; Refresh();
    }
    void SetError(const std::wstring& text) {
        lastError=text;icon.uFlags=NIF_TIP;
        std::wstring tip=L"SDR EOTF · "+text;wcsncpy_s(icon.szTip,tip.c_str(),_TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &icon);
    }
    void Refresh() {
        if (!channel.s) return;
        LONG state = InterlockedCompareExchange(&channel.s->status, 0, 0); MemoryBarrier();
        std::wstring detail = channel.s->detail;
        auto curve = state == Patched ? (TransferCurve)InterlockedCompareExchange(&channel.s->activeCurve,0,0) : TransferCurve::SRGB;
        if (!ValidCurve(curve)) curve = TransferCurve::SRGB;
        if (state != lastStatus || detail != lastDetail || curve != lastCurve) {
            icon.uFlags = NIF_ICON | NIF_TIP; icon.hIcon = curveIcons[(int)curve];
            std::wstring tip = L"SDR EOTF · " + detail; wcsncpy_s(icon.szTip, tip.c_str(), _TRUNCATE); Shell_NotifyIconW(NIM_MODIFY, &icon);
            lastStatus = state; lastDetail = detail; lastCurve = curve;
        }
    }
    void Set(Command cmd) {
        if (closing) return;
        if (!channel.Alive()) { if (channel.s->dirty) RestoreIsolated(*channel.s); channel.Start(); }
        if (CurveCommand(cmd)) selected = cmd;
        channel.Send(cmd);lastError.clear();lastStatus=-1;
    }
    struct OutputRow { std::wstring label; bool hdr, selected; UINT id; };
    static std::wstring OutputLabel(const wchar_t* name,bool hdr) {
        return std::wstring(name)+(hdr?L" · 仅处理 SDR 内容":L" · SDR 屏幕不适用");
    }
    std::vector<OutputRow> OutputRows() const {
        std::vector<OutputRow> rows;
        if (!channel.s) return rows;
        const unsigned count=std::min<unsigned>(channel.s->outputCount,MaxOutputs);
        for (unsigned i=0;i<count;++i) {
            bool hdr=channel.s->outputHdr[i]!=FALSE;
            std::wstring label=OutputLabel(channel.s->outputNames[i],hdr);
            rows.push_back({std::move(label),hdr,channel.s->selectedOutput==(LONG)i,IdOutputBase+i});
        }
        return rows;
    }
    HMENU CreateMenu() const {
        HMENU menu = CreatePopupMenu();
        // Standard string items let Windows measure the complete text at the menu's DPI.
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, ProductName);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (selected == Gamma22 ? MF_CHECKED : 0), IdGamma, L"Gamma 2.2");
        AppendMenuW(menu, MF_STRING | (selected == Gamma24 ? MF_CHECKED : 0), IdGamma24, L"Gamma 2.4");
        AppendMenuW(menu, MF_STRING | (selected == Rec709 ? MF_CHECKED : 0), IdRec709, L"Rec.709");
        AppendMenuW(menu, MF_STRING | (selected == SRGB ? MF_CHECKED : 0), IdSRGB, L"sRGB");
        auto outputs=OutputRows();
        if (!outputs.empty()) {
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            for (const auto& row:outputs) {
                AppendMenuW(menu,MF_STRING|(!row.hdr?MF_GRAYED:0)|(row.selected?MF_CHECKED:0),row.id,row.label.c_str());
            }
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IdInfo, L"状态与说明");
        AppendMenuW(menu, MF_STRING, IdExit, L"退出并恢复");
        return menu;
    }
    void Menu() {
        Refresh(); HMENU menu=CreateMenu();
        POINT p; GetCursorPos(&p); SetForegroundWindow(window);
        UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, 0, window, nullptr);
        DestroyMenu(menu); PostMessageW(window, WM_NULL, 0, 0);
        if (id == IdGamma) Set(Gamma22); else if (id == IdGamma24) Set(Gamma24); else if (id == IdRec709) Set(Rec709); else if (id == IdSRGB) Set(SRGB); else if (id == IdExit) Close(); else if (id == IdInfo) Info();
        else if (id>=IdOutputBase&&id<IdOutputBase+MaxOutputs) Set((Command)(100+id-IdOutputBase));
    }
    std::wstring InfoText(const IsolationConfig& config) const {
        std::wstring text;
        for(unsigned i=0;i<config.count;++i)text+=OutputLabel(config.outputs[i].name,config.outputs[i].hdr!=FALSE)+L"\r\n";
        if (text.empty()) text=L"当前没有可用的显示器列表。\r\n";
        text += L"\r\n作者已实测 HDR 与杜比视界模式均适用。按屏幕渲染上下文隔离，仅切换所选 HDR / 杜比视界输出中的 SDR 着色器；其他 SDR 屏幕保持原有路径，不做 Gamma 补偿。无法确认屏幕归属时使用原着色器。\r\n\r\nGamma 2.2：以 2.2 次幂解码 SDR 内容，用于匹配采用 Gamma 2.2 的常规 SDR 显示器。\r\nGamma 2.4：以 2.4 次幂解码 SDR 内容，用于匹配采用 Gamma 2.4 的参考级 SDR 监视器。\r\nRec.709：BT.709 分段传递曲线的逆变换，由暗部线性段和指数约 2.222 的幂函数段组成。\r\nsRGB：标准 sRGB 分段解码曲线，由暗部线性段和带偏移的 2.4 次幂段组成；使用系统原始解码，适合 sRGB 监看环境下制作的 SDR 内容。\r\n\r\n断开或连接显示器时静默更新；没有可用 HDR 目标屏时保持原始路径，重新连接后恢复所选曲线。切换和退出均不重启 DWM。退出后入口字节恢复；为避免卸载竞态，已停用的辅助模块保留到 DWM 自行退出。";
        std::wstring error=lastError;
        if(error.empty()&&channel.s&&channel.s->status==Failed)error=channel.s->detail;
        if(!error.empty())text+=L"\r\n\r\n当前问题："+error;
        return text;
    }
    void Info() {
        if(closing)return;
        info.Open(window,std::wstring(ProductName)+L" · 状态与说明",
            [this](const IsolationConfig& config){return InfoText(config);},[this](const std::wstring& error){SetError(error);});
    }
    void Poll() {
        if (!channel.Alive() && !closing) {
            try { RestoreIsolated(*channel.s); channel.Start(); selected = SRGB;lastError.clear(); }
            catch (const std::exception& e) { KillTimer(window, TimerId);SetError(Wide(e.what())); }
        }
        Refresh();
    }
    void Close() {
        if (closing) return; closing = true; KillTimer(window, TimerId);
        info.Close();
        bool restored = false;
        try { restored = channel.StopAndWait(); } catch (const std::exception& e) { SetError(Wide(e.what())); }
        if (!restored) {
            closing = false; SetTimer(window, TimerId, 500, nullptr);
            if(lastError.empty())SetError(L"尚未确认恢复成功，程序将保持运行以便重试。可再次选择“退出并恢复”。");return;
        }
        Shell_NotifyIconW(NIM_DELETE, &icon); DestroyWindow(window);
    }
    ~Tray() {
        for (auto handle : curveIcons) if (handle) DestroyIcon(handle);
        // Closing our parent handle signals the surviving child only when this process actually exits.
    }
};

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto app = (Tray*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) { app = (Tray*)((CREATESTRUCTW*)lp)->lpCreateParams; SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app); app->window = hwnd; }
    if (!app) return DefWindowProcW(hwnd, msg, wp, lp);
    try {
        if (app->taskbarCreated && msg == app->taskbarCreated) { app->AddIcon(); return 0; }
        if (msg == TrayMessage) {
            UINT code = LOWORD(lp);
            if (code == WM_CONTEXTMENU || code == NIN_SELECT || code == NIN_KEYSELECT) app->Menu(); return 0;
        }
        if (msg == ControlMessage) {
            if (wp == Stop) app->Close(); else if (CurveCommand((Command)wp) || (wp>=100&&wp<100+MaxOutputs)) app->Set((Command)wp); return 1;
        }
        if (msg == StateMessage) return app->channel.s ? InterlockedCompareExchange(&app->channel.s->status, 0, 0) : Starting;
        if (msg == InfoWindow::RequestMessage) { app->info.Dispatch();return 0; }
        if (msg == WM_DISPLAYCHANGE) { app->info.DisplayChanged();return 0; }
        if (msg == WM_TIMER) { if(wp==InfoWindow::Timer)app->info.Tick();else if(wp==TimerId)app->Poll(); return 0; }
        if (msg == WM_CLOSE) { app->Close(); return 0; }
        if (msg == WM_QUERYENDSESSION) return TRUE;
        if (msg == WM_ENDSESSION && wp) { app->channel.StopAndWait(); return 0; }
        if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    } catch (const std::exception& e) { app->SetError(Wide(e.what())); }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int RunTray(bool startSrgb) {
    Handle mutex(CreateMutexW(nullptr, TRUE, L"Local\\LumaVista.SdrEotfPatcher.v1"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    Tray app; app.selected = startSrgb ? SRGB : Gamma22;
    app.channel.Start(); // No patch until the independent restoration guardian is ready.
    const int iconSize = std::max(16,GetSystemMetrics(SM_CXSMICON));
    for (int i=0;i<4;++i) app.curveIcons[i] = MakeTrayIcon((TransferCurve)i,iconSize);
    app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc); wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = ClassName; wc.lpfnWndProc = WindowProc;
    if (!RegisterClassExW(&wc)) throw Error(WinError("Register tray window"));
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, ClassName, ProductName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, &app);
    if (!window) throw Error(WinError("Create tray window"));
    app.AddIcon(); SetTimer(window, TimerId, 500, nullptr);
    app.channel.Send(app.selected);
    MSG msg; int rc;
    while ((rc = GetMessageW(&msg, nullptr, 0, 0)) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return rc == -1 ? 1 : 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    struct FreeArgs { wchar_t** p; ~FreeArgs() { LocalFree(p); } } freeArgs{argv};
    try {
        if (argc == 3 && !wcscmp(argv[1], L"--verify-live")) return DiagnoseIsolation(argv[2],true);
        if (argc == 3 && !wcscmp(argv[1], L"--diagnose-isolation")) return DiagnoseIsolation(argv[2],false);
        if (argc == 3 && !wcscmp(argv[1], L"--verify-watchdog")) return DiagnoseIsolation(argv[2],true,true);
        if (argc == 3 && !wcscmp(argv[1], L"--verify-gamma24")) return DiagnoseIsolation(argv[2],true,false,TransferCurve::Gamma24);
        if (argc == 3 && !wcscmp(argv[1], L"--verify-bt709")) return DiagnoseIsolation(argv[2],true,false,TransferCurve::Rec709);
        if (argc == 6 && !wcscmp(argv[1], L"--worker")) {
            Handle mapping((HANDLE)(uintptr_t)_wcstoui64(argv[2], nullptr, 10));
            Handle request((HANDLE)(uintptr_t)_wcstoui64(argv[3], nullptr, 10));
            Handle response((HANDLE)(uintptr_t)_wcstoui64(argv[4], nullptr, 10));
            Handle parent((HANDLE)(uintptr_t)_wcstoui64(argv[5], nullptr, 10));
            auto s = (Shared*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
            if (!s || !GetProcessId(parent)) throw Error("Invalid inherited guardian handles");
            int rc = IsolatedWorker(*s, parent, request, response); UnmapViewOfFile(s); return rc;
        }
        if (argc == 3 && !wcscmp(argv[1], L"--inspect")) { WriteText(argv[2], Utf8(InspectReport())); return 0; }
        HWND existing = FindWindowW(ClassName, nullptr);
        if (argc >= 2 && (!wcscmp(argv[1], L"--exit") || !wcscmp(argv[1], L"--srgb") || !wcscmp(argv[1], L"--gamma22") || !wcscmp(argv[1], L"--gamma24") || !wcscmp(argv[1], L"--bt709") || !wcscmp(argv[1], L"--status"))) {
            if (!existing) return 2;
            DWORD_PTR result = 0;
            bool status = !wcscmp(argv[1], L"--status");
            Command c = !wcscmp(argv[1], L"--exit") ? Stop : !wcscmp(argv[1], L"--srgb") ? SRGB : !wcscmp(argv[1], L"--gamma24") ? Gamma24 : !wcscmp(argv[1], L"--bt709") ? Rec709 : Gamma22;
            if (!SendMessageTimeoutW(existing, status ? StateMessage : ControlMessage, status ? 0 : c, 0, SMTO_ABORTIFHUNG, 60000, &result)) throw Error(WinError("Send tray command (use the same privilege level)"));
            if (status && argc == 3) WriteText(argv[2], "status=" + std::to_string(result) + "\r\n");
            return status ? (int)result : 0;
        }
        if (existing) return 0;
        if (argc > 2 || (argc == 2 && wcscmp(argv[1], L"--srgb-start"))) throw Error("Usage: SdrEotfPatcher.exe [--srgb-start | --inspect REPORT | --gamma22 | --gamma24 | --bt709 | --srgb | --status REPORT | --exit]");
        if (!Admin()) {
            // Only the resident memory patcher needs elevation; read-only inspection does not.
            std::wstring path = ExePath();
            SHELLEXECUTEINFOW se = {}; se.cbSize = sizeof(se); se.fMask = SEE_MASK_NOCLOSEPROCESS;
            se.lpVerb = L"runas"; se.lpFile = path.c_str(); se.lpParameters = argc == 2 ? L"--srgb-start" : L""; se.nShow = SW_HIDE;
            if (!ShellExecuteExW(&se)) throw Error(WinError("Administrator permission is required to access DWM memory"));
            if (se.hProcess) CloseHandle(se.hProcess); return 0;
        }
        return RunTray(argc == 2);
    } catch (const std::exception&) {
        return 1;
    }
}
