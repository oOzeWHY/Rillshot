#pragma once

#include <Windows.h>

#include <string>

namespace rillshot::gui::win32 {

[[nodiscard]] int scaled(int value, UINT dpi) noexcept;
[[nodiscard]] std::wstring controlText(HWND control);
[[nodiscard]] bool parseIntegerControl(HWND control, int& value);
void setIntegerControl(HWND control, int value);

} // namespace rillshot::gui::win32
