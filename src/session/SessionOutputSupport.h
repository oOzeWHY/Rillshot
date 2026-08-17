#pragma once

#include "core/Image.h"
#include "core/Types.h"
#include "output/WicImageWriter.h"
#include "session/CaptureSession.h"

#include <string>

namespace rillshot::session::detail {

[[nodiscard]] std::wstring partialPathFor(const std::wstring& outPath);
[[nodiscard]] std::wstring comparisonFramePathFor(const std::wstring& outPath);
[[nodiscard]] rillshot::core::Image initialResultFrom(
    const rillshot::core::Image& firstFrame,
    int ignoreTopPx,
    int ignoreBottomPx,
    rillshot::core::ScrollDirection direction);
[[nodiscard]] rillshot::output::ImageFormat formatFromPath(
    const std::wstring& path);
[[nodiscard]] rillshot::core::Status writeImageNoThrow(
    const rillshot::output::WicImageWriter& writer,
    const rillshot::core::Image& image,
    const std::wstring& path,
    rillshot::output::ImageFormat format,
    bool allowOverwrite);
[[nodiscard]] rillshot::core::Status validateOutputCollisionPolicy(
    const CaptureSessionOptions& options);

} // namespace rillshot::session::detail
