#include "TestSupport.h"
#include "StitchTestFixtures.h"
#include "gui/CaptureDimmer.h"
#include "output/WicImageWriter.h"
#include "stitch/StitchedImageBuilder.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using rillshot::core::Image;
using rillshot::core::ScrollDirection;
using rillshot::output::ImageFormat;

static Image decode(const std::filesystem::path& path) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return {};
    UINT width = 0, height = 0;
    if (FAILED(converter->GetSize(&width, &height))) return {};
    Image result(static_cast<int>(width), static_cast<int>(height));
    if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(result.stride()),
            static_cast<UINT>(result.bytes().size()), result.bytes().data()))) return {};
    return result;
}

static int testStreamedOutput(const std::filesystem::path& directory) {
    const auto document = makeDocument(137, 719);
    rillshot::output::WicImageWriter writer;
    for (const auto direction : {ScrollDirection::Down, ScrollDirection::Up}) {
        const int start = direction == ScrollDirection::Down ? 0 : 517;
        rillshot::stitch::StitchedImageBuilder builder(
            cropDocumentWindow(document, start, 202), direction);
        if (direction == ScrollDirection::Down) {
            builder.addRowsFrom(document, 202, 393);
            builder.addRowsFrom(document, 393, 719);
        } else {
            builder.addRowsFrom(document, 211, 517);
            builder.addRowsFrom(document, 0, 211);
        }
        for (const auto format : {ImageFormat::Png, ImageFormat::Bmp}) {
            const auto path = directory / (format == ImageFormat::Png ? L"out.png" : L"out.bmp");
            const auto status = writer.write(builder, path.wstring(), format, true);
            if (!status.ok) return fail(status.message.c_str());
            const auto actual = decode(path);
            if (actual.empty() || actual.width() != document.width() || actual.height() != document.height() ||
                !std::equal(actual.bytes().begin(), actual.bytes().end(), document.bytes().begin())) {
                return fail("streamed PNG/BMP changed pixels, seams, or upward row order");
            }
            const auto collision = writer.write(builder, path.wstring(), format, false);
            if (collision.ok || collision.code != "wic-output-exists") {
                return fail("streamed writer overwrote an existing unconfirmed output");
            }
            const auto preserved = decode(path);
            if (preserved.empty() || !std::equal(preserved.bytes().begin(),
                    preserved.bytes().end(), document.bytes().begin())) {
                return fail("refused overwrite changed the existing image");
            }
        }
    }
    return EXIT_SUCCESS;
}

static BOOL CALLBACK findDimmer(HWND window, LPARAM parameter) {
    wchar_t className[80]{};
    GetClassNameW(window, className, 80);
    if (std::wstring(className) == L"Rillshot.CaptureDimmer") {
        *reinterpret_cast<HWND*>(parameter) = window;
        return FALSE;
    }
    return TRUE;
}

static int testDimmerBoundaryAndCleanup() {
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width < 32 || height < 32) return fail("test desktop is unavailable");
    const rillshot::core::RectI capture{x + 8, y + 8, width - 16, height - 16};
    const HWND foreground = GetForegroundWindow();
    rillshot::gui::CaptureDimmer dimmer;
    if (!dimmer.show(capture)) return fail("could not create capture dimmer");
    HWND window = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), findDimmer, reinterpret_cast<LPARAM>(&window));
    const HRGN shape = CreateRectRgn(0, 0, 0, 0);
    if (!window || !shape) {
        if (shape) DeleteObject(shape);
        return fail("dimmer window/region is missing");
    }
    const bool valid = GetWindowRgn(window, shape) != ERROR &&
        PtInRegion(shape, 7, 8) && PtInRegion(shape, 8, 7) &&
        PtInRegion(shape, width - 8, height - 9) &&
        !PtInRegion(shape, 8, 8) && !PtInRegion(shape, width - 9, height - 9) &&
        GetForegroundWindow() == foreground &&
        SendMessageW(window, WM_NCHITTEST, 0, 0) == HTTRANSPARENT &&
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0;
    DeleteObject(shape);
    dimmer.hide();
    dimmer.hide();
    if (!valid || IsWindow(window)) return fail("dimmer covered crop pixels, took focus, or survived cleanup");
    if (dimmer.show({x, y, 0, height})) return fail("dimmer accepted an empty capture rectangle");
    return EXIT_SUCCESS;
}

int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return fail("COM initialization failed");
    // All files are scoped to this process's temporary directory.
    const auto directory = std::filesystem::temp_directory_path() /
        (L"rillshot-output-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const int result = testStreamedOutput(directory);
    std::filesystem::remove(directory / L"out.png");
    std::filesystem::remove(directory / L"out.bmp");
    std::filesystem::remove(directory);
    const int dimmerResult = result == EXIT_SUCCESS ? testDimmerBoundaryAndCleanup() : result;
    CoUninitialize();
    return dimmerResult;
}
