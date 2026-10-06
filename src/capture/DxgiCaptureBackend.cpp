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

// One backend is owned by one capture session/worker. COM resources are never
// shared across threads or capture sessions, and are released with the backend.
struct DxgiCaptureState {
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    DXGI_OUTPUT_DESC description{};
    rillshot::core::RectI region{};
    bool hasPixels = false;
};

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
    DxgiCaptureState& state,
    DxgiCaptureStatistics& statistics,
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

    auto& device = state.device;
    auto& context = state.context;
    auto& duplication = state.duplication;
    auto& staging = state.staging;
    HRESULT hr = S_OK;
    if (!duplication) {
        D3D_FEATURE_LEVEL selectedLevel{};
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

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

        hr = output1->DuplicateOutput(device.Get(), &duplication);
        if (FAILED(hr)) {
            return rillshot::core::Status::failure("dxgi-duplicate-output-failed", hrMessage("DuplicateOutput", hr));
        }
        state.output = output;
        state.description = outputDesc;
        state.region = region;
        ++statistics.deviceInitializations;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopResource;
    // An unchanged desktop is a valid stabilization sample. Reuse our own ROI
    // staging copy rather than block for 700 ms or return a timeout failure.
    // The first sample has no cached pixels and gets a bounded 100 ms wait.
    hr = duplication->AcquireNextFrame(state.hasPixels ? 0U : 100U, &frameInfo, &desktopResource);
    const bool newFrame = SUCCEEDED(hr);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT && state.hasPixels) {
        ++statistics.unchangedFrames;
    } else if (FAILED(hr)) {
        return rillshot::core::Status::failure("dxgi-acquire-frame-failed", hrMessage("AcquireNextFrame", hr));
    }
    const AcquiredFrameGuard acquiredFrame(newFrame ? duplication.Get() : nullptr);

    if (newFrame) {
        ++statistics.framesAcquired;

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
        // Only the selected capture rectangle is read by the CPU. Keeping the
        // staging texture region-sized avoids copying and mapping the rest of a
        // 4K/8K desktop for every stabilization sample.
        stagingDesc.Width = static_cast<UINT>(region.width);
        stagingDesc.Height = static_cast<UINT>(region.height);
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.SampleDesc.Quality = 0;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        if (!staging) {
            hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
            if (FAILED(hr)) {
                return rillshot::core::Status::failure("dxgi-create-staging-failed", hrMessage("CreateTexture2D(staging)", hr));
            }
            ++statistics.stagingAllocations;
        }

        const D3D11_BOX sourceBox{
            static_cast<UINT>(relativeX),
            static_cast<UINT>(relativeY),
            0,
            static_cast<UINT>(relativeRight),
            static_cast<UINT>(relativeBottom),
            1};
        context->CopySubresourceRegion(
            staging.Get(), 0, 0, 0, 0,
            acquiredTexture.Get(), 0, &sourceBox);
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        return rillshot::core::Status::failure("dxgi-map-failed", hrMessage("Map(staging)", hr));
    }
    const MappedTextureGuard mappedTexture(context.Get(), staging.Get());

    rillshot::core::Image image(region.width, region.height);
    const auto rowBytes = static_cast<std::size_t>(image.stride());
    if (!mapped.pData) {
        return rillshot::core::Status::failure(
            "dxgi-invalid-frame-layout", "mapped desktop texture has an invalid layout");
    }
    if (static_cast<std::size_t>(mapped.RowPitch) < rowBytes) {
        return rillshot::core::Status::failure(
            "dxgi-invalid-row-pitch",
            "mapped desktop texture row pitch is smaller than the capture span");
    }
    const auto finalSourceRow =
        static_cast<std::uint64_t>(region.height - 1);
    if (mapped.RowPitch == 0 ||
        finalSourceRow >
            static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)() /
                                       static_cast<std::size_t>(mapped.RowPitch))) {
        return rillshot::core::Status::failure(
            "dxgi-frame-offset-overflow",
            "mapped desktop texture offset exceeds the addressable range");
    }

    for (int y = 0; y < region.height; ++y) {
        const auto sourceY = static_cast<std::size_t>(y);
        const auto* src = static_cast<const unsigned char*>(mapped.pData) +
            sourceY * static_cast<std::size_t>(mapped.RowPitch);
        std::memcpy(image.row(y), src, rowBytes);
    }

    out.image = std::move(image);
    state.hasPixels = true;
    out.backendName = "DXGI";
    out.unstable = false;
    out.capturedAt = std::chrono::steady_clock::now();
    return rillshot::core::Status::success();
}

} // namespace

DxgiCaptureBackend::DxgiCaptureBackend() = default;
DxgiCaptureBackend::~DxgiCaptureBackend() = default;

rillshot::core::Status DxgiCaptureBackend::capture(const rillshot::core::RectI& region, CaptureFrame& out) {
    if (!region.isValid()) {
        return rillshot::core::Status::failure("invalid-region", "capture region must be positive");
    }

    if (state_) {
        DXGI_OUTPUT_DESC description{};
        const auto& old = state_->description.DesktopCoordinates;
        const bool sameRegion = region.x == state_->region.x && region.y == state_->region.y &&
            region.width == state_->region.width && region.height == state_->region.height;
        if (sameRegion && SUCCEEDED(state_->output->GetDesc(&description)) &&
            description.AttachedToDesktop && description.Rotation == state_->description.Rotation &&
            description.DesktopCoordinates.left == old.left && description.DesktopCoordinates.top == old.top &&
            description.DesktopCoordinates.right == old.right && description.DesktopCoordinates.bottom == old.bottom) {
            auto status = captureFromOutput(*state_, statistics_, nullptr,
                state_->output.Get(), description, region, out);
            // Access-lost/device errors invalidate cached pixels. Auto mode
            // can use GDI for this sample and reinitialize DXGI next time.
            if (!status.ok) state_.reset();
            return status;
        }
        state_.reset();
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

            auto candidate = std::make_unique<DxgiCaptureState>();
            auto status = captureFromOutput(*candidate, statistics_, adapter.Get(), output.Get(), outputDesc, region, out);
            if (status.ok) {
                state_ = std::move(candidate);
                return status;
            }
            lastFailure = status;
        }
    }

    return lastFailure;
}

} // namespace rillshot::capture
