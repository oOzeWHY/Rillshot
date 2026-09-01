#include "pch.h"

#include "MainWindow.xaml.h"
#include "MainWindow.Navigation.Motion.h"
#include "MainWindow.WindowSizing.h"

#include "gui/WindowGeometry.h"

#include <algorithm>
#include <chrono>

using namespace winrt;
using namespace Microsoft::UI::Dispatching;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation::Numerics;

namespace winrt::Rillshot::WinUI::implementation {
namespace {

using navigation_motion::navigationDistance;
using navigation_motion::navigationDuration;

void resetNavigationProperties(UIElement const& element) {
    element.Opacity(1.0);
    element.Translation(float3{});
    element.Scale(float3{1.0F, 1.0F, 1.0F});
    element.CenterPoint(float3{});
}

bool sameBounds(
    const rillshot::gui::geometry::WindowBounds& first,
    const rillshot::gui::geometry::WindowBounds& second) noexcept {
    return first.x == second.x && first.y == second.y &&
        first.width == second.width && first.height == second.height;
}

} // namespace

bool MainWindow::navigationAnimationsEnabled() noexcept {
    try {
        if (!navigationUiSettings_) {
            navigationUiSettings_ = Windows::UI::ViewManagement::UISettings{};
        }
        return navigationUiSettings_.AnimationsEnabled();
    } catch (...) {
        // Motion is the normal navigation contract. Only an explicit system
        // preference disables it; a settings-query failure must not create an
        // unexpected hard cut between the capture and settings surfaces.
        return true;
    }
}

void MainWindow::navigateToSettings(bool open) {
    if (navigationTransitionRunning_) {
        // Preserve the latest intent instead of dropping rapid keyboard,
        // automation, or programmatic reversals. The current compositor batch
        // finishes cleanly, then at most one follow-up transition is started.
        navigationPendingTarget_ = open;
        return;
    }
    navigationPendingTarget_.reset();
    if (settingsOpen_ == open) {
        return;
    }
    if (open &&
        (workflow_.stage() != rillshot::gui::CaptureUiStage::Ready ||
         selectionPending_ || captureController_.hasWorker())) {
        return;
    }

    if (!outputAttentionPending_) {
        ShellInfoBar().IsOpen(false);
    }
    navigationTransitionRunning_ = true;
    navigationTargetOpen_ = open;
    prepareNavigationWindowResize(open);

    const UIElement outgoing = open
        ? CaptureScrollViewer().as<UIElement>()
        : SettingsScrollViewer().as<UIElement>();
    const UIElement incoming = open
        ? SettingsScrollViewer().as<UIElement>()
        : CaptureScrollViewer().as<UIElement>();
    outgoing.IsHitTestVisible(false);
    incoming.IsHitTestVisible(false);

    if (!navigationAnimationsEnabled()) {
        completePageNavigation(open);
        return;
    }

    // Keep each page viewport at its own start/final size while the HWND changes.
    // This prevents every native resize sample from remeasuring the full settings
    // tree. Opacity, translation, and scale remain compositor-owned.
    prepareNavigationViewportIsolation(open);
    incoming.Opacity(0.0);
    incoming.Translation(float3{
        open ? navigationDistance : -navigationDistance, 0.0F, 0.0F});
    incoming.Visibility(Visibility::Visible);

    // Start on a low-priority dispatcher turn so the initiating control can
    // present its pressed/released state and XAML can process the new visible
    // tree before the compositor clock starts. The cold path performs one
    // explicit layout; the normal prewarmed path does not force another pass.
    const bool queued = DispatcherQueue().TryEnqueue(
        DispatcherQueuePriority::Low,
        [weak = get_weak(), open] {
            if (const auto self = weak.get()) {
                self->beginPageNavigationAnimation(open);
            }
        });
    if (!queued) {
        beginPageNavigationAnimation(open);
    }
}

void MainWindow::prepareNavigationWindowResize(bool open) noexcept {
    stopNavigationWindowResizeAnimation();
    navigationResizeTargetOpen_ = open;

    try {
        const auto appWindow = AppWindow();
        const auto presenter = appWindow.Presenter().try_as<OverlappedPresenter>();
        if (!presenter || presenter.State() != OverlappedPresenterState::Restored) {
            return;
        }

        const auto position = appWindow.Position();
        const auto size = appWindow.Size();
        const rillshot::gui::geometry::WindowBounds current{
            position.X, position.Y, size.Width, size.Height};
        if (!current.isValid()) {
            return;
        }

        const HWND handle = windowHandle();
        const HMONITOR monitor = MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST);
        const UINT dpi = std::max<UINT>(96U, GetDpiForWindow(handle));
        if (open) {
            compactWindowSize_ = Windows::Graphics::SizeInt32{
                current.width, current.height};
            compactWindowDpi_ = dpi;
            compactWindowSizeValid_ = true;
        } else if (!compactWindowSizeValid_) {
            return;
        }

        MONITORINFO monitorInfo{sizeof(MONITORINFO)};
        if (!monitor || GetMonitorInfoW(monitor, &monitorInfo) == FALSE) {
            return;
        }

        int workWidth = 0;
        int workHeight = 0;
        using rillshot::gui::geometry::tryPositiveCoordinateSpan;
        if (!tryPositiveCoordinateSpan(
                monitorInfo.rcWork.left, monitorInfo.rcWork.right, workWidth) ||
            !tryPositiveCoordinateSpan(
                monitorInfo.rcWork.top, monitorInfo.rcWork.bottom, workHeight)) {
            return;
        }

        using rillshot::gui::geometry::pixelsForDip;
        const int margin = pixelsForDip(window_sizing::workAreaMarginDip, dpi);
        const rillshot::gui::geometry::WindowBounds workArea{
            monitorInfo.rcWork.left,
            monitorInfo.rcWork.top,
            workWidth,
            workHeight};
        const auto adaptiveSettings =
            rillshot::gui::geometry::fitAdaptiveWindowSizeAroundCenter(
                current,
                workArea,
                pixelsForDip(window_sizing::settingsWidthDip, dpi),
                pixelsForDip(window_sizing::settingsHeightDip, dpi),
                pixelsForDip(window_sizing::minimumWidthDip, dpi),
                pixelsForDip(window_sizing::minimumHeightDip, dpi),
                margin,
                window_sizing::maximumWorkAreaWidthPercent,
                window_sizing::maximumWorkAreaHeightPercent);
        const int desiredWidth = open
            ? std::max(current.width, adaptiveSettings.width)
            : rillshot::gui::geometry::scalePixelDimensionForDpi(
                  compactWindowSize_.Width, compactWindowDpi_, dpi);
        const int desiredHeight = open
            ? std::max(current.height, adaptiveSettings.height)
            : rillshot::gui::geometry::scalePixelDimensionForDpi(
                  compactWindowSize_.Height, compactWindowDpi_, dpi);
        const auto target = rillshot::gui::geometry::fitWindowSizeAroundCenter(
            current, workArea, desiredWidth, desiredHeight, margin);
        if (!target.isValid() || sameBounds(current, target)) {
            return;
        }

        navigationResizeStart_ = current;
        navigationResizeTarget_ = target;
        navigationResizeLastApplied_ = current;
        navigationWindowResizePrepared_ = true;
    } catch (...) {
        navigationWindowResizePrepared_ = false;
    }
}

