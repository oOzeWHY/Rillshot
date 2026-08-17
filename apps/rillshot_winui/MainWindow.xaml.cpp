#include "pch.h"

#include "MainWindow.xaml.h"
#include "MainWindow.WindowSizing.h"
#include "gui/WindowGeometry.h"
#include "resource.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "gui/WindowsShellUtils.h"

#include <algorithm>

using namespace winrt;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;

namespace winrt::Rillshot::WinUI::implementation {
namespace {

void applyWindowIcon(HWND window) noexcept {
    const HINSTANCE module = GetModuleHandleW(nullptr);
    if (!window || !module) {
        return;
    }
    const auto load = [module](int width, int height) noexcept {
        return static_cast<HICON>(LoadImageW(
            module,
            MAKEINTRESOURCEW(IDI_RILLSHOT_APP_ICON),
            IMAGE_ICON,
            width,
            height,
            LR_DEFAULTCOLOR | LR_SHARED));
    };
    // Some Windows SDK headers define `small`; keep icon locals explicit.
    if (const HICON largeIcon = load(
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))) {
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
    }
    if (const HICON smallIcon = load(
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))) {
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    }
}

} // namespace

void MainWindow::RootGrid_Loaded(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] RoutedEventArgs const& eventArgs) {
    if (initialized_) {
        return;
    }
    initialized_ = true;

    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBarDragRegion());
    updateTitleBarPadding();

    const HWND handle = windowHandle();
    applyWindowIcon(handle);
    const UINT dpi = std::max<UINT>(96U, GetDpiForWindow(handle));
    const HMONITOR monitor = MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(MONITORINFO)};
    const bool hasWorkArea = monitor && GetMonitorInfoW(monitor, &monitorInfo) != FALSE;
    using rillshot::gui::geometry::pixelsForDip;
    const int minimumWidth = pixelsForDip(window_sizing::minimumWidthDip, dpi);
    const int minimumHeight = pixelsForDip(window_sizing::minimumHeightDip, dpi);
    int targetWidth = pixelsForDip(window_sizing::compactWidthDip, dpi);
    int targetHeight = pixelsForDip(window_sizing::compactHeightDip, dpi);

    int workWidth = 0;
    int workHeight = 0;
    using rillshot::gui::geometry::tryPositiveCoordinateSpan;
    if (hasWorkArea &&
        tryPositiveCoordinateSpan(
            monitorInfo.rcWork.left, monitorInfo.rcWork.right, workWidth) &&
        tryPositiveCoordinateSpan(
            monitorInfo.rcWork.top, monitorInfo.rcWork.bottom, workHeight)) {
        const int margin = pixelsForDip(window_sizing::workAreaMarginDip, dpi);
        const auto availableWidthWide = static_cast<std::int64_t>(workWidth) -
            static_cast<std::int64_t>(margin) * 2;
        const auto availableHeightWide = static_cast<std::int64_t>(workHeight) -
            static_cast<std::int64_t>(margin) * 2;
        const int availableWidth = rillshot::gui::geometry::clampCoordinate(
            std::max<std::int64_t>(1, availableWidthWide));
        const int availableHeight = rillshot::gui::geometry::clampCoordinate(
            std::max<std::int64_t>(1, availableHeightWide));
        const rillshot::gui::geometry::WindowBounds workArea{
            monitorInfo.rcWork.left,
            monitorInfo.rcWork.top,
            workWidth,
            workHeight};
        const auto target = rillshot::gui::geometry::fitAdaptiveWindowSizeAroundCenter(
            workArea,
            workArea,
            targetWidth,
            targetHeight,
            minimumWidth,
            minimumHeight,
            margin,
            window_sizing::maximumWorkAreaWidthPercent,
            window_sizing::maximumWorkAreaHeightPercent);
        targetWidth = target.width;
        targetHeight = target.height;

        if (const auto presenter = AppWindow().Presenter().try_as<OverlappedPresenter>()) {
            presenter.PreferredMinimumWidth(std::min(minimumWidth, availableWidth));
            presenter.PreferredMinimumHeight(std::min(minimumHeight, availableHeight));
        }
        if (target.isValid()) {
            AppWindow().MoveAndResize(Windows::Graphics::RectInt32{
                target.x, target.y, target.width, target.height});
        }
    } else {
        if (const auto presenter = AppWindow().Presenter().try_as<OverlappedPresenter>()) {
            presenter.PreferredMinimumWidth(minimumWidth);
            presenter.PreferredMinimumHeight(minimumHeight);
        }
        AppWindow().Resize(Windows::Graphics::SizeInt32{targetWidth, targetHeight});
    }

    appWindowClosingToken_ = AppWindow().Closing(
        [weak = get_weak()](
            Microsoft::UI::Windowing::AppWindow const& appWindow,
            Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
            if (const auto self = weak.get()) {
                self->appWindowClosing(appWindow, args);
            }
        });

    if (OutputPathBox().Text().empty()) {
        OutputPathBox().Text(rillshot::gui::win32::defaultOutputPath());
    }
    initializePreferences();
    renderStage();
    refreshConfigurationSummary();

    scheduleNavigationPrewarmAfterFirstFrame();
}

