#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
static BOOL CALLBACK Print(PSYMBOL_INFO info, ULONG, PVOID file) {
    char undecorated[16384];
    if (!UnDecorateSymbolName(info->Name, undecorated, sizeof(undecorated), UNDNAME_COMPLETE)) strcpy_s(undecorated, info->Name);
    fprintf((FILE*)file, "%08llx %08lx %s\n", info->Address - info->ModBase, info->Size, undecorated);
    return TRUE;
}
int main() {
    HANDLE p = GetCurrentProcess();
    SymSetOptions(SYMOPT_EXACT_SYMBOLS | SYMOPT_FAIL_CRITICAL_ERRORS);
    if (!SymInitialize(p, "build\\symbols", FALSE)) return 1;
    auto base = SymLoadModuleEx(p, nullptr, "C:\\Windows\\System32\\dwmcore.dll", "dwmcore", 0x180000000, 0, nullptr, 0);
    if (!base) { printf("SymLoadModule: %lu\n", GetLastError()); return 2; }
    FILE* f = nullptr; fopen_s(&f, "build/dwm-symbols.txt", "wb");
    BOOL ok = SymEnumSymbols(p, base, "*", Print, f); fclose(f);
    char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]; auto type = (SYMBOL_INFO*)storage;
    ZeroMemory(storage, sizeof(storage)); type->SizeOfStruct = sizeof(SYMBOL_INFO); type->MaxNameLen = MAX_SYM_NAME;
    const char* types[] = {"tagCOMPOSITION_TARGET_ID", "PixelFormatInfo", "CommonRenderingShaderDesc", "RenderTargetInfo", "CLegacyRenderTarget"};
    for (auto name : types) {
        if (!SymGetTypeFromName(p, base, name, type)) { printf("type %s unavailable (%lu)\n", name, GetLastError()); continue; }
        ULONG64 len = 0; SymGetTypeInfo(p, base, type->TypeIndex, TI_GET_LENGTH, &len);
        printf("type %s: index %lu, length %llu\n", name, type->TypeIndex, len);
    }
    printf("symbols: %s, error %lu\n", ok ? "ok" : "failed", GetLastError());
    SymCleanup(p); return ok ? 0 : 3;
}
