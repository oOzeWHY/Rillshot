#include "pch.h"

#include "MainWindow.xaml.h"

#include "gui/SelectionLifecycle.h"
#include "gui/SelectionOverlay.h"
#include "gui/WindowsShellUtils.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

using namespace winrt;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Rillshot::WinUI::implementation {
namespace {

struct SelectionExitPresentation {
    InfoBarSeverity severity = InfoBarSeverity::Error;
    std::wstring title;
    std::wstring message;
};

SelectionExitPresentation selectionExitPresentation(
    rillshot::gui::SelectionExitReason reason,
    DWORD systemError,
    std::wstring_view unchangedItem) {
    using rillshot::gui::SelectionExitReason;
    switch (reason) {
    case SelectionExitReason::UserCancelled:
        return {
            InfoBarSeverity::Informational,
            L"已取消",
            std::wstring(unchangedItem) + L"未更改。"};
    case SelectionExitReason::ForegroundChanged:
        return {
            InfoBarSeverity::Warning,
            L"选择已停止",
            L"检测到已切换到其他应用。请重新选择；如果没有主动切换，请附上应用数据目录中的启动日志。"};
    case SelectionExitReason::DisplayChanged:
        return {
            InfoBarSeverity::Warning,
            L"显示配置已变化",
            L"为避免坐标错误，本次选择已停止。请在显示器状态稳定后重试。"};
    case SelectionExitReason::MessageLoopFailed:
    case SelectionExitReason::InitializationFailed:
    case SelectionExitReason::WindowClosed:
        break;
    case SelectionExitReason::Accepted:
        return {
            InfoBarSeverity::Error,
            L"选择结果无效",
            L"选择器报告成功但没有返回坐标。"};
    }

    std::wstring message =
        L"屏幕选择器没有启动完成。诊断代码：" +
        std::wstring(rillshot::gui::selectionExitReasonCode(reason));
    if (systemError != ERROR_SUCCESS) {
        message += L" / Win32 " + std::to_wstring(systemError);
    }
    message += L"。详细记录位于应用数据目录中的启动日志。";
    return {InfoBarSeverity::Error, L"选择器无法启动", std::move(message)};
}

class AppWindowVisibilityRestorer final {
public:
    AppWindowVisibilityRestorer(
        AppWindow const& appWindow,
        HWND window,
        bool placementValid,
        WINDOWPLACEMENT placement)
        : appWindow_(appWindow),
          window_(window),
          placementValid_(placementValid),
          placement_(placement) {}

    ~AppWindowVisibilityRestorer() noexcept {
        try {
            rillshot::gui::win32::clearWindowCaptureProtection(window_);
            appWindow_.Show();
        } catch (...) {
        }
        if (window_ && IsWindow(window_)) {
            if (placementValid_) {
                SetWindowPlacement(window_, &placement_);
                ShowWindow(window_, placement_.showCmd);
            } else {
                ShowWindow(window_, SW_SHOW);
            }
            SetForegroundWindow(window_);
        }
    }

    AppWindowVisibilityRestorer(const AppWindowVisibilityRestorer&) = delete;
    AppWindowVisibilityRestorer& operator=(const AppWindowVisibilityRestorer&) = delete;

private:
    AppWindow appWindow_;
    HWND window_ = nullptr;
    bool placementValid_ = false;
    WINDOWPLACEMENT placement_{sizeof(WINDOWPLACEMENT)};
};

class SelectionInteractionRestorer final {
public:
    SelectionInteractionRestorer(UIElement const& element, bool& pending)
        : element_(element), pending_(pending) {}

    ~SelectionInteractionRestorer() noexcept {
        pending_ = false;
        try {
            element_.IsHitTestVisible(true);
        } catch (...) {
        }
    }

    SelectionInteractionRestorer(const SelectionInteractionRestorer&) = delete;
    SelectionInteractionRestorer& operator=(const SelectionInteractionRestorer&) = delete;

private:
    UIElement element_;
    bool& pending_;
};

} // namespace

void MainWindow::PickRegion_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    try {
        queueSelection(SelectionKind::Region);
    } catch (const hresult_error& error) {
        showInfo(InfoBarSeverity::Error, L"无法选择区域", error.message().c_str());
    } catch (...) {
        showInfo(InfoBarSeverity::Error, L"无法选择区域", L"窗口初始化失败。");
    }
}

