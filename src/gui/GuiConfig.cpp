#include "gui/GuiConfig.h"

namespace rillshot::gui {

rillshot::session::CaptureSessionOptions GuiConfig::toSessionOptions() const {
    rillshot::session::CaptureSessionOptions options;
    options.region = region;
    options.scrollPoint = scrollPoint;
    options.outPath = outPath;
    options.backend = backend;
    options.driver = driver;
    options.direction = direction;
    options.keyboardKey = keyboardKey;
    options.maxFrames = maxFrames;
    options.wheelNotches = wheelNotches;
    options.keyRepeats = wheelNotches;
    options.ignoreTopPx = ignoreTopPx;
    options.ignoreBottomPx = ignoreBottomPx;
    options.allowOverwrite = allowOverwrite;
    return options;
}

rillshot::core::Status validateGuiConfig(const GuiConfig& config) {
    const auto sessionValidation =
        rillshot::session::validateCaptureSessionOptions(config.toSessionOptions());
    if (!sessionValidation.ok) {
        return sessionValidation;
    }
    return rillshot::core::Status::success();
}

} // namespace rillshot::gui
