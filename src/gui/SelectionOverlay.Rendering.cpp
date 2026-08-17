#include "gui/SelectionOverlay.h"
#include "gui/SelectionVisuals.h"
#include "gui/WindowGeometry.h"

#include <dwmapi.h>

#include <algorithm>
#include <cstdint>
#include <span>

namespace rillshot::gui {
namespace {

constexpr int pointerRadius = 18;
constexpr int outlinePadding = 6;
constexpr COLORREF warningColor = RGB(196, 43, 28);

int localCoordinate(int screenCoordinate, int screenOrigin) noexcept {
    return geometry::saturatedCoordinateDelta(
        screenOrigin, screenCoordinate);
}

int translatedCoordinate(
    int targetOrigin,
    int coordinate,
    int sourceOrigin) noexcept {
    return geometry::saturatedCoordinateOffset(
        targetOrigin,
        geometry::coordinateDelta(sourceOrigin, coordinate));
}

COLORREF selectionAccentColor() noexcept {
    HIGHCONTRASTW highContrast{sizeof(HIGHCONTRASTW)};
    if (SystemParametersInfoW(
            SPI_GETHIGHCONTRAST,
            sizeof(HIGHCONTRASTW),
            &highContrast,
            0) &&
        (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0) {
        return GetSysColor(COLOR_HIGHLIGHT);
    }

    DWORD argb = 0;
    BOOL opaqueBlend = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&argb, &opaqueBlend))) {
        return RGB(
            static_cast<BYTE>((argb >> 16U) & 0xFFU),
            static_cast<BYTE>((argb >> 8U) & 0xFFU),
            static_cast<BYTE>(argb & 0xFFU));
    }
    return GetSysColor(COLOR_HIGHLIGHT);
}

bool pointInside(
    const POINT& point,
    const rillshot::core::RectI& region) noexcept {
    return point.x >= region.x && point.y >= region.y &&
           static_cast<long long>(point.x) < region.right() &&
           static_cast<long long>(point.y) < region.bottom();
}

RECT normalizedClientRectangle(
    const POINT& first,
    const POINT& second,
    const RECT& virtualScreen) noexcept {
    return RECT{
        localCoordinate(std::min(first.x, second.x), virtualScreen.left),
        localCoordinate(std::min(first.y, second.y), virtualScreen.top),
        localCoordinate(std::max(first.x, second.x), virtualScreen.left),
        localCoordinate(std::max(first.y, second.y), virtualScreen.top)};
}

void drawSelectionBorder(
    HDC dc,
    const RECT& rectangle,
    COLORREF accent) {
    if (rectangle.right <= rectangle.left || rectangle.bottom <= rectangle.top) {
        return;
    }

    const HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    const HPEN whitePen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
    if (whitePen) {
        const HGDIOBJ oldPen = SelectObject(dc, whitePen);
        Rectangle(dc, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
        SelectObject(dc, oldPen);
        DeleteObject(whitePen);
    }

    const HPEN accentPen = CreatePen(PS_SOLID, 1, accent);
    if (accentPen) {
        const HGDIOBJ oldPen = SelectObject(dc, accentPen);
        Rectangle(dc, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
        SelectObject(dc, oldPen);
        DeleteObject(accentPen);
    }
    SelectObject(dc, oldBrush);
}

void drawSelectionCursor(
    HDC dc,
    int x,
    int y,
    COLORREF accent,
    bool lightBackdrop) {
    const auto drawCross = [dc, x, y](int width, COLORREF color) {
        const HPEN pen = CreatePen(PS_SOLID, width, color);
        if (!pen) {
            return;
        }
        const HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, x - 11, y, nullptr);
        LineTo(dc, x - 4, y);
        MoveToEx(dc, x + 4, y, nullptr);
        LineTo(dc, x + 12, y);
        MoveToEx(dc, x, y - 11, nullptr);
        LineTo(dc, x, y - 4);
        MoveToEx(dc, x, y + 4, nullptr);
        LineTo(dc, x, y + 12);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    };

    const COLORREF outer = lightBackdrop
        ? RGB(0, 0, 0)
        : RGB(255, 255, 255);
    const COLORREF inner = lightBackdrop
        ? RGB(255, 255, 255)
        : RGB(0, 0, 0);
    drawCross(5, outer);
    drawCross(3, inner);
    drawCross(1, accent);

    const auto drawCenter = [dc, x, y](int radius, COLORREF color) {
        const HBRUSH centerBrush = CreateSolidBrush(color);
        if (!centerBrush) {
            return;
        }
        const HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        const HGDIOBJ oldBrush = SelectObject(dc, centerBrush);
        Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(centerBrush);
    };
    drawCenter(4, outer);
    drawCenter(3, inner);
    drawCenter(2, accent);
}

void drawHorizontalBoundary(
    HDC dc,
    const RECT& region,
    int y,
    COLORREF accent) {
    if (region.right <= region.left ||
        y < region.top || y >= region.bottom) {
        return;
    }

    const auto drawLine = [dc, &region, y](int width, COLORREF color) {
        const HPEN pen = CreatePen(PS_SOLID, width, color);
        if (!pen) {
            return;
        }
        const HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, region.left, y, nullptr);
        LineTo(dc, region.right, y);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    };

    drawLine(5, RGB(255, 255, 255));
    drawLine(2, accent);
}

} // namespace

