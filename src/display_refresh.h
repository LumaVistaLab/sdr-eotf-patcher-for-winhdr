#pragma once
#include "core.h"

namespace eotf {
// A bounded burst while mapping/arming, followed by a final composition request
// after the requested curve is confirmed. Idle desktops never request damage.
struct CompositionRefreshPlan {
    static constexpr unsigned MaxAttempts = 8;
    static constexpr ULONGLONG RetryInterval = 125;
    LONG sequence = -1;
    TransferCurve requested = TransferCurve::SRGB, confirmed = TransferCurve::SRGB;
    bool enabled = false;
    unsigned remaining = 0;
    ULONGLONG next = 0;

    bool Next(LONG configSequence, TransferCurve curve, bool conversionEnabled,
              TransferCurve applied, ULONGLONG now);
    bool Pending() const { return remaining != 0; }
};

// Request composition without activating a window or changing display modes.
// This does not guarantee that all SDR source regions change in one frame.
bool RefreshComposedDesktop();
bool RefreshRestoredDesktop();
}
