#include "pch.h"

#include "MainWindow.xaml.h"

#include "gui/GlobalHotkey.h"
#include "gui/HotkeyBinding.h"
#include "gui/HotkeyInvocation.h"
#include "platform/StartupDiagnostics.h"
#include "platform/UserPreferences.h"

#include <cstdint>
#include <string_view>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Rillshot::WinUI::implementation {
namespace {

const wchar_t* hotkeyStatusName(
    rillshot::gui::HotkeyRegistrationStatus status) noexcept {
    using rillshot::gui::HotkeyRegistrationStatus;
    switch (status) {
    case HotkeyRegistrationStatus::Disabled: return L"disabled";
    case HotkeyRegistrationStatus::Registered: return L"registered";
    case HotkeyRegistrationStatus::Unchanged: return L"unchanged";
    case HotkeyRegistrationStatus::Invalid: return L"invalid";
    case HotkeyRegistrationStatus::Conflict: return L"conflict";
    case HotkeyRegistrationStatus::SystemError: return L"system-error";
    }
    return L"unknown";
}

void logHotkeyResult(
    std::wstring_view operation,
    const rillshot::gui::HotkeyRegistrationResult& result) {
    std::wstring message(L"Global hotkey ");
    message.append(operation);
    message += L": status=";
    message += hotkeyStatusName(result.status);
    message += L", error=";
    message += std::to_wstring(result.systemError);
    rillshot::platform::writeStartupLog(message);
}

} // namespace

MainWindow::~MainWindow() noexcept {
    stopNavigationPrewarmObservation();
    stopNavigationWindowResizeAnimation();
    stopPageNavigationAnimation();
    flushPreferenceSave();
    globalHotkey_.shutdown();
}

void MainWindow::initializePreferences() {
    initializePreferencePersistence();
    preferences_ = rillshot::platform::loadUserPreferences();
    applyingPreferences_ = true;
    switch (preferences_.theme) {
    case rillshot::platform::ThemePreference::System:
        ThemeBox().SelectedIndex(0);
        break;
    case rillshot::platform::ThemePreference::Light:
        ThemeBox().SelectedIndex(1);
        break;
    case rillshot::platform::ThemePreference::Dark:
        ThemeBox().SelectedIndex(2);
        break;
    }
    GlobalHotkeySwitch().IsOn(preferences_.globalHotkeyEnabled);
    HotkeyBox().Text(rillshot::gui::formatHotkeyBinding(
        preferences_.globalHotkey));
    applyingPreferences_ = false;

    applyThemePreference();
    const auto dispatcher = DispatcherQueue();
    const auto weak = get_weak();
    if (globalHotkey_.initialize([dispatcher, weak]() noexcept {
            try {
                const bool queued = dispatcher.TryEnqueue([weak]() {
                    if (const auto self = weak.get()) {
                        self->onGlobalHotkey();
                    }
                });
                rillshot::platform::writeStartupLog(
                    queued
                        ? L"Global hotkey UI dispatch queued."
                        : L"Global hotkey UI dispatch rejected.");
            } catch (...) {
                rillshot::platform::writeStartupLog(
                    L"Global hotkey UI dispatch raised an exception.");
            }
        })) {
        rillshot::platform::writeStartupLog(
            L"Global hotkey receiver thread initialized.");
        applyGlobalHotkey(false, false);
    } else {
        rillshot::platform::writeStartupLog(
            L"Global hotkey receiver initialization failed.");
        preferences_.globalHotkeyEnabled = false;
        applyingPreferences_ = true;
        GlobalHotkeySwitch().IsOn(false);
        applyingPreferences_ = false;
        HotkeyStatusText().Text(L"快捷键服务初始化失败");
    }
    updatePreferenceSummary();
}

