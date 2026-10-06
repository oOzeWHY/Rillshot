#include "TestSupport.h"
#include "gui/SelectionOverlay.h"
#include "platform/AppPaths.h"
#include "platform/UserPreferences.h"

#include <Windows.h>
#include <dwmapi.h>
#include <filesystem>
#include <string>

namespace rillshot::gui {

// Exercise the production snapshot, WM_PAINT renderer and cursor contrast
// without a modal full-desktop selector or any synthetic user input.
struct SelectionOverlayTestAccess {
    static bool check(bool dimOutside, int x, int y) {
        for (const auto mode : {SelectionOverlay::Mode::Region,
                 SelectionOverlay::Mode::Point, SelectionOverlay::Mode::HorizontalBoundary}) {
            if (!checkMode(mode, dimOutside, x, y)) return false;
        }
        return true;
    }

    static bool checkMode(SelectionOverlay::Mode mode, bool dimOutside, int x, int y) {
        constexpr int width = 260, height = 180;
        SelectionOverlay overlay(mode, rillshot::core::RectI{x + 64, y + 48, 128, 80}, dimOutside);
        overlay.virtualScreen_ = {x, y, x + width, y + height};
        overlay.virtualScreenWidth_ = width;
        overlay.virtualScreenHeight_ = height;
        if (!overlay.captureDesktopSnapshot()) {
            std::cerr << "desktop snapshot failed, Windows error=" << GetLastError() << '\n';
            return false;
        }
        if (!SelectionOverlay::registerWindowClass()) return false;
        if (!dimOutside && (overlay.dimmedSnapshotBitmap_ || overlay.dimmedSnapshotDc_)) return false;
        const COLORREF outsideOriginal = GetPixel(overlay.snapshotDc_, 14, 14);
        const COLORREF insideOriginal = GetPixel(overlay.snapshotDc_, 100, 90);
        overlay.dragging_ = mode == SelectionOverlay::Mode::Region;
        overlay.startScreen_ = {x + 64, y + 48};
        overlay.currentScreen_ = {x + 192, y + 128};
        if (mode == SelectionOverlay::Mode::HorizontalBoundary)
            overlay.currentScreen_ = {x + 180, y + 111};
        struct WindowGuard {
            HWND handle;
            ~WindowGuard() { if (handle && IsWindow(handle)) DestroyWindow(handle); }
        } guard{CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            L"Rillshot.SelectionOverlay", L"", WS_POPUP,
            x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), &overlay)};
        if (!guard.handle) return false;
        SetWindowPos(guard.handle, HWND_TOPMOST, x, y, width, height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateWindow(guard.handle);
        DwmFlush();
        const HDC dc = GetDC(guard.handle);
        if (!dc) return false;
        const COLORREF outside = GetPixel(dc, 14, 14);
        const COLORREF inside = GetPixel(dc, 100, 90);
        ReleaseDC(guard.handle, dc);
        if (outside == CLR_INVALID || inside == CLR_INVALID ||
            outsideOriginal == CLR_INVALID || inside != insideOriginal) return false;
        if (dimOutside) {
            if (GetRValue(outside) >= GetRValue(outsideOriginal) ||
                GetGValue(outside) >= GetGValue(outsideOriginal) ||
                GetBValue(outside) >= GetBValue(outsideOriginal)) return false;
        } else if (outside != outsideOriginal) {
            return false;
        }
        // Fixed-region point selection lets the cursor move outside the region
        // without changing the rectangle itself, unlike a live drag endpoint.
        overlay.mode_ = SelectionOverlay::Mode::Point;
        overlay.allowedRegion_ = rillshot::core::RectI{x + 64, y + 48, 128, 80};
        overlay.currentScreen_ = {x + 14, y + 14};
        if (overlay.cursorBackdropIsLight() != !dimOutside) return false;
        overlay.currentScreen_ = {x + 100, y + 90};
        return overlay.cursorBackdropIsLight();
    }
};
} // namespace rillshot::gui

