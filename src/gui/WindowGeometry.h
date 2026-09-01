#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rillshot::gui::geometry {

struct WindowBounds {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return width > 0 && height > 0;
    }
};

[[nodiscard]] constexpr std::int64_t coordinateDelta(
    std::int32_t start,
    std::int32_t end) noexcept {
    return static_cast<std::int64_t>(end) -
           static_cast<std::int64_t>(start);
}

[[nodiscard]] constexpr std::int32_t clampCoordinate(
    std::int64_t value) noexcept {
    constexpr auto minimum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::min)());
    constexpr auto maximum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::max)());
    return static_cast<std::int32_t>(std::clamp(value, minimum, maximum));
}

[[nodiscard]] constexpr std::int32_t saturatedCoordinateDelta(
    std::int32_t start,
    std::int32_t end) noexcept {
    return clampCoordinate(coordinateDelta(start, end));
}

[[nodiscard]] constexpr std::int32_t saturatedCoordinateOffset(
    std::int32_t origin,
    std::int64_t offset) noexcept {
    return clampCoordinate(static_cast<std::int64_t>(origin) + offset);
}

[[nodiscard]] constexpr bool tryPositiveCoordinateSpan(
    std::int32_t start,
    std::int32_t end,
    std::int32_t& span) noexcept {
    const auto wideSpan = coordinateDelta(start, end);
    constexpr auto maximum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::max)());
    if (wideSpan <= 0 || wideSpan > maximum) {
        return false;
    }
    span = static_cast<std::int32_t>(wideSpan);
    return true;
}

[[nodiscard]] constexpr bool tryCoordinateOffset(
    std::int32_t origin,
    std::int64_t offset,
    std::int32_t& result) noexcept {
    const auto wideResult = static_cast<std::int64_t>(origin) + offset;
    constexpr auto minimum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::min)());
    constexpr auto maximum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::max)());
    if (wideResult < minimum || wideResult > maximum) {
        return false;
    }
    result = static_cast<std::int32_t>(wideResult);
    return true;
}

[[nodiscard]] inline std::int32_t pixelsForDip(
    double dip,
    std::uint32_t dpi) noexcept {
    if (!std::isfinite(dip) || dip <= 0.0) {
        return 1;
    }
    const double scaled = dip * static_cast<double>(dpi) / 96.0;
    constexpr double maximum = static_cast<double>(
        (std::numeric_limits<std::int32_t>::max)());
    if (!std::isfinite(scaled) || scaled >= maximum) {
        return (std::numeric_limits<std::int32_t>::max)();
    }
    return std::max<std::int32_t>(
        1, static_cast<std::int32_t>(std::lround(scaled)));
}

// Preserve a window's effective size when it crosses monitors with different
// scale factors. AppWindow geometry uses physical pixels, while the layout
// contract is expressed in DIPs.
[[nodiscard]] constexpr std::int32_t scalePixelDimensionForDpi(
    std::int32_t pixels,
    std::uint32_t sourceDpi,
    std::uint32_t targetDpi) noexcept {
    if (pixels <= 0) {
        return 1;
    }
    const std::int64_t safeSourceDpi = std::max<std::int64_t>(1, sourceDpi);
    const std::int64_t safeTargetDpi = std::max<std::int64_t>(1, targetDpi);
    const std::int64_t scaled =
        (static_cast<std::int64_t>(pixels) * safeTargetDpi +
         safeSourceDpi / 2) /
        safeSourceDpi;
    return std::max<std::int32_t>(1, clampCoordinate(scaled));
}

[[nodiscard]] constexpr bool coordinateDistanceWithin(
    std::int32_t first,
    std::int32_t second,
    std::int32_t tolerance) noexcept {
    if (tolerance < 0) {
        return false;
    }
    const auto difference = coordinateDelta(second, first);
    const auto widenedTolerance = static_cast<std::int64_t>(tolerance);
    return difference >= -widenedTolerance &&
           difference <= widenedTolerance;
}

[[nodiscard]] inline std::int32_t interpolateCoordinate(
    std::int32_t start,
    std::int32_t end,
    double progress) noexcept {
    if (!std::isfinite(progress)) {
        return progress > 0.0 ? end : start;
    }
    const double boundedProgress = std::clamp(progress, 0.0, 1.0);
    const double value = static_cast<double>(start) +
        (static_cast<double>(end) - static_cast<double>(start)) *
            boundedProgress;
    return static_cast<std::int32_t>(std::lround(value));
}

// Quintic smootherstep keeps velocity and acceleration continuous at both
// endpoints, avoiding the small start/stop jerk visible on high-refresh
// displays while preserving a deterministic, time-based transition.
[[nodiscard]] inline double smootherstepProgress(double progress) noexcept {
    if (!std::isfinite(progress)) {
        return progress > 0.0 ? 1.0 : 0.0;
    }
    const double value = std::clamp(progress, 0.0, 1.0);
    return value * value * value *
        (value * (value * 6.0 - 15.0) + 10.0);
}

