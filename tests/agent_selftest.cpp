#include "../src/core.h"
#include <iostream>
int main(){
    try{
        auto code=eotf::ReadFileBytes(L"build\\isolated-original.dxbc");
        wchar_t absolute[32768];GetFullPathNameW(L"build\\SdrEotfAgentTest.dll",32768,absolute,nullptr);
        HMODULE dll=LoadLibraryW(absolute);if(!dll)throw eotf::Error(eotf::WinError("Load test agent"));
        auto test=(DWORD(WINAPI*)(const BYTE*,size_t))GetProcAddress(dll,"TestAgentHooks");
        if(!test)throw eotf::Error("Test export missing");DWORD result=test(code.data(),code.size());FreeLibrary(dll);
        if(result)throw eotf::Error("Agent hook simulation failed: "+std::to_string(result));
        std::cout<<"PASS: actual native hooks and x64 thunks; immediate mapping/confirmed-curve events, no notifications for pending or unchanged curves; continuous curve switching with cached fallback, HDR-only variant selection, SDR identity, disable restoration, unknown-context bypass.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