void MainWindow::applyThemePreference() {
    ElementTheme theme = ElementTheme::Default;
    switch (preferences_.theme) {
    case rillshot::platform::ThemePreference::System:
        theme = ElementTheme::Default;
        break;
    case rillshot::platform::ThemePreference::Light:
        theme = ElementTheme::Light;
        break;
    case rillshot::platform::ThemePreference::Dark:
        theme = ElementTheme::Dark;
        break;
    }
    RootGrid().RequestedTheme(theme);
}

void MainWindow::queueThemePreferenceApply() {
    queuePreferenceSave();
    if (themeApplyQueued_) {
        return;
    }

    themeApplyQueued_ = true;
    const auto weak = get_weak();
    const bool queued = DispatcherQueue().TryEnqueue(
        Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
        [weak] {
            if (const auto self = weak.get()) {
                self->themeApplyQueued_ = false;
                if (!self->initialized_ || self->applyingPreferences_) {
                    return;
                }
                // Theme invalidation is intentionally delayed until the
                // ComboBox has presented its selected/closed visual state.
                // Rapid selections coalesce to the latest preference.
                self->applyThemePreference();
                self->updatePreferenceSummary();
            }
        });
    if (!queued) {
        themeApplyQueued_ = false;
        applyThemePreference();
        updatePreferenceSummary();
    }
}

void MainWindow::applyGlobalHotkey(bool persist, bool announceFailure) {
    const auto result = globalHotkey_.apply(
        preferences_.globalHotkeyEnabled,
        preferences_.globalHotkey);
    logHotkeyResult(L"apply", result);

    switch (result.status) {
    case rillshot::gui::HotkeyRegistrationStatus::Registered:
    case rillshot::gui::HotkeyRegistrationStatus::Unchanged:
        HotkeyStatusText().Text(
            L"已启用 · " +
            rillshot::gui::formatHotkeyBinding(preferences_.globalHotkey) +
            L" 可从其他应用直接框选");
        break;
    case rillshot::gui::HotkeyRegistrationStatus::Disabled:
        HotkeyStatusText().Text(L"已关闭 · 仍可从主窗口选择区域");
        break;
    case rillshot::gui::HotkeyRegistrationStatus::Conflict:
    case rillshot::gui::HotkeyRegistrationStatus::Invalid:
    case rillshot::gui::HotkeyRegistrationStatus::SystemError:
        preferences_.globalHotkeyEnabled = false;
        applyingPreferences_ = true;
        GlobalHotkeySwitch().IsOn(false);
        applyingPreferences_ = false;
        HotkeyStatusText().Text(
            result.status == rillshot::gui::HotkeyRegistrationStatus::Conflict
                ? L"存在冲突 · 已安全关闭全局快捷键"
                : L"注册失败 · 已安全关闭全局快捷键");
        if (announceFailure) {
            showInfo(
                InfoBarSeverity::Warning,
                result.status == rillshot::gui::HotkeyRegistrationStatus::Conflict
                    ? L"快捷键已被占用"
                    : L"无法启用快捷键",
                result.status == rillshot::gui::HotkeyRegistrationStatus::Conflict
                    ? L"该组合已由系统或其他应用注册。原有组合未被覆盖，请换一个组合。"
                    : L"Windows 未接受该全局快捷键。请换一个组合后重试。");
        }
        break;
    }

    if (persist) {
        queuePreferenceSave();
    }
    updatePreferenceSummary();
}

void MainWindow::updatePreferenceSummary() {
    const wchar_t* theme = L"跟随系统";
    switch (preferences_.theme) {
    case rillshot::platform::ThemePreference::System: break;
    case rillshot::platform::ThemePreference::Light: theme = L"浅色"; break;
    case rillshot::platform::ThemePreference::Dark: theme = L"深色"; break;
    }

    const std::wstring hotkey = preferences_.globalHotkeyEnabled
        ? rillshot::gui::formatHotkeyBinding(preferences_.globalHotkey)
        : std::wstring(L"快捷键已关闭");
    PreferenceSummaryText().Text(std::wstring(theme) + L" · " + hotkey);
}