// Resize around the current window centre while keeping the complete target
// inside the monitor work area. Horizontal and vertical margins are reduced
// independently on very small work areas, so a valid one-pixel target always
// remains possible and negative-coordinate monitors stay supported.
[[nodiscard]] inline WindowBounds fitWindowSizeAroundCenter(
    const WindowBounds& current,
    const WindowBounds& workArea,
    std::int32_t desiredWidth,
    std::int32_t desiredHeight,
    std::int32_t requestedMargin) noexcept {
    if (!workArea.isValid()) {
        return current;
    }

    const std::int64_t margin = std::max<std::int64_t>(0, requestedMargin);
    const std::int64_t horizontalMargin = std::min<std::int64_t>(
        margin, (static_cast<std::int64_t>(workArea.width) - 1) / 2);
    const std::int64_t verticalMargin = std::min<std::int64_t>(
        margin, (static_cast<std::int64_t>(workArea.height) - 1) / 2);
    const std::int64_t availableWidth =
        static_cast<std::int64_t>(workArea.width) - horizontalMargin * 2;
    const std::int64_t availableHeight =
        static_cast<std::int64_t>(workArea.height) - verticalMargin * 2;
    const std::int64_t targetWidth = std::clamp<std::int64_t>(
        desiredWidth, 1, availableWidth);
    const std::int64_t targetHeight = std::clamp<std::int64_t>(
        desiredHeight, 1, availableHeight);

    const WindowBounds centreSource = current.isValid() ? current : workArea;
    const std::int64_t centreX = static_cast<std::int64_t>(centreSource.x) +
        static_cast<std::int64_t>(centreSource.width) / 2;
    const std::int64_t centreY = static_cast<std::int64_t>(centreSource.y) +
        static_cast<std::int64_t>(centreSource.height) / 2;
    const std::int64_t minimumX =
        static_cast<std::int64_t>(workArea.x) + horizontalMargin;
    const std::int64_t minimumY =
        static_cast<std::int64_t>(workArea.y) + verticalMargin;
    const std::int64_t maximumX = static_cast<std::int64_t>(workArea.x) +
        static_cast<std::int64_t>(workArea.width) - horizontalMargin - targetWidth;
    const std::int64_t maximumY = static_cast<std::int64_t>(workArea.y) +
        static_cast<std::int64_t>(workArea.height) - verticalMargin - targetHeight;

    return WindowBounds{
        clampCoordinate(std::clamp(
            centreX - targetWidth / 2, minimumX, maximumX)),
        clampCoordinate(std::clamp(
            centreY - targetHeight / 2, minimumY, maximumY)),
        static_cast<std::int32_t>(targetWidth),
        static_cast<std::int32_t>(targetHeight)};
}

// Choose a comfortable default without turning a settings surface into a
// near-full-screen window on common laptop work areas. The preferred size is
// capped to a percentage of the work area, but the usable minimum wins when
// there is enough room. Hard margins and very small work areas remain governed
// by fitWindowSizeAroundCenter.
[[nodiscard]] inline WindowBounds fitAdaptiveWindowSizeAroundCenter(
    const WindowBounds& current,
    const WindowBounds& workArea,
    std::int32_t preferredWidth,
    std::int32_t preferredHeight,
    std::int32_t minimumWidth,
    std::int32_t minimumHeight,
    std::int32_t requestedMargin,
    std::uint32_t maximumWidthPercent,
    std::uint32_t maximumHeightPercent) noexcept {
    if (!workArea.isValid()) {
        return current;
    }

    const auto adaptiveDimension = [](
        std::int32_t preferred,
        std::int32_t minimum,
        std::int32_t workAreaDimension,
        std::uint32_t maximumPercent) noexcept {
        const std::int64_t safePreferred = std::max<std::int64_t>(1, preferred);
        const std::int64_t safeMinimum = std::max<std::int64_t>(1, minimum);
        const std::int64_t boundedPercent =
            std::clamp<std::int64_t>(maximumPercent, 1, 100);
        const std::int64_t softMaximum = std::max<std::int64_t>(
            1,
            static_cast<std::int64_t>(workAreaDimension) * boundedPercent / 100);
        const std::int64_t upperBound = std::max(safeMinimum, softMaximum);
        return clampCoordinate(std::clamp(
            safePreferred,
            safeMinimum,
            upperBound));
    };

    return fitWindowSizeAroundCenter(
        current,
        workArea,
        adaptiveDimension(
            preferredWidth,
            minimumWidth,
            workArea.width,
            maximumWidthPercent),
        adaptiveDimension(
            preferredHeight,
            minimumHeight,
            workArea.height,
            maximumHeightPercent),
        requestedMargin);
}

[[nodiscard]] inline WindowBounds interpolateWindowBounds(
    const WindowBounds& start,
    const WindowBounds& end,
    double progress) noexcept {
    return WindowBounds{
        interpolateCoordinate(start.x, end.x, progress),
        interpolateCoordinate(start.y, end.y, progress),
        interpolateCoordinate(start.width, end.width, progress),
        interpolateCoordinate(start.height, end.height, progress)};
}

} // namespace rillshot::gui::geometry
