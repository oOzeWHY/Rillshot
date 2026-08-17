#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace rillshot::gui::selection {

enum class CursorOuterTone {
    White,
    Black
};

// Chooses the outer cursor stroke from the immutable BGRA desktop snapshot.
// The decision is intentionally local so the cursor can cross light and dark
// content without depending on the user's current Windows accent color.
[[nodiscard]] inline CursorOuterTone chooseCursorOuterTone(
    std::span<const std::uint8_t> bgraPixels,
    int width,
    int height,
    std::size_t strideBytes,
    int cursorX,
    int cursorY) noexcept {
    if (width <= 0 || height <= 0 ||
        static_cast<std::size_t>(width) >
            (std::numeric_limits<std::size_t>::max)() / 4U) {
        return CursorOuterTone::White;
    }
    const std::size_t minimumStride = static_cast<std::size_t>(width) * 4U;
    if (strideBytes < minimumStride ||
        static_cast<std::size_t>(height) >
            (std::numeric_limits<std::size_t>::max)() / strideBytes ||
        bgraPixels.size() < strideBytes * static_cast<std::size_t>(height)) {
        return CursorOuterTone::White;
    }

    constexpr int sampleRadius = 6;
    constexpr int sampleStep = 2;
    const int left = std::clamp(cursorX - sampleRadius, 0, width - 1);
    const int right = std::clamp(cursorX + sampleRadius, 0, width - 1);
    const int top = std::clamp(cursorY - sampleRadius, 0, height - 1);
    const int bottom = std::clamp(cursorY + sampleRadius, 0, height - 1);

    std::uint64_t luminanceSum = 0;
    std::uint64_t sampleCount = 0;
    for (int y = top; y <= bottom; y += sampleStep) {
        const std::size_t row = static_cast<std::size_t>(y) * strideBytes;
        for (int x = left; x <= right; x += sampleStep) {
            const std::size_t offset = row + static_cast<std::size_t>(x) * 4U;
            const auto blue = static_cast<std::uint64_t>(bgraPixels[offset]);
            const auto green = static_cast<std::uint64_t>(bgraPixels[offset + 1U]);
            const auto red = static_cast<std::uint64_t>(bgraPixels[offset + 2U]);
            // Integer Rec. 709 approximation on the 0-255 sRGB byte range.
            luminanceSum += (54U * red + 183U * green + 19U * blue) >> 8U;
            ++sampleCount;
        }
    }

    if (sampleCount == 0) {
        return CursorOuterTone::White;
    }
    return luminanceSum / sampleCount >= 144U
        ? CursorOuterTone::Black
        : CursorOuterTone::White;
}

} // namespace rillshot::gui::selection
