#include "core/Image.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace rillshot::core {
namespace {

int checkedStrideForWidth(int width) {
    if (width <= 0) {
        throw std::invalid_argument("Image width must be positive");
    }
    if (width > std::numeric_limits<int>::max() / 4) {
        throw std::overflow_error("Image width is too large for 32bpp stride");
    }
    return width * 4;
}

size_t checkedByteCount(int stride, int height, size_t maxSize) {
    if (height <= 0) {
        throw std::invalid_argument("Image height must be positive");
    }
    const auto strideSize = static_cast<size_t>(stride);
    const auto heightSize = static_cast<size_t>(height);
    if (strideSize != 0 && heightSize > maxSize / strideSize) {
        throw std::overflow_error("Image byte size is too large");
    }
    return strideSize * heightSize;
}

} // namespace

Image::Image(int width, int height) {
    const int stride = checkedStrideForWidth(width);
    std::vector<std::uint8_t> pixels;
    pixels.resize(checkedByteCount(stride, height, pixels.max_size()));

    width_ = width;
    height_ = height;
    stride_ = stride;
    pixels_ = std::move(pixels);
}

std::uint8_t* Image::row(int y) {
    if (y < 0 || y >= height_) {
        throw std::out_of_range("Image row out of range");
    }
    return pixels_.data() + static_cast<size_t>(y) * static_cast<size_t>(stride_);
}

const std::uint8_t* Image::row(int y) const {
    if (y < 0 || y >= height_) {
        throw std::out_of_range("Image row out of range");
    }
    return pixels_.data() + static_cast<size_t>(y) * static_cast<size_t>(stride_);
}

void Image::appendRowsFrom(const Image& src, int startY, int endYExclusive) {
    if (src.width() != width_) {
        throw std::invalid_argument("appendRowsFrom requires equal image width");
    }
    startY = std::clamp(startY, 0, src.height());
    endYExclusive = std::clamp(endYExclusive, startY, src.height());
    const int rowsToAppend = endYExclusive - startY;
    if (rowsToAppend <= 0) {
        return;
    }
    if (rowsToAppend > std::numeric_limits<int>::max() - height_) {
        throw std::overflow_error("appendRowsFrom would exceed maximum image height");
    }

    const int oldHeight = height_;
    const int newHeight = height_ + rowsToAppend;
    pixels_.resize(checkedByteCount(stride_, newHeight, pixels_.max_size()));
    height_ = newHeight;

    for (int y = 0; y < rowsToAppend; ++y) {
        std::memcpy(row(oldHeight + y), src.row(startY + y), static_cast<size_t>(stride_));
    }
}

void Image::prependRowsFrom(const Image& src, int startY, int endYExclusive) {
    if (src.width() != width_) {
        throw std::invalid_argument("prependRowsFrom requires equal image width");
    }
    startY = std::clamp(startY, 0, src.height());
    endYExclusive = std::clamp(endYExclusive, startY, src.height());
    const int rowsToPrepend = endYExclusive - startY;
    if (rowsToPrepend <= 0) {
        return;
    }
    if (rowsToPrepend > std::numeric_limits<int>::max() - height_) {
        throw std::overflow_error("prependRowsFrom would exceed maximum image height");
    }

    std::vector<std::uint8_t> prefix;
    const size_t prefixBytes = checkedByteCount(stride_, rowsToPrepend, prefix.max_size());
    prefix.resize(prefixBytes);
    for (int y = 0; y < rowsToPrepend; ++y) {
        std::memcpy(
            prefix.data() + static_cast<size_t>(y) * static_cast<size_t>(stride_),
            src.row(startY + y),
            static_cast<size_t>(stride_));
    }

    const int oldHeight = height_;
    const int newHeight = height_ + rowsToPrepend;
    pixels_.resize(checkedByteCount(stride_, newHeight, pixels_.max_size()));

    const size_t oldBytes = static_cast<size_t>(oldHeight) * static_cast<size_t>(stride_);
    std::memmove(pixels_.data() + prefixBytes, pixels_.data(), oldBytes);
    std::memcpy(pixels_.data(), prefix.data(), prefixBytes);
    height_ = newHeight;
}

void Image::fillTestPattern() {
    for (int y = 0; y < height_; ++y) {
        auto* out = row(y);
        for (int x = 0; x < width_; ++x) {
            const auto v0 = static_cast<std::uint8_t>((x * 17 + y * 31 + (x * y) % 37) & 0xFF);
            const auto v1 = static_cast<std::uint8_t>((x * 3 + y * 13 + 91) & 0xFF);
            const auto v2 = static_cast<std::uint8_t>((x * 11 + y * 7 + 53) & 0xFF);
            out[x * 4 + 0] = v0;
            out[x * 4 + 1] = v1;
            out[x * 4 + 2] = v2;
            out[x * 4 + 3] = 255;
        }
    }
}

} // namespace rillshot::core
