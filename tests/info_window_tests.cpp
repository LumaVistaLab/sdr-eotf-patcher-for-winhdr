#define wWinMain ResidentEntryPoint
#include "../src/main.cpp"
#undef wWinMain
#include <iostream>

static IsolationConfig testConfig;
static bool transition=false;
static std::function<void()> duringDisplay;
static std::wstring displayed;
static unsigned calls=0,depth=0,maxDepth=0;
static IsolationConfig ReadTestConfig(){if(transition)throw DisplayTransition("Simulated hotplug transition");return testConfig;}
static void Require(bool condition,const char* message){if(!condition)throw Error(message);}
static void Pump(){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
static int WINAPI QuietDisplay(HWND,LPCWSTR text,LPCWSTR,UINT){
    ++calls;++depth;maxDepth=std::max(depth,maxDepth);displayed=text;if(duringDisplay)duringDisplay();--depth;return IDOK;
}
static unsigned DialogCount(HWND owner){
    struct Count{HWND owner;unsigned value;}count{owner,0};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL {
        auto state=(Count*)data;wchar_t name[64]={};GetClassNameW(hwnd,name,64);
        if(GetWindow(hwnd,GW_OWNER)==state->owner&&!wcscmp(name,L"#32770"))++state->value;return TRUE;
    },(LPARAM)&count);return count.value;
}
static Tray* nativeApp=nullptr;
static unsigned nativeBase=0,nativeStep=0;
static LONG firstWidth=0;
static ULONGLONG nativeStart=0;
static void CALLBACK NativeTimer(HWND owner,UINT,UINT_PTR,DWORD){
    if(GetTickCount64()-nativeStart>5000){std::cerr<<"FAIL: native message box did not close\n"<<std::flush;ExitProcess(78);}
    HWND box=nativeApp->info.Handle();if(!box)return;
    if(DialogCount(owner)!=1){std::cerr<<"FAIL: multiple native message boxes\n"<<std::flush;ExitProcess(79);}
    RECT rect={};GetWindowRect(box,&rect);
    if(nativeStep==0){
        firstWidth=rect.right-rect.left;nativeStep=1;nativeApp->Info();
    }else if(nativeStep==1&&nativeApp->info.Generation()==nativeBase+2){
        if(firstWidth!=rect.right-rect.left){std::cerr<<"FAIL: native width changed between replacements\n"<<std::flush;ExitProcess(80);}
        std::cout<<"Native Windows message box width="<<firstWidth<<" dpi="<<GetDpiForWindow(box)<<"\n";
        nativeStep=2;EndDialog(box,IDOK);
    }
}
int main(){
    try {
        testConfig={};testConfig.protocol=IsolationProtocol;testConfig.count=3;
        testConfig.outputs[0]={{0,0,1920,1080},{},0,FALSE,FALSE,L"Integrated Monitor"};
        testConfig.outputs[1]={{1920,0,5760,2160},{},1,TRUE,TRUE,L"Mi Monitor"};
        testConfig.outputs[2]={{0,1080,1920,2160},{},2,FALSE,FALSE,L"ASLFHD"};
        Tray app;app.info.read=ReadTestConfig;app.info.display=QuietDisplay;
        WNDCLASSEXW cls={};cls.cbSize=sizeof(cls);cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=WindowProc;cls.lpszClassName=L"SDREotf.Info.TestHost";
        Require(RegisterClassExW(&cls)!=0,"Register test host");
        HWND owner=CreateWindowW(cls.lpszClassName,L"Info window test host",WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,cls.hInstance,&app);
        Require(owner!=nullptr,"Create test host");
        // Reenter through the tray while the prior dialog's call is still active.
        duringDisplay=[&](){if(calls<25){app.Info();Pump();}};app.Info();Pump();
        Require(calls==25&&maxDepth==1&&!app.info.Wanted(),"Repeated requests were nested or lost");
        Require(displayed.find(L"Mi Monitor · 仅处理 SDR 内容")!=std::wstring::npos,"Initial current snapshot missing");
        calls=0;duringDisplay=[&](){app.info.DisplayChanged();};app.Info();Pump();
        Require(calls==1&&app.info.Wanted(),"Hotplug did not preserve the requested window");
        testConfig.count=2;testConfig.outputs[1]=testConfig.outputs[2];duringDisplay={};
        app.info.Tick(1000);app.info.Tick(1999);Pump();Require(calls==1,"Hotplug reopened an unstable snapshot");
        app.info.Tick(2000);Pump();Require(calls==2&&displayed.find(L"Mi Monitor")==std::wstring::npos,"Disconnected HDR screen remained in the replacement");
        duringDisplay=[&](){testConfig.count=0;app.info.Tick(3000);};app.Info();Pump();duringDisplay={};
        app.info.Tick(4000);Pump();Require(displayed.find(L"当前没有可用的显示器列表")!=std::wstring::npos,"Polling missed empty topology");
        transition=true;unsigned before=calls;app.Info();app.Info();Pump();Require(calls==before&&app.info.Wanted(),"Transition opened stale content");
        transition=false;testConfig.count=1;app.info.Tick(6000);app.info.Tick(7000);Pump();
        Require(displayed.find(L"Integrated Monitor")!=std::wstring::npos&&!app.info.Wanted(),"Reconnect did not show a fresh snapshot");
        before=calls;SendMessageW(owner,WM_DISPLAYCHANGE,32,0);app.info.Tick(8000);Pump();Require(calls==before,"Closed information box reopened automatically");
        // Exercise the actual Windows box twice, rather than approximate its layout.
        app.info.read=OutputConfiguration;app.info.display=MessageBoxW;nativeApp=&app;nativeBase=app.info.Generation();nativeStart=GetTickCount64();
        SetTimer(owner,99,100,NativeTimer);app.Info();
        MSG message;while(app.info.Wanted()||app.info.Active()){Require(GetMessageW(&message,nullptr,0,0)>0,"Native message loop ended");TranslateMessage(&message);DispatchMessageW(&message);}
        KillTimer(owner,99);Require(nativeStep==2&&DialogCount(owner)==0,"Native replacement/cleanup failed");
        app.info.Close();DestroyWindow(owner);
        std::cout<<"PASS: 25 reentrant opens without nesting; latest stable hotplug/empty/reconnect snapshots; user close; actual native message-box replacement and layout.\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}
}