static LRESULT CALLBACK fixtureProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        const auto brush = CreateSolidBrush(RGB(220, 190, 170));
        FillRect(dc, &paint.rcPaint, brush);
        DeleteObject(brush);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

static int checkPreferences() {
    using namespace rillshot::platform;
    const auto directory = executableDirectory() / L"settings";
    const auto path = directory / L"Rillshot.ini";
    // This is the test executable's directory, not the application's data.
    // Preserve any unexpected existing file rather than overwrite it.
    if (std::filesystem::exists(path)) return fail("test settings path already exists");
    const bool directoryExisted = std::filesystem::exists(directory);
    struct SettingsGuard {
        std::filesystem::path path;
        bool directoryExisted;
        ~SettingsGuard() {
            WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
            std::error_code error;
            std::filesystem::remove(path, error);
            if (!directoryExisted) std::filesystem::remove(path.parent_path(), error);
        }
    } cleanup{path, directoryExisted};
    if (!UserPreferences{}.dimOutsideCapture || !loadUserPreferences().dimOutsideCapture)
        return fail("lower outside brightness must default on with no configuration");
    if (UserPreferences{}.windowBackground != WindowBackgroundPreference::Solid ||
        loadUserPreferences().windowBackground != WindowBackgroundPreference::Solid)
        return fail("window background must default to an opaque solid surface");
    auto preferences = loadUserPreferences();
    preferences.dimOutsideCapture = false;
    if (!saveUserPreferences(preferences) || loadUserPreferences().dimOutsideCapture)
        return fail("an explicit disabled brightness preference did not survive reload");
    preferences.dimOutsideCapture = true;
    if (!saveUserPreferences(preferences) || !loadUserPreferences().dimOutsideCapture)
        return fail("an enabled brightness preference did not survive reload");
    preferences.windowBackground = WindowBackgroundPreference::SystemMaterial;
    if (!saveUserPreferences(preferences) ||
        loadUserPreferences().windowBackground != WindowBackgroundPreference::SystemMaterial)
        return fail("system material preference did not survive reload");
    preferences.windowBackground = WindowBackgroundPreference::Solid;
    if (!saveUserPreferences(preferences) ||
        loadUserPreferences().windowBackground != WindowBackgroundPreference::Solid)
        return fail("solid background preference did not survive reload");
    WritePrivateProfileStringW(L"appearance", L"windowBackground", L"999", path.c_str());
    if (loadUserPreferences().windowBackground != WindowBackgroundPreference::Solid)
        return fail("invalid background preference must fall back to opaque solid");
    WritePrivateProfileStringW(L"appearance", L"windowBackground", nullptr, path.c_str());
    if (loadUserPreferences().windowBackground != WindowBackgroundPreference::Solid)
        return fail("legacy configuration must default to opaque solid");
    WritePrivateProfileStringW(L"capture", L"dimOutside", nullptr, path.c_str());
    if (!loadUserPreferences().dimOutsideCapture)
        return fail("legacy configuration without the brightness key must default on");
    return EXIT_SUCCESS;
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW type{};
    type.lpfnWndProc = fixtureProc;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"Rillshot.SelectionBrightnessFixture";
    if (!RegisterClassW(&type)) return fail("fixture registration failed");
    const int x = 40, y = 40;
    const HWND fixture = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        type.lpszClassName, L"", WS_POPUP, x, y, 260, 180,
        nullptr, nullptr, type.hInstance, nullptr);
    if (!fixture) return fail("fixture creation failed");
    SetWindowPos(fixture, HWND_TOPMOST, x, y, 260, 180, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateWindow(fixture);
    DwmFlush();
    const bool pixelsMatch = rillshot::gui::SelectionOverlayTestAccess::check(false, x, y) &&
        rillshot::gui::SelectionOverlayTestAccess::check(true, x, y);
    DestroyWindow(fixture);
    if (!pixelsMatch) return fail("selection changed crop brightness or ignored the disabled setting");
    return checkPreferences();
}
