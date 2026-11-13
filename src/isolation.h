#pragma once
#include "core.h"

namespace eotf {
constexpr unsigned MaxOutputs = 8;
constexpr DWORD IsolationProtocol = 0x00030003;
struct OutputConfig {
    RECT bounds;
    LUID adapter;
    UINT32 target;
    BOOL hdr;
    BOOL selected;
    wchar_t name[64];
};
struct IsolationConfig {
    DWORD protocol;
    DWORD count;
    TransferCurve curve;
    OutputConfig outputs[MaxOutputs];
};
struct DisplayTransition : Error { using Error::Error; };
bool SameOutputTopology(const IsolationConfig& a, const IsolationConfig& b);
DWORD DisplaySourceNumber(const wchar_t* source);
void SortOutputs(IsolationConfig& config, const DWORD sourceNumbers[MaxOutputs]);
struct TopologyStability {
    IsolationConfig previous = {};
    bool observed = false;
    ULONGLONG changedAt = 0;
    bool Observe(const IsolationConfig& config, ULONGLONG now);
    void Reset() { observed = false; }
};
struct IsolationStats {
    volatile LONG64 frames[MaxOutputs];
    volatile LONG64 shaderCalls[MaxOutputs];
    volatile LONG64 replacements[MaxOutputs];
    volatile LONG64 sdrBypasses[MaxOutputs];
    volatile LONG64 unsupported[MaxOutputs];
    volatile LONG64 candidates[MaxOutputs];
    volatile LONG64 variants, unscoped;
    volatile LONG contextsMask, mappingConflict;
    volatile LONG appliedSequence, appliedCurve;
};
struct IsolationShared {
    DWORD protocol;
    volatile LONG enabled, stop, ready, state, configSequence;
    volatile LONG inspect, requestedCurve, fallbackCurve;
    volatile LONG64 heartbeat;
    IsolationConfig config;
    IsolationStats stats;
    wchar_t detail[512];
};
// Curve requests do not invalidate an unchanged output mapping.
void UpdateIsolationConfig(IsolationShared& shared, const IsolationConfig& config);
bool ReadIsolationConfig(IsolationShared& shared, IsolationConfig& config, LONG& sequence);
TransferCurve AppliedIsolationCurve(IsolationShared& shared);
struct AgentStart {
    wchar_t channel[128];
};
// Exact rectangle identity, including desktop origin. Never match by resolution alone.
int MatchOutput(const IsolationConfig& config, const float bounds[4]);
bool ValidOutputConfig(const IsolationConfig& config);
bool ShouldUseGamma(const IsolationConfig& config, int output, bool completeMapping, bool enabled);
// Patch only complete sRGB EOTF instruction groups in executable DXBC chunks.
// Returns false without modifying anything when validation or semantic matching fails.
bool MakeIsolatedShader(const BYTE* data, size_t size, Bytes& variant, TransferCurve curve = TransferCurve::Gamma22, unsigned* groups = nullptr);
IsolationConfig OutputConfiguration();
int IsolatedWorker(Shared& shared, HANDLE parent, HANDLE request, HANDLE response);
void RestoreIsolated(Shared& shared);
int DiagnoseIsolation(const std::wstring& report, bool enable, bool simulateGuardianLoss = false, TransferCurve curve = TransferCurve::Gamma22);
bool MatchesIsolationProfile();
}
