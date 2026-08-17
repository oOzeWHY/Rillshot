#pragma once

#include "core/Image.h"
#include "core/Types.h"

#include <chrono>
#include <string>

namespace rillshot::capture {

struct CaptureFrame {
    rillshot::core::Image image;
    std::string backendName;
    bool unstable = false;
    std::chrono::steady_clock::time_point capturedAt = std::chrono::steady_clock::now();
};

class ICaptureBackend {
public:
    virtual ~ICaptureBackend() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual rillshot::core::Status capture(const rillshot::core::RectI& region, CaptureFrame& out) = 0;
};

} // namespace rillshot::capture
