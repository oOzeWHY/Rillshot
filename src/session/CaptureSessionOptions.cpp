#include "session/CaptureSession.h"

#include <algorithm>
#include <cmath>

namespace rillshot::session {
namespace {

bool hasSupportedOutputExtension(const std::wstring& path) {
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return false;
    }

    std::wstring extension = path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
        return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch + (L'a' - L'A')) : ch;
    });
    return extension == L"png" || extension == L"bmp";
}

bool isValidBackend(BackendChoice backend) noexcept {
    switch (backend) {
    case BackendChoice::Auto:
    case BackendChoice::Dxgi:
    case BackendChoice::Gdi:
        return true;
    }
    return false;
}

bool isValidDriver(DriverChoice driver) noexcept {
    switch (driver) {
    case DriverChoice::Wheel:
    case DriverChoice::Keyboard:
    case DriverChoice::Manual:
        return true;
    }
    return false;
}

bool isValidDirection(rillshot::core::ScrollDirection direction) noexcept {
    switch (direction) {
    case rillshot::core::ScrollDirection::Down:
    case rillshot::core::ScrollDirection::Up:
        return true;
    }
    return false;
}

bool isValidKeyboardKey(rillshot::input::KeyboardKey key) noexcept {
    switch (key) {
    case rillshot::input::KeyboardKey::Page:
    case rillshot::input::KeyboardKey::Space:
    case rillshot::input::KeyboardKey::Arrow:
        return true;
    }
    return false;
}

bool isUnitInterval(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

} // namespace

bool imageSizeFitsByteBudget(
    int width,
    std::int64_t height,
    std::uint64_t maxBytes) noexcept {
    if (width <= 0 || height <= 0 || maxBytes == 0) {
        return false;
    }
    const auto rowBytes = static_cast<std::uint64_t>(width) * 4ULL;
    return rowBytes <= maxBytes &&
        static_cast<std::uint64_t>(height) <= maxBytes / rowBytes;
}

rillshot::core::Status validateCaptureSessionOptions(const CaptureSessionOptions& options) {
    using rillshot::core::Status;

    if (!options.region.isValid()) {
        return Status::failure("invalid-region", "capture region must have positive dimensions and valid coordinates");
    }
    if (options.scrollPoint.x < options.region.x ||
        options.scrollPoint.y < options.region.y ||
        static_cast<long long>(options.scrollPoint.x) >= options.region.right() ||
        static_cast<long long>(options.scrollPoint.y) >= options.region.bottom()) {
        return Status::failure(
            "invalid-scroll-point",
            "scroll point must be inside the capture region");
    }
    if (!hasSupportedOutputExtension(options.outPath)) {
        return Status::failure("invalid-output-path", "output path must end in .png or .bmp");
    }
    if (!isValidBackend(options.backend)) {
        return Status::failure("invalid-backend", "capture backend choice is invalid");
    }
    if (!isValidDriver(options.driver)) {
        return Status::failure("invalid-driver", "scroll driver choice is invalid");
    }
    if (!isValidDirection(options.direction)) {
        return Status::failure("invalid-scroll-direction", "scroll direction is invalid");
    }
    if (!isValidKeyboardKey(options.keyboardKey)) {
        return Status::failure("invalid-keyboard-key", "keyboard key choice is invalid");
    }
    if (options.maxFrames < 1 || options.maxFrames > 10000) {
        return Status::failure("invalid-max-frames", "maxFrames must be between 1 and 10000");
    }
    if (options.wheelNotches < 1 || options.wheelNotches > 120) {
        return Status::failure("invalid-wheel-notches", "wheelNotches must be between 1 and 120");
    }
    if (options.keyRepeats < 1 || options.keyRepeats > 500) {
        return Status::failure("invalid-key-repeats", "keyRepeats must be between 1 and 500");
    }
    if (options.minWaitMs < 0 || options.minWaitMs > 60000 ||
        options.maxWaitMs < options.minWaitMs || options.maxWaitMs > 60000) {
        return Status::failure("invalid-wait-range", "wait values must satisfy 0 <= minWaitMs <= maxWaitMs <= 60000");
    }
    if (options.sampleIntervalMs < 1 || options.sampleIntervalMs > 60000) {
        return Status::failure("invalid-sample-interval", "sampleIntervalMs must be between 1 and 60000");
    }
    if (options.stableSamplesRequired < 1 || options.stableSamplesRequired > 100) {
        return Status::failure("invalid-stable-samples", "stableSamplesRequired must be between 1 and 100");
    }
    if (!isUnitInterval(options.diffThreshold)) {
        return Status::failure("invalid-diff-threshold", "diffThreshold must be finite and between 0 and 1");
    }
    if (!isUnitInterval(options.hardStitchConfidenceFloor)) {
        return Status::failure(
            "invalid-stitch-confidence-floor",
            "hardStitchConfidenceFloor must be finite and between 0 and 1");
    }
    if (options.maxAssembledImageBytes == 0 ||
        options.maxAssembledImageBytes > maximumEncodableImageBytes ||
        !imageSizeFitsByteBudget(
            options.region.width,
            options.region.height,
            options.maxAssembledImageBytes)) {
        return Status::failure(
            "invalid-output-budget",
            "stitched image byte budget must fit one frame and the WIC encoder limit");
    }

    const long long ignoredRows =
        static_cast<long long>(options.ignoreTopPx) + static_cast<long long>(options.ignoreBottomPx);
    const int minimumUsableRows = std::min(16, std::max(1, options.region.height / 2));
    if (options.ignoreTopPx < 0 || options.ignoreBottomPx < 0 ||
        ignoredRows > static_cast<long long>(options.region.height - minimumUsableRows)) {
        return Status::failure("invalid-ignore-regions", "ignored borders leave too little usable image content");
    }

    return Status::success();
}

} // namespace rillshot::session
