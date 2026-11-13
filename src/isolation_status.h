#pragma once
#include "isolation.h"

namespace eotf {
inline void PublishIsolation(Shared& s,Status status,const std::wstring& message,TransferCurve curve=TransferCurve::SRGB) {
    wcsncpy_s(s.detail,message.c_str(),_TRUNCATE);InterlockedExchange(&s.activeCurve,(LONG)curve);MemoryBarrier();InterlockedExchange(&s.status,status);
}

template<class Refresh>
void SynchronizeIsolationStatus(Shared& owner,IsolationShared& a,Refresh&& refresh) {
    const auto& config=a.config;
    if(a.state==3)throw Error(Utf8(a.detail));
    bool complete=a.stats.contextsMask==((1L<<config.count)-1);
    InterlockedExchange(&a.enabled,!a.stats.mappingConflict&&complete?1:0);
    // A composition request can confirm the new curve. Read and publish its
    // result afterwards, without waiting for another worker timeout.
    refresh();
    if(a.state==3)throw Error(Utf8(a.detail));
    if(a.stats.mappingConflict){InterlockedExchange(&a.enabled,0);PublishIsolation(owner,BlockedMixedOutputs,L"检测到显示器上下文冲突，所有输出保持原始路径");}
    else if(!complete||a.stats.contextsMask!=((1L<<config.count)-1)){InterlockedExchange(&a.enabled,0);PublishIsolation(owner,Working,L"正在核对显示器归属 · "+std::to_wstring(a.stats.contextsMask)+L" / "+std::to_wstring((1L<<config.count)-1));}
    else{
        auto active=AppliedIsolationCurve(a);bool hit=active!=TransferCurve::SRGB;
        std::wstring detail=hit?std::wstring(CurveName(active))+L" · 仅 "+std::wstring(config.outputs[owner.selectedOutput].name)+L" · SDR 副屏旁路":L"显示器隔离已确认 · 等待 SDR 着色器命中";
        if(hit&&(LONG)active!=a.requestedCurve)detail+=L" · 正在切换到 "+std::wstring(CurveName((TransferCurve)a.requestedCurve));
        PublishIsolation(owner,hit?Patched:Working,detail,active);
    }
}
}
