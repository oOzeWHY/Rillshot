#include "input/KeyboardDriver.h"
#include "input/CursorPositionGuard.h"

#include <Windows.h>

#include <array>
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

    if (SetForegroundWindow(target)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
}

struct KeyboardGesture {
    WORD virtualKey = 0;
    bool shift = false;
};

KeyboardGesture gestureFor(KeyboardKey key, bool down) noexcept {
    switch (key) {
    case KeyboardKey::Page:
        return KeyboardGesture{static_cast<WORD>(down ? VK_NEXT : VK_PRIOR), false};
    case KeyboardKey::Space: return KeyboardGesture{VK_SPACE, !down};
    case KeyboardKey::Arrow:
        return KeyboardGesture{static_cast<WORD>(down ? VK_DOWN : VK_UP), false};
    }
    return KeyboardGesture{VK_NEXT, false};
}

INPUT keyInput(WORD virtualKey, bool released) noexcept {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = released ? KEYEVENTF_KEYUP : 0;
    return input;
}

void releaseKeyBestEffort(WORD virtualKey) noexcept {
    INPUT input = keyInput(virtualKey, true);
    SendInput(1, &input, sizeof(INPUT));
}

} // namespace

rillshot::core::Status KeyboardDriver::advance(const ScrollRequest& request) {
    if (request.keyRepeats <= 0) {
        return rillshot::core::Status::failure("keyboard-invalid-repeats", "keyboard repeat count must be positive");
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
    std::uniform_int_distribution<int> delayMs(25, 60);
    const KeyboardGesture gesture = gestureFor(request.keyboardKey, request.down);

    for (int i = 0; i < request.keyRepeats; ++i) {
        std::array<INPUT, 4> inputs{};
        UINT inputCount = 0;
        if (gesture.shift) {
            inputs[inputCount++] = keyInput(VK_SHIFT, false);
        }
        inputs[inputCount++] = keyInput(gesture.virtualKey, false);
        inputs[inputCount++] = keyInput(gesture.virtualKey, true);
        if (gesture.shift) {
            inputs[inputCount++] = keyInput(VK_SHIFT, true);
        }

        const UINT sent = SendInput(inputCount, inputs.data(), sizeof(INPUT));
        if (sent != inputCount) {
            // A partial SendInput must not leave the target with a logically
            // held key or Shift modifier.
            releaseKeyBestEffort(gesture.virtualKey);
            if (gesture.shift) {
                releaseKeyBestEffort(VK_SHIFT);
            }
            return rillshot::core::Status::failure(
                "keyboard-sendinput-failed",
                "SendInput did not insert the keyboard event; UIPI, focus, or foreground-window state may have blocked input");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs(rng)));
    }

    return rillshot::core::Status::success();
}

} // namespace rillshot::input
