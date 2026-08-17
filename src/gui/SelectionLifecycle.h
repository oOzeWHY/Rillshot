#pragma once

namespace rillshot::gui::selection {

enum class CancelModeAction {
    Ignore,
    AbortDrag
};

[[nodiscard]] constexpr bool shouldCancelForAppDeactivation(
    bool foregroundTransitionArmed,
    bool foregroundBelongsToCurrentProcess) noexcept {
    return foregroundTransitionArmed && !foregroundBelongsToCurrentProcess;
}

[[nodiscard]] constexpr CancelModeAction cancelModeAction(bool dragging) noexcept {
    return dragging ? CancelModeAction::AbortDrag : CancelModeAction::Ignore;
}

[[nodiscard]] constexpr bool canBeginFinish(bool finishing) noexcept {
    return !finishing;
}

} // namespace rillshot::gui::selection
