#pragma once

#include "gui/CaptureWorkflow.h"

namespace rillshot::gui {

enum class HotkeyInvocationAction {
    Ignore,
    BeginRegionSelection,
    RestoreResultWindow
};

struct HotkeyInvocationState {
    CaptureUiStage stage = CaptureUiStage::Ready;
    bool selectionPending = false;
    bool closePending = false;
    bool captureWorkerActive = false;
};

// View navigation and keyboard focus are intentionally excluded: an enabled
// global shortcut has the same meaning on the capture page, in settings, and
// while the window is in the background.
[[nodiscard]] constexpr HotkeyInvocationAction hotkeyInvocationAction(
    const HotkeyInvocationState& state) noexcept {
    if (state.selectionPending || state.closePending || state.captureWorkerActive) {
        return HotkeyInvocationAction::Ignore;
    }
    switch (state.stage) {
    case CaptureUiStage::Ready:
        return HotkeyInvocationAction::BeginRegionSelection;
    case CaptureUiStage::Capturing:
        return HotkeyInvocationAction::Ignore;
    case CaptureUiStage::Result:
    case CaptureUiStage::Recovery:
        return HotkeyInvocationAction::RestoreResultWindow;
    }
    return HotkeyInvocationAction::Ignore;
}

} // namespace rillshot::gui