void MainWindow::PickScrollPoint_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    try {
        const auto region = selectedRegion();
        if (!region) {
            showInfo(
                InfoBarSeverity::Error,
                L"请先选择截图区域",
                L"截图区域坐标和尺寸必须是有效整数。");
            return;
        }
        queueSelection(SelectionKind::ScrollPoint, *region);
    } catch (const hresult_error& error) {
        showInfo(InfoBarSeverity::Error, L"无法选择滚动位置", error.message().c_str());
    } catch (...) {
        showInfo(InfoBarSeverity::Error, L"无法选择滚动位置", L"窗口初始化失败。");
    }
}

void MainWindow::PickHeaderBoundary_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    try {
        const auto region = selectedRegion();
        if (!region) {
            showInfo(
                InfoBarSeverity::Error,
                L"请先选择截图区域",
                L"固定页头必须位于有效的截图区域内。");
            return;
        }
        queueSelection(SelectionKind::HeaderBoundary, *region);
    } catch (const hresult_error& error) {
        showInfo(InfoBarSeverity::Error, L"无法选择页头", error.message().c_str());
    } catch (...) {
        showInfo(InfoBarSeverity::Error, L"无法选择页头", L"窗口初始化失败。");
    }
}

void MainWindow::ClearHeader_Click(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    HeaderHeightBox().Value(0);
    showInfo(
        InfoBarSeverity::Informational,
        L"固定页头已清除",
        L"所有画面都将用于查找重叠内容。");
}

void MainWindow::queueSelection(
    SelectionKind kind,
    std::optional<rillshot::core::RectI> allowedRegion) {
    if (selectionPending_) {
        return;
    }

    const HWND handle = windowHandle();
    const auto appWindow = AppWindow();
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    const bool placementValid = GetWindowPlacement(handle, &placement) != FALSE;
    selectionPending_ = true;
    RootGrid().IsHitTestVisible(false);
    ShellInfoBar().IsOpen(false);

    try {
        // Hiding is intentionally separated from desktop capture by a new
        // dispatcher turn. Returning from the click handler gives WinUI and DWM
        // a chance to commit the invisible frame before the snapshot is taken.
        appWindow.Hide();
        ShowWindow(handle, SW_HIDE);
        const bool queued = DispatcherQueue().TryEnqueue(
            Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weak = get_weak(), kind, allowedRegion, handle, appWindow,
             placementValid, placement]() mutable {
                if (const auto self = weak.get()) {
                    self->runQueuedSelection(
                        kind, allowedRegion, handle, placementValid, placement);
                    return;
                }
                try {
                    rillshot::gui::win32::clearWindowCaptureProtection(handle);
                    appWindow.Show();
                } catch (...) {
                }
                if (IsWindow(handle)) {
                    if (placementValid) {
                        SetWindowPlacement(handle, &placement);
                        ShowWindow(handle, placement.showCmd);
                    } else {
                        ShowWindow(handle, SW_SHOW);
                    }
                }
            });
        if (!queued) {
            throw hresult_error(E_FAIL, L"无法启动屏幕选择。");
        }
    } catch (...) {
        selectionPending_ = false;
        RootGrid().IsHitTestVisible(true);
        try {
            rillshot::gui::win32::clearWindowCaptureProtection(handle);
            appWindow.Show();
        } catch (...) {
        }
        if (IsWindow(handle)) {
            if (placementValid) {
                SetWindowPlacement(handle, &placement);
                ShowWindow(handle, placement.showCmd);
            } else {
                ShowWindow(handle, SW_SHOW);
            }
            SetForegroundWindow(handle);
        }
        throw;
    }
}

