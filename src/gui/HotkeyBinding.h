#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace rillshot::gui {

enum HotkeyModifier : std::uint32_t {
    HotkeyAlt = 0x0001U,
    HotkeyControl = 0x0002U,
    HotkeyShift = 0x0004U,
};

struct HotkeyBinding {
    std::uint32_t modifiers = HotkeyControl | HotkeyAlt;
    std::uint32_t virtualKey = 0x53U; // S

    [[nodiscard]] friend constexpr bool operator==(
        const HotkeyBinding& left,
        const HotkeyBinding& right) noexcept = default;
};

[[nodiscard]] constexpr bool isSupportedHotkeyKey(
    std::uint32_t virtualKey) noexcept {
    const bool letter = virtualKey >= 0x41U && virtualKey <= 0x5AU;
    const bool digit = virtualKey >= 0x30U && virtualKey <= 0x39U;
    const bool functionKey = virtualKey >= 0x70U && virtualKey <= 0x7AU; // F1-F11
    return letter || digit || functionKey;
}

[[nodiscard]] constexpr bool isValidHotkeyBinding(
    const HotkeyBinding& binding) noexcept {
    constexpr std::uint32_t supportedModifiers =
        HotkeyAlt | HotkeyControl | HotkeyShift;
    const bool hasPrimaryModifier =
        (binding.modifiers & (HotkeyAlt | HotkeyControl)) != 0;
    return (binding.modifiers & ~supportedModifiers) == 0 &&
           hasPrimaryModifier &&
           isSupportedHotkeyKey(binding.virtualKey);
}

[[nodiscard]] inline std::wstring hotkeyKeyName(std::uint32_t virtualKey) {
    if (virtualKey >= 0x41U && virtualKey <= 0x5AU) {
        return std::wstring(1, static_cast<wchar_t>(virtualKey));
    }
    if (virtualKey >= 0x30U && virtualKey <= 0x39U) {
        return std::wstring(1, static_cast<wchar_t>(virtualKey));
    }
    if (virtualKey >= 0x70U && virtualKey <= 0x7AU) {
        return L"F" + std::to_wstring(virtualKey - 0x6FU);
    }
    return L"?";
}

[[nodiscard]] inline std::wstring formatHotkeyBinding(
    const HotkeyBinding& binding) {
    std::wstring text;
    const auto append = [&text](std::wstring_view part) {
        if (!text.empty()) {
            text += L" + ";
        }
        text += part;
    };
    if ((binding.modifiers & HotkeyControl) != 0) {
        append(L"Ctrl");
    }
    if ((binding.modifiers & HotkeyAlt) != 0) {
        append(L"Alt");
    }
    if ((binding.modifiers & HotkeyShift) != 0) {
        append(L"Shift");
    }
    append(hotkeyKeyName(binding.virtualKey));
    return text;
}

} // namespace rillshot::gui
