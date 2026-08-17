#pragma once

#include <Windows.h>

namespace rillshot::input::detail {

class CursorPositionGuard final {
public:
    CursorPositionGuard() noexcept
        : valid_(GetCursorPos(&originalPosition_) != FALSE) {}

    ~CursorPositionGuard() noexcept {
        if (valid_) {
            [[maybe_unused]] const BOOL restored =
                SetCursorPos(originalPosition_.x, originalPosition_.y);
        }
    }

    CursorPositionGuard(const CursorPositionGuard&) = delete;
    CursorPositionGuard& operator=(const CursorPositionGuard&) = delete;

private:
    POINT originalPosition_{};
    bool valid_ = false;
};

} // namespace rillshot::input::detail
