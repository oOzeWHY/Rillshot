#include "pch.h"

#include "MainWindow.xaml.h"
#include "MainWindow.Navigation.Motion.h"
#include "MainWindow.WindowSizing.h"

#include "gui/WindowGeometry.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <utility>

using namespace winrt;
using namespace Microsoft::UI::Dispatching;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation::Numerics;
using namespace Windows::System::Threading;

namespace winrt::Rillshot::WinUI::implementation {
namespace {

using navigation_motion::navigationDistance;
using navigation_motion::navigationDuration;
using navigation_motion::navigationResizeFallbackInterval;
using navigation_motion::navigationResizeMaximumInterval;
using navigation_motion::navigationResizeMinimumInterval;

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

std::chrono::microseconds refreshIntervalForMonitor(HMONITOR monitor) noexcept {
    if (!monitor) {
        return navigationResizeFallbackInterval;
    }

    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = static_cast<DWORD>(sizeof(monitorInfo));
    if (GetMonitorInfoW(
            monitor,
            reinterpret_cast<MONITORINFO*>(&monitorInfo)) == FALSE) {
        return navigationResizeFallbackInterval;
    }

    DEVMODEW displayMode{};
    displayMode.dmSize = static_cast<WORD>(sizeof(displayMode));
    if (EnumDisplaySettingsW(
            monitorInfo.szDevice,
            ENUM_CURRENT_SETTINGS,
            &displayMode) == FALSE ||
        displayMode.dmDisplayFrequency < 30 ||
        displayMode.dmDisplayFrequency > 500) {
        return navigationResizeFallbackInterval;
    }

    const auto interval = std::chrono::microseconds{
        rillshot::gui::geometry::navigationPulseIntervalMicros(
            static_cast<std::uint32_t>(displayMode.dmDisplayFrequency))};
    return std::clamp(
        interval,
        navigationResizeMinimumInterval,
        navigationResizeMaximumInterval);
}

} // namespace

struct MainWindow::NavigationResizePulseState {
    std::atomic_bool stopped{false};
    std::atomic_bool dispatchPending{false};
};

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

        navigationResizePulseInterval_ = refreshIntervalForMonitor(monitor);
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
        const auto state = std::make_shared<NavigationResizePulseState>();
        const auto dispatcher = DispatcherQueue();
        const auto weak = get_weak();
        navigationResizePulseState_ = state;
        navigationResizeTimer_ = ThreadPoolTimer::CreatePeriodicTimer(
            [state, dispatcher, weak](
                [[maybe_unused]] ThreadPoolTimer const& timer) noexcept {
                if (state->stopped.load(std::memory_order_acquire)) {
                    return;
                }

                bool expected = false;
                if (!state->dispatchPending.compare_exchange_strong(
                        expected,
                        true,
                        std::memory_order_acq_rel)) {
                    return;
                }

                bool queued = false;
                try {
                    queued = dispatcher.TryEnqueue(
                        DispatcherQueuePriority::High,
                        [state, weak] {
                            try {
                                if (!state->stopped.load(std::memory_order_acquire)) {
                                    if (const auto self = weak.get()) {
                                        self->advanceNavigationWindowResize();
                                    }
                                }
                            } catch (...) {
                            }
                            state->dispatchPending.store(
                                false,
                                std::memory_order_release);
                        });
                } catch (...) {
                }
                if (!queued) {
                    state->dispatchPending.store(
                        false,
                        std::memory_order_release);
                }
            },
            std::chrono::duration_cast<Windows::Foundation::TimeSpan>(
                navigationResizePulseInterval_));
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
    const auto state = std::exchange(
        navigationResizePulseState_,
        std::shared_ptr<NavigationResizePulseState>{});
    if (state) {
        state->stopped.store(true, std::memory_order_release);
    }
    try {
        if (navigationResizeTimer_) {
            navigationResizeTimer_.Cancel();
        }
    } catch (...) {
    }
    navigationResizeTimer_ = nullptr;
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
