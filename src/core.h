#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace eotf {
using Bytes = std::vector<BYTE>;
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
struct Handle {
    HANDLE h = nullptr;
    Handle() = default;
    explicit Handle(HANDLE v) : h(v) {}
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& v) noexcept : h(v.h) { v.h = nullptr; }
    Handle& operator=(Handle&& v) noexcept { if (this != &v) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = v.h; v.h = nullptr; } return *this; }
    operator HANDLE() const { return h; }
};
struct ShaderContainer { size_t offset; DWORD rva; Bytes original; int shaders; };
struct ShaderInventory { std::vector<ShaderContainer> containers; int shaders = 0; int untouched = 0; };

enum class TransferCurve : LONG { Gamma22, Gamma24, Rec709, SRGB };
inline bool ValidCurve(TransferCurve c) { return c >= TransferCurve::Gamma22 && c <= TransferCurve::SRGB; }
inline const wchar_t* CurveName(TransferCurve c) {
    switch (c) { case TransferCurve::Gamma22: return L"Gamma 2.2"; case TransferCurve::Gamma24: return L"Gamma 2.4";
    case TransferCurve::Rec709: return L"Rec.709"; default: return L"sRGB"; }
}
inline const wchar_t* CurveLabel(TransferCurve c) {
    switch (c) { case TransferCurve::Gamma22: return L"2.2"; case TransferCurve::Gamma24: return L"2.4";
    case TransferCurve::Rec709: return L"709"; default: return L"sRGB"; }
}
enum Command : LONG { None, Gamma22, SRGB, Stop, Gamma24, Rec709 };
inline bool CurveCommand(Command c) { return c == Gamma22 || c == Gamma24 || c == Rec709 || c == SRGB; }
inline TransferCurve CommandCurve(Command c) {
    return c == Gamma22 ? TransferCurve::Gamma22 : c == Gamma24 ? TransferCurve::Gamma24 : c == Rec709 ? TransferCurve::Rec709 : TransferCurve::SRGB;
}
enum Status : LONG { Starting, Working, Patched, Original, WaitingHDR, Failed, Stopped, BlockedMixedOutputs, WaitingTopology };
// Shared only between this elevated program and its child, via inherited handles.
struct Shared {
    volatile LONG command, status, dirty;
    volatile LONG activeCurve;
    DWORD dwmPid;
    ULONGLONG dwmCreated;
    wchar_t isolationChannel[128];
    DWORD outputCount;
    LONG selectedOutput;
    BOOL outputHdr[8];
    wchar_t outputNames[8][64];
    wchar_t detail[768];
};

std::wstring Wide(const std::string& s);
std::string Utf8(const std::wstring& s);
std::string WinError(const char* action, DWORD error = GetLastError());
std::wstring SystemDwmPath();
Bytes ReadFileBytes(const std::wstring& path);
ShaderInventory InspectShaders(const Bytes& pe);
bool IsKnownSdrShader(const BYTE* digest);
bool ValidChecksum(const BYTE* p, size_t size);
void FixChecksum(BYTE* p, size_t size);
void EnableDebugPrivilege();
DWORD FindDwm();
ULONGLONG CreationTime(HANDLE process);
void VerifyShaderLibraries();
std::wstring InspectReport();
void WriteText(const std::wstring& path, const std::string& text);
}
