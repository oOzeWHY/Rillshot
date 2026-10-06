#pragma once

#include "capture/ICaptureBackend.h"
#include <cstdint>
#include <memory>

namespace rillshot::capture {

struct DxgiCaptureState;
struct DxgiCaptureStatistics {
    std::uint64_t deviceInitializations = 0;
    std::uint64_t stagingAllocations = 0;
    std::uint64_t framesAcquired = 0;
    std::uint64_t unchangedFrames = 0;
};

class DxgiCaptureBackend final : public ICaptureBackend {
public:
    DxgiCaptureBackend();
    ~DxgiCaptureBackend() override;
    [[nodiscard]] std::string name() const override { return "DXGI"; }
    [[nodiscard]] rillshot::core::Status capture(const rillshot::core::RectI& region, CaptureFrame& out) override;
    [[nodiscard]] DxgiCaptureStatistics statistics() const noexcept { return statistics_; }

private:
    std::unique_ptr<DxgiCaptureState> state_;
    DxgiCaptureStatistics statistics_;
};

} // namespace rillshot::capture