void MainWindow::scheduleNavigationPrewarmAfterFirstFrame() noexcept {
    if (navigationPrewarmRenderedSubscribed_ || settingsLayoutPrewarmed_) {
        return;
    }
    try {
        // Rendered is observed exactly once. Its UI-thread callback only
        // removes itself and queues low-priority work, so the first presented
        // frame never pays the settings layout or template creation cost.
        navigationPrewarmRenderedToken_ =
            Microsoft::UI::Xaml::Media::CompositionTarget::Rendered(
                [weak = get_weak()](
                    [[maybe_unused]] IInspectable const& sender,
                    [[maybe_unused]] Microsoft::UI::Xaml::Media::RenderedEventArgs const& args) {
                    if (const auto self = weak.get()) {
                        self->stopNavigationPrewarmObservation();
                        try {
                            [[maybe_unused]] const bool queued =
                                self->DispatcherQueue().TryEnqueue(
                                    Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
                                    [weak] {
                                        if (const auto current = weak.get()) {
                                            current->prewarmNavigationExperience();
                                        }
                                    });
                        } catch (...) {
                            // The cold navigation path remains the fallback if
                            // the dispatcher is already shutting down.
                        }
                    }
                });
        navigationPrewarmRenderedSubscribed_ = true;
    } catch (...) {
        stopNavigationPrewarmObservation();
    }
}

void MainWindow::stopNavigationPrewarmObservation() noexcept {
    try {
        if (navigationPrewarmRenderedSubscribed_) {
            Microsoft::UI::Xaml::Media::CompositionTarget::Rendered(
                navigationPrewarmRenderedToken_);
        }
    } catch (...) {
    }
    navigationPrewarmRenderedSubscribed_ = false;
    navigationPrewarmRenderedToken_ = {};
}

void MainWindow::AppTitleBar_SizeChanged(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] SizeChangedEventArgs const& eventArgs) {
    if (initialized_) {
        updateTitleBarPadding();
    }
}

void MainWindow::updateTitleBarPadding() {
    const auto titleBar = AppWindow().TitleBar();
    const auto xamlRoot = AppTitleBar().XamlRoot();
    const double rawScale = xamlRoot ? xamlRoot.RasterizationScale() : 1.0;
    const double scale = rawScale > 0.0 ? rawScale : 1.0;
    AppTitleBar().Padding(Thickness{
        static_cast<double>(titleBar.LeftInset()) / scale + 16.0,
        0.0,
        static_cast<double>(titleBar.RightInset()) / scale + 12.0,
        0.0});
}

HWND MainWindow::windowHandle() const {
    HWND handle = nullptr;
    const auto windowNative = m_inner.as<::IWindowNative>();
    check_hresult(windowNative->get_WindowHandle(&handle));
    return handle;
}

} // namespace winrt::Rillshot::WinUI::implementation
