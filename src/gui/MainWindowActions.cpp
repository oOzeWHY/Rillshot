#include "gui/MainWindow.h"

#include "gui/CaptureSummary.h"
#include "gui/GuiStrings.h"
#include "gui/SelectionOverlay.h"
#include "gui/Win32ControlUtils.h"
#include "gui/WindowsShellUtils.h"
#include "platform/WinUtf.h"
#include "session/CaptureSession.h"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace rillshot::gui {
namespace {

bool pointInside(const rillshot::core::PointI& point, const rillshot::core::RectI& region) {
    return point.x >= region.x && point.y >= region.y &&
           static_cast<long long>(point.x) < region.right() &&
           static_cast<long long>(point.y) < region.bottom();
}

} // namespace

void MainWindow::pickRegion() {
    const auto selection = SelectionOverlay::pickRegion(window_);
    if (!selection.value) {
        setStatus(selection.reason == SelectionExitReason::UserCancelled
            ? L"已取消区域选择。"
            : L"区域选择器未完成；请查看 logs\\startup.log 中的 selection 记录。");
        return;
    }
    const auto& region = selection.value;
    win32::setIntegerControl(regionX_, region->x);
    win32::setIntegerControl(regionY_, region->y);
    win32::setIntegerControl(regionWidth_, region->width);
    win32::setIntegerControl(regionHeight_, region->height);

    int scrollX = 0;
    int scrollY = 0;
    if (!win32::parseIntegerControl(scrollX_, scrollX) || !win32::parseIntegerControl(scrollY_, scrollY) ||
        !pointInside(rillshot::core::PointI{scrollX, scrollY}, *region)) {
        win32::setIntegerControl(scrollX_, region->x + region->width / 2);
        win32::setIntegerControl(scrollY_, region->y + std::max(0, region->height - 40));
    }
    setStatus(L"截图区域已选定");
}

void MainWindow::pickScrollPoint() {
    rillshot::core::RectI region;
    if (!win32::parseIntegerControl(regionX_, region.x) ||
        !win32::parseIntegerControl(regionY_, region.y) ||
        !win32::parseIntegerControl(regionWidth_, region.width) ||
        !win32::parseIntegerControl(regionHeight_, region.height) ||
        !region.isValid()) {
        setStatus(L"请先选择一个有效的截图区域，再选择滚动位置。");
        return;
    }

    const auto selection = SelectionOverlay::pickPoint(window_, region);
    if (!selection.value) {
        setStatus(selection.reason == SelectionExitReason::UserCancelled
            ? L"已取消滚动位置选择。"
            : L"滚动位置选择器未完成；请查看 logs\\startup.log 中的 selection 记录。");
        return;
    }
    const auto& point = selection.value;
    win32::setIntegerControl(scrollX_, point->x);
    win32::setIntegerControl(scrollY_, point->y);
    setStatus(L"滚动位置已选定");
}

void MainWindow::browseOutput() {
    const std::wstring current = win32::controlText(outputPath_);
    if (const auto path = win32::chooseOutputPath(window_, current)) {
        std::error_code error;
        confirmedOverwritePath_ = std::filesystem::exists(*path, error) && !error
            ? *path
            : L"";
        SetWindowTextW(outputPath_, path->c_str());
    }
}

void MainWindow::startCapture() {
    std::wstring error;
    const auto config = readConfig(error);
    if (!config) {
        MessageBoxW(window_, error.c_str(), L"截图设置无效", MB_OK | MB_ICONWARNING);
        return;
    }

    auto sessionOptions = config->toSessionOptions();
    const auto outputValidation =
        rillshot::session::validateCaptureOutputCollisionPolicy(sessionOptions);
    if (!outputValidation.ok) {
        const auto message = rillshot::gui::validationMessageZh(outputValidation);
        MessageBoxW(window_, message.c_str(), L"Rillshot",
            MB_OK | (outputValidation.code == "output-exists" ? MB_ICONWARNING : MB_ICONERROR));
        return;
    }

    setRunningUi(true);
    setStatus(L"正在隐藏窗口并稳定首帧……按 Esc 可停止。 ");
    captureWindowPlacementValid_ =
        GetWindowPlacement(window_, &captureWindowPlacement_) != FALSE;
    if (!win32::hideWindowForCapture(window_)) {
        restoreAfterCapture();
        setRunningUi(false);
        MessageBoxW(window_, L"无法隐藏窗口，截图未启动。",
            L"Rillshot", MB_OK | MB_ICONERROR);
        return;
    }

    if (!captureController_.start(window_, std::move(sessionOptions))) {
        restoreAfterCapture();
        setRunningUi(false);
        MessageBoxW(window_, L"已有截图任务正在运行，或后台任务无法启动。",
            L"Rillshot", MB_OK | MB_ICONERROR);
        return;
    }
}

void MainWindow::stopCapture() {
    captureController_.requestStop();
    setStatus(L"正在停止；当前输入/截图操作结束后会安全保存……");
    EnableWindow(stopButton_, FALSE);
}

void MainWindow::captureCompleted(CaptureCompletion completion) {
    setRunningUi(false);

    std::wstring message;
    try {
        message = rillshot::platform::utf8ToWide(completion.result.message);
    } catch (...) {
        message = L"截图已结束，但技术详情的 UTF-8 文本无法显示。";
    }

    setStatus(buildCaptureSummaryZh(completion.result, completion.outputPath, message));

    if (pendingClose_) {
        DestroyWindow(window_);
        return;
    }
    if (completion.result.outputSaved) {
        const auto nextOutputPath = win32::defaultOutputPath();
        if (!nextOutputPath.empty()) {
            confirmedOverwritePath_.clear();
            SetWindowTextW(outputPath_, nextOutputPath.c_str());
        }
    }
    restoreAfterCapture();
}

void MainWindow::restoreAfterCapture() {
    if (!window_ || !IsWindow(window_)) {
        captureWindowPlacementValid_ = false;
        return;
    }
    win32::clearWindowCaptureProtection(window_);
    if (captureWindowPlacementValid_) {
        SetWindowPlacement(window_, &captureWindowPlacement_);
        ShowWindow(window_, captureWindowPlacement_.showCmd);
    } else {
        ShowWindow(window_, SW_SHOW);
    }
    captureWindowPlacementValid_ = false;
    SetForegroundWindow(window_);
}

void MainWindow::setRunningUi(bool running) {
    for (const HWND control : configurationControls_) {
        EnableWindow(control, !running);
    }
    EnableWindow(startButton_, !running);
    EnableWindow(stopButton_, running);
}

void MainWindow::setStatus(const std::wstring& status) {
    SetWindowTextW(status_, status.c_str());
}

} // namespace rillshot::gui
