#include "capture/DxgiCaptureBackend.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <sstream>

namespace rillshot::capture {
namespace {

using Microsoft::WRL::ComPtr;

class AcquiredFrameGuard final {
public:
    explicit AcquiredFrameGuard(IDXGIOutputDuplication* duplication) noexcept
        : duplication_(duplication) {}

    ~AcquiredFrameGuard() {
        if (duplication_) {
            duplication_->ReleaseFrame();
        }
    }

    AcquiredFrameGuard(const AcquiredFrameGuard&) = delete;
    AcquiredFrameGuard& operator=(const AcquiredFrameGuard&) = delete;

private:
    IDXGIOutputDuplication* duplication_ = nullptr;
};

class MappedTextureGuard final {
public:
    MappedTextureGuard(
        ID3D11DeviceContext* context,
        ID3D11Texture2D* texture) noexcept
        : context_(context), texture_(texture) {}

    ~MappedTextureGuard() {
        if (context_ && texture_) {
            context_->Unmap(texture_, 0);
        }
    }

    MappedTextureGuard(const MappedTextureGuard&) = delete;
    MappedTextureGuard& operator=(const MappedTextureGuard&) = delete;

private:
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11Texture2D* texture_ = nullptr;
};

std::string hrMessage(const char* where, HRESULT hr) {
    std::ostringstream oss;
    oss << where << " failed, HRESULT=0x" << std::hex << static_cast<unsigned long>(hr);
    return oss.str();
}

bool contains(const RECT& outer, const rillshot::core::RectI& inner) {
    return inner.x >= outer.left && inner.y >= outer.top && inner.right() <= outer.right && inner.bottom() <= outer.bottom;
}

rillshot::core::Status captureFromOutput(
    IDXGIAdapter1* adapter,
    IDXGIOutput* output,
    const DXGI_OUTPUT_DESC& outputDesc,
    const rillshot::core::RectI& region,
    CaptureFrame& out) {

    if (outputDesc.Rotation != DXGI_MODE_ROTATION_IDENTITY) {
        return rillshot::core::Status::failure(
            "dxgi-rotated-output-unsupported",
            "DXGI MVP only supports non-rotated outputs; use GDI fallback");
    }

    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL selectedLevel{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        adapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        flags,
        levels,
        static_cast<UINT>(std::size(levels)),
        D3D11_SDK_VERSION,
        &device,
        &selectedLevel,
        &context);

#if defined(_DEBUG)
    if (FAILED(hr)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            adapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            levels,
            static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION,
            &device,
            &selectedLevel,
            &context);
    }
#endif

    if (FAILED(hr)) {
        return rillshot::core::Status::failure("dxgi-create-device-failed", hrMessage("D3D11CreateDevice", hr));
    }

    ComPtr<IDXGIOutput1> output1;
    hr = output->QueryInterface(IID_PPV_ARGS(&output1));
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("dxgi-output1-failed", hrMessage("IDXGIOutput1 QI", hr));
    }