void MainWindow::startNavigationWindowResizeAnimation() noexcept {
    if (!navigationWindowResizePrepared_) {
        return;
    }

    try {
        navigationResizeStartedAt_ = std::chrono::steady_clock::now();
        // Rendering runs on the UI thread immediately before a XAML frame.
        // A finite subscription aligns native resize samples with presented
        // frames and naturally drops samples when the UI thread is busy.
        navigationResizeRenderingToken_ = CompositionTarget::Rendering(
            [weak = get_weak()](
                [[maybe_unused]] IInspectable const& sender,
                [[maybe_unused]] IInspectable const& args) {
                if (const auto self = weak.get()) {
                    self->advanceNavigationWindowResize();
                }
            });
        navigationResizeRenderingSubscribed_ = true;
    } catch (...) {
        finishNavigationWindowResizeAnimation();
    }
}

void MainWindow::advanceNavigationWindowResize() noexcept {
    if (!navigationWindowResizePrepared_) {
        stopNavigationWindowResizeAnimation();
        return;
    }

    try {
        const auto elapsed = std::chrono::steady_clock::now() -
            navigationResizeStartedAt_;
        const double progress = std::chrono::duration<double>(elapsed).count() /
            std::chrono::duration<double>(navigationDuration).count();
        const auto bounds = rillshot::gui::geometry::interpolateWindowBounds(
            navigationResizeStart_,
            navigationResizeTarget_,
            rillshot::gui::geometry::smootherstepProgress(progress));
        if (!sameBounds(bounds, navigationResizeLastApplied_)) {
            AppWindow().MoveAndResize(Windows::Graphics::RectInt32{
                bounds.x, bounds.y, bounds.width, bounds.height});
            navigationResizeLastApplied_ = bounds;
        }
        if (progress >= 1.0) {
            finishNavigationWindowResizeAnimation();
        }
    } catch (...) {
        finishNavigationWindowResizeAnimation();
    }
}

