#pragma once

#include "capture/ICaptureBackend.h"

namespace rillshot::capture {

class DxgiCaptureBackend final : public ICaptureBackend {
public:
    [[nodiscard]] std::string name() const override { return "DXGI"; }
    [[nodiscard]] rillshot::core::Status capture(const rillshot::core::RectI& region, CaptureFrame& out) override;
};

} // namespace rillshot::capture