void MainWindow::runQueuedSelection(
    SelectionKind kind,
    std::optional<rillshot::core::RectI> allowedRegion,
    HWND window,
    bool placementValid,
    WINDOWPLACEMENT placement) {
    SelectionInteractionRestorer interactionRestorer(RootGrid(), selectionPending_);
    AppWindowVisibilityRestorer restorer(
        AppWindow(), window, placementValid, placement);
    try {
        if (!rillshot::gui::win32::hideWindowForCapture(window)) {
            throw hresult_error(E_FAIL, L"主窗口未隐藏，选择已取消。");
        }

        if (kind == SelectionKind::Region) {
            const auto selection =
                rillshot::gui::SelectionOverlay::pickRegion(window);
            if (!selection.value) {
                const auto presentation = selectionExitPresentation(
                    selection.reason, selection.systemError, L"截图区域");
                showInfo(
                    presentation.severity,
                    presentation.title,
                    presentation.message);
            } else {
                const auto& region = selection.value;
                RegionXBox().Value(region->x);
                RegionYBox().Value(region->y);
                RegionWidthBox().Value(region->width);
                RegionHeightBox().Value(region->height);

                // A newly selected region invalidates any point authored for a
                // previous target. Do not invent a default: the input location
                // is an explicit user choice and remains visibly incomplete.
                const double unsetValue =
                    std::numeric_limits<double>::quiet_NaN();
                ScrollXBox().Value(unsetValue);
                ScrollYBox().Value(unsetValue);
                const auto headerHeight = checkedInt(HeaderHeightBox().Value());
                const int minimumUsableRows =
                    std::min(16, std::max(1, region->height / 2));
                if (!headerHeight || *headerHeight < 0 ||
                    *headerHeight > region->height - minimumUsableRows) {
                    HeaderHeightBox().Value(0);
                }
                showInfo(
                    InfoBarSeverity::Success,
                    L"捕获区域已更新",
                    L"请继续选择滚动点。");
            }
        } else if (kind == SelectionKind::ScrollPoint) {
            const auto selection =
                rillshot::gui::SelectionOverlay::pickPoint(
                    window, allowedRegion);
            if (!selection.value) {
                const auto presentation = selectionExitPresentation(
                    selection.reason, selection.systemError, L"滚动位置");
                showInfo(
                    presentation.severity,
                    presentation.title,
                    presentation.message);
            } else {
                const auto& point = selection.value;
                ScrollXBox().Value(point->x);
                ScrollYBox().Value(point->y);
                showInfo(
                    InfoBarSeverity::Success,
                    L"位置已更新",
                    L"可以开始截图。");
            }
        } else if (!allowedRegion) {
            throw hresult_error(E_INVALIDARG, L"固定页头缺少截图区域。");
        } else {
            const auto selection =
                rillshot::gui::SelectionOverlay::pickHorizontalBoundary(
                    window, *allowedRegion);
            if (!selection.value) {
                const auto presentation = selectionExitPresentation(
                    selection.reason, selection.systemError, L"固定页头");
                showInfo(
                    presentation.severity,
                    presentation.title,
                    presentation.message);
            } else {
                const auto& boundary = selection.value;
                const int height = boundary->y - allowedRegion->y;
                const int minimumUsableRows =
                    std::min(16, std::max(1, allowedRegion->height / 2));
                const int maximumHeaderHeight =
                    allowedRegion->height - minimumUsableRows;
                if (height < 0 || height > maximumHeaderHeight) {
                    showInfo(
                        InfoBarSeverity::Error,
                        L"页头范围过大",
                        L"请在正文开始的位置选择边界，并为滚动内容保留足够高度。");
                } else {
                    HeaderHeightBox().Value(height);
                    showInfo(
                        InfoBarSeverity::Success,
                        height == 0 ? L"固定页头已清除" : L"固定页头已设置",
                        height == 0
                            ? L"所有画面都将用于查找重叠内容。"
                            : L"页头会保留一次，后续帧不会重复拼接该区域。");
                }
            }
        }
    } catch (const hresult_error& error) {
        showInfo(InfoBarSeverity::Error, L"选择失败", error.message().c_str());
    } catch (...) {
        showInfo(InfoBarSeverity::Error, L"选择失败", L"屏幕选择器意外关闭。");
    }
    refreshConfigurationSummary();
}

} // namespace winrt::Rillshot::WinUI::implementation
