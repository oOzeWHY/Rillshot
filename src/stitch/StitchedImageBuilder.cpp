#include "stitch/StitchedImageBuilder.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rillshot::stitch {
namespace {

bool isValidDirection(rillshot::core::ScrollDirection direction) noexcept {
    switch (direction) {
    case rillshot::core::ScrollDirection::Down:
    case rillshot::core::ScrollDirection::Up:
        return true;
    }
    return false;
}

void copyChunk(
    const rillshot::core::Image& chunk,
    rillshot::core::Image& output,
    int& outputY) {
    for (int y = 0; y < chunk.height(); ++y) {
        std::memcpy(
            output.row(outputY),
            chunk.row(y),
            static_cast<std::size_t>(chunk.stride()));
        ++outputY;
    }
}

} // namespace

StitchedImageBuilder::StitchedImageBuilder(
    rillshot::core::Image initial,
    rillshot::core::ScrollDirection direction)
    : width_(initial.width()),
      height_(initial.height()),
      direction_(direction) {
    if (initial.empty()) {
        throw std::invalid_argument(
            "StitchedImageBuilder requires a non-empty initial image");
    }
    if (!isValidDirection(direction)) {
        throw std::invalid_argument(
            "StitchedImageBuilder requires a valid scroll direction");
    }
    chunks_.push_back(std::move(initial));
}

void StitchedImageBuilder::addRowsFrom(
    const rillshot::core::Image& source,
    int startY,
    int endYExclusive) {
    if (empty() || source.width() != width_) {
        throw std::invalid_argument(
            "StitchedImageBuilder requires equal non-empty image widths");
    }
    startY = std::clamp(startY, 0, source.height());
    endYExclusive = std::clamp(
        endYExclusive, startY, source.height());
    const int rows = endYExclusive - startY;
    if (rows <= 0) {
        return;
    }
    if (rows > (std::numeric_limits<int>::max)() - height_) {
        throw std::overflow_error(
            "StitchedImageBuilder would exceed maximum image height");
    }

    rillshot::core::Image chunk(width_, rows);
    for (int y = 0; y < rows; ++y) {
        std::memcpy(
            chunk.row(y),
            source.row(startY + y),
            static_cast<std::size_t>(chunk.stride()));
    }
    chunks_.push_back(std::move(chunk));
    height_ += rows;
}

rillshot::core::Image StitchedImageBuilder::materialize() const {
    if (empty()) {
        throw std::logic_error(
            "cannot materialize an empty StitchedImageBuilder");
    }

    rillshot::core::Image output(width_, height_);
    int outputY = 0;
    visitChunks([&](const auto& chunk) {
        copyChunk(chunk, output, outputY);
        return true;
    });
    return output;
}

bool StitchedImageBuilder::empty() const noexcept {
    return chunks_.empty();
}

int StitchedImageBuilder::width() const noexcept {
    return width_;
}

int StitchedImageBuilder::height() const noexcept {
    return height_;
}

std::size_t StitchedImageBuilder::chunkCount() const noexcept {
    return chunks_.size();
}

} // namespace rillshot::stitch
