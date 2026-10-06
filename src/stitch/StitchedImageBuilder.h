#pragma once

#include "core/Image.h"
#include "core/Types.h"

#include <cstddef>
#include <vector>

namespace rillshot::stitch {

// Stores stitched row chunks in capture order and materializes a contiguous
// image only for checkpoints and final output. Upward capture therefore avoids
// moving every previously assembled pixel after each seam.
class StitchedImageBuilder final {
public:
    StitchedImageBuilder() = default;
    StitchedImageBuilder(
        rillshot::core::Image initial,
        rillshot::core::ScrollDirection direction);

    void addRowsFrom(
        const rillshot::core::Image& source,
        int startY,
        int endYExclusive);

    [[nodiscard]] rillshot::core::Image materialize() const;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] std::size_t chunkCount() const noexcept;

private:
    int width_ = 0;
    int height_ = 0;
    rillshot::core::ScrollDirection direction_ =
        rillshot::core::ScrollDirection::Down;
    std::vector<rillshot::core::Image> chunks_;
};

} // namespace rillshot::stitch
