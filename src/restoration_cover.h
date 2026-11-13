#pragma once
#include "isolation.h"
#include <functional>
#include <memory>

namespace eotf {
struct RestorationVisual {
    virtual ~RestorationVisual() = default;
    virtual bool Show() = 0;
    virtual bool Active() const = 0;
    virtual void PrepareReveal() = 0;
    virtual void Release() noexcept = 0;
};
// The restore callback must confirm hook restoration before it returns. Cleanup
// happens on both success and exception; fallback runs only without a live cover.
void RestoreWithVisual(RestorationVisual& visual, const std::function<void()>& restore,
                       const std::function<void()>& fallback);
struct SnapshotDescription {
    UINT width, height;
    bool linearFp16, unrotated, protectedContent, desktopUpdated;
};
bool UsableRestorationSnapshot(const RECT& bounds, const SnapshotDescription& image);
bool RestorationCopyFits(UINT width, UINT height, const POINT& offset, UINT targetWidth, UINT targetHeight);
unsigned RestorationCoverMask(const IsolationConfig& config, const IsolationStats& stats);
// Keep underlying windows participating in composition instead of fully occluding
// them. The small residual blend also applies while the original path is restored.
constexpr float RestorationCoverOpacity = 254.0f / 255.0f;
constexpr float RestorationRevealOpacity = 253.0f / 255.0f;
static_assert(RestorationRevealOpacity > 0 && RestorationRevealOpacity < RestorationCoverOpacity && RestorationCoverOpacity < 1,
    "Restoration visuals must keep underlying windows participating in composition");

class RestorationCover final : public RestorationVisual {
public:
    RestorationCover(const IsolationConfig& config, unsigned mask, std::function<void()> heartbeat = {});
    ~RestorationCover();
    bool Show() override;
    bool Active() const override;
    void PrepareReveal() override;
    void Release() noexcept override;
    // Called during the guardian's wait. A topology change or a one-second
    // timeout removes the cover so an unsuccessful restoration cannot trap input.
    void Poll();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
