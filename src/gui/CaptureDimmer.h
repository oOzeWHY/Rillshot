#pragma once

#include "core/Types.h"
#include <Windows.h>

namespace rillshot::gui {

// UI-thread owned. A static, click-through window has an exact physical-pixel
// hole for the capture rectangle; no screenshot bitmap or repaint timer.
class CaptureDimmer final {
public:
    ~CaptureDimmer() { hide(); }
    CaptureDimmer() = default;
    CaptureDimmer(const CaptureDimmer&) = delete;
    CaptureDimmer& operator=(const CaptureDimmer&) = delete;
    [[nodiscard]] bool show(const rillshot::core::RectI& region) noexcept;
    void hide() noexcept;

private:
    HWND window_ = nullptr;
};

} // namespace rillshot::gui
