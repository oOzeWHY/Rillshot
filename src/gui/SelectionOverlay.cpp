#include "gui/SelectionOverlay.h"
#include "gui/SelectionLifecycle.h"
#include "gui/WindowGeometry.h"
#include "gui/WindowsShellUtils.h"
#include "platform/StartupDiagnostics.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <utility>

namespace rillshot::gui {
namespace {

constexpr wchar_t overlayClassName[] = L"Rillshot.SelectionOverlay";

} // namespace

const wchar_t* selectionExitReasonCode(SelectionExitReason reason) noexcept {
    switch (reason) {
    case SelectionExitReason::Accepted: return L"accepted";
    case SelectionExitReason::UserCancelled: return L"user-cancelled";
    case SelectionExitReason::ForegroundChanged: return L"foreground-changed";
    case SelectionExitReason::DisplayChanged: return L"display-changed";
    case SelectionExitReason::InitializationFailed: return L"initialization-failed";
    case SelectionExitReason::MessageLoopFailed: return L"message-loop-failed";
    case SelectionExitReason::WindowClosed: return L"window-closed";
    }
    return L"unknown";
}

SelectionResult<rillshot::core::PointI> SelectionOverlay::pickPoint(
    HWND owner,
    std::optional<rillshot::core::RectI> allowedRegion) {
    SelectionOverlay overlay(Mode::Point, allowedRegion);
    [[maybe_unused]] const bool accepted = overlay.run(owner);
    rillshot::platform::writeStartupLog(
        L"Selection point finished: " +
        std::wstring(selectionExitReasonCode(overlay.exitReason_)) +
        L", error=" + std::to_wstring(overlay.systemError_));
    return {overlay.point_, overlay.exitReason_, overlay.systemError_};
}

SelectionResult<rillshot::core::PointI> SelectionOverlay::pickHorizontalBoundary(
    HWND owner,
    rillshot::core::RectI allowedRegion) {
    if (!allowedRegion.isValid()) {
        return {
            std::nullopt,
            SelectionExitReason::InitializationFailed,
            ERROR_INVALID_PARAMETER};
    }
    SelectionOverlay overlay(Mode::HorizontalBoundary, allowedRegion);
    [[maybe_unused]] const bool accepted = overlay.run(owner);
    rillshot::platform::writeStartupLog(
        L"Selection boundary finished: " +
        std::wstring(selectionExitReasonCode(overlay.exitReason_)) +
        L", error=" + std::to_wstring(overlay.systemError_));
    return {overlay.point_, overlay.exitReason_, overlay.systemError_};
}

SelectionResult<rillshot::core::RectI> SelectionOverlay::pickRegion(HWND owner) {
    SelectionOverlay overlay(Mode::Region);
    [[maybe_unused]] const bool accepted = overlay.run(owner);
    rillshot::platform::writeStartupLog(
        L"Selection region finished: " +
        std::wstring(selectionExitReasonCode(overlay.exitReason_)) +
        L", error=" + std::to_wstring(overlay.systemError_));
    return {overlay.region_, overlay.exitReason_, overlay.systemError_};
}

SelectionOverlay::~SelectionOverlay() {
    releaseDesktopSnapshot();
}

bool SelectionOverlay::registerWindowClass() {
    static std::once_flag once;
    static bool registered = false;
    std::call_once(once, [] {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = overlayClassName;
        windowClass.lpfnWndProc = &SelectionOverlay::windowProc;
        windowClass.hCursor = nullptr;
        // Every pixel is painted from an immutable desktop snapshot. Omitting a
        // class brush prevents a full-screen erase between pointer frames.
        windowClass.hbrBackground = nullptr;
        registered =
            RegisterClassExW(&windowClass) != 0 ||
            GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    });
    return registered;
}

bool SelectionOverlay::run(HWND owner) {
    exitReason_ = SelectionExitReason::InitializationFailed;
    systemError_ = ERROR_SUCCESS;
    if (!registerWindowClass()) {
        systemError_ = GetLastError();
        return false;
    }

    owner_ = owner && IsWindow(owner) ? owner : nullptr;
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) {
        systemError_ = ERROR_INVALID_DATA;
        return false;
    }
    int right = 0;
    int bottom = 0;
    using geometry::tryCoordinateOffset;
    if (!tryCoordinateOffset(left, width, right) ||
        !tryCoordinateOffset(top, height, bottom)) {
        systemError_ = ERROR_ARITHMETIC_OVERFLOW;
        return false;
    }
    virtualScreen_ = RECT{left, top, right, bottom};
    virtualScreenWidth_ = width;
    virtualScreenHeight_ = height;

