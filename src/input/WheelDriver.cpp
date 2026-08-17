#include "input/WheelDriver.h"
#include "input/CursorPositionGuard.h"

#include <Windows.h>

#include <chrono>
#include <random>
#include <thread>

namespace rillshot::input {
namespace {

void activateWindowAtPointBestEffort(const POINT& point) {
    HWND target = WindowFromPoint(point);
    if (!target) {
        return;
    }
    if (HWND root = GetAncestor(target, GA_ROOT); root) {
        target = root;
    }
    if (!IsWindowVisible(target) || !IsWindowEnabled(target) || GetForegroundWindow() == target) {
        return;
    }

    // Wheel routing differs with the user's "scroll inactive windows" setting.
    // Foreground activation avoids making coordinate-directed input depend on
    // that setting. Failure is non-fatal because hover routing may still work.
    if (SetForegroundWindow(target)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
}

} // namespace

rillshot::core::Status WheelDriver::advance(const ScrollRequest& request) {
    if (request.notches <= 0) {
        return rillshot::core::Status::failure("wheel-invalid-notches", "wheel notch count must be positive");
    }

    detail::CursorPositionGuard cursorPositionGuard;
    if (!SetCursorPos(request.point.x, request.point.y)) {
        return rillshot::core::Status::failure(
            "input-set-cursor-failed",
            "SetCursorPos failed; check that --scroll-point is on an attached display");
    }

    activateWindowAtPointBestEffort(POINT{request.point.x, request.point.y});

    std::mt19937 rng{static_cast<std::mt19937::result_type>(
        GetTickCount64() ^ static_cast<ULONGLONG>(GetCurrentThreadId()))};
    std::uniform_int_distribution<int> delayMs(15, 40);

    for (int i = 0; i < request.notches; ++i) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(request.down ? -WHEEL_DELTA : WHEEL_DELTA);

        const UINT sent = SendInput(1, &input, sizeof(INPUT));
        if (sent != 1) {
            return rillshot::core::Status::failure(
                "wheel-sendinput-failed",
                "SendInput did not insert the wheel event; UIPI or focus may have blocked input");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs(rng)));
    }

    return rillshot::core::Status::success();
}

} // namespace rillshot::input