void MainWindow::Theme_SelectionChanged(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] SelectionChangedEventArgs const& eventArgs) {
    if (!initialized_ || applyingPreferences_) {
        return;
    }
    switch (ThemeBox().SelectedIndex()) {
    case 1: preferences_.theme = rillshot::platform::ThemePreference::Light; break;
    case 2: preferences_.theme = rillshot::platform::ThemePreference::Dark; break;
    default: preferences_.theme = rillshot::platform::ThemePreference::System; break;
    }
    queueThemePreferenceApply();
}

void MainWindow::GlobalHotkey_Toggled(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    if (!initialized_ || applyingPreferences_) {
        return;
    }

    // Let ToggleSwitch finish the current pointer/keyboard transaction and
    // present its new visual state before RegisterHotKey crosses to the
    // receiver thread. Coalescing means rapid user changes apply only the
    // final IsOn value instead of replaying stale intermediate states.
    if (hotkeyApplyQueued_) {
        return;
    }
    hotkeyApplyQueued_ = true;
    const auto weak = get_weak();
    const bool queued = DispatcherQueue().TryEnqueue(
        Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
        [weak]() {
            if (const auto self = weak.get()) {
                self->hotkeyApplyQueued_ = false;
                if (!self->initialized_ || self->applyingPreferences_) {
                    return;
                }
                self->preferences_.globalHotkeyEnabled =
                    self->GlobalHotkeySwitch().IsOn();
                self->applyGlobalHotkey(true, true);
            }
        });
    if (!queued) {
        hotkeyApplyQueued_ = false;
        preferences_.globalHotkeyEnabled = GlobalHotkeySwitch().IsOn();
        applyGlobalHotkey(true, true);
    }
}

void MainWindow::HotkeyBox_KeyDown(
    [[maybe_unused]] IInspectable const& sender,
    Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& eventArgs) {
    const std::uint32_t virtualKey = static_cast<std::uint32_t>(eventArgs.Key());
    std::uint32_t modifiers = 0;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        modifiers |= rillshot::gui::HotkeyControl;
    }
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
        modifiers |= rillshot::gui::HotkeyAlt;
    }
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        modifiers |= rillshot::gui::HotkeyShift;
    }

    if (virtualKey == VK_TAB || virtualKey == VK_ESCAPE) {
        return;
    }
    if (!rillshot::gui::isSupportedHotkeyKey(virtualKey)) {
        if (modifiers != 0) {
            eventArgs.Handled(true);
            HotkeyStatusText().Text(L"请使用字母、数字或 F1–F11");
        }
        return;
    }
    eventArgs.Handled(true);

    const rillshot::gui::HotkeyBinding candidate{modifiers, virtualKey};
    if (!rillshot::gui::isValidHotkeyBinding(candidate)) {
        HotkeyStatusText().Text(L"请按 Ctrl 或 Alt，加字母、数字或 F1–F11");
        return;
    }

    const auto previous = preferences_.globalHotkey;
    const bool previousEnabled = preferences_.globalHotkeyEnabled;
    preferences_.globalHotkey = candidate;
    preferences_.globalHotkeyEnabled = true;
    const auto result = globalHotkey_.apply(true, candidate);
    logHotkeyResult(L"edit", result);
    if (!result.usable()) {
        preferences_.globalHotkey = previous;
        preferences_.globalHotkeyEnabled = previousEnabled;
        [[maybe_unused]] const auto restored = globalHotkey_.apply(
            previousEnabled, previous);
        applyingPreferences_ = true;
        GlobalHotkeySwitch().IsOn(previousEnabled);
        HotkeyBox().Text(rillshot::gui::formatHotkeyBinding(previous));
        applyingPreferences_ = false;
        HotkeyStatusText().Text(
            result.status == rillshot::gui::HotkeyRegistrationStatus::Conflict
                ? L"该组合已被系统或其他应用占用"
                : L"Windows 未接受该组合");
        showInfo(
            InfoBarSeverity::Warning,
            L"快捷键未更改",
            result.status == rillshot::gui::HotkeyRegistrationStatus::Conflict
                ? L"检测到全局冲突，已恢复原有快捷键。"
                : L"注册失败，已恢复原有快捷键。");
        updatePreferenceSummary();
        return;
    }

    applyingPreferences_ = true;
    GlobalHotkeySwitch().IsOn(true);
    HotkeyBox().Text(rillshot::gui::formatHotkeyBinding(candidate));
    applyingPreferences_ = false;
    HotkeyStatusText().Text(
        L"已启用 · " + rillshot::gui::formatHotkeyBinding(candidate) +
        L" 可从其他应用直接框选");
    queuePreferenceSave();
    updatePreferenceSummary();
}

