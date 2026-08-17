#include "pch.h"

#include "MainWindow.xaml.h"

#include "gui/GuiStrings.h"
#include "gui/WindowsShellUtils.h"
#include "platform/WinUtf.h"
#include "session/CaptureSession.h"

#include <filesystem>
#include <utility>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Rillshot::WinUI::implementation {

void MainWindow::BrowseOutput_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    try {
        const std::wstring current = OutputPathBox().Text().c_str();
        if (const auto path = rillshot::gui::win32::chooseOutputPath(windowHandle(), current)) {
            std::error_code error;
            confirmedOverwritePath_ = std::filesystem::exists(*path, error) && !error
                ? *path
                : L"";
            OutputPathBox().Text(*path);
            showInfo(InfoBarSeverity::Success, L"保存位置已更新", *path);
        }
    } catch (const hresult_error& error) {
        showInfo(InfoBarSeverity::Error, L"无法选择文件", error.message().c_str());
    } catch (...) {
        showInfo(InfoBarSeverity::Error, L"无法选择文件", L"请稍后重试或选择其他保存位置。");
    }
}

void MainWindow::OpenOutput_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    if (!lastOutputSaved_ ||
        !rillshot::gui::win32::openOutputFile(windowHandle(), lastOutputPath_)) {
        showInfo(
            InfoBarSeverity::Error,
            L"无法打开结果",
            L"结果文件可能已被移动、删除，或系统没有可用的图像查看器。");
        return;
    }
    showInfo(InfoBarSeverity::Success, L"已打开结果", lastOutputPath_);
}

void MainWindow::RevealOutput_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    if (!lastOutputSaved_ ||
        !rillshot::gui::win32::revealOutputFile(windowHandle(), lastOutputPath_)) {
        showInfo(
            InfoBarSeverity::Error,
            L"无法定位结果",
            L"结果文件可能已被移动、删除，或文件资源管理器暂时不可用。");
        return;
    }
    showInfo(InfoBarSeverity::Success, L"已在文件夹中定位结果", lastOutputPath_);
}

void MainWindow::CopyOutputPath_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    if (!lastOutputSaved_ ||
        !rillshot::gui::win32::copyTextToClipboard(windowHandle(), lastOutputPath_)) {
        showInfo(
            InfoBarSeverity::Error,
            L"无法复制路径",
            L"剪贴板正被其他程序占用，或当前没有可用的结果路径。");
        return;
    }
    showInfo(InfoBarSeverity::Success, L"路径已复制", lastOutputPath_);
}

