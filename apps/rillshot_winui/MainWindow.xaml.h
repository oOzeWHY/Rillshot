#pragma once

#include "MainWindow.g.h"
#include "gui/CaptureController.h"
#include "gui/CaptureWorkflow.h"
#include "gui/GlobalHotkey.h"
#include "gui/WindowGeometry.h"
#include "platform/UserPreferences.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace winrt::Rillshot::WinUI::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow() = default;
    ~MainWindow() noexcept;

    void RootGrid_Loaded(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void AppTitleBar_SizeChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::SizeChangedEventArgs const& eventArgs);
    void StartCapture_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void StopCapture_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void Reset_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void OpenSettings_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void CloseSettings_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void PickRegion_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void PickScrollPoint_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void PickHeaderBoundary_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void ClearHeader_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void BrowseOutput_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void OpenOutput_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void RevealOutput_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void CopyOutputPath_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void ConfigurationNumber_ValueChanged(
        Microsoft::UI::Xaml::Controls::NumberBox const& sender,
        Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& eventArgs);
    void OutputPath_TextChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& eventArgs);
    void CaptureBehavior_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& eventArgs);
    void ScrollDirection_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& eventArgs);
    void Theme_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& eventArgs);
    void GlobalHotkey_Toggled(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void HotkeyBox_KeyDown(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& eventArgs);
    void ResetHotkey_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& eventArgs);
    void StopCapture_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& eventArgs);

