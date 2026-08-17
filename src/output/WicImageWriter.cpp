#include "output/WicImageWriter.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <filesystem>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace rillshot::output {
namespace {

using Microsoft::WRL::ComPtr;

std::string hrMessage(const char* where, HRESULT hr) {
    std::ostringstream oss;
    oss << where << " failed, HRESULT=0x" << std::hex << static_cast<unsigned long>(hr);
    return oss.str();
}

const GUID& containerGuid(ImageFormat format) noexcept {
    switch (format) {
    case ImageFormat::Png: return GUID_ContainerFormatPng;
    case ImageFormat::Bmp: return GUID_ContainerFormatBmp;
    }
    return GUID_ContainerFormatPng;
}

std::filesystem::path temporaryPathFor(const std::filesystem::path& outputPath) {
    const auto processId = static_cast<unsigned long>(GetCurrentProcessId());
    const auto tick = static_cast<unsigned long long>(GetTickCount64());
    for (unsigned int attempt = 0; attempt < 1000; ++attempt) {
        std::wostringstream name;
        name << outputPath.filename().wstring()
             << L".rillshot-" << processId
             << L"-" << tick
             << L"-" << attempt
             << L".tmp";
        const auto candidate = outputPath.parent_path() / name.str();
        std::error_code error;
        if (!std::filesystem::exists(candidate, error) && !error) {
            return candidate;
        }
    }
    return {};
}

class TemporaryFileGuard final {
public:
    explicit TemporaryFileGuard(std::filesystem::path path) : path_(std::move(path)) {}
    ~TemporaryFileGuard() {
        if (!committed_ && !path_.empty()) {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }
    }

    void commit() noexcept { committed_ = true; }

private:
    std::filesystem::path path_;
    bool committed_ = false;
};

} // namespace

rillshot::core::Status WicImageWriter::write(
    const rillshot::core::Image& image,
    const std::wstring& path,
    ImageFormat format,
    bool allowOverwrite) const {

    if (image.empty()) {
        return rillshot::core::Status::failure("wic-empty-image", "cannot write an empty image");
    }

    const auto bytes = image.bytes();
    if (bytes.size() > (std::numeric_limits<UINT>::max)()) {
        return rillshot::core::Status::failure("wic-image-too-large", "image byte size exceeds WIC WritePixels UINT limit");
    }

    const std::filesystem::path outputPath(path);
    std::error_code outputError;
    const bool outputExists = std::filesystem::exists(outputPath, outputError);
    if (outputError) {
        return rillshot::core::Status::failure(
            "wic-output-status-failed", outputError.message());
    }
    if (outputExists && !allowOverwrite) {
        return rillshot::core::Status::failure(
            "wic-output-exists", "output already exists and overwrite was not confirmed");
    }

    const auto parentPath = outputPath.parent_path();
    if (!parentPath.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parentPath, ec);
        if (ec) {
            return rillshot::core::Status::failure("wic-create-output-directory-failed", ec.message());
        }
    }

    const auto temporaryPath = temporaryPathFor(outputPath);
    if (temporaryPath.empty()) {
        return rillshot::core::Status::failure(
            "wic-temporary-path-failed",
            "could not reserve a temporary output path");
    }
    TemporaryFileGuard temporaryFile(temporaryPath);

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("wic-factory-failed", hrMessage("CoCreateInstance(CLSID_WICImagingFactory)", hr));
    }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("wic-create-stream-failed", hrMessage("CreateStream", hr));
    }

    hr = stream->InitializeFromFilename(temporaryPath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("wic-open-output-failed", hrMessage("InitializeFromFilename", hr));
    }

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(containerGuid(format), nullptr, &encoder);
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("wic-create-encoder-failed", hrMessage("CreateEncoder", hr));
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("wic-encoder-init-failed", hrMessage("Encoder Initialize", hr));
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> propertyBag;
    hr = encoder->CreateNewFrame(&frame, &propertyBag);
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("wic-create-frame-failed", hrMessage("CreateNewFrame", hr));
    }

    hr = frame->Initialize(propertyBag.Get());
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("wic-frame-init-failed", hrMessage("Frame Initialize", hr));
    }

    hr = frame->SetSize(static_cast<UINT>(image.width()), static_cast<UINT>(image.height()));
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("wic-set-size-failed", hrMessage("SetSize", hr));
    }

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("wic-set-pixel-format-failed", hrMessage("SetPixelFormat", hr));
    }
    if (pixelFormat != GUID_WICPixelFormat32bppBGRA) {
        return rillshot::core::Status::failure("wic-pixel-format-changed", "WIC encoder did not accept 32bpp BGRA");
    }

    hr = frame->WritePixels(
        static_cast<UINT>(image.height()),
        static_cast<UINT>(image.stride()),
        static_cast<UINT>(bytes.size()),
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(bytes.data())));
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("wic-write-pixels-failed", hrMessage("WritePixels", hr));
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("wic-frame-commit-failed", hrMessage("Frame Commit", hr));
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("wic-encoder-commit-failed", hrMessage("Encoder Commit", hr));
    }

    propertyBag.Reset();
    frame.Reset();
    encoder.Reset();
    stream.Reset();
    factory.Reset();

    DWORD moveFlags = MOVEFILE_WRITE_THROUGH;
    if (allowOverwrite) {
        moveFlags |= MOVEFILE_REPLACE_EXISTING;
    }
    if (!MoveFileExW(
            temporaryPath.c_str(),
            outputPath.c_str(),
            moveFlags)) {
        return rillshot::core::Status::failure(
            "wic-atomic-replace-failed",
            hrMessage("MoveFileExW", HRESULT_FROM_WIN32(GetLastError())));
    }
    temporaryFile.commit();
    return rillshot::core::Status::success();
}

} // namespace rillshot::output
