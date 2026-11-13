#include "restoration_cover.h"
#include <limits>

namespace eotf {
void RestoreWithVisual(RestorationVisual& visual, const std::function<void()>& restore,
    const std::function<void()>& fallback) {
    struct Cleanup {
        RestorationVisual& visual;
        bool pending = true;
        void Release() { if (pending) { pending = false; visual.Release(); } }
        ~Cleanup() { Release(); }
    } cleanup{visual};
    bool shown = false;
    try { shown = visual.Show(); }
    catch (...) { cleanup.Release(); } // Optional presentation must never prevent recovery.
    restore();
    bool covered = shown && visual.Active();
    if (covered) {
        try { visual.PrepareReveal(); covered = visual.Active(); }
        catch (...) { covered = false; }
    }
    cleanup.Release();
    if (!covered) fallback();
}
bool UsableRestorationSnapshot(const RECT& bounds, const SnapshotDescription& image) {
    auto width = (int64_t)bounds.right - bounds.left, height = (int64_t)bounds.bottom - bounds.top;
    return width > 0 && height > 0 && width <= (std::numeric_limits<int>::max)() &&
        height <= (std::numeric_limits<int>::max)() && image.width == (UINT)width && image.height == (UINT)height &&
        image.linearFp16 && image.unrotated && !image.protectedContent && image.desktopUpdated;
}
bool RestorationCopyFits(UINT width, UINT height, const POINT& offset, UINT targetWidth, UINT targetHeight) {
    return width && height && offset.x >= 0 && offset.y >= 0 &&
        (uint64_t)(UINT)offset.x + width <= targetWidth && (uint64_t)(UINT)offset.y + height <= targetHeight;
}
unsigned RestorationCoverMask(const IsolationConfig& config, const IsolationStats& stats) {
    if (!ValidOutputConfig(config)) return 0;
    unsigned mask = 0;
    for (unsigned i = 0; i < config.count; ++i)
        if (config.outputs[i].hdr && stats.replacements[i] > 0) mask |= 1u << i;
    return mask;
}
}
