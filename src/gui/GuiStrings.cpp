#include "gui/GuiStrings.h"

namespace rillshot::gui {

std::wstring validationMessageZh(const rillshot::core::Status& status) {
    if (status.code == "invalid-region") return L"截图区域无效：宽和高必须为正数，坐标不能溢出。";
    if (status.code == "invalid-scroll-point") return L"滚轮作用点必须位于截图区域内。请重新选择坐标。";
    if (status.code == "invalid-output-path") {
        return L"输出路径必须以 .png 或 .bmp 结尾。若默认路径为空，请确认应用数据目录可写，或手动选择保存位置。";
    }
    if (status.code == "invalid-backend") return L"截图后端无效。";
    if (status.code == "invalid-max-frames") return L"最多帧数必须在 1 到 10000 之间。";
    if (status.code == "invalid-wheel-notches") return L"每次滚轮格数必须在 1 到 120 之间；首次建议使用 1。";
    if (status.code == "invalid-ignore-regions") return L"固定页头或页脚过高，剩余可滚动内容不足。";
    if (status.code == "invalid-output-budget") return L"截图区域超过本版本的安全图像内存上限。请缩小区域。";
    if (status.code == "output-exists") {
        return L"输出文件或伴随文件已存在。请通过“浏览”确认覆盖，或更改文件名。";
    }
    if (status.code == "output-status-failed") {
        return L"无法检查输出文件状态。请重新选择保存位置。";
    }
    return L"截图选项无效，请检查所有输入。";
}

std::wstring sessionMessageZh(const std::string& message) {
    if (message == "max frames reached") return L"已达到设置的最多帧数。";
    if (message == "user stopped capture") return L"已按用户要求停止截图。";
    if (message == "capture cancelled before the first frame") return L"已在首帧完成前取消，没有生成截图。";
    if (message == "initial frame remained unstable; reliable stitching was not started") {
        return L"首帧在等待上限内仍未稳定；已保留可恢复截图，但未开始自动拼接。";
    }
    if (message == "frame remained unstable after scrolling; partial result preserved") {
        return L"滚动后画面在等待上限内仍未稳定；已保留上一个可靠结果和对照帧。";
    }
    if (message == "no visual progress detected") return L"连续画面没有可追加的新内容。";
    if (message == "stitch confidence below hard floor; partial result preserved") {
        return L"当前帧与上一可靠帧的匹配低于安全下限；已保留可靠结果和对照帧。";
    }
    if (message == "low-confidence seam rejected; partial result preserved") {
        return L"当前接缝存在低信息或歧义；已保留可靠结果和对照帧。";
    }
    if (message == "stitched image memory limit reached; reliable partial result preserved") {
        return L"长图已达到安全内存上限；已保留最后可靠结果和对照帧。";
    }
    if (message == "could not initialize COM on the capture worker") {
        return L"后台截图线程无法初始化 COM。";
    }
    return {};
}

const wchar_t* stopReasonZh(rillshot::core::StopReason reason) noexcept {
    using rillshot::core::StopReason;
    switch (reason) {
    case StopReason::MaxFramesReached: return L"已达到最多帧数";
    case StopReason::UserStopped: return L"用户已停止";
    case StopReason::NoVisualProgress: return L"页面没有继续滚动";
    case StopReason::ScrollRejected: return L"滚动输入未完成";
    case StopReason::CaptureFailed: return L"截图失败";
    case StopReason::StitchUnreliable: return L"拼接不可靠（已安全停止，并非程序崩溃）";
    case StopReason::UnstableTooLong: return L"画面长时间不稳定";
    case StopReason::OutputLimitReached: return L"已达到安全图像内存上限";
    }
    return L"未知";
}

const wchar_t* stopGuidanceZh(rillshot::core::StopReason reason) noexcept {
    using rillshot::core::StopReason;
    switch (reason) {
    case StopReason::StitchUnreliable:
        return L"建议：把“每次滚轮格数”设为 1，确认滚动位置位于正文，并对照诊断帧与 JSONL。";
    case StopReason::ScrollRejected:
        return L"建议：确认目标窗口权限和滚动位置正确；人工模式还应确认输入终端仍然可用。";
    case StopReason::CaptureFailed:
        return L"建议：先将最多帧数设为 1，并切换“自动 / DXGI / GDI”后重试。";
    case StopReason::NoVisualProgress:
        return L"若尚未到底，请重新选择正文内的滚动位置，或检查目标窗口是否获得焦点。";
    case StopReason::OutputLimitReached:
        return L"建议：缩小截图区域、减少最多帧数，或用 CLI 明确调整 --max-output-mib。";
    default:
        return L"";
    }
}

} // namespace rillshot::gui
