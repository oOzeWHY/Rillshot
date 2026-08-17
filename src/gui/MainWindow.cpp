#include "gui/MainWindow.h"

#include "gui/GuiResourceIds.h"
#include "gui/Win32ControlUtils.h"
#include "gui/WindowGeometry.h"
#include "gui/WindowsShellUtils.h"

#include <utility>

namespace rillshot::gui {
namespace {

constexpr wchar_t mainWindowClassName[] = L"Rillshot.MainWindow";
constexpr int logicalClientWidth = 760;
constexpr int logicalClientHeight = 660;

} // namespace

MainWindow::~MainWindow() {
    captureController_.requestStop();
    captureController_.joinCompleted();
    if (font_) {
        DeleteObject(font_);
    }
}

bool MainWindow::create(int showCommand) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = mainWindowClassName;
    windowClass.lpfnWndProc = &MainWindow::windowProc;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    dpi_ = GetDpiForSystem();
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT bounds{0, 0, win32::scaled(logicalClientWidth, dpi_), win32::scaled(logicalClientHeight, dpi_)};
    AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, dpi_);
    int windowWidth = 0;
    int windowHeight = 0;
    if (!geometry::tryPositiveCoordinateSpan(bounds.left, bounds.right, windowWidth) ||
        !geometry::tryPositiveCoordinateSpan(bounds.top, bounds.bottom, windowHeight)) {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        mainWindowClassName,
        L"Rillshot",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        instance_,
        this);
    if (!window_) {
        return false;
    }

    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    return true;
}

LRESULT MainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        return createControls() ? 0 : -1;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case pickRegionId: pickRegion(); return 0;
        case pickPointId: pickScrollPoint(); return 0;
        case browseOutputId: browseOutput(); return 0;
        case startCaptureId: startCapture(); return 0;
        case stopCaptureId: stopCapture(); return 0;
        default: break;
        }
        break;
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        int suggestedWidth = 0;
        int suggestedHeight = 0;
        if (suggested &&
            geometry::tryPositiveCoordinateSpan(
                suggested->left, suggested->right, suggestedWidth) &&
            geometry::tryPositiveCoordinateSpan(
                suggested->top, suggested->bottom, suggestedHeight)) {
            SetWindowPos(
                window_, nullptr, suggested->left, suggested->top,
                suggestedWidth, suggestedHeight,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        layoutControls(LOWORD(wParam));
        return 0;
    }
    case captureCompleteMessage:
        captureController_.joinCompleted();
        if (auto completion = captureController_.takeCompletion()) {
            captureCompleted(std::move(*completion));
        }
        return 0;
    case WM_CLOSE:
        if (captureController_.hasWorker()) {
            pendingClose_ = true;
            if (captureController_.running()) {
                stopCapture();
            } else {
                captureController_.joinCompleted();
                if (auto completion = captureController_.takeCompletion()) {
                    captureCompleted(std::move(*completion));
                } else {
                    DestroyWindow(window_);
                }
            }
            return 0;
        }
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

LRESULT CALLBACK MainWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return self ? self->handleMessage(message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

} // namespace rillshot::gui
