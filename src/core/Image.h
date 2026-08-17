#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace rillshot::core {

class Image {
public:
    Image() = default;
    Image(int width, int height);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] int stride() const noexcept { return stride_; }
    [[nodiscard]] bool empty() const noexcept { return width_ <= 0 || height_ <= 0 || pixels_.empty(); }

    [[nodiscard]] std::uint8_t* row(int y);
    [[nodiscard]] const std::uint8_t* row(int y) const;

    [[nodiscard]] std::span<std::uint8_t> bytes() noexcept { return pixels_; }
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return pixels_; }

    void appendRowsFrom(const Image& src, int startY, int endYExclusive);
    void prependRowsFrom(const Image& src, int startY, int endYExclusive);
    void fillTestPattern();

private:
    int width_ = 0;
    int height_ = 0;
    int stride_ = 0;
    std::vector<std::uint8_t> pixels_;
};

inline double grayAt(const Image& image, int x, int y) {
    const auto* p = image.row(y) + x * 4;
    const double b = static_cast<double>(p[0]);
    const double g = static_cast<double>(p[1]);
    const double r = static_cast<double>(p[2]);
    return 0.114 * b + 0.587 * g + 0.299 * r;
}

} // namespace rillshot::core
