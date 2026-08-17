#pragma once

#include "core/Image.h"
#include "core/Types.h"

#include <string>

namespace rillshot::output {

enum class ImageFormat {
    Png,
    Bmp
};

class WicImageWriter final {
public:
    [[nodiscard]] rillshot::core::Status write(
        const rillshot::core::Image& image,
        const std::wstring& path,
        ImageFormat format,
        bool allowOverwrite = false) const;
};

} // namespace rillshot::output
