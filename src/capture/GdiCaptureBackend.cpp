#include "capture/GdiCaptureBackend.h"

#include <Windows.h>

#include <cstring>
#include <string>

namespace rillshot::capture {

rillshot::core::Status GdiCaptureBackend::capture(const rillshot::core::RectI& region, CaptureFrame& out) {
    if (!region.isValid()) {
        return rillshot::core::Status::failure("invalid-region", "capture region must be positive");
    }

    // Allocate before acquiring GDI handles so an allocation exception cannot
    // strand a screen DC, memory DC, or bitmap.
    rillshot::core::Image image(region.width, region.height);

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return rillshot::core::Status::failure("gdi-getdc-failed", "GetDC(nullptr) failed");
    }

    HDC memoryDc = CreateCompatibleDC(screenDc);
    if (!memoryDc) {
        ReleaseDC(nullptr, screenDc);
        return rillshot::core::Status::failure("gdi-create-compatible-dc-failed", "CreateCompatibleDC failed");
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = region.width;
    bmi.bmiHeader.biHeight = -region.height; // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return rillshot::core::Status::failure("gdi-create-dib-failed", "CreateDIBSection failed");
    }

    HGDIOBJ old = SelectObject(memoryDc, bitmap);
    if (!old || old == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return rillshot::core::Status::failure(
            "gdi-select-bitmap-failed", "SelectObject failed for the capture bitmap");
    }
    const BOOL ok = BitBlt(
        memoryDc,
        0,
        0,
        region.width,
        region.height,
        screenDc,
        region.x,
        region.y,
        SRCCOPY | CAPTUREBLT);

    if (ok) {
        const auto bytes = image.bytes();
        std::memcpy(bytes.data(), bits, bytes.size());
    }

    SelectObject(memoryDc, old);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);

    if (!ok) {
        return rillshot::core::Status::failure("gdi-bitblt-failed", "BitBlt failed");
    }

    out.image = std::move(image);
    out.backendName = name();
    out.unstable = false;
    out.capturedAt = std::chrono::steady_clock::now();
    return rillshot::core::Status::success();
}

} // namespace rillshot::capture
