#include "session/SessionOutputSupport.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <filesystem>

namespace rillshot::session::detail {

std::wstring partialPathFor(const std::wstring& outPath) {
    return outPath + L".partial.png";
}

std::wstring comparisonFramePathFor(const std::wstring& outPath) {
    return outPath + L".comparison.png";
}

rillshot::core::Image initialResultFrom(
    const rillshot::core::Image& firstFrame,
    int ignoreTopPx,
    int ignoreBottomPx,
    rillshot::core::ScrollDirection direction) {

    const bool downward = direction == rillshot::core::ScrollDirection::Down;
    const int startY = downward
        ? 0
        : std::clamp(ignoreTopPx, 0, firstFrame.height() - 1);
    const int endY = downward
        ? std::clamp(
            firstFrame.height() - ignoreBottomPx,
            startY + 1,
            firstFrame.height())
        : firstFrame.height();
    rillshot::core::Image result(firstFrame.width(), endY - startY);
    for (int y = startY; y < endY; ++y) {
        std::memcpy(
            result.row(y - startY),
            firstFrame.row(y),
            static_cast<size_t>(firstFrame.stride()));
    }
    return result;
}

rillshot::output::ImageFormat formatFromPath(const std::wstring& path) {
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return rillshot::output::ImageFormat::Png;
    }
    std::wstring extension = path.substr(dot + 1);
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](wchar_t value) {
            return value >= L'A' && value <= L'Z'
                ? static_cast<wchar_t>(value + (L'a' - L'A'))
                : value;
        });
    return extension == L"bmp"
        ? rillshot::output::ImageFormat::Bmp
        : rillshot::output::ImageFormat::Png;
}

rillshot::core::Status writeImageNoThrow(
    const rillshot::output::WicImageWriter& writer,
    const rillshot::core::Image& image,
    const std::wstring& path,
    rillshot::output::ImageFormat format,
    bool allowOverwrite) {
    try {
        return writer.write(image, path, format, allowOverwrite);
    } catch (const std::exception& exception) {
        return rillshot::core::Status::failure(
            "image-write-exception",
            std::string("image writer raised an exception: ") +
                exception.what());
    } catch (...) {
        return rillshot::core::Status::failure(
            "image-write-exception",
            "image writer raised an unknown exception");
    }
}

rillshot::core::Status validateOutputCollisionPolicy(
    const CaptureSessionOptions& options) {
    if (options.allowOverwrite) {
        return rillshot::core::Status::success();
    }

    const std::array<std::filesystem::path, 4> paths = {
        std::filesystem::path(options.outPath),
        std::filesystem::path(options.outPath + L".jsonl"),
        std::filesystem::path(partialPathFor(options.outPath)),
        std::filesystem::path(comparisonFramePathFor(options.outPath))
    };
    for (const auto& path : paths) {
        std::error_code error;
        if (std::filesystem::exists(path, error)) {
            return rillshot::core::Status::failure(
                "output-exists",
                "an output or companion file already exists; overwrite was not confirmed");
        }
        if (error) {
            return rillshot::core::Status::failure(
                "output-status-failed", error.message());
        }
    }
    return rillshot::core::Status::success();
}

} // namespace rillshot::session::detail
