#include "gui/CaptureSummary.h"

#include "gui/GuiStrings.h"

namespace rillshot::gui {

std::wstring buildCaptureSummaryZh(
    const rillshot::session::CaptureSessionResult& result,
    const std::wstring& outputPath,
    const std::wstring& diagnosticMessage) {

    const bool completedWithOutput = result.ok && result.outputSaved;
    std::wstring summary = completedWithOutput
        ? L"截图已完成。"
        : L"截图已安全结束，需要检查结果。";
    summary += L"\r\n停止原因：";
    summary += stopReasonZh(result.stopReason);
    summary += L"\r\n已捕获 " + std::to_wstring(result.framesCaptured);
    summary += L" 帧；已接受 " + std::to_wstring(result.seams) + L" 条接缝。";

    const std::wstring localizedMessage = sessionMessageZh(result.message);
    if (!localizedMessage.empty()) {
        summary += L"\r\n状态说明：" + localizedMessage;
    } else if (!diagnosticMessage.empty()) {
        summary += L"\r\n诊断详情：" + diagnosticMessage;
    }

    if (result.outputSaved) {
        summary += L"\r\n已保存最后可靠结果：" + outputPath;
    } else {
        summary += L"\r\n未保存输出图像。";
    }

    if (result.comparisonFrameSaved) {
        summary += L"\r\n已保存未通过拼接的对照帧：" + result.comparisonFramePath;
    }

    if (result.diagnosticsSaved) {
        summary += L"\r\n诊断记录：" + outputPath + L".jsonl";
    } else {
        summary += L"\r\n诊断记录不可用。";
    }

    const wchar_t* guidance = stopGuidanceZh(result.stopReason);
    if (guidance[0] != L'\0') {
        summary += L"\r\n";
        summary += guidance;
    }
    return summary;
}

} // namespace rillshot::gui
