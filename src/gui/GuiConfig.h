#pragma once

#include "session/CaptureSession.h"

#include <string>

namespace rillshot::gui {

// Platform-neutral form model. Win32 controls only read/write this structure;
// capture defaults and session mapping stay independently testable.
struct GuiConfig {
    rillshot::core::RectI region{100, 100, 900, 700};
    rillshot::core::PointI scrollPoint{550, 760};
    std::wstring outPath = L"rillshot-output.png";
    rillshot::session::BackendChoice backend = rillshot::session::BackendChoice::Auto;
    rillshot::session::DriverChoice driver = rillshot::session::DriverChoice::Wheel;
    rillshot::core::ScrollDirection direction = rillshot::core::ScrollDirection::Down;
    rillshot::input::KeyboardKey keyboardKey =
        rillshot::input::KeyboardKey::Page;
    int maxFrames = 12;
    // One notch preserves ample overlap on editors that scroll by a large
    // logical step. Users can raise it after the first successful run.
    int wheelNotches = 1;
    int ignoreTopPx = 0;
    int ignoreBottomPx = 0;
    bool allowOverwrite = false;

    [[nodiscard]] rillshot::session::CaptureSessionOptions toSessionOptions() const;
};

// Validates the full GUI contract through the shared CaptureSession boundary.
// Every desktop shell should use this entry point.
[[nodiscard]] rillshot::core::Status validateGuiConfig(const GuiConfig& config);

} // namespace rillshot::gui
