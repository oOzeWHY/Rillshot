#pragma once

#include "core/Image.h"
#include "core/Types.h"

#include <cstddef>
#include <vector>

namespace rillshot::stitch {

// Stores stitched row chunks in capture order. Encoders visit them in output
// order without allocating another full-size image, including upward capture.
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
    // Returning false stops visitation, e.g. after an encoder failure.
    template <typename Visitor>
    bool visitChunks(Visitor&& visitor) const {
        if (direction_ == rillshot::core::ScrollDirection::Down) {
            for (const auto& chunk : chunks_) {
                if (!visitor(chunk)) return false;
            }
        } else {
            for (auto iterator = chunks_.rbegin(); iterator != chunks_.rend(); ++iterator) {
                if (!visitor(*iterator)) return false;
            }
        }
        return true;
    }
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
