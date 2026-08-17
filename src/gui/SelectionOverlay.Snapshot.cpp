#include "gui/SelectionOverlay.h"

#include <cstdint>
#include <cstring>
#include <limits>

namespace rillshot::gui {

bool SelectionOverlay::captureDesktopSnapshot() {
    releaseDesktopSnapshot();

    const int width = virtualScreenWidth_;
    const int height = virtualScreenHeight_;
    if (width <= 0 || height <= 0 ||
        static_cast<std::size_t>(width) >
            (std::numeric_limits<std::size_t>::max)() / 4U ||
        static_cast<std::size_t>(height) >
            (std::numeric_limits<std::size_t>::max)() /
                (static_cast<std::size_t>(width) * 4U)) {
        return false;
    }

    const HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* originalPixels = nullptr;
    void* dimmedPixels = nullptr;
    snapshotDc_ = CreateCompatibleDC(screenDc);
    dimmedSnapshotDc_ = CreateCompatibleDC(screenDc);
    snapshotBitmap_ = CreateDIBSection(
        screenDc, &info, DIB_RGB_COLORS, &originalPixels, nullptr, 0);
    dimmedSnapshotBitmap_ = CreateDIBSection(
        screenDc, &info, DIB_RGB_COLORS, &dimmedPixels, nullptr, 0);
    if (!snapshotDc_ || !dimmedSnapshotDc_ ||
        !snapshotBitmap_ || !dimmedSnapshotBitmap_ ||
        !originalPixels || !dimmedPixels) {
        ReleaseDC(nullptr, screenDc);
        releaseDesktopSnapshot();
        return false;
    }

    snapshotPreviousBitmap_ = SelectObject(snapshotDc_, snapshotBitmap_);
    dimmedSnapshotPreviousBitmap_ =
        SelectObject(dimmedSnapshotDc_, dimmedSnapshotBitmap_);
    if (!snapshotPreviousBitmap_ ||
        snapshotPreviousBitmap_ == HGDI_ERROR ||
        !dimmedSnapshotPreviousBitmap_ ||
        dimmedSnapshotPreviousBitmap_ == HGDI_ERROR) {
        if (snapshotPreviousBitmap_ == HGDI_ERROR) {
            snapshotPreviousBitmap_ = nullptr;
        }
        if (dimmedSnapshotPreviousBitmap_ == HGDI_ERROR) {
            dimmedSnapshotPreviousBitmap_ = nullptr;
        }
        ReleaseDC(nullptr, screenDc);
        releaseDesktopSnapshot();
        return false;
    }
    snapshotPixels_ = static_cast<const std::uint8_t*>(originalPixels);
    dimmedSnapshotPixels_ = static_cast<const std::uint8_t*>(dimmedPixels);
    snapshotStrideBytes_ = static_cast<std::size_t>(width) * 4U;

    const BOOL captured = BitBlt(
        snapshotDc_,
        0,
        0,
        width,
        height,
        screenDc,
        virtualScreen_.left,
        virtualScreen_.top,
        SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, screenDc);
    if (!captured) {
        releaseDesktopSnapshot();
        return false;
    }

    GdiFlush();
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::memcpy(dimmedPixels, originalPixels, pixelCount * 4U);
    auto* pixels = static_cast<std::uint8_t*>(dimmedPixels);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        auto* pixel = pixels + index * 4U;
        pixel[0] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(pixel[0]) * 62U / 100U);
        pixel[1] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(pixel[1]) * 62U / 100U);
        pixel[2] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(pixel[2]) * 62U / 100U);
        pixel[3] = 255U;
    }
    GdiFlush();
    return true;
}

void SelectionOverlay::releaseDesktopSnapshot() noexcept {
    snapshotPixels_ = nullptr;
    dimmedSnapshotPixels_ = nullptr;
    snapshotStrideBytes_ = 0;
    if (snapshotDc_ && snapshotPreviousBitmap_) {
        SelectObject(snapshotDc_, snapshotPreviousBitmap_);
    }
    if (dimmedSnapshotDc_ && dimmedSnapshotPreviousBitmap_) {
        SelectObject(dimmedSnapshotDc_, dimmedSnapshotPreviousBitmap_);
    }
    snapshotPreviousBitmap_ = nullptr;
    dimmedSnapshotPreviousBitmap_ = nullptr;
    if (snapshotBitmap_) {
        DeleteObject(snapshotBitmap_);
        snapshotBitmap_ = nullptr;
    }
    if (dimmedSnapshotBitmap_) {
        DeleteObject(dimmedSnapshotBitmap_);
        dimmedSnapshotBitmap_ = nullptr;
    }
    if (snapshotDc_) {
        DeleteDC(snapshotDc_);
        snapshotDc_ = nullptr;
    }
    if (dimmedSnapshotDc_) {
        DeleteDC(dimmedSnapshotDc_);
        dimmedSnapshotDc_ = nullptr;
    }
}

} // namespace rillshot::gui
