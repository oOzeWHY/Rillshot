#include "gui/CaptureWorkflow.h"

#include "gui/CaptureSummary.h"

#include <utility>

namespace rillshot::gui {

CaptureWorkflowModel::CaptureWorkflowModel(GuiConfig config)
    : config_(std::move(config)) {}

rillshot::core::Status CaptureWorkflowModel::updateConfig(GuiConfig config) {
    if (stage_ == CaptureUiStage::Capturing) {
        return rillshot::core::Status::failure(
            "capture-in-progress",
            "capture configuration cannot change while capture is running");
    }
    config_ = std::move(config);
    return rillshot::core::Status::success();
}

rillshot::core::Status CaptureWorkflowModel::beginCapture() {
    if (stage_ == CaptureUiStage::Capturing) {
        return rillshot::core::Status::failure(
            "capture-in-progress",
            "capture is already running");
    }

    const auto validation = validateGuiConfig(config_);
    if (!validation.ok) {
        return validation;
    }

    stage_ = CaptureUiStage::Capturing;
    stopPending_ = false;
    summary_.clear();
    return rillshot::core::Status::success();
}

rillshot::core::Status CaptureWorkflowModel::requestStop() {
    if (stage_ != CaptureUiStage::Capturing) {
        return rillshot::core::Status::failure(
            "capture-not-running",
            "capture can only be stopped while it is running");
    }
    stopPending_ = true;
    return rillshot::core::Status::success();
}

rillshot::core::Status CaptureWorkflowModel::completeCapture(
    const rillshot::session::CaptureSessionResult& result,
    const std::wstring& outputPath,
    const std::wstring& diagnosticMessage) {
    if (stage_ != CaptureUiStage::Capturing) {
        return rillshot::core::Status::failure(
            "capture-not-running",
            "capture completion requires an active capture");
    }

    summary_ = buildCaptureSummaryZh(result, outputPath, diagnosticMessage);
    stage_ = result.ok && result.outputSaved
        ? CaptureUiStage::Result
        : CaptureUiStage::Recovery;
    stopPending_ = false;
    return rillshot::core::Status::success();
}

rillshot::core::Status CaptureWorkflowModel::reset() {
    if (stage_ == CaptureUiStage::Capturing) {
        return rillshot::core::Status::failure(
            "capture-in-progress",
            "capture must finish or stop before reset");
    }
    stage_ = CaptureUiStage::Ready;
    stopPending_ = false;
    summary_.clear();
    return rillshot::core::Status::success();
}

const wchar_t* captureUiStageTitleZh(CaptureUiStage stage) noexcept {
    switch (stage) {
    case CaptureUiStage::Ready: return L"准备截图";
    case CaptureUiStage::Capturing: return L"正在滚动并拼接";
    case CaptureUiStage::Result: return L"截图已完成";
    case CaptureUiStage::Recovery: return L"已安全停止";
    }
    return L"未知状态";
}

} // namespace rillshot::gui
