#pragma once

#include <Windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace rillshot::gui::win32 {

// Best-effort defense in depth for the application's own top-level window.
// Callers must still hide and settle the window before any desktop snapshot.
[[nodiscard]] bool protectWindowFromCapture(HWND window) noexcept;
void clearWindowCaptureProtection(HWND window) noexcept;

// Applies capture exclusion for the transition, hides a top-level window and
// waits until two compositor boundaries have passed. Callers clear the
// temporary exclusion immediately before restoring the window. Failure is
// closed: callers must not start selection or capture.
[[nodiscard]] bool hideWindowForCapture(HWND window) noexcept;

// Returns a collision-resistant PNG path below the running executable's
// captures directory. An unwritable executable directory fails closed; the
// application never falls back to Pictures, LocalAppData, or a temp directory.
[[nodiscard]] std::wstring defaultOutputPath();

// Shows the Windows Common Item save dialog without creating an empty placeholder
// file. The caller owns validation of the returned .png/.bmp path.
[[nodiscard]] std::optional<std::wstring> chooseOutputPath(
    HWND owner,
    const std::wstring& currentPath);

// Result actions shared by desktop shells. All functions fail closed and never
// manufacture success when the target path or clipboard operation is unavailable.
[[nodiscard]] bool openOutputFile(HWND owner, const std::wstring& path) noexcept;
[[nodiscard]] bool revealOutputFile(HWND owner, const std::wstring& path) noexcept;
[[nodiscard]] bool copyTextToClipboard(HWND owner, std::wstring_view text) noexcept;

} // namespace rillshot::gui::win32