void SelectionOverlay::paint() {
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(window_, &paint);
    int paintWidth = 0;
    int paintHeight = 0;
    if (!geometry::tryPositiveCoordinateSpan(
            paint.rcPaint.left, paint.rcPaint.right, paintWidth) ||
        !geometry::tryPositiveCoordinateSpan(
            paint.rcPaint.top, paint.rcPaint.bottom, paintHeight)) {
        EndPaint(window_, &paint);
        return;
    }

    HDC bufferDc = CreateCompatibleDC(dc);
    HBITMAP bufferBitmap =
        bufferDc ? CreateCompatibleBitmap(dc, paintWidth, paintHeight) : nullptr;
    HGDIOBJ oldBufferBitmap = nullptr;
    if (bufferDc && bufferBitmap) {
        oldBufferBitmap = SelectObject(bufferDc, bufferBitmap);
        if (!oldBufferBitmap || oldBufferBitmap == HGDI_ERROR) {
            oldBufferBitmap = nullptr;
            DeleteObject(bufferBitmap);
            bufferBitmap = nullptr;
            DeleteDC(bufferDc);
            bufferDc = nullptr;
        }
    }

    const bool buffered = bufferDc && bufferBitmap;
    const HDC targetDc = buffered ? bufferDc : dc;
    const int targetBaseX = buffered ? 0 : paint.rcPaint.left;
    const int targetBaseY = buffered ? 0 : paint.rcPaint.top;
    const HDC backgroundDc = dimmedSnapshotDc_ ? dimmedSnapshotDc_ : snapshotDc_;
    if (backgroundDc) {
        BitBlt(
            targetDc,
            targetBaseX,
            targetBaseY,
            paintWidth,
            paintHeight,
            backgroundDc,
            paint.rcPaint.left,
            paint.rcPaint.top,
            SRCCOPY);
    } else {
        RECT fallback{
            targetBaseX,
            targetBaseY,
            geometry::saturatedCoordinateOffset(targetBaseX, paintWidth),
            geometry::saturatedCoordinateOffset(targetBaseY, paintHeight)};
        const HBRUSH fallbackBrush = CreateSolidBrush(RGB(38, 40, 46));
        FillRect(targetDc, &fallback, fallbackBrush);
        DeleteObject(fallbackBrush);
    }

    RECT activeRectangle{};
    bool hasActiveRectangle = false;
    if ((mode_ == Mode::Point || mode_ == Mode::HorizontalBoundary) &&
        allowedRegion_) {
        activeRectangle = RECT{
            localCoordinate(allowedRegion_->x, virtualScreen_.left),
            localCoordinate(allowedRegion_->y, virtualScreen_.top),
            localCoordinate(
                static_cast<int>(allowedRegion_->right()), virtualScreen_.left),
            localCoordinate(
                static_cast<int>(allowedRegion_->bottom()), virtualScreen_.top)};
        hasActiveRectangle = true;
    } else if (mode_ == Mode::Region && dragging_) {
        activeRectangle =
            normalizedClientRectangle(startScreen_, currentScreen_, virtualScreen_);
        hasActiveRectangle =
            activeRectangle.right > activeRectangle.left &&
            activeRectangle.bottom > activeRectangle.top;
    }

    if (hasActiveRectangle && snapshotDc_) {
        RECT visible{};
        if (IntersectRect(&visible, &activeRectangle, &paint.rcPaint)) {
            int visibleWidth = 0;
            int visibleHeight = 0;
            if (geometry::tryPositiveCoordinateSpan(
                    visible.left, visible.right, visibleWidth) &&
                geometry::tryPositiveCoordinateSpan(
                    visible.top, visible.bottom, visibleHeight)) {
                BitBlt(
                    targetDc,
                    translatedCoordinate(
                        targetBaseX, visible.left, paint.rcPaint.left),
                    translatedCoordinate(
                        targetBaseY, visible.top, paint.rcPaint.top),
                    visibleWidth,
                    visibleHeight,
                    snapshotDc_,
                    visible.left,
                    visible.top,
                    SRCCOPY);
            }
        }
    }

    const COLORREF accent = selectionAccentColor();
    if (hasActiveRectangle) {
        RECT border{
            translatedCoordinate(
                targetBaseX, activeRectangle.left, paint.rcPaint.left),
            translatedCoordinate(
                targetBaseY, activeRectangle.top, paint.rcPaint.top),
            translatedCoordinate(
                targetBaseX, activeRectangle.right, paint.rcPaint.left),
            translatedCoordinate(
                targetBaseY, activeRectangle.bottom, paint.rcPaint.top)};
        drawSelectionBorder(
            targetDc,
            border,
            rejectedPoint_ ? warningColor : accent);
        if (mode_ == Mode::HorizontalBoundary && pointAllowed(currentScreen_)) {
            const int boundaryY =
                translatedCoordinate(
                    targetBaseY,
                    localCoordinate(currentScreen_.y, virtualScreen_.top),
                    paint.rcPaint.top);
            drawHorizontalBoundary(targetDc, border, boundaryY, accent);
        }
    }

    const int cursorX =
        translatedCoordinate(
            targetBaseX,
            localCoordinate(currentScreen_.x, virtualScreen_.left),
            paint.rcPaint.left);
    const int cursorY =
        translatedCoordinate(
            targetBaseY,
            localCoordinate(currentScreen_.y, virtualScreen_.top),
            paint.rcPaint.top);
    drawSelectionCursor(
        targetDc,
        cursorX,
        cursorY,
        accent,
        cursorBackdropIsLight());

    if (buffered) {
        BitBlt(
            dc,
            paint.rcPaint.left,
            paint.rcPaint.top,
            paintWidth,
            paintHeight,
            bufferDc,
            0,
            0,
            SRCCOPY);
        SelectObject(bufferDc, oldBufferBitmap);
    }
    if (bufferBitmap) {
        DeleteObject(bufferBitmap);
    }
    if (bufferDc) {
        DeleteDC(bufferDc);
    }
    EndPaint(window_, &paint);
}

