#include "gui/CaptureDimmer.h"

namespace rillshot::gui {
namespace {
constexpr wchar_t windowClass[] = L"Rillshot.CaptureDimmer";

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DISPLAYCHANGE:
        // Never keep a stale hole over a changed desktop topology.
        ShowWindow(window, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

bool CaptureDimmer::show(const rillshot::core::RectI& region) noexcept {
    hide();
    if (!region.isValid()) return false;
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const rillshot::core::RectI desktop{x, y, width, height};
    if (!desktop.isValid() || region.x < x || region.y < y ||
        region.right() > desktop.right() || region.bottom() > desktop.bottom()) {
        return false;
    }
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = windowProc;
    type.hInstance = instance;
    type.lpszClassName = windowClass;
    type.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    const HRGN outside = CreateRectRgn(0, 0, width, height);
    const HRGN hole = CreateRectRgn(region.x - x, region.y - y,
        static_cast<int>(region.right() - x), static_cast<int>(region.bottom() - y));
    if (!outside || !hole) {
        if (outside) DeleteObject(outside);
        if (hole) DeleteObject(hole);
        return false;
    }
    const int combined = CombineRgn(outside, outside, hole, RGN_DIFF);
    DeleteObject(hole);
    if (combined == ERROR || combined == NULLREGION) {
        DeleteObject(outside);
        return combined == NULLREGION;
    }
    window_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        windowClass, L"", WS_POPUP, x, y, width, height,
        nullptr, nullptr, instance, nullptr);
    if (!window_ || !SetWindowRgn(window_, outside, FALSE)) {
        DeleteObject(outside);
        hide();
        return false;
    }
    // Windows owns outside after SetWindowRgn succeeds. The capture rectangle
    // is absent from the window, including its first and last pixel rows.
    if (!SetLayeredWindowAttributes(window_, 0, 120, LWA_ALPHA) ||
        !SetWindowPos(window_, HWND_TOPMOST, x, y, width, height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        hide();
        return false;
    }
    UpdateWindow(window_);
    return true;
}

void CaptureDimmer::hide() noexcept {
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
}
} // namespace rillshot::gui
