#include "session/SessionCaptureSupport.h"

#include "capture/DxgiCaptureBackend.h"
#include "capture/GdiCaptureBackend.h"
#include "core/ImageMetrics.h"
#include "input/KeyboardDriver.h"
#include "input/ManualDriver.h"
#include "input/WheelDriver.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iomanip>
#include <optional>
#include <sstream>
#include <thread>

namespace rillshot::session::detail {
namespace {

using rillshot::capture::CaptureFrame;
using rillshot::capture::DxgiCaptureBackend;
using rillshot::capture::GdiCaptureBackend;
using rillshot::capture::ICaptureBackend;
using rillshot::core::DifferenceOptions;
using rillshot::core::Status;
using rillshot::input::IScrollDriver;
using rillshot::input::KeyboardDriver;
using rillshot::input::ManualDriver;
using rillshot::input::WheelDriver;

bool interruptibleWait(const CaptureSessionOptions& options, int durationMs) {
    constexpr int sliceMs = 25;
    int remainingMs = std::max(0, durationMs);
    while (remainingMs > 0) {
        if (cancellationRequested(options)) {
            return false;
        }
        const int currentSliceMs = std::min(sliceMs, remainingMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(currentSliceMs));
        remainingMs -= currentSliceMs;
    }
    return !cancellationRequested(options);
}

Status captureWithFallback(
    std::vector<std::unique_ptr<ICaptureBackend>>& backends,
    const rillshot::core::RectI& region,
    CaptureFrame& frame,
    JsonlLogger& logger) {

    Status last = Status::failure("no-backend", "no capture backend available");
    std::optional<CaptureFrame> suspiciousFrame;
    std::string suspiciousBackend;
    double suspiciousStdDev = -1.0;
    double suspiciousMeanLuma = -1.0;
    for (size_t backendIndex = 0; backendIndex < backends.size(); ++backendIndex) {
        auto& backend = backends[backendIndex];
        CaptureFrame candidate;
        Status status;
        try {
            status = backend->capture(region, candidate);
        } catch (const std::exception& ex) {
            status = Status::failure(
                "capture-backend-exception",
                std::string("capture backend raised an exception: ") + ex.what());
        } catch (...) {
            status = Status::failure("capture-backend-exception", "capture backend raised an unknown exception");
        }
        if (status.ok) {
            if (candidate.image.empty() ||
                candidate.image.width() != region.width ||
                candidate.image.height() != region.height) {
                status = Status::failure(
                    "capture-invalid-frame",
                    "capture backend reported success with an empty or incorrectly sized frame");
                logger.event(
                    "capture_backend_failed",
                    "\"backend\":" + q(backend->name()) +
                        ",\"code\":" + q(status.code) +
                        ",\"message\":" + q(status.message));
                last = status;
                continue;
            }
            candidate.unstable = false;
            if (candidate.backendName.empty()) {
                candidate.backendName = backend->name();
            }
            const auto summary = rillshot::core::summarizeImage(candidate.image);
            std::ostringstream fields;
            fields << std::fixed << std::setprecision(6)
                   << "\"backend\":" << q(candidate.backendName)
                   << ",\"meanLuma\":" << summary.meanLuma
                   << ",\"lumaStdDev\":" << summary.lumaStdDev
                   << ",\"darkPixelRatio\":" << summary.darkPixelRatio
                   << ",\"suspiciouslyNearBlack\":" << (summary.suspiciouslyNearBlack ? "true" : "false");
            logger.event("capture", fields.str());

            if (summary.suspiciouslyNearBlack) {
                if (!suspiciousFrame ||
                    summary.lumaStdDev > suspiciousStdDev ||
                    (summary.lumaStdDev == suspiciousStdDev &&
                     summary.meanLuma > suspiciousMeanLuma)) {
                    suspiciousFrame = std::move(candidate);
                    suspiciousBackend = suspiciousFrame->backendName;
                    suspiciousStdDev = summary.lumaStdDev;
                    suspiciousMeanLuma = summary.meanLuma;
                }
                if (backendIndex + 1U < backends.size()) {
                    logger.event(
                        "capture_backend_suspicious",
                        "\"backend\":" + q(backend->name()) +
                            ",\"reason\":\"near-black frame; trying fallback\"");
                }
                continue;
            }
            frame = std::move(candidate);
            return status;
        }

        logger.event(
            "capture_backend_failed",
            "\"backend\":" + q(backend->name()) +
                ",\"code\":" + q(status.code) +
                ",\"message\":" + q(status.message));
        last = status;
    }

    if (suspiciousFrame) {
        frame = std::move(*suspiciousFrame);
        logger.event(
            "capture_suspicious_fallback_exhausted",
            "\"backend\":" + q(suspiciousBackend) +
                ",\"message\":\"other capture backends failed; preserving available frame\"");
        return Status::success("captured a suspiciously near-black frame after fallback exhaustion");
    }
    return last;
}

} // namespace

std::unique_ptr<IScrollDriver> makeDriver(DriverChoice choice) {
    switch (choice) {
    case DriverChoice::Wheel: return std::make_unique<WheelDriver>();
    case DriverChoice::Keyboard: return std::make_unique<KeyboardDriver>();
    case DriverChoice::Manual: return std::make_unique<ManualDriver>();
    }
    return std::make_unique<WheelDriver>();
}

std::vector<std::unique_ptr<ICaptureBackend>> makeBackends(BackendChoice choice) {
    std::vector<std::unique_ptr<ICaptureBackend>> backends;
    switch (choice) {
    case BackendChoice::Auto:
        backends.push_back(std::make_unique<DxgiCaptureBackend>());
        backends.push_back(std::make_unique<GdiCaptureBackend>());
        break;
    case BackendChoice::Dxgi:
        backends.push_back(std::make_unique<DxgiCaptureBackend>());
        break;
    case BackendChoice::Gdi:
        backends.push_back(std::make_unique<GdiCaptureBackend>());
        break;
    }
    return backends;
}

bool cancellationRequested(const CaptureSessionOptions& options) noexcept {
    if (!options.shouldStop) {
        return false;
    }
    try {
        return options.shouldStop();
    } catch (...) {
        return true;
    }
}

Status waitForStableFrame(
    std::vector<std::unique_ptr<ICaptureBackend>>& backends,
    const CaptureSessionOptions& options,
    CaptureFrame& stableFrame,
    JsonlLogger& logger,
    const char* phase) {

    logger.event("stabilize_start", "\"phase\":" + q(phase));
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::milliseconds(options.maxWaitMs);
    if (!interruptibleWait(options, options.minWaitMs)) {
        return Status::failure("session-cancelled", "user stopped capture");
    }

    CaptureFrame previous;
    auto status = captureWithFallback(backends, options.region, previous, logger);
    if (!status.ok) {
        return status;
    }

    const auto timeout = [&]() {
        previous.unstable = true;
        stableFrame = std::move(previous);
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        logger.event(
            "stabilize_timeout",
            "\"phase\":" + q(phase) +
                ",\"unstable\":true,\"elapsedMs\":" +
                std::to_string(elapsedMs));
        return Status::success("captured unstable frame after timeout");
    };

    int stableCount = 0;
    DifferenceOptions diffOptions;
    diffOptions.ignoreTopPx = options.ignoreTopPx;
    diffOptions.ignoreBottomPx = options.ignoreBottomPx;
    diffOptions.xStep = 12;
    diffOptions.yStep = 6;

    while (true) {
        if (cancellationRequested(options)) {
            return Status::failure("session-cancelled", "user stopped capture");
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return timeout();
        }
        const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now).count();
        const int waitMs = static_cast<int>(std::min<long long>(
            std::max(1, options.sampleIntervalMs),
            std::max(1LL, remainingMs)));
        if (!interruptibleWait(options, waitMs)) {
            return Status::failure("session-cancelled", "user stopped capture");
        }

        CaptureFrame current;
        status = captureWithFallback(backends, options.region, current, logger);
        if (!status.ok) {
            return status;
        }
        // A backend call may block until after the stabilization deadline.
        // Do not let that late frame satisfy the stability contract.
        if (std::chrono::steady_clock::now() >= deadline) {
            return timeout();
        }

        double diff = 0.0;
        try {
            diff = rillshot::core::meanAbsDiffRatio(previous.image, current.image, diffOptions);
        } catch (const std::exception& ex) {
            return Status::failure(
                "stabilization-compare-exception",
                std::string("could not compare stabilization frames: ") + ex.what());
        } catch (...) {
            return Status::failure(
                "stabilization-compare-exception",
                "could not compare stabilization frames: unknown exception");
        }

        std::ostringstream fields;
        fields << std::fixed << std::setprecision(6)
               << "\"phase\":" << q(phase)
               << ",\"diff\":" << diff
               << ",\"threshold\":" << options.diffThreshold;
        logger.event("stabilize_sample", fields.str());

        if (diff <= options.diffThreshold) {
            ++stableCount;
            if (stableCount >= options.stableSamplesRequired) {
                current.unstable = false;
                stableFrame = std::move(current);
                logger.event("stabilize_done", "\"phase\":" + q(phase) + ",\"unstable\":false");
                return Status::success();
            }
        } else {
            stableCount = 0;
        }
        previous = std::move(current);
    }
}

} // namespace rillshot::session::detail