    ownerWasVisible_ = owner_ && IsWindowVisible(owner_) != FALSE;
    ownerWasEnabled_ = owner_ && IsWindowEnabled(owner_) != FALSE;
    if (ownerWasVisible_) {
        ownerPlacementValid_ = GetWindowPlacement(owner_, &ownerPlacement_) != FALSE;
        if (!win32::hideWindowForCapture(owner_)) {
            systemError_ = GetLastError();
            restoreOwner();
            return false;
        }
    }
    if (owner_ && ownerWasVisible_) {
        EnableWindow(owner_, FALSE);
    }
    SetLastError(ERROR_SUCCESS);
    if (!captureDesktopSnapshot()) {
        systemError_ = GetLastError();
        if (systemError_ == ERROR_SUCCESS) {
            systemError_ = ERROR_GEN_FAILURE;
        }
        restoreOwner();
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    window_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        overlayClassName,
        L"",
        WS_POPUP,
        left,
        top,
        width,
        height,
        owner_,
        nullptr,
        GetModuleHandleW(nullptr),
        this);
    if (!window_) {
        systemError_ = GetLastError();
        restoreOwner();
        return false;
    }

    GetCursorPos(&currentScreen_);
    SetCursor(nullptr);
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    [[maybe_unused]] const BOOL foregroundSet = SetForegroundWindow(window_);
    SetActiveWindow(window_);
    SetFocus(window_);
    foregroundTransitionArmed_ = foregroundBelongsToCurrentProcess();

    MSG message{};
    while (window_ && IsWindow(window_)) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) {
                PostQuitMessage(static_cast<int>(message.wParam));
                if (exitReason_ == SelectionExitReason::InitializationFailed) {
                    exitReason_ = SelectionExitReason::WindowClosed;
                }
            } else {
                exitReason_ = SelectionExitReason::MessageLoopFailed;
                systemError_ = GetLastError();
            }
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (window_ && IsWindow(window_)) {
        finishing_ = true;
        if (GetCapture() == window_) {
            ReleaseCapture();
        }
        DestroyWindow(window_);
    }

    restoreOwner();
    return exitReason_ == SelectionExitReason::Accepted;
}

void SelectionOverlay::restoreOwner() {
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    if (!owner_) {
        return;
    }
    EnableWindow(owner_, ownerWasEnabled_ ? TRUE : FALSE);
    if (!ownerWasVisible_) {
        return;
    }
    win32::clearWindowCaptureProtection(owner_);
    if (ownerPlacementValid_) {
        SetWindowPlacement(owner_, &ownerPlacement_);
        ShowWindow(owner_, ownerPlacement_.showCmd);
    } else {
        ShowWindow(owner_, SW_SHOW);
    }
    SetForegroundWindow(owner_);
}

void SelectionOverlay::finish(SelectionExitReason reason) {
    if (!selection::canBeginFinish(finishing_)) {
        return;
    }
    finishing_ = true;
    exitReason_ = reason;
    if (window_) {
        if (GetCapture() == window_) {
            ReleaseCapture();
        }
        DestroyWindow(window_);
    }
}

void SelectionOverlay::cancelDrag() {
    if (!dragging_) {
        return;
    }
    invalidateSelectionOutline(startScreen_, currentScreen_);
    invalidateSelectionFillDelta(startScreen_, currentScreen_, startScreen_);
    dragging_ = false;
    if (GetCapture() == window_) {
        ReleaseCapture();
    }
    invalidatePointer(currentScreen_);
}