private:
    enum class SelectionKind {
        Region,
        ScrollPoint,
        HeaderBoundary
    };

    [[nodiscard]] std::optional<rillshot::gui::GuiConfig> readConfig();
    [[nodiscard]] std::optional<rillshot::core::RectI> selectedRegion();
    [[nodiscard]] std::optional<rillshot::core::PointI> selectedScrollPoint(
        const rillshot::core::RectI& region);
    [[nodiscard]] static std::optional<int> checkedInt(double value) noexcept;
    [[nodiscard]] HWND windowHandle() const;
    void queueSelection(
        SelectionKind kind,
        std::optional<rillshot::core::RectI> allowedRegion = std::nullopt);
    void runQueuedSelection(
        SelectionKind kind,
        std::optional<rillshot::core::RectI> allowedRegion,
        HWND window,
        bool placementValid,
        WINDOWPLACEMENT placement);
    [[nodiscard]] bool hideForCapture();
    void restoreAfterCapture();
    void requestCaptureStop();
    void captureCompletedOnUiThread();
    void initializePreferences();
    void initializePreferencePersistence();
    void applyThemePreference();
    void queueThemePreferenceApply();
    void queuePreferenceSave() noexcept;
    void completePreferenceSave(std::uint64_t generation, bool saved) noexcept;
    void flushPreferenceSave() noexcept;
    void applyGlobalHotkey(bool persist, bool announceFailure);
    void updatePreferenceSummary();
    void onGlobalHotkey();
    void navigateToSettings(bool open);
    [[nodiscard]] bool navigationAnimationsEnabled() noexcept;
    void scheduleNavigationPrewarmAfterFirstFrame() noexcept;
    void stopNavigationPrewarmObservation() noexcept;
    void prewarmNavigationExperience() noexcept;
    [[nodiscard]] bool ensureNavigationAnimationTemplates() noexcept;
    void prepareNavigationViewportIsolation(bool open) noexcept;
    void clearNavigationViewportIsolation() noexcept;
    void beginPageNavigationAnimation(bool open);
    void completePageNavigation(bool open);
    void stopPageNavigationAnimation() noexcept;
    void prepareNavigationWindowResize(bool open) noexcept;
    void startNavigationWindowResizeAnimation() noexcept;
    void advanceNavigationWindowResize() noexcept;
    void finishNavigationWindowResizeAnimation() noexcept;
    void stopNavigationWindowResizeAnimation() noexcept;
    void settlePageNavigationForSelection();
    void appWindowClosing(
        Microsoft::UI::Windowing::AppWindow const& sender,
        Microsoft::UI::Windowing::AppWindowClosingEventArgs const& eventArgs);
    void updateTitleBarPadding();
    void renderPage();
    void renderStage();
    void refreshConfigurationSummary();
    void updateOutcomeActions(bool outputSaved, const std::wstring& outputPath);
    void showInfo(
        Microsoft::UI::Xaml::Controls::InfoBarSeverity severity,
        const std::wstring& title,
        const std::wstring& message);

    rillshot::gui::CaptureWorkflowModel workflow_;
    rillshot::gui::CaptureController captureController_;
    rillshot::gui::GlobalHotkey globalHotkey_;
    rillshot::platform::UserPreferences preferences_;
    struct PreferenceSaveState;
    std::shared_ptr<PreferenceSaveState> preferenceSaveState_;
    winrt::event_token appWindowClosingToken_{};
    bool initialized_ = false;
    bool applyingPreferences_ = false;
    bool themeApplyQueued_ = false;
    bool hotkeyApplyQueued_ = false;
    bool settingsOpen_ = false;
    bool navigationTransitionRunning_ = false;
    bool navigationTargetOpen_ = false;
    std::optional<bool> navigationPendingTarget_;
    bool outputAttentionPending_ = false;
    bool selectionPending_ = false;
    bool closePending_ = false;
    bool allowClose_ = false;
    bool lastOutputSaved_ = false;
    bool captureWindowPlacementValid_ = false;
    bool navigationBatchCompletionSubscribed_ = false;
    bool navigationPrewarmRenderedSubscribed_ = false;
    bool navigationResizeRenderingSubscribed_ = false;
    bool navigationWindowResizePrepared_ = false;
    bool navigationViewportsIsolated_ = false;
    bool navigationResizeTargetOpen_ = false;
    bool compactWindowSizeValid_ = false;
    bool navigationTemplatesReady_ = false;
    bool settingsLayoutPrewarmed_ = false;
    Microsoft::UI::Composition::CompositionScopedBatch navigationBatch_{nullptr};
    Microsoft::UI::Composition::CompositionEasingFunction navigationEnterEasing_{nullptr};
    Microsoft::UI::Composition::CompositionEasingFunction navigationExitEasing_{nullptr};
    Microsoft::UI::Composition::ScalarKeyFrameAnimation outgoingOpacityAnimation_{nullptr};
    Microsoft::UI::Composition::ScalarKeyFrameAnimation incomingOpacityAnimation_{nullptr};
    Microsoft::UI::Composition::Vector3KeyFrameAnimation outgoingTranslationAnimation_{nullptr};
    Microsoft::UI::Composition::Vector3KeyFrameAnimation incomingTranslationAnimation_{nullptr};
    Microsoft::UI::Composition::Vector3KeyFrameAnimation outgoingScaleAnimation_{nullptr};
    Microsoft::UI::Composition::Vector3KeyFrameAnimation incomingScaleAnimation_{nullptr};
    Windows::UI::ViewManagement::UISettings navigationUiSettings_{nullptr};
    winrt::event_token navigationBatchCompletedToken_{};
    winrt::event_token navigationPrewarmRenderedToken_{};
    winrt::event_token navigationResizeRenderingToken_{};
    std::chrono::steady_clock::time_point navigationResizeStartedAt_{};
    rillshot::gui::geometry::WindowBounds navigationResizeStart_{};
    rillshot::gui::geometry::WindowBounds navigationResizeTarget_{};
    rillshot::gui::geometry::WindowBounds navigationResizeLastApplied_{};
    Windows::Graphics::SizeInt32 compactWindowSize_{};
    std::uint32_t compactWindowDpi_ = 96;
    WINDOWPLACEMENT captureWindowPlacement_{sizeof(WINDOWPLACEMENT)};
    std::wstring lastOutputPath_;
    std::wstring confirmedOverwritePath_;
};

} // namespace winrt::Rillshot::WinUI::implementation

namespace winrt::Rillshot::WinUI::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

} // namespace winrt::Rillshot::WinUI::factory_implementation