void SelectionOverlay::invalidatePointer(const POINT& point) {
    if (!window_) {
        return;
    }
    const int x = localCoordinate(point.x, virtualScreen_.left);
    const int y = localCoordinate(point.y, virtualScreen_.top);
    RECT dirty{
        geometry::saturatedCoordinateOffset(x, -pointerRadius),
        geometry::saturatedCoordinateOffset(y, -pointerRadius),
        geometry::saturatedCoordinateOffset(x, pointerRadius + 1),
        geometry::saturatedCoordinateOffset(y, pointerRadius + 1)};
    InvalidateRect(window_, &dirty, FALSE);
}

void SelectionOverlay::invalidateSelectionOutline(
    const POINT& first,
    const POINT& second) {
    if (!window_) {
        return;
    }
    const RECT bounds = normalizedClientRectangle(first, second, virtualScreen_);
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return;
    }
    const RECT edges[] = {
        {geometry::saturatedCoordinateOffset(bounds.left, -outlinePadding),
         geometry::saturatedCoordinateOffset(bounds.top, -outlinePadding),
         geometry::saturatedCoordinateOffset(bounds.right, outlinePadding),
         geometry::saturatedCoordinateOffset(bounds.top, outlinePadding)},
        {geometry::saturatedCoordinateOffset(bounds.left, -outlinePadding),
         geometry::saturatedCoordinateOffset(bounds.bottom, -outlinePadding),
         geometry::saturatedCoordinateOffset(bounds.right, outlinePadding),
         geometry::saturatedCoordinateOffset(bounds.bottom, outlinePadding)},
        {geometry::saturatedCoordinateOffset(bounds.left, -outlinePadding),
         geometry::saturatedCoordinateOffset(bounds.top, -outlinePadding),
         geometry::saturatedCoordinateOffset(bounds.left, outlinePadding),
         geometry::saturatedCoordinateOffset(bounds.bottom, outlinePadding)},
        {geometry::saturatedCoordinateOffset(bounds.right, -outlinePadding),
         geometry::saturatedCoordinateOffset(bounds.top, -outlinePadding),
         geometry::saturatedCoordinateOffset(bounds.right, outlinePadding),
         geometry::saturatedCoordinateOffset(bounds.bottom, outlinePadding)},
    };
    for (const RECT& edge : edges) {
        InvalidateRect(window_, &edge, FALSE);
    }
}

