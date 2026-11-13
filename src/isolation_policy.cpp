#include "isolation.h"
#include <cmath>
#include <cstring>
#include <array>
#include <algorithm>

namespace eotf {
static DWORD Word(const BYTE* p) { DWORD v; memcpy(&v, p, 4); return v; }
DWORD DisplaySourceNumber(const wchar_t* source) {
    constexpr wchar_t prefix[]=L"\\\\.\\DISPLAY";constexpr size_t length=_countof(prefix)-1;
    if(!source||wcsncmp(source,prefix,length))return MAXDWORD;
    const wchar_t* p=source+length;if(!*p)return MAXDWORD;
    DWORD result=0;
    for(;*p;++p){if(*p<L'0'||*p>L'9')return MAXDWORD;DWORD digit=*p-L'0';if(result>(MAXDWORD-digit)/10)return MAXDWORD;result=result*10+digit;}
    return result?result:MAXDWORD;
}
void SortOutputs(IsolationConfig& config,const DWORD sourceNumbers[MaxOutputs]) {
    if(config.count>MaxOutputs)return;
    std::array<unsigned,MaxOutputs> order={};for(unsigned i=0;i<config.count;++i)order[i]=i;
    std::stable_sort(order.begin(),order.begin()+config.count,[&](unsigned a,unsigned b){return sourceNumbers[a]<sourceNumbers[b];});
    auto original=config;for(unsigned i=0;i<config.count;++i)config.outputs[i]=original.outputs[order[i]];
}
bool SameOutputTopology(const IsolationConfig& a, const IsolationConfig& b) {
    if (a.count != b.count || a.count > MaxOutputs) return false;
    // Compare identities, not enumeration order, display names or the selected curve.
    for (unsigned i=0;i<a.count;++i) {
        bool found=false;
        for (unsigned j=0;j<b.count;++j) {
            const auto& x=a.outputs[i];const auto& y=b.outputs[j];
            if (!memcmp(&x.adapter,&y.adapter,sizeof(LUID)) && x.target==y.target && x.hdr==y.hdr && EqualRect(&x.bounds,&y.bounds)) {found=true;break;}
        }
        if(!found)return false;
    }
    return true;
}
bool TopologyStability::Observe(const IsolationConfig& config, ULONGLONG now) {
    if (!observed || !SameOutputTopology(previous,config)) { previous=config;observed=true;changedAt=now; }
    return now-changedAt>=1000;
}
void UpdateIsolationConfig(IsolationShared& shared, const IsolationConfig& config) {
    auto outputs = config; outputs.curve = shared.config.curve;
    if (memcmp(&shared.config, &outputs, sizeof(outputs))) {
        InterlockedExchange(&shared.enabled, 0);
        InterlockedIncrement(&shared.configSequence);
        memcpy(&shared.config, &config, sizeof(config));
        InterlockedExchange(&shared.fallbackCurve, (LONG)TransferCurve::SRGB);
        MemoryBarrier(); InterlockedIncrement(&shared.configSequence);
    } else if (shared.requestedCurve != (LONG)config.curve) {
        // Keep the last confirmed curve available while a new variant is prepared.
        InterlockedExchange(&shared.fallbackCurve, (LONG)AppliedIsolationCurve(shared));
    }
    InterlockedExchange(&shared.requestedCurve, (LONG)config.curve);
}
bool ReadIsolationConfig(IsolationShared& shared, IsolationConfig& config, LONG& sequence) {
    sequence = InterlockedCompareExchange(&shared.configSequence, 0, 0);
    if (sequence & 1) return false;
    memcpy(&config, &shared.config, sizeof(config));
    config.curve = (TransferCurve)InterlockedCompareExchange(&shared.requestedCurve, 0, 0);
    MemoryBarrier();
    return sequence == InterlockedCompareExchange(&shared.configSequence, 0, 0) && ValidOutputConfig(config);
}
TransferCurve AppliedIsolationCurve(IsolationShared& shared) {
    LONG sequence = InterlockedCompareExchange(&shared.configSequence, 0, 0);
    if ((sequence & 1) || sequence != InterlockedCompareExchange(&shared.stats.appliedSequence, 0, 0)) return TransferCurve::SRGB;
    auto curve = (TransferCurve)InterlockedCompareExchange(&shared.stats.appliedCurve, 0, 0);
    return ValidCurve(curve) ? curve : TransferCurve::SRGB;
}
bool ValidOutputConfig(const IsolationConfig& c) {
    if (c.protocol != IsolationProtocol || !c.count || c.count > MaxOutputs || !ValidCurve(c.curve)) return false;
    for (unsigned i = 0; i < c.count; ++i) {
        const auto& o = c.outputs[i];
        if (o.bounds.left >= o.bounds.right || o.bounds.top >= o.bounds.bottom) return false;
        if (o.selected && !o.hdr) return false;
        for (unsigned j = 0; j < i; ++j) if (EqualRect(&o.bounds, &c.outputs[j].bounds)) return false;
    }
    return true;
}
int MatchOutput(const IsolationConfig& c, const float b[4]) {
    if (!ValidOutputConfig(c)) return -1;
    for (int k = 0; k < 4; ++k) if (!std::isfinite(b[k])) return -1;
    int match = -1;
    for (unsigned i = 0; i < c.count; ++i) {
        const auto& r = c.outputs[i].bounds;
        if (std::fabs(b[0] - r.left) < 0.01f && std::fabs(b[1] - r.top) < 0.01f &&
            std::fabs(b[2] - r.right) < 0.01f && std::fabs(b[3] - r.bottom) < 0.01f) {
            if (match != -1) return -1; match = (int)i;
        }
    }
    return match;
}
bool ShouldUseGamma(const IsolationConfig& c, int i, bool complete, bool enabled) {
    return enabled && complete && ValidOutputConfig(c) && c.curve != TransferCurve::SRGB && i >= 0 && (unsigned)i < c.count && c.outputs[i].hdr && c.outputs[i].selected;
}
bool MakeIsolatedShader(const BYTE* p, size_t n, Bytes& variant, TransferCurve curve, unsigned* groupCount) {
    variant.clear(); if (groupCount) *groupCount = 0;
    if (!ValidCurve(curve) || curve == TransferCurve::SRGB) return false;
    if (!p || n < 32 || n > 4 * 1024 * 1024 || memcmp(p, "DXBC", 4) || Word(p + 20) != 1 || Word(p + 24) != n || !ValidChecksum(p, n)) return false;
    DWORD chunks = Word(p + 28);
    if (!chunks || chunks > (n - 32) / 4) return false;
    const float values[] = {2.4f, 0.04045f, 0.055f, 0.94786733f};
    const bool rec709 = curve == TransferCurve::Rec709;
    const float targets[] = {rec709 ? 1.0f / .45f : curve == TransferCurve::Gamma24 ? 2.4f : 2.2f,
        rec709 ? .081f : 0, rec709 ? .099f : 0, rec709 ? 1.0f / 1.099f : 1};
    // D3D10_SB_OPCODE_MUL=56, GE=29, ADD=0. Float4 immediate operand=0x4002.
    const DWORD expectedOpcodes[] = {56, 29, 0, 56};
    std::array<std::vector<size_t>, 4> sites;
    std::vector<size_t> slopeMad, slopeMul;
    unsigned executable = 0;
    for (DWORD c = 0; c < chunks; ++c) {
        size_t start = Word(p + 32 + c * 4);
        if (start < 32 + chunks * 4 || start > n - 8) return false;
        size_t length = Word(p + start + 4);
        if (length > n - start - 8) return false;
        if (memcmp(p + start, "SHDR", 4) && memcmp(p + start, "SHEX", 4)) continue;
        if (++executable != 1 || length < 8 || length % 4) return false;
        auto code = p + start + 8;
        DWORD words = Word(code + 4);
        if (words != length / 4) return false;
        for (size_t token = 2; token < words;) {
            DWORD instruction = Word(code + token * 4);
            DWORD op = instruction & 0x7ff, count = (instruction >> 24) & 0x7f;
            // CUSTOMDATA uses its second word for length. It never contains executable operands.
            if (op == 53) { if (token + 1 >= words) return false; count = Word(code + (token + 1) * 4); }
            if (!count || count > words - token) return false;
            if (op != 53) for (size_t j = token + 1; j + 5 <= token + count; ++j) {
                if (Word(code + j * 4) != 0x4002) continue;
                auto literal = code + (j + 1) * 4;
                const float slope[4] = {1.0f / 12.92f, 1.0f / 12.92f, 1.0f / 12.92f, 0};
                if (rec709 && !memcmp(literal, slope, sizeof(slope))) {
                    if (op == 50) slopeMad.push_back((size_t)(literal - p));
                    else if (op == 56) slopeMul.push_back((size_t)(literal - p));
                    else return false;
                }
                for (unsigned k = 0; k < 4; ++k) {
                    float value[4] = {values[k], values[k], values[k], 0};
                    if (!memcmp(literal, value, sizeof(value))) {
                        if (op != expectedOpcodes[k]) return false;
                        sites[k].push_back((size_t)(literal - p));
                    }
                }
            }
            token += count;
        }
    }
    if (!executable || sites[0].empty() || sites[0].size() > 8) return false;
    for (unsigned i = 1; i < 4; ++i) if (sites[i].size() != sites[0].size()) return false;
    if (rec709 && (slopeMad.size() != sites[0].size() || slopeMul.size() != sites[0].size())) return false;
    // Each exponent must be paired with the immediately preceding scale/offset and following threshold.
    // This rejects unrelated constants, reflected metadata, and partial/changed compiler patterns.
    for (size_t i = 0; i < sites[0].size(); ++i) {
        if (!(sites[2][i] < sites[3][i] && sites[3][i] < sites[0][i] && sites[0][i] < sites[1][i])) return false;
        if (sites[1][i] - sites[2][i] > 512) return false;
        if (rec709 && !(sites[0][i] < slopeMad[i] && slopeMad[i] < sites[1][i] && sites[1][i] < slopeMul[i] && slopeMul[i] - sites[2][i] <= 512)) return false;
    }
    variant.assign(p, p + n);
    if (rec709) for (const auto* list : {&slopeMad, &slopeMul}) for (size_t site : *list) {
        const float slope[4] = {1.0f / 4.5f, 1.0f / 4.5f, 1.0f / 4.5f, 0}; memcpy(variant.data() + site, slope, sizeof(slope));
    }
    for (unsigned k = 0; k < 4; ++k) for (size_t site : sites[k]) {
        float target[4] = {targets[k], targets[k], targets[k], 0}; memcpy(variant.data() + site, target, sizeof(target));
    }
    FixChecksum(variant.data(), variant.size());
    if (groupCount) *groupCount = (unsigned)sites[0].size();
    return true;
}
}
