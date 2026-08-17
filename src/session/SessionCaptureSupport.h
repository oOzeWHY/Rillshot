#pragma once

#include "capture/ICaptureBackend.h"
#include "input/IScrollDriver.h"
#include "session/CaptureSession.h"
#include "session/SessionDiagnostics.h"

#include <memory>
#include <vector>

namespace rillshot::session::detail {

[[nodiscard]] std::unique_ptr<rillshot::input::IScrollDriver> makeDriver(DriverChoice choice);
[[nodiscard]] std::vector<std::unique_ptr<rillshot::capture::ICaptureBackend>> makeBackends(
    BackendChoice choice);
[[nodiscard]] bool cancellationRequested(const CaptureSessionOptions& options) noexcept;
[[nodiscard]] rillshot::core::Status waitForStableFrame(
    std::vector<std::unique_ptr<rillshot::capture::ICaptureBackend>>& backends,
    const CaptureSessionOptions& options,
    rillshot::capture::CaptureFrame& stableFrame,
    JsonlLogger& logger,
    const char* phase);

} // namespace rillshot::session::detail
