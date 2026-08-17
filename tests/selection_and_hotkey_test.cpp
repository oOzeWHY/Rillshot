#include "gui/HotkeyBinding.h"
#include "gui/HotkeyInvocation.h"
#include "gui/SelectionLifecycle.h"

#include <cstdlib>
#include <iostream>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace rillshot::gui;
    using namespace rillshot::gui::selection;

    bool ok = true;
    ok &= expect(
        !shouldCancelForAppDeactivation(false, false),
        "a stale startup deactivation must not cancel an unarmed overlay");
    ok &= expect(
        !shouldCancelForAppDeactivation(true, true),
        "a stale deactivation must be ignored while this process owns foreground");
    ok &= expect(
        shouldCancelForAppDeactivation(true, false),
        "an armed overlay must cancel after a real foreground process change");
    ok &= expect(
        cancelModeAction(false) == CancelModeAction::Ignore,
        "WM_CANCELMODE must not close an idle overlay");
    ok &= expect(
        cancelModeAction(true) == CancelModeAction::AbortDrag,
        "WM_CANCELMODE should only abort the active drag");
    ok &= expect(
        canBeginFinish(false),
        "the first exit reason must be allowed to commit");
    ok &= expect(
        !canBeginFinish(true),
        "teardown messages must not overwrite a committed exit reason");

    const HotkeyBinding defaultBinding{};
    ok &= expect(isValidHotkeyBinding(defaultBinding), "the default hotkey must be valid");
    ok &= expect(
        formatHotkeyBinding(defaultBinding) == L"Ctrl + Alt + S",
        "the default hotkey label must be stable");
    ok &= expect(
        !isValidHotkeyBinding(HotkeyBinding{HotkeyShift, 0x53U}),
        "a global hotkey needs Ctrl or Alt");
    ok &= expect(
        !isValidHotkeyBinding(HotkeyBinding{HotkeyControl, 0x7BU}),
        "F12 must remain reserved");
    ok &= expect(
        isValidHotkeyBinding(HotkeyBinding{HotkeyAlt | HotkeyShift, 0x70U}),
        "Alt+Shift+F1 should be supported");
    ok &= expect(hotkeyInvocationAction({CaptureUiStage::Ready, false, false, false}) == HotkeyInvocationAction::BeginRegionSelection, "a ready workflow must start selection regardless of view or focus");
    ok &= expect(hotkeyInvocationAction({CaptureUiStage::Ready, true, false, false}) == HotkeyInvocationAction::Ignore, "a pending selection must suppress hotkey re-entry");
    ok &= expect(hotkeyInvocationAction({CaptureUiStage::Capturing, false, false, false}) == HotkeyInvocationAction::Ignore, "the capture stage must suppress the startup race before the worker becomes visible");
    ok &= expect(hotkeyInvocationAction({CaptureUiStage::Result, false, false, false}) == HotkeyInvocationAction::RestoreResultWindow, "a result hotkey must restore the result window");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