void MainWindow::finishNavigationWindowResizeAnimation() noexcept {
    if (navigationWindowResizePrepared_) {
        try {
            AppWindow().MoveAndResize(Windows::Graphics::RectInt32{
                navigationResizeTarget_.x,
                navigationResizeTarget_.y,
                navigationResizeTarget_.width,
                navigationResizeTarget_.height});
        } catch (...) {
        }
    }
    stopNavigationWindowResizeAnimation();
}

void MainWindow::stopNavigationWindowResizeAnimation() noexcept {
    try {
        if (navigationResizeRenderingSubscribed_) {
            CompositionTarget::Rendering(navigationResizeRenderingToken_);
        }
    } catch (...) {
    }
    navigationResizeRenderingSubscribed_ = false;
    navigationResizeRenderingToken_ = {};
    navigationWindowResizePrepared_ = false;
}

void MainWindow::completePageNavigation(bool open) {
    if (navigationResizeTargetOpen_ != open) {
        prepareNavigationWindowResize(open);
    }
    finishNavigationWindowResizeAnimation();
    stopPageNavigationAnimation();
    settingsOpen_ = open &&
        workflow_.stage() == rillshot::gui::CaptureUiStage::Ready;
    const bool showSettings = settingsOpen_;

    resetNavigationProperties(CaptureScrollViewer());
    resetNavigationProperties(SettingsScrollViewer());
    CaptureScrollViewer().Visibility(
        showSettings ? Visibility::Collapsed : Visibility::Visible);
    SettingsScrollViewer().Visibility(
        showSettings ? Visibility::Visible : Visibility::Collapsed);
    CaptureScrollViewer().IsHitTestVisible(!showSettings);
    SettingsScrollViewer().IsHitTestVisible(showSettings);
    clearNavigationViewportIsolation();
    CaptureScrollViewer().ScrollToVerticalOffset(0.0);
    SettingsScrollViewer().ScrollToVerticalOffset(0.0);
    navigationTransitionRunning_ = false;
    navigationTargetOpen_ = showSettings;
    OpenSettingsButton().Visibility(
        showSettings ? Visibility::Collapsed : Visibility::Visible);

    const auto pendingTarget = navigationPendingTarget_;
    navigationPendingTarget_.reset();
    if (pendingTarget && *pendingTarget != showSettings) {
        navigateToSettings(*pendingTarget);
        return;
    }

    if (showSettings) {
        if (outputAttentionPending_) {
            outputAttentionPending_ = false;
            BrowseOutputButton().Focus(FocusState::Programmatic);
        } else {
            SettingsBackButton().Focus(FocusState::Programmatic);
        }
    } else {
        outputAttentionPending_ = false;
        OpenSettingsButton().Focus(FocusState::Programmatic);
    }
}

void MainWindow::renderPage() {
    stopNavigationWindowResizeAnimation();
    stopPageNavigationAnimation();
    navigationTransitionRunning_ = false;
    navigationPendingTarget_.reset();
    const bool showSettings = settingsOpen_ &&
        workflow_.stage() == rillshot::gui::CaptureUiStage::Ready;
    if (!showSettings) {
        settingsOpen_ = false;
    }
    navigationTargetOpen_ = showSettings;
    resetNavigationProperties(CaptureScrollViewer());
    resetNavigationProperties(SettingsScrollViewer());
    CaptureScrollViewer().Visibility(
        showSettings ? Visibility::Collapsed : Visibility::Visible);
    SettingsScrollViewer().Visibility(
        showSettings ? Visibility::Visible : Visibility::Collapsed);
    CaptureScrollViewer().IsHitTestVisible(!showSettings);
    SettingsScrollViewer().IsHitTestVisible(showSettings);
    clearNavigationViewportIsolation();
    OpenSettingsButton().Visibility(
        !showSettings && workflow_.stage() == rillshot::gui::CaptureUiStage::Ready
            ? Visibility::Visible
            : Visibility::Collapsed);
    CaptureScrollViewer().ScrollToVerticalOffset(0.0);
    SettingsScrollViewer().ScrollToVerticalOffset(0.0);
}

void MainWindow::settlePageNavigationForSelection() {
    navigationPendingTarget_.reset();
    if (navigationTransitionRunning_ || settingsOpen_ || navigationTargetOpen_) {
        completePageNavigation(false);
        return;
    }
    renderPage();
}

} // namespace winrt::Rillshot::WinUI::implementation
