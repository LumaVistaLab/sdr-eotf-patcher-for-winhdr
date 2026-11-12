#pragma once
#include "isolation.h"
#include <functional>
#include <cstring>

namespace eotf {
// Preserve Windows' original message-box layout, DPI rules and information sound.
// Reentrant requests end the current box; its replacement is posted only after
// MessageBoxW returns. The application never nests information message boxes.
class InfoWindow {
    HWND owner_ = nullptr;
    bool wanted_ = false, pending_ = false, ready_ = false, queued_ = false, active_ = false, observed_ = false;
    IsolationConfig shown_ = {}, probe_ = {};
    ULONGLONG stableSince_ = 0;
    unsigned generation_ = 0;
    std::wstring title_;
    std::function<std::wstring(const IsolationConfig&)> build_;
    std::function<void(const std::wstring&)> error_;
    static bool Same(const IsolationConfig& a,const IsolationConfig& b) {
        if(!SameOutputTopology(a,b))return false;
        for(unsigned i=0;i<a.count;++i) {
            if(memcmp(&a.outputs[i].adapter,&b.outputs[i].adapter,sizeof(LUID))||a.outputs[i].target!=b.outputs[i].target||
                wcscmp(a.outputs[i].name,b.outputs[i].name))return false;
        }
        return true;
    }
    void EndCurrent(){HWND current=Handle();if(current)EndDialog(current,IDOK);}
    void Schedule() {
        if(!active_&&wanted_&&pending_&&ready_&&!queued_) {
            if(!PostMessageW(owner_,RequestMessage,0,0))throw Error(WinError("Queue information window"));
            queued_=true;
        }
    }
    void Queue(bool immediate){pending_=true;ready_=immediate;observed_=false;EndCurrent();Schedule();}
    void Failed(const std::exception& error){Close();if(error_)error_(Wide(error.what()));}
public:
    static constexpr UINT Timer=2,RequestMessage=WM_APP+4;
    IsolationConfig (*read)()=OutputConfiguration;
    int (WINAPI *display)(HWND,LPCWSTR,LPCWSTR,UINT)=MessageBoxW;
    ~InfoWindow(){Close();}
    HWND Handle() const {
        if(!active_)return nullptr;
        struct Search{HWND owner,found;};Search search{owner_,nullptr};
        EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL {
            auto state=(Search*)data;wchar_t name[32]={};GetClassNameW(hwnd,name,32);
            if(GetWindow(hwnd,GW_OWNER)==state->owner&&!wcscmp(name,L"#32770")){state->found=hwnd;return FALSE;}
            return TRUE;
        },(LPARAM)&search);
        return search.found;
    }
    unsigned Generation() const{return generation_;}
    bool Wanted() const{return wanted_;}
    bool Active() const{return active_;}
    void Open(HWND owner,const std::wstring& title,std::function<std::wstring(const IsolationConfig&)> build,
              std::function<void(const std::wstring&)> error) {
        owner_=owner;title_=title;build_=std::move(build);error_=std::move(error);wanted_=true;
        SetTimer(owner_,Timer,500,nullptr);Queue(true);
    }
    void Dispatch() {
        queued_=false;if(active_||!wanted_||!pending_||!ready_)return;
        try {
            auto config=read();auto text=build_(config);auto title=title_;
            shown_=config;pending_=false;ready_=false;observed_=false;active_=true;++generation_;
            int result=display(owner_,text.c_str(),title.c_str(),MB_OK|MB_ICONINFORMATION);
            active_=false;
            if(!result)throw Error(WinError("Show information window"));
            if(!pending_)Close();else Schedule();
        }catch(const DisplayTransition&){active_=false;pending_=true;ready_=false;observed_=false;}
        catch(const std::exception& failure){active_=false;Failed(failure);}
    }
    void DisplayChanged(){if(wanted_)Queue(false);}
    void Tick(ULONGLONG now=GetTickCount64()) {
        if(!wanted_)return;
        try {
            auto config=read();if(active_&&!Same(config,shown_))Queue(false);
            if(!pending_||ready_)return;
            if(!observed_||!Same(config,probe_)){probe_=config;stableSince_=now;observed_=true;return;}
            if(now-stableSince_>=1000){ready_=true;Schedule();}
        }catch(const DisplayTransition&){Queue(false);}
        catch(const std::exception& failure){Failed(failure);}
    }
    void Close(){wanted_=false;pending_=false;ready_=false;observed_=false;if(owner_)KillTimer(owner_,Timer);EndCurrent();}
};
}
