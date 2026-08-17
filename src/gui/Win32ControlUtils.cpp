#include "gui/Win32ControlUtils.h"

#include <algorithm>

namespace rillshot::gui::win32 {

int scaled(int value, UINT dpi) noexcept {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

std::wstring controlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(std::max(0, length)) + 1U, L'\0');
    const int copied = GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
    value.resize(static_cast<size_t>(std::max(0, copied)));
    return value;
}

bool parseIntegerControl(HWND control, int& value) {
    const std::wstring text = controlText(control);
    try {
        size_t used = 0;
        const int parsed = std::stoi(text, &used, 10);
        if (used != text.size()) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

void setIntegerControl(HWND control, int value) {
    SetWindowTextW(control, std::to_wstring(value).c_str());
}

} // namespace rillshot::gui::win32
