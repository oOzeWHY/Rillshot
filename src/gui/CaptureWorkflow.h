#pragma once

#include "gui/GuiConfig.h"

#include <string>

namespace rillshot::gui {

enum class CaptureUiStage {
    Ready,
    Capturing,
    Result,
    Recovery
};

// Framework-independent release UI state. The Win32 baseline and the WinUI 3
// shell can share transition rules without sharing controls or window handles.
class CaptureWorkflowModel final {
public:
    CaptureWorkflowModel() = default;
    explicit CaptureWorkflowModel(GuiConfig config);

    [[nodiscard]] const GuiConfig& config() const noexcept { return config_; }
    [[nodiscard]] CaptureUiStage stage() const noexcept { return stage_; }
    [[nodiscard]] bool stopPending() const noexcept { return stopPending_; }
    [[nodiscard]] const std::wstring& summary() const noexcept { return summary_; }

    [[nodiscard]] rillshot::core::Status updateConfig(GuiConfig config);
    [[nodiscard]] rillshot::core::Status beginCapture();
    [[nodiscard]] rillshot::core::Status requestStop();
    [[nodiscard]] rillshot::core::Status completeCapture(
        const rillshot::session::CaptureSessionResult& result,
        const std::wstring& outputPath,
        const std::wstring& diagnosticMessage = {});
    [[nodiscard]] rillshot::core::Status reset();

private:
    GuiConfig config_;
    CaptureUiStage stage_ = CaptureUiStage::Ready;
    bool stopPending_ = false;
    std::wstring summary_;
};

[[nodiscard]] const wchar_t* captureUiStageTitleZh(CaptureUiStage stage) noexcept;

} // namespace rillshot::gui
