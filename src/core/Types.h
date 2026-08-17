#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace rillshot::core {

struct PointI {
    int x = 0;
    int y = 0;
};

struct RectI {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool isValid() const noexcept {
        if (width <= 0 || height <= 0) {
            return false;
        }
        return right() <= (std::numeric_limits<int>::max)() &&
               bottom() <= (std::numeric_limits<int>::max)();
    }

    [[nodiscard]] long long right() const noexcept { return static_cast<long long>(x) + static_cast<long long>(width); }
    [[nodiscard]] long long bottom() const noexcept { return static_cast<long long>(y) + static_cast<long long>(height); }
};

struct Status {
    bool ok = false;
    std::string code;
    std::string message;

    [[nodiscard]] static Status success(std::string message = {}) {
        return Status{true, "ok", std::move(message)};
    }

    [[nodiscard]] static Status failure(std::string code, std::string message) {
        return Status{false, std::move(code), std::move(message)};
    }
};

enum class ScrollDirection {
    Down,
    Up
};

inline const char* toString(ScrollDirection direction) noexcept {
    switch (direction) {
    case ScrollDirection::Down: return "down";
    case ScrollDirection::Up: return "up";
    }
    return "unknown";
}

inline PointI defaultScrollPoint(
    const RectI& region,
    ScrollDirection direction) noexcept {

    const int maximumLocalY = region.height > 0 ? region.height - 1 : 0;
    // Keep the direction points on their intended half even for very short
    // regions. A fixed 40 px inset would otherwise put the upward point below
    // the downward point whenever the height is less than 80 px.
    const int proportionalInset = maximumLocalY / 3;
    const int inset = proportionalInset < 40 ? proportionalInset : 40;
    const int downwardCandidate = region.height - inset;
    const int downwardY = downwardCandidate < maximumLocalY
        ? downwardCandidate
        : maximumLocalY;
    const int localY = direction == ScrollDirection::Up
        ? inset
        : downwardY;
    const auto clampToInt = [](long long value) noexcept {
        return static_cast<int>(value < (std::numeric_limits<int>::min)()
            ? (std::numeric_limits<int>::min)()
            : value > (std::numeric_limits<int>::max)()
                ? (std::numeric_limits<int>::max)()
                : value);
    };
    return PointI{
        clampToInt(static_cast<long long>(region.x) +
                   static_cast<long long>(region.width) / 2LL),
        clampToInt(static_cast<long long>(region.y) +
                   static_cast<long long>(localY))};
}

enum class StopReason {
    MaxFramesReached,
    UserStopped,
    NoVisualProgress,
    ScrollRejected,
    CaptureFailed,
    StitchUnreliable,
    UnstableTooLong,
    OutputLimitReached
};

inline const char* toString(StopReason reason) noexcept {
    switch (reason) {
    case StopReason::MaxFramesReached: return "MaxFramesReached";
    case StopReason::UserStopped: return "UserStopped";
    case StopReason::NoVisualProgress: return "NoVisualProgress";
    case StopReason::ScrollRejected: return "ScrollRejected";
    case StopReason::CaptureFailed: return "CaptureFailed";
    case StopReason::StitchUnreliable: return "StitchUnreliable";
    case StopReason::UnstableTooLong: return "UnstableTooLong";
    case StopReason::OutputLimitReached: return "OutputLimitReached";
    }
    return "Unknown";
}

inline bool isGracefulStop(StopReason reason) noexcept {
    switch (reason) {
    case StopReason::MaxFramesReached:
    case StopReason::UserStopped:
    case StopReason::NoVisualProgress:
        return true;
    case StopReason::ScrollRejected:
    case StopReason::CaptureFailed:
    case StopReason::StitchUnreliable:
    case StopReason::UnstableTooLong:
    case StopReason::OutputLimitReached:
        return false;
    }
    return false;
}

} // namespace rillshot::core