bool SelectionOverlay::foregroundBelongsToCurrentProcess() const noexcept {
    const HWND foreground = GetForegroundWindow();
    if (!foreground) {
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

LRESULT SelectionOverlay::handleMessage(
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {
    // DestroyWindow synchronously emits activation and capture messages. Once
    // an exit reason is committed, those teardown messages must not overwrite
    // an accepted selection or recursively destroy the same HWND.
    if (finishing_ && message != WM_NCDESTROY) {
        return DefWindowProcW(window_, message, wParam, lParam);
    }
    switch (message) {
    case WM_MOUSEMOVE: {
        SetCursor(nullptr);
        const POINT previous = currentScreen_;
        GetCursorPos(&currentScreen_);
        if (previous.x == currentScreen_.x && previous.y == currentScreen_.y) {
            return 0;
        }

        if (dragging_) {
            invalidateSelectionOutline(startScreen_, previous);
            invalidateSelectionFillDelta(startScreen_, previous, currentScreen_);
            invalidateSelectionOutline(startScreen_, currentScreen_);
        }
        if (mode_ == Mode::HorizontalBoundary) {
            invalidateBoundaryLine(previous);
            invalidateBoundaryLine(currentScreen_);
        }
        if (std::exchange(rejectedPoint_, false)) {
            invalidateAllowedOutline();
        }
        invalidatePointer(previous);
        invalidatePointer(currentScreen_);
        return 0;
    }
    case WM_LBUTTONDOWN:
        foregroundTransitionArmed_ = true;
        SetFocus(window_);
        GetCursorPos(&startScreen_);
        currentScreen_ = startScreen_;
        if (mode_ == Mode::Point || mode_ == Mode::HorizontalBoundary) {
            if (!pointAllowed(startScreen_)) {
                rejectedPoint_ = true;
                MessageBeep(MB_ICONWARNING);
                invalidateAllowedOutline();
                invalidatePointer(currentScreen_);
                return 0;
            }
            point_ = rillshot::core::PointI{startScreen_.x, startScreen_.y};
            finish(SelectionExitReason::Accepted);
        } else {
            dragging_ = true;
            SetCapture(window_);
            if (GetCapture() != window_) {
                dragging_ = false;
                MessageBeep(MB_ICONWARNING);
                return 0;
            }
            invalidatePointer(currentScreen_);
        }
        return 0;
    case WM_LBUTTONUP:
        if (mode_ == Mode::Region && dragging_) {
            const POINT previous = currentScreen_;
            GetCursorPos(&currentScreen_);
            invalidateSelectionOutline(startScreen_, previous);
            const int left = std::min(startScreen_.x, currentScreen_.x);
            const int top = std::min(startScreen_.y, currentScreen_.y);
            const int right = std::max(startScreen_.x, currentScreen_.x);
            const int bottom = std::max(startScreen_.y, currentScreen_.y);
            const long long width =
                static_cast<long long>(right) - static_cast<long long>(left);
            const long long height =
                static_cast<long long>(bottom) - static_cast<long long>(top);
            if (width > 0 && height > 0 &&
                width <= std::numeric_limits<int>::max() &&
                height <= std::numeric_limits<int>::max()) {
                region_ = rillshot::core::RectI{
                    left, top, static_cast<int>(width), static_cast<int>(height)};
                finish(SelectionExitReason::Accepted);
            } else {
                invalidateSelectionFillDelta(
                    startScreen_, currentScreen_, startScreen_);
                dragging_ = false;
                ReleaseCapture();
                invalidatePointer(currentScreen_);
            }
        }
        return 0;
    case WM_RBUTTONDOWN:
        finish(SelectionExitReason::UserCancelled);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            finish(SelectionExitReason::UserCancelled);
            return 0;
        }
        break;
    case WM_SETCURSOR:
        SetCursor(nullptr);
        return TRUE;
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_CAPTURECHANGED:
        if (dragging_ && reinterpret_cast<HWND>(lParam) != window_) {
            cancelDrag();
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            foregroundTransitionArmed_ = true;
        }
        // WM_ACTIVATE also fires when focus moves to another top-level window
        // in this process, including a retiring XAML popup. App-level
        // deactivation is handled below so in-process transitions cannot end
        // a selection before the first drag. DefWindowProc still receives the
        // message so normal keyboard-focus activation behavior is preserved.
        break;
    case WM_ACTIVATEAPP:
        if (wParam != FALSE) {
            foregroundTransitionArmed_ = true;
            return 0;
        }
        if (selection::shouldCancelForAppDeactivation(
                foregroundTransitionArmed_,
                foregroundBelongsToCurrentProcess())) {
            finish(SelectionExitReason::ForegroundChanged);
            return 0;
        }
        return 0;
    case WM_CANCELMODE:
        if (selection::cancelModeAction(dragging_) ==
            selection::CancelModeAction::AbortDrag) {
            cancelDrag();
        }
        return 0;
    case WM_DISPLAYCHANGE:
        finish(SelectionExitReason::DisplayChanged);
        return 0;
    case WM_CLOSE:
        finish(SelectionExitReason::WindowClosed);
        return 0;
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY: {
        const HWND destroyedWindow = window_;
        window_ = nullptr;
        SetWindowLongPtrW(destroyedWindow, GWLP_USERDATA, 0);
        return DefWindowProcW(destroyedWindow, message, wParam, lParam);
    }
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

LRESULT CALLBACK SelectionOverlay::windowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {
    SelectionOverlay* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<SelectionOverlay*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(
            window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<SelectionOverlay*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return self
        ? self->handleMessage(message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}

} // namespace rillshot::gui