void MainWindow::ResetHotkey_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    const auto previous = preferences_.globalHotkey;
    const bool previousEnabled = preferences_.globalHotkeyEnabled;
    const rillshot::gui::HotkeyBinding candidate{};
    const auto result = globalHotkey_.apply(true, candidate);
    logHotkeyResult(L"reset", result);
    if (!result.usable()) {
        preferences_.globalHotkey = previous;
        preferences_.globalHotkeyEnabled = previousEnabled;
        [[maybe_unused]] const auto restored = globalHotkey_.apply(
            previousEnabled, previous);
        applyingPreferences_ = true;
        GlobalHotkeySwitch().IsOn(previousEnabled);
        HotkeyBox().Text(rillshot::gui::formatHotkeyBinding(previous));
        applyingPreferences_ = false;
        HotkeyStatusText().Text(
            result.status == rillshot::gui::HotkeyRegistrationStatus::Conflict
                ? L"默认组合已被系统或其他应用占用"
                : L"Windows 未接受默认组合");
        showInfo(
            InfoBarSeverity::Warning,
            L"无法恢复默认快捷键",
            L"原有快捷键已保留。");
        updatePreferenceSummary();
        return;
    }

    preferences_.globalHotkey = candidate;
    preferences_.globalHotkeyEnabled = true;
    applyingPreferences_ = true;
    GlobalHotkeySwitch().IsOn(true);
    HotkeyBox().Text(rillshot::gui::formatHotkeyBinding(preferences_.globalHotkey));
    applyingPreferences_ = false;
    HotkeyStatusText().Text(L"已恢复默认 · Ctrl + Alt + S");
    queuePreferenceSave();
    updatePreferenceSummary();
}

void MainWindow::OpenSettings_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    if (workflow_.stage() != rillshot::gui::CaptureUiStage::Ready ||
        selectionPending_ || captureController_.hasWorker()) {
        return;
    }
    navigateToSettings(true);
}

void MainWindow::CloseSettings_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    navigateToSettings(false);
}

void MainWindow::onGlobalHotkey() {
    rillshot::platform::writeStartupLog(L"Global hotkey received.");
    const auto action = rillshot::gui::hotkeyInvocationAction({workflow_.stage(), selectionPending_, closePending_, captureController_.hasWorker()});
    if (action == rillshot::gui::HotkeyInvocationAction::Ignore) {
        rillshot::platform::writeStartupLog(
            L"Global hotkey ignored while selection, capture, or close is active.");
        return;
    }
    if (action == rillshot::gui::HotkeyInvocationAction::BeginRegionSelection) {
        rillshot::platform::writeStartupLog(
            L"Global hotkey starting region selection from the current view.");
        settlePageNavigationForSelection();
        queueSelection(SelectionKind::Region);
        return;
    }
    rillshot::platform::writeStartupLog(
        L"Global hotkey restoring the result window.");
    try {
        AppWindow().Show();
    } catch (...) {
    }
    const HWND window = windowHandle();
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    ResetButton().Focus(FocusState::Programmatic);
}

} // namespace winrt::Rillshot::WinUI::implementation
