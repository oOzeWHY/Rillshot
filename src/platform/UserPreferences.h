#pragma once

#include "gui/HotkeyBinding.h"

namespace rillshot::platform {

enum class ThemePreference {
    System,
    Light,
    Dark
};

struct UserPreferences {
    ThemePreference theme = ThemePreference::System;
    bool globalHotkeyEnabled = true;
    rillshot::gui::HotkeyBinding globalHotkey{};
};

[[nodiscard]] UserPreferences loadUserPreferences() noexcept;
[[nodiscard]] bool saveUserPreferences(const UserPreferences& preferences) noexcept;

} // namespace rillshot::platform