void MainWindow::StartCapture_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    if (settingsOpen_ || selectionPending_ || captureController_.hasWorker() ||
        workflow_.stage() != rillshot::gui::CaptureUiStage::Ready) {
        return;
    }

    const auto config = readConfig();
    if (!config) {
        showInfo(InfoBarSeverity::Error, L"输入不完整", L"所有坐标和数值选项都必须是有效整数。");
        return;
    }

    const auto configValidation = rillshot::gui::validateGuiConfig(*config);
    if (!configValidation.ok) {
        showInfo(
            InfoBarSeverity::Error,
            L"请检查截图设置",
            rillshot::gui::validationMessageZh(configValidation));
        return;
    }

    auto sessionOptions = config->toSessionOptions();
    const auto outputValidation =
        rillshot::session::validateCaptureOutputCollisionPolicy(sessionOptions);
    if (!outputValidation.ok) {
        outputAttentionPending_ = true;
        navigateToSettings(true);
        showInfo(
            InfoBarSeverity::Warning,
            outputValidation.code == "output-exists" ? L"文件已存在" : L"无法检查输出路径",
            rillshot::gui::validationMessageZh(outputValidation));
        return;
    }

    const auto update = workflow_.updateConfig(*config);
    if (!update.ok) {
        showInfo(InfoBarSeverity::Error, L"当前无法更新配置", rillshot::gui::validationMessageZh(update));
        return;
    }
    const auto start = workflow_.beginCapture();
    if (!start.ok) {
        showInfo(InfoBarSeverity::Error, L"请检查截图设置", rillshot::gui::validationMessageZh(start));
        return;
    }

    renderStage();
    try {
        const std::filesystem::path outputPath(config->outPath);
        const std::wstring fileName = outputPath.filename().wstring();
        CaptureDestinationText().Text(fileName.empty() ? config->outPath : fileName);
    } catch (...) {
        CaptureDestinationText().Text(config->outPath);
    }

    if (!hideForCapture()) {
        rillshot::session::CaptureSessionResult failure;
        failure.stopReason = rillshot::core::StopReason::CaptureFailed;
        failure.message = "main window could not be hidden before capture";
        [[maybe_unused]] const auto completed = workflow_.completeCapture(
            failure, config->outPath, L"主窗口未能隐藏，截图没有启动，也未写入图像。");
        lastOutputSaved_ = false;
        lastOutputPath_.clear();
        restoreAfterCapture();
        renderStage();
        showInfo(
            InfoBarSeverity::Error,
            L"无法隐藏窗口",
            L"截图未启动，避免主窗口进入截图区域或接收滚轮输入。");
        ResetButton().Focus(FocusState::Programmatic);
        return;
    }

    const auto dispatcher = DispatcherQueue();
    const auto weak = get_weak();
    const bool workerStarted = captureController_.start(
        windowHandle(),
        std::move(sessionOptions),
        [dispatcher, weak]() noexcept {
            try {
                const bool queued = dispatcher.TryEnqueue([weak]() {
                    if (const auto self = weak.get()) {
                        self->captureCompletedOnUiThread();
                    }
                });
                if (!queued) {
                    // Shutdown owns the remaining stop/join path. The completion
                    // stays published in CaptureController for a closing callback
                    // or the controller destructor to consume safely.
                    return;
                }
            } catch (...) {
                // The window may already be tearing down. CaptureController owns
                // cancellation/join and keeps worker failures away from XAML.
            }
        });

    if (!workerStarted) {
        rillshot::session::CaptureSessionResult failure;
        failure.stopReason = rillshot::core::StopReason::CaptureFailed;
        failure.message = "capture worker could not be started";
        [[maybe_unused]] const auto completed = workflow_.completeCapture(
            failure, config->outPath, L"后台截图线程未启动，未写入图像。");
        lastOutputSaved_ = false;
        lastOutputPath_.clear();
        restoreAfterCapture();
        renderStage();
        showInfo(InfoBarSeverity::Error, L"无法开始截图", L"已有任务正在运行，或系统无法创建后台线程。");
        ResetButton().Focus(FocusState::Programmatic);
        return;
    }
}

void MainWindow::StopCapture_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    requestCaptureStop();
}

