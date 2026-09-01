#include "gui/WindowGeometry.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

int fail(const char* message) {
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

int testCoordinateDistanceDoesNotOverflow() {
    using rillshot::gui::geometry::clampCoordinate;
    using rillshot::gui::geometry::coordinateDelta;
    using rillshot::gui::geometry::coordinateDistanceWithin;
    using rillshot::gui::geometry::saturatedCoordinateDelta;
    using rillshot::gui::geometry::saturatedCoordinateOffset;
    using rillshot::gui::geometry::tryCoordinateOffset;
    using rillshot::gui::geometry::tryPositiveCoordinateSpan;
    constexpr auto minimum = (std::numeric_limits<std::int32_t>::min)();
    constexpr auto maximum = (std::numeric_limits<std::int32_t>::max)();

    if (coordinateDistanceWithin(minimum, maximum, 2)) {
        return fail("opposite coordinate extremes must not compare as nearby");
    }
    if (coordinateDelta(minimum, maximum) != 4294967295LL ||
        coordinateDelta(maximum, minimum) != -4294967295LL) {
        return fail("coordinate deltas must preserve the complete 32-bit range");
    }
    if (saturatedCoordinateDelta(minimum, maximum) != maximum ||
        saturatedCoordinateDelta(maximum, minimum) != minimum ||
        clampCoordinate(4294967295LL) != maximum ||
        saturatedCoordinateOffset(maximum, 1) != maximum) {
        return fail("rendering coordinates must saturate instead of wrapping");
    }
    std::int32_t value = 0;
    if (tryPositiveCoordinateSpan(minimum, maximum, value) ||
        !tryPositiveCoordinateSpan(-100, 100, value) || value != 200 ||
        tryCoordinateOffset(maximum, 1, value) ||
        !tryCoordinateOffset(-100, 200, value) || value != 100) {
        return fail("checked coordinate spans and offsets must reject overflow");
    }
    if (!coordinateDistanceWithin(minimum, minimum + 2, 2)) {
        return fail("coordinates at the tolerance boundary should compare as nearby");
    }
    if (coordinateDistanceWithin(10, 10, -1)) {
        return fail("a negative coordinate tolerance must be rejected");
    }
    return EXIT_SUCCESS;
}

int testCoordinateInterpolationDoesNotOverflow() {
    using rillshot::gui::geometry::interpolateCoordinate;
    constexpr auto minimum = (std::numeric_limits<std::int32_t>::min)();
    constexpr auto maximum = (std::numeric_limits<std::int32_t>::max)();

    if (interpolateCoordinate(minimum, maximum, 0.0) != minimum ||
        interpolateCoordinate(minimum, maximum, 1.0) != maximum ||
        interpolateCoordinate(minimum, maximum, 0.5) != -1) {
        return fail("coordinate interpolation should preserve its endpoints and midpoint");
    }
    if (interpolateCoordinate(10, 20, -1.0) != 10 ||
        interpolateCoordinate(10, 20, 2.0) != 20) {
        return fail("coordinate interpolation should clamp progress to the segment");
    }
    if (interpolateCoordinate(
            10, 20, std::numeric_limits<double>::infinity()) != 20 ||
        interpolateCoordinate(
            10, 20, -std::numeric_limits<double>::infinity()) != 10 ||
        interpolateCoordinate(
            10, 20, std::numeric_limits<double>::quiet_NaN()) != 10) {
        return fail("coordinate interpolation must handle non-finite progress");
    }
    return EXIT_SUCCESS;
}

int testDipScalingDoesNotOverflow() {
    using rillshot::gui::geometry::pixelsForDip;
    using rillshot::gui::geometry::scalePixelDimensionForDpi;
    constexpr auto maximum = (std::numeric_limits<std::int32_t>::max)();
    if (pixelsForDip(24.0, 96) != 24 ||
        pixelsForDip(24.0, 192) != 48 ||
        pixelsForDip(-1.0, 96) != 1 ||
        pixelsForDip(std::numeric_limits<double>::infinity(), 96) != 1 ||
        pixelsForDip(static_cast<double>(maximum), 192) != maximum) {
        return fail("DIP scaling must clamp invalid or overflowing inputs");
    }
    if (scalePixelDimensionForDpi(760, 96, 144) != 1140 ||
        scalePixelDimensionForDpi(630, 144, 96) != 420 ||
        scalePixelDimensionForDpi(0, 96, 144) != 1 ||
        scalePixelDimensionForDpi(maximum, 1, 0xFFFFFFFFU) != maximum) {
        return fail("cross-monitor window scaling must preserve effective size safely");
    }
    return EXIT_SUCCESS;
}

int testSmootherstepProgressIsBoundedAndSymmetric() {
    using rillshot::gui::geometry::smootherstepProgress;
    if (smootherstepProgress(-1.0) != 0.0 ||
        smootherstepProgress(0.0) != 0.0 ||
        smootherstepProgress(0.5) != 0.5 ||
        smootherstepProgress(1.0) != 1.0 ||
        smootherstepProgress(2.0) != 1.0) {
        return fail("smootherstep must preserve clamped endpoints and midpoint");
    }
    if (smootherstepProgress(0.1) >= smootherstepProgress(0.2) ||
        smootherstepProgress(0.8) >= smootherstepProgress(0.9) ||
        smootherstepProgress(std::numeric_limits<double>::quiet_NaN()) != 0.0 ||
        smootherstepProgress(std::numeric_limits<double>::infinity()) != 1.0) {
        return fail("smootherstep must be monotonic and handle non-finite input");
    }
    return EXIT_SUCCESS;
}

int testWindowSizeFitsWorkAreaAndPreservesCentre() {
    using rillshot::gui::geometry::WindowBounds;
    using rillshot::gui::geometry::fitWindowSizeAroundCenter;

    const WindowBounds current{100, 100, 760, 420};
    const WindowBounds workArea{0, 0, 1920, 1040};
    const auto expanded = fitWindowSizeAroundCenter(
        current, workArea, 1000, 900, 24);
    if (expanded.x != 24 || expanded.y != 24 ||
        expanded.width != 1000 || expanded.height != 900) {
        return fail("settings size should stay centred until constrained by work-area margins");
    }

    const WindowBounds negativeMonitor{-1920, -200, 1920, 1080};
    const WindowBounds negativeCurrent{-1800, -100, 760, 420};
    const auto negative = fitWindowSizeAroundCenter(
        negativeCurrent, negativeMonitor, 1000, 900, 24);
    if (negative.x < negativeMonitor.x + 24 ||
        negative.y < negativeMonitor.y + 24 ||
        static_cast<long long>(negative.x) + negative.width >
            static_cast<long long>(negativeMonitor.x) + negativeMonitor.width - 24 ||
        static_cast<long long>(negative.y) + negative.height >
            static_cast<long long>(negativeMonitor.y) + negativeMonitor.height - 24) {
        return fail("negative-coordinate monitor targets must remain inside the work area");
    }

    const WindowBounds tiny{10, 20, 30, 20};
    const auto constrained = fitWindowSizeAroundCenter(
        current, tiny, 1000, 900, 24);
    if (!constrained.isValid() || constrained.x < tiny.x || constrained.y < tiny.y ||
        static_cast<long long>(constrained.x) + constrained.width > tiny.x + tiny.width ||
        static_cast<long long>(constrained.y) + constrained.height > tiny.y + tiny.height) {
        return fail("tiny work areas must still produce a valid in-bounds target");
    }
    return EXIT_SUCCESS;
}

int testAdaptiveWindowSizeCoversCommonDisplays() {
    using rillshot::gui::geometry::WindowBounds;
    using rillshot::gui::geometry::fitAdaptiveWindowSizeAroundCenter;
    using rillshot::gui::geometry::pixelsForDip;

    struct DisplayCase {
        int width;
        int height;
        std::uint32_t dpi;
        int expectedWidth;
        int expectedHeight;
    };
    constexpr DisplayCase cases[] = {
        {800, 560, 96, 704, 459},
        {1024, 728, 96, 901, 596},
        {1280, 680, 96, 1000, 557},
        {1366, 728, 96, 1000, 596},
        {1920, 1040, 96, 1000, 760},
        {1920, 1040, 120, 1250, 852},
        {1920, 1040, 144, 1500, 852},
        {2560, 1400, 144, 1500, 1140},
        {3840, 2080, 192, 2000, 1520},
    };

    for (const auto& display : cases) {
        const WindowBounds workArea{0, 0, display.width, display.height};
        const WindowBounds current{
            display.width / 4,
            display.height / 4,
            pixelsForDip(760.0, display.dpi),
            pixelsForDip(420.0, display.dpi)};
        const int margin = pixelsForDip(24.0, display.dpi);
        const auto target = fitAdaptiveWindowSizeAroundCenter(
            current,
            workArea,
            pixelsForDip(1000.0, display.dpi),
            pixelsForDip(760.0, display.dpi),
            pixelsForDip(620.0, display.dpi),
            pixelsForDip(400.0, display.dpi),
            margin,
            88,
            82);
        if (target.width != display.expectedWidth ||
            target.height != display.expectedHeight) {
            return fail("adaptive settings window size changed for a common display profile");
        }
        if (target.x < margin || target.y < margin ||
            static_cast<long long>(target.x) + target.width >
                static_cast<long long>(display.width) - margin ||
            static_cast<long long>(target.y) + target.height >
                static_cast<long long>(display.height) - margin) {
            return fail("adaptive settings window must remain inside work-area margins");
        }
    }
    return EXIT_SUCCESS;
}

int testWindowBoundsInterpolationUsesSafeCoordinateMath() {
    using rillshot::gui::geometry::WindowBounds;
    using rillshot::gui::geometry::interpolateWindowBounds;
    constexpr auto minimum = (std::numeric_limits<std::int32_t>::min)();
    constexpr auto maximum = (std::numeric_limits<std::int32_t>::max)();
    const WindowBounds start{minimum, minimum, 1, 1};
    const WindowBounds end{maximum, maximum, 1000, 900};
    const auto midpoint = interpolateWindowBounds(start, end, 0.5);
    if (midpoint.x != -1 || midpoint.y != -1 ||
        midpoint.width != 501 || midpoint.height != 451) {
        return fail("window bounds interpolation must preserve safe rounded midpoints");
    }
    return EXIT_SUCCESS;
}

} // namespace

int main() {
    if (testCoordinateDistanceDoesNotOverflow() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testCoordinateInterpolationDoesNotOverflow() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testDipScalingDoesNotOverflow() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testSmootherstepProgressIsBoundedAndSymmetric() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testWindowSizeFitsWorkAreaAndPreservesCentre() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testAdaptiveWindowSizeCoversCommonDisplays() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testWindowBoundsInterpolationUsesSafeCoordinateMath() != EXIT_SUCCESS) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
