#include "core.h"
#include "isolation.h"
#include <tlhelp32.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace eotf {
std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring r(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &r[0], n); return r;
}
std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0); WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &r[0], n, nullptr, nullptr); return r;
}
std::string WinError(const char* action, DWORD error) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  nullptr, error, 0, (wchar_t*)&msg, 0, nullptr);
    std::string r = std::string(action) + " (" + std::to_string(error) + "): " + (msg ? Utf8(msg) : "Windows error");
    if (msg) LocalFree(msg); return r;
}
std::wstring SystemDwmPath() {
    wchar_t path[MAX_PATH]; if (!GetSystemDirectoryW(path, MAX_PATH)) throw Error(WinError("GetSystemDirectory"));
    return std::wstring(path) + L"\\dwmcore.dll";
}
Bytes ReadFileBytes(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw Error("Cannot read: " + Utf8(path));
    auto n = f.tellg(); if (n < 0 || n > 64 * 1024 * 1024) throw Error("Invalid input size");
    Bytes b((size_t)n); f.seekg(0);
    if (!f.read((char*)b.data(), (std::streamsize)b.size())) throw Error("Incomplete file read"); return b;
}
void WriteText(const std::wstring& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f || !f.write(text.data(), (std::streamsize)text.size())) throw Error("Cannot write report: " + Utf8(path));
}
static uint32_t U32(const BYTE* p) { uint32_t v; memcpy(&v, p, 4); return v; }
// Byte order here is the byte order stored in the DXBC header, not u128 display order.
static const char* allowed[] = {
    "4cde30edff9bee225da9465fdf59cd84", "7042a5de5c816b2d89d14ad3ccd76569",
    "96e6d1589255eccd1dd7d4dbec54d285", "2126b037c1a2fbdde355b6e6dd9caf3c",
    "2c8926ffe229f05d967c72668dc3addb", "f693bfbbaf24b3d9366354be8898a7f5"
};
static std::string Hex(const BYTE* p, size_t n) {
    std::ostringstream s; s << std::hex << std::setfill('0');
    for (size_t i = 0; i < n; ++i) s << std::setw(2) << (unsigned)p[i]; return s.str();
}
bool IsKnownSdrShader(const BYTE* digest) {
    auto s = Hex(digest, 16); for (auto h : allowed) if (s == h) return true; return false;
}
static bool Container(const Bytes& data, size_t i, size_t end, size_t& size) {
    if (end > data.size() || i > end || end - i < 32 || memcmp(data.data() + i, "DXBC", 4)) return false;
    auto p = data.data() + i; size = U32(p + 24);
    uint32_t count = U32(p + 28);
    if (U32(p + 20) != 1 || size < 32 || size > end - i || count > (size - 32) / 4) return false;
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t off = U32(p + 32 + k * 4);
        if (off < 32 + count * 4 || off > size - 8) return false;
        if (U32(p + off + 4) > size - off - 8) return false;
    }
    return true;
}
static int InspectContainer(const Bytes& data, size_t start, size_t size, int depth, ShaderInventory& inventory) {
    if (depth > 4) throw Error("DXBC nesting limit exceeded");
    if (!ValidChecksum(data.data() + start, size)) throw Error("DXBC checksum mismatch");
    bool known = IsKnownSdrShader(data.data() + start + 4);
    int shaders = 0, children = 0;
    for (size_t i = start + 32; i + 32 <= start + size;) {
        size_t childSize = 0;
        if (Container(data, i, start + size, childSize)) {
            ++children; shaders += InspectContainer(data, i, childSize, depth + 1, inventory); i += childSize;
        } else ++i;
    }
    if (children == 0) {
        if (known) return 1;
        ++inventory.untouched;
    } else if (known) throw Error("Unexpected nested known SDR shader");
    return shaders;
}
ShaderInventory InspectShaders(const Bytes& pe) {
    if (pe.size() < sizeof(IMAGE_DOS_HEADER)) throw Error("Truncated PE");
    IMAGE_DOS_HEADER dos; memcpy(&dos, pe.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        (size_t)dos.e_lfanew > pe.size() || pe.size() - dos.e_lfanew < sizeof(IMAGE_NT_HEADERS64)) throw Error("Invalid PE header");
    IMAGE_NT_HEADERS64 nt; memcpy(&nt, pe.data() + dos.e_lfanew, sizeof(nt));
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) throw Error("Only Windows x64 dwmcore.dll is supported");
    size_t table = dos.e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    if (table > pe.size() || nt.FileHeader.NumberOfSections > (pe.size() - table) / sizeof(IMAGE_SECTION_HEADER)) throw Error("Truncated PE sections");
    ShaderInventory result;
    for (unsigned s = 0; s < nt.FileHeader.NumberOfSections; ++s) {
        IMAGE_SECTION_HEADER sec; memcpy(&sec, pe.data() + table + s * sizeof(sec), sizeof(sec));
        size_t begin = sec.PointerToRawData, n = sec.SizeOfRawData;
        if (begin > pe.size() || n > pe.size() - begin) throw Error("Invalid PE section range");
        if (!(sec.Characteristics & IMAGE_SCN_MEM_READ) || (sec.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_WRITE))) continue;
        for (size_t i = begin; i + 32 <= begin + n;) {
            size_t size = 0;
            if (!Container(pe, i, begin + n, size)) { ++i; continue; }
            int count = InspectContainer(pe, i, size, 0, result);
            if (count) {
                if (i - begin + size > sec.Misc.VirtualSize || (ULONGLONG)sec.VirtualAddress + (i - begin) + size > nt.OptionalHeader.SizeOfImage)
                    throw Error("DXBC outside mapped section");
                ShaderContainer container{i, (DWORD)(sec.VirtualAddress + i - begin), Bytes(pe.begin() + i, pe.begin() + i + size), count};
                result.containers.push_back(std::move(container)); result.shaders += count;
            }
            i += size;
        }
    }
    if (!result.shaders) throw Error("No known SDR-to-scRGB shaders. This Windows build is unsupported; nothing was changed.");
    return result;
}