    ComPtr<IDXGIOutputDuplication> duplication;
    hr = output1->DuplicateOutput(device.Get(), &duplication);
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("dxgi-duplicate-output-failed", hrMessage("DuplicateOutput", hr));
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopResource;
    hr = duplication->AcquireNextFrame(700, &frameInfo, &desktopResource);
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("dxgi-acquire-frame-failed", hrMessage("AcquireNextFrame", hr));
    }
    const AcquiredFrameGuard acquiredFrame(duplication.Get());

    ComPtr<ID3D11Texture2D> acquiredTexture;
    hr = desktopResource->QueryInterface(IID_PPV_ARGS(&acquiredTexture));
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("dxgi-texture-qi-failed", hrMessage("QueryInterface(ID3D11Texture2D)", hr));
    }

    D3D11_TEXTURE2D_DESC desc{};
    acquiredTexture->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        return rillshot::core::Status::failure(
            "dxgi-unexpected-frame-format",
            "desktop duplication returned an unexpected pixel format");
    }

    const auto relativeX =
        static_cast<std::int64_t>(region.x) -
        static_cast<std::int64_t>(outputDesc.DesktopCoordinates.left);
    const auto relativeY =
        static_cast<std::int64_t>(region.y) -
        static_cast<std::int64_t>(outputDesc.DesktopCoordinates.top);
    const auto relativeRight = relativeX + static_cast<std::int64_t>(region.width);
    const auto relativeBottom = relativeY + static_cast<std::int64_t>(region.height);
    if (relativeX < 0 || relativeY < 0 ||
        relativeRight > static_cast<std::int64_t>(desc.Width) ||
        relativeBottom > static_cast<std::int64_t>(desc.Height)) {
        return rillshot::core::Status::failure(
            "dxgi-frame-bounds-mismatch",
            "capture region does not fit the acquired desktop texture");
    }

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("dxgi-create-staging-failed", hrMessage("CreateTexture2D(staging)", hr));
    }

    context->CopyResource(staging.Get(), acquiredTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("dxgi-map-failed", hrMessage("Map(staging)", hr));
    }
    const MappedTextureGuard mappedTexture(context.Get(), staging.Get());

    rillshot::core::Image image(region.width, region.height);
    const auto rowBytes = static_cast<std::size_t>(image.stride());
    constexpr auto bytesPerPixel = static_cast<std::size_t>(4);
    if (!mapped.pData ||
        static_cast<std::uint64_t>(relativeX) >
            static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)() / bytesPerPixel)) {
        return rillshot::core::Status::failure(
            "dxgi-invalid-frame-layout", "mapped desktop texture has an invalid layout");
    }
    const auto sourceXBytes =
        static_cast<std::size_t>(relativeX) * bytesPerPixel;
    if (static_cast<std::size_t>(mapped.RowPitch) < rowBytes ||
        sourceXBytes > static_cast<std::size_t>(mapped.RowPitch) - rowBytes) {
        return rillshot::core::Status::failure(
            "dxgi-invalid-row-pitch",
            "mapped desktop texture row pitch is smaller than the capture span");
    }
    const auto finalSourceRow =
        static_cast<std::uint64_t>(relativeBottom - 1);
    if (mapped.RowPitch == 0 ||
        finalSourceRow >
            static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)() /
                                       static_cast<std::size_t>(mapped.RowPitch))) {
        return rillshot::core::Status::failure(
            "dxgi-frame-offset-overflow",
            "mapped desktop texture offset exceeds the addressable range");
    }

    for (int y = 0; y < region.height; ++y) {
        const auto sourceY = static_cast<std::size_t>(relativeY) +
            static_cast<std::size_t>(y);
        const auto* src = static_cast<const unsigned char*>(mapped.pData) +
            sourceY * static_cast<std::size_t>(mapped.RowPitch) + sourceXBytes;
        std::memcpy(image.row(y), src, rowBytes);
    }

    out.image = std::move(image);
    out.backendName = "DXGI";
    out.unstable = false;
    out.capturedAt = std::chrono::steady_clock::now();
    return rillshot::core::Status::success();
}

} // namespace

rillshot::core::Status DxgiCaptureBackend::capture(const rillshot::core::RectI& region, CaptureFrame& out) {
    if (!region.isValid()) {
        return rillshot::core::Status::failure("invalid-region", "capture region must be positive");
    }

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("dxgi-create-factory-failed", hrMessage("CreateDXGIFactory1", hr));
    }

    rillshot::core::Status lastFailure = rillshot::core::Status::failure("dxgi-no-output", "no containing output found");

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            lastFailure = rillshot::core::Status::failure("dxgi-enum-adapters-failed", hrMessage("EnumAdapters1", hr));
            break;
        }

        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(outputIndex, &output);
            if (hr == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(hr)) {
                lastFailure = rillshot::core::Status::failure("dxgi-enum-outputs-failed", hrMessage("EnumOutputs", hr));
                break;
            }

            DXGI_OUTPUT_DESC outputDesc{};
            hr = output->GetDesc(&outputDesc);
            if (FAILED(hr)) {
                lastFailure = rillshot::core::Status::failure("dxgi-output-desc-failed", hrMessage("GetDesc", hr));
                continue;
            }

            if (!contains(outputDesc.DesktopCoordinates, region)) {
                continue;
            }

            auto status = captureFromOutput(adapter.Get(), output.Get(), outputDesc, region, out);
            if (status.ok) {
                return status;
            }
            lastFailure = status;
        }
    }

    return lastFailure;
}

} // namespace rillshot::capture