void MainWindow::StopCapture_Invoked(
    [[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
    Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& eventArgs) {
    if (settingsOpen_) {
        navigateToSettings(false);
        eventArgs.Handled(true);
        return;
    }
    if (workflow_.stage() == rillshot::gui::CaptureUiStage::Capturing) {
        requestCaptureStop();
        eventArgs.Handled(true);
    }
}

void MainWindow::requestCaptureStop() {
    if (workflow_.stage() != rillshot::gui::CaptureUiStage::Capturing) {
        return;
    }
    if (!workflow_.stopPending()) {
        [[maybe_unused]] const auto stop = workflow_.requestStop();
    }
    captureController_.requestStop();
    renderStage();
    showInfo(
        InfoBarSeverity::Informational,
        L"正在停止",
        L"正在保存可用结果。");
}

bool MainWindow::hideForCapture() {
    try {
        const HWND handle = windowHandle();
        captureWindowPlacementValid_ =
            GetWindowPlacement(handle, &captureWindowPlacement_) != FALSE;
        AppWindow().Hide();
        return rillshot::gui::win32::hideWindowForCapture(handle);
    } catch (...) {
        return false;
    }
}

void MainWindow::restoreAfterCapture() {
    try {
        rillshot::gui::win32::clearWindowCaptureProtection(windowHandle());
        AppWindow().Show();
    } catch (...) {
    }
    try {
        const HWND handle = windowHandle();
        if (handle && IsWindow(handle)) {
            if (captureWindowPlacementValid_) {
                SetWindowPlacement(handle, &captureWindowPlacement_);
                ShowWindow(handle, captureWindowPlacement_.showCmd);
            } else {
                ShowWindow(handle, SW_SHOW);
            }
            captureWindowPlacementValid_ = false;
            SetForegroundWindow(handle);
        }
    } catch (...) {
        // Output is already finalized. Failure to restore must not turn a
        // completed capture into an unhandled UI exception.
    }
}

void MainWindow::captureCompletedOnUiThread() {
    captureController_.joinCompleted();
    auto completion = captureController_.takeCompletion();
    if (!completion) {
        return;
    }

    std::wstring diagnosticMessage;
    try {
        diagnosticMessage = rillshot::platform::utf8ToWide(completion->result.message);
    } catch (...) {
        diagnosticMessage = L"截图已结束，但技术详情的 UTF-8 文本无法显示。";
    }

    if (workflow_.stage() == rillshot::gui::CaptureUiStage::Capturing) {
        [[maybe_unused]] const auto completed = workflow_.completeCapture(
            completion->result,
            completion->outputPath,
            diagnosticMessage);
    }
    lastOutputSaved_ = completion->result.outputSaved;
    lastOutputPath_ = completion->outputPath;
    renderStage();

    if (closePending_) {
        allowClose_ = true;
        Close();
        return;
    }

    restoreAfterCapture();
    const bool saved = completion->result.outputSaved;
    const bool completedSuccessfully = completion->result.ok && saved;
    showInfo(
        completedSuccessfully ? InfoBarSeverity::Success : InfoBarSeverity::Warning,
        completedSuccessfully ? L"截图已保存"
            : (saved ? L"已保存可用结果" : L"未生成图像"),
        saved ? L"可打开或定位结果文件。"
              : L"请检查详细信息。");
    if (saved) {
        OpenOutputButton().Focus(FocusState::Programmatic);
    } else {
        ResetButton().Focus(FocusState::Programmatic);
    }
}

void MainWindow::appWindowClosing(
    [[maybe_unused]] Microsoft::UI::Windowing::AppWindow const& sender,
    Microsoft::UI::Windowing::AppWindowClosingEventArgs const& eventArgs) {
    if (allowClose_ || !captureController_.hasWorker()) {
        return;
    }

    if (!captureController_.running()) {
        captureController_.joinCompleted();
        [[maybe_unused]] const auto completion = captureController_.takeCompletion();
        allowClose_ = true;
        return;
    }

    eventArgs.Cancel(true);
    closePending_ = true;
    requestCaptureStop();
    showInfo(
        InfoBarSeverity::Informational,
        L"正在关闭",
        L"正在停止截图。");
}

void MainWindow::Reset_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    if (captureController_.hasWorker()) {
        return;
    }
    [[maybe_unused]] const auto reset = workflow_.reset();
    settingsOpen_ = false;
    lastOutputSaved_ = false;
    lastOutputPath_.clear();
    confirmedOverwritePath_.clear();
    const auto nextOutputPath = rillshot::gui::win32::defaultOutputPath();
    OutputPathBox().Text(nextOutputPath);
    renderStage();
    if (nextOutputPath.empty()) {
        showInfo(
            InfoBarSeverity::Error,
            L"无法创建默认保存位置",
            L"请选择一个可写的 PNG 或 BMP 文件路径后再开始截图。");
    } else {
        showInfo(
            InfoBarSeverity::Informational,
            L"新截图",
            L"可重新框选，也可以沿用当前区域。");
    }
    StartCaptureButton().Focus(FocusState::Programmatic);
}

} // namespace winrt::Rillshot::WinUI::implementation