void SelectionOverlay::invalidateSelectionFillDelta(
    const POINT& start,
    const POINT& oldCurrent,
    const POINT& newCurrent) {
    if (!window_) {
        return;
    }

    const RECT oldRectangle =
        normalizedClientRectangle(start, oldCurrent, virtualScreen_);
    const RECT newRectangle =
        normalizedClientRectangle(start, newCurrent, virtualScreen_);
    const HRGN oldRegion = CreateRectRgnIndirect(&oldRectangle);
    const HRGN newRegion = CreateRectRgnIndirect(&newRectangle);
    const HRGN deltaRegion = CreateRectRgn(0, 0, 0, 0);
    if (oldRegion && newRegion && deltaRegion) {
        CombineRgn(deltaRegion, oldRegion, newRegion, RGN_XOR);
        InvalidateRgn(window_, deltaRegion, FALSE);
    }
    if (oldRegion) {
        DeleteObject(oldRegion);
    }
    if (newRegion) {
        DeleteObject(newRegion);
    }
    if (deltaRegion) {
        DeleteObject(deltaRegion);
    }
}

void SelectionOverlay::invalidateBoundaryLine(const POINT& point) {
    if (!window_ || !allowedRegion_) {
        return;
    }
    const int y = localCoordinate(point.y, virtualScreen_.top);
    const RECT dirty{
        geometry::saturatedCoordinateOffset(
            localCoordinate(allowedRegion_->x, virtualScreen_.left),
            -outlinePadding),
        geometry::saturatedCoordinateOffset(y, -outlinePadding),
        geometry::saturatedCoordinateOffset(
            localCoordinate(
                static_cast<int>(allowedRegion_->right()), virtualScreen_.left),
            outlinePadding),
        geometry::saturatedCoordinateOffset(y, outlinePadding + 1)};
    InvalidateRect(window_, &dirty, FALSE);
}

void SelectionOverlay::invalidateAllowedOutline() {
    if (!allowedRegion_) {
        return;
    }
    const POINT first{allowedRegion_->x, allowedRegion_->y};
    const POINT second{
        static_cast<LONG>(allowedRegion_->right()),
        static_cast<LONG>(allowedRegion_->bottom())};
    invalidateSelectionOutline(first, second);
}

bool SelectionOverlay::pointAllowed(const POINT& point) const noexcept {
    return !allowedRegion_ || pointInside(point, *allowedRegion_);
}

bool SelectionOverlay::cursorBackdropIsLight() const noexcept {
    const int width = virtualScreenWidth_;
    const int height = virtualScreenHeight_;
    if (!snapshotPixels_ || !dimmedSnapshotPixels_ || snapshotStrideBytes_ == 0 ||
        width <= 0 || height <= 0) {
        return false;
    }
    bool cursorOverOriginalSnapshot = false;
    if ((mode_ == Mode::Point || mode_ == Mode::HorizontalBoundary) && allowedRegion_) {
        cursorOverOriginalSnapshot = pointAllowed(currentScreen_);
    } else if (mode_ == Mode::Region && dragging_) {
        const RECT active =
            normalizedClientRectangle(startScreen_, currentScreen_, virtualScreen_);
        const POINT clientPoint{
            localCoordinate(currentScreen_.x, virtualScreen_.left),
            localCoordinate(currentScreen_.y, virtualScreen_.top)};
        cursorOverOriginalSnapshot =
            clientPoint.x >= active.left && clientPoint.x < active.right &&
            clientPoint.y >= active.top && clientPoint.y < active.bottom;
    }
    const std::uint8_t* visiblePixels = cursorOverOriginalSnapshot
        ? snapshotPixels_
        : dimmedSnapshotPixels_;
    const std::size_t byteCount =
        snapshotStrideBytes_ * static_cast<std::size_t>(height);
    return selection::chooseCursorOuterTone(
               std::span<const std::uint8_t>(visiblePixels, byteCount),
               width,
               height,
               snapshotStrideBytes_,
               localCoordinate(currentScreen_.x, virtualScreen_.left),
               localCoordinate(currentScreen_.y, virtualScreen_.top)) ==
        selection::CursorOuterTone::Black;
}

} // namespace rillshot::gui