void EnableDebugPrivilege() {
    HANDLE raw; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &raw)) throw Error(WinError("OpenProcessToken"));
    Handle token(raw); TOKEN_PRIVILEGES tp = {}; tp.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &tp.Privileges[0].Luid)) throw Error(WinError("LookupPrivilegeValue"));
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    if (!AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr) || GetLastError() != ERROR_SUCCESS) throw Error(WinError("SeDebugPrivilege (run as administrator)"));
}
DWORD FindDwm() {
    DWORD session = 0; if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) throw Error(WinError("ProcessIdToSessionId"));
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snap.h == INVALID_HANDLE_VALUE) throw Error(WinError("Process snapshot"));
    PROCESSENTRY32W entry = {}; entry.dwSize = sizeof(entry);
    if (Process32FirstW(snap, &entry)) do {
        DWORD foundSession = 0;
        if (!_wcsicmp(entry.szExeFile, L"dwm.exe") && ProcessIdToSessionId(entry.th32ProcessID, &foundSession) && foundSession == session) return entry.th32ProcessID;
    } while (Process32NextW(snap, &entry));
    return 0;
}
ULONGLONG CreationTime(HANDLE process) {
    FILETIME c, e, k, u; if (!GetProcessTimes(process, &c, &e, &k, &u)) throw Error(WinError("GetProcessTimes"));
    return (ULONGLONG(c.dwHighDateTime) << 32) | c.dwLowDateTime;
}
static Handle OpenDwm(DWORD pid) {
    Handle p(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid)); if (!p.h) throw Error(WinError("OpenProcess(dwm.exe)"));
    wchar_t name[MAX_PATH] = {}; DWORD len = MAX_PATH;
    if (!QueryFullProcessImageNameW(p, 0, name, &len)) throw Error(WinError("QueryFullProcessImageName"));
    wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
    if (_wcsicmp(name, (std::wstring(sys) + L"\\dwm.exe").c_str())) throw Error("Unexpected DWM executable path");
    DWORD session, ours;
    if (!ProcessIdToSessionId(pid, &session) || !ProcessIdToSessionId(GetCurrentProcessId(), &ours) || session != ours) throw Error("DWM session mismatch");
    return p;
}
static uintptr_t ModuleBase(DWORD pid) {
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (snap.h == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W m = {}; m.dwSize = sizeof(m);
    if (Module32FirstW(snap, &m)) do {
        if (!_wcsicmp(m.szModule, L"dwmcore.dll")) {
            if (_wcsicmp(m.szExePath, SystemDwmPath().c_str())) throw Error("Unexpected dwmcore.dll path");
            return (uintptr_t)m.modBaseAddr;
        }
    } while (Module32NextW(snap, &m));
    return 0;
}
static void CheckMemory(HANDLE p, uintptr_t base, const ShaderInventory& inventory) {
    for (const auto& b : inventory.containers) {
        const auto& expect = b.original;
        Bytes actual(expect.size()); SIZE_T got = 0;
        if (!ReadProcessMemory(p, (void*)(base + b.rva), actual.data(), actual.size(), &got) || got != actual.size()) throw Error(WinError("ReadProcessMemory"));
        if (actual != expect) throw Error("DWM shader bytes differ from the expected image. Another patch or an updated module may be present; refusing to overwrite.");
    }
}
void VerifyShaderLibraries() {
    EnableDebugPrivilege(); auto inventory = InspectShaders(ReadFileBytes(SystemDwmPath()));
    DWORD pid = FindDwm(); if (!pid) throw Error("No DWM to verify");
    auto p = OpenDwm(pid); auto base = ModuleBase(pid);
    if (!base) throw Error("No dwmcore.dll to verify"); CheckMemory(p, base, inventory);
}
std::wstring InspectReport() {
    auto inventory = InspectShaders(ReadFileBytes(SystemDwmPath()));
    std::wostringstream r;
    r << L"SDR EOTF Patcher — read-only inspection\r\nModule: " << SystemDwmPath() << L"\r\n";
    r << L"Known SDR -> scRGB shaders: " << inventory.shaders << L"\r\nShader library containers: " << inventory.containers.size()
      << L"\r\nOther shader leaves left unchanged: " << inventory.untouched << L"\r\n";
    for (const auto& b : inventory.containers) r << L"RVA 0x" << std::hex << b.rva << std::dec << L", bytes " << b.original.size() << L", SDR shader leaves " << b.shaders << L"\r\n";
    auto config = OutputConfiguration();
    r << L"\r\n";
    for (unsigned i = 0; i < config.count; ++i) {
        const auto& output = config.outputs[i];
        r << output.name << L": " << (output.hdr ? L"HDR / Advanced Color" : L"SDR / WCG") << L"\r\n";
    }
    if (!config.count) r << L"未检测到活动显示器\r\n";
    r << L"\r\nExact isolation profile: " << (MatchesIsolationProfile() ? L"matched" : L"unsupported") << L"\r\n";
    r << L"Output identity configuration: " << (ValidOutputConfig(config) ? L"valid, including mixed HDR/SDR" : L"ambiguous") << L"\r\n";
    r << L"\r\nRuntime: isolated SDR EOTF instruction groups only on the selected HDR output. Shared shader libraries, PQ/HLG and calibration transforms remain unchanged.\r\n";
    return r.str();
}
}
