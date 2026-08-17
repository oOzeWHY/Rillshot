#pragma once

#include "core/Types.h"
#include "input/IScrollDriver.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <string>

namespace rillshot::session {

inline constexpr std::uint64_t defaultMaxAssembledImageBytes =
    512ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t maximumEncodableImageBytes =
    (std::numeric_limits<std::uint32_t>::max)();

enum class BackendChoice {
    Auto,
    Dxgi,
    Gdi
};

enum class DriverChoice {
    Wheel,
    Keyboard,
    Manual
};

struct CaptureSessionOptions {
    rillshot::core::RectI region;
    rillshot::core::PointI scrollPoint;
    std::wstring outPath;
    BackendChoice backend = BackendChoice::Auto;
    DriverChoice driver = DriverChoice::Wheel;
    rillshot::core::ScrollDirection direction = rillshot::core::ScrollDirection::Down;
    int maxFrames = 20;
    int wheelNotches = 5;
    int keyRepeats = 1;
    int minWaitMs = 120;
    int maxWaitMs = 1500;
    int sampleIntervalMs = 50;
    int stableSamplesRequired = 2;
    double diffThreshold = 0.003;
    int ignoreTopPx = 0;
    int ignoreBottomPx = 0;
    double hardStitchConfidenceFloor = 0.30;
    // Bounds the assembled image buffer before growth. Temporary working
    // allocations can add overhead. WIC accepts a UINT byte count, so larger
    // final buffers could never be encoded by this release.
    std::uint64_t maxAssembledImageBytes = defaultMaxAssembledImageBytes;
    bool stopOnLowConfidenceSeams = true;
    // Existing output and companion files are preserved unless the caller has
    // obtained an explicit overwrite confirmation from the user.
    bool allowOverwrite = false;
    rillshot::input::KeyboardKey keyboardKey = rillshot::input::KeyboardKey::Page;
    // Optional cooperative cancellation hook. It may be called from the
    // capture worker thread and should therefore be fast and thread-safe.
    std::function<bool()> shouldStop;
};

struct CaptureSessionResult {
    bool ok = false;
    bool outputSaved = false;
    bool diagnosticsSaved = false;
    bool comparisonFrameSaved = false;
    rillshot::core::StopReason stopReason = rillshot::core::StopReason::CaptureFailed;
    int framesCaptured = 0;
    int seams = 0;
    std::string message;
    std::wstring comparisonFramePath;
};

// A recovery checkpoint may replace an existing path only when the user
// authorized overwrite for the whole session, or after this session itself
// successfully created that checkpoint. This keeps periodic recovery writes
// working without weakening the startup collision policy.
[[nodiscard]] constexpr bool mayOverwritePartialCheckpoint(
    bool overwriteAuthorized,
    bool checkpointCreatedBySession) noexcept {
    return overwriteAuthorized || checkpointCreatedBySession;
}

// A checkpoint is recovery data, not a second successful output. Remove only
// a path created by this session, and only after a graceful final image write.
[[nodiscard]] inline bool shouldDeletePartialCheckpoint(
    bool checkpointCreatedBySession,
    bool finalOutputSaved,
    rillshot::core::StopReason stopReason) noexcept {
    return checkpointCreatedBySession && finalOutputSaved &&
        rillshot::core::isGracefulStop(stopReason);
}

[[nodiscard]] rillshot::core::Status validateCaptureSessionOptions(
    const CaptureSessionOptions& options);
[[nodiscard]] bool imageSizeFitsByteBudget(
    int width,
    std::int64_t height,
    std::uint64_t maxBytes) noexcept;
[[nodiscard]] rillshot::core::Status validateCaptureOutputCollisionPolicy(
    const CaptureSessionOptions& options);

class CaptureSession final {
public:
    [[nodiscard]] CaptureSessionResult run(const CaptureSessionOptions& options);
};

} // namespace rillshot::session
