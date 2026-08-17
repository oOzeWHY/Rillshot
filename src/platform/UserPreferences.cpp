#include "platform/UserPreferences.h"

#include "platform/AppPaths.h"

#include <Windows.h>

#include <filesystem>
#include <string>

namespace rillshot::platform {
namespace {

std::filesystem::path preferencesPath() noexcept {
    const auto directory = applicationDirectory(L"settings");
    return directory.empty() ? std::filesystem::path{} : directory / L"Rillshot.ini";
}

int themeValue(ThemePreference theme) noexcept {
    switch (theme) {
    case ThemePreference::System: return 0;
    case ThemePreference::Light: return 1;
    case ThemePreference::Dark: return 2;
    }
    return 0;
}

ThemePreference parseTheme(int value) noexcept {
    switch (value) {
    case 1: return ThemePreference::Light;
    case 2: return ThemePreference::Dark;
    default: return ThemePreference::System;
    }
}

bool writeInteger(
    const std::filesystem::path& path,
    const wchar_t* section,
    const wchar_t* key,
    std::uint32_t value) noexcept {
    const std::wstring text = std::to_wstring(value);
    return WritePrivateProfileStringW(
               section, key, text.c_str(), path.c_str()) != FALSE;
}

} // namespace

UserPreferences loadUserPreferences() noexcept {
    UserPreferences preferences;
    try {
        const auto path = preferencesPath();
        if (path.empty()) {
            return preferences;
        }
        preferences.theme = parseTheme(GetPrivateProfileIntW(
            L"appearance", L"theme", 0, path.c_str()));
        preferences.globalHotkeyEnabled = GetPrivateProfileIntW(
            L"shortcut", L"enabled", 1, path.c_str()) != 0;
        preferences.globalHotkey.modifiers = static_cast<std::uint32_t>(
            GetPrivateProfileIntW(
                L"shortcut",
                L"modifiers",
                static_cast<int>(preferences.globalHotkey.modifiers),
                path.c_str()));
        preferences.globalHotkey.virtualKey = static_cast<std::uint32_t>(
            GetPrivateProfileIntW(
                L"shortcut",
                L"virtualKey",
                static_cast<int>(preferences.globalHotkey.virtualKey),
                path.c_str()));
        if (!rillshot::gui::isValidHotkeyBinding(preferences.globalHotkey)) {
            preferences.globalHotkey = {};
        }
    } catch (...) {
        return UserPreferences{};
    }
    return preferences;
}

bool saveUserPreferences(const UserPreferences& preferences) noexcept {
    try {
        if (!rillshot::gui::isValidHotkeyBinding(preferences.globalHotkey)) {
            return false;
        }
        const auto path = preferencesPath();
        if (path.empty()) {
            return false;
        }
        return writeInteger(
                   path,
                   L"appearance",
                   L"theme",
                   static_cast<std::uint32_t>(themeValue(preferences.theme))) &&
               writeInteger(
                   path,
                   L"shortcut",
                   L"enabled",
                   preferences.globalHotkeyEnabled ? 1U : 0U) &&
               writeInteger(
                   path,
                   L"shortcut",
                   L"modifiers",
                   preferences.globalHotkey.modifiers) &&
               writeInteger(
                   path,
                   L"shortcut",
                   L"virtualKey",
                   preferences.globalHotkey.virtualKey);
    } catch (...) {
        return false;
    }
}

} // namespace rillshot::platform
