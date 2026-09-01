#include "pch.h"

#include "MainWindow.xaml.h"
#include "MainWindow.Navigation.Motion.h"

#include <algorithm>
#include <limits>

using namespace winrt;
using namespace Microsoft::UI::Composition;
using namespace Microsoft::UI::Xaml;
using namespace Windows::Foundation::Numerics;

namespace winrt::Rillshot::WinUI::implementation {
namespace {

using navigation_motion::incomingScale;
using navigation_motion::incomingDuration;
using navigation_motion::navigationDistance;
using navigation_motion::outgoingDuration;
using navigation_motion::outgoingScale;

void setNavigationCenterPoint(UIElement const& element) {
    const auto frameworkElement = element.as<FrameworkElement>();
    const double elementWidth = std::max(0.0, frameworkElement.ActualWidth());
    const double elementHeight = std::max(0.0, frameworkElement.ActualHeight());
    element.CenterPoint(float3{
        static_cast<float>(elementWidth * 0.5),
        static_cast<float>(elementHeight * 0.5),
        0.0F});
}

void clearExplicitSize(FrameworkElement const& element) noexcept {
    const double automatic = std::numeric_limits<double>::quiet_NaN();
    try {
        element.Width(automatic);
    } catch (...) {
    }
    try {
        element.Height(automatic);
    } catch (...) {
    }
}

} // namespace

void MainWindow::prewarmNavigationExperience() noexcept {
    if (navigationTransitionRunning_ || settingsOpen_ ||
        workflow_.stage() != rillshot::gui::CaptureUiStage::Ready) {
        return;
    }

    [[maybe_unused]] const bool animationsEnabled = navigationAnimationsEnabled();
    [[maybe_unused]] const bool templatesReady =
        ensureNavigationAnimationTemplates();
    if (settingsLayoutPrewarmed_) {
        return;
    }

    try {
        prepareNavigationWindowResize(true);
        prepareNavigationViewportIsolation(true);
        const UIElement settings = SettingsScrollViewer().as<UIElement>();
        settings.IsHitTestVisible(false);
        settings.Opacity(0.0);
        settings.Visibility(Visibility::Visible);
        // Both state changes are completed in one dispatcher callback, so the
        // invisible prewarm is never presented as an interactive frame.
        PageHost().UpdateLayout();
        settings.Visibility(Visibility::Collapsed);
        settings.Opacity(1.0);
        clearNavigationViewportIsolation();
        stopNavigationWindowResizeAnimation();
        settingsLayoutPrewarmed_ = true;
    } catch (...) {
        clearNavigationViewportIsolation();
        stopNavigationWindowResizeAnimation();
        try {
            SettingsScrollViewer().Opacity(1.0);
            SettingsScrollViewer().Translation(float3{});
            SettingsScrollViewer().Scale(float3{1.0F, 1.0F, 1.0F});
            SettingsScrollViewer().CenterPoint(float3{});
            SettingsScrollViewer().Visibility(Visibility::Collapsed);
            SettingsScrollViewer().IsHitTestVisible(false);
        } catch (...) {
        }
    }
}

bool MainWindow::ensureNavigationAnimationTemplates() noexcept {
    if (navigationTemplatesReady_) {
        return true;
    }

    try {
        const auto compositor = Microsoft::UI::Xaml::Media::CompositionTarget::
            GetCompositorForCurrentThread();
        navigationEnterEasing_ = compositor.CreateCubicBezierEasingFunction(
            float2{0.0F, 0.0F}, float2{0.0F, 1.0F});
        navigationExitEasing_ = compositor.CreateCubicBezierEasingFunction(
            float2{1.0F, 0.0F}, float2{1.0F, 1.0F});

        outgoingOpacityAnimation_ = compositor.CreateScalarKeyFrameAnimation();
        outgoingOpacityAnimation_.Target(L"Opacity");
        outgoingOpacityAnimation_.InsertKeyFrame(0.0F, 1.0F);
        outgoingOpacityAnimation_.InsertKeyFrame(
            1.0F, 0.0F, navigationExitEasing_);
        outgoingOpacityAnimation_.Duration(outgoingDuration);

        incomingOpacityAnimation_ = compositor.CreateScalarKeyFrameAnimation();
        incomingOpacityAnimation_.Target(L"Opacity");
        incomingOpacityAnimation_.InsertKeyFrame(0.0F, 0.0F);
        incomingOpacityAnimation_.InsertKeyFrame(
            1.0F, 1.0F, navigationEnterEasing_);
        incomingOpacityAnimation_.Duration(incomingDuration);

        outgoingTranslationAnimation_ =
            compositor.CreateVector3KeyFrameAnimation();
        outgoingTranslationAnimation_.Target(L"Translation");
        outgoingTranslationAnimation_.Duration(outgoingDuration);

        incomingTranslationAnimation_ =
            compositor.CreateVector3KeyFrameAnimation();
        incomingTranslationAnimation_.Target(L"Translation");
        incomingTranslationAnimation_.Duration(incomingDuration);

        outgoingScaleAnimation_ = compositor.CreateVector3KeyFrameAnimation();
        outgoingScaleAnimation_.Target(L"Scale");
        outgoingScaleAnimation_.InsertKeyFrame(
            0.0F, float3{1.0F, 1.0F, 1.0F});
        outgoingScaleAnimation_.InsertKeyFrame(
            1.0F,
            float3{outgoingScale, outgoingScale, 1.0F},
            navigationExitEasing_);
        outgoingScaleAnimation_.Duration(outgoingDuration);

        incomingScaleAnimation_ = compositor.CreateVector3KeyFrameAnimation();
        incomingScaleAnimation_.Target(L"Scale");
        incomingScaleAnimation_.InsertKeyFrame(
            0.0F, float3{incomingScale, incomingScale, 1.0F});
        incomingScaleAnimation_.InsertKeyFrame(
            1.0F,
            float3{1.0F, 1.0F, 1.0F},
            navigationEnterEasing_);
        incomingScaleAnimation_.Duration(incomingDuration);
        navigationTemplatesReady_ = true;
        return true;
    } catch (...) {
        navigationTemplatesReady_ = false;
        navigationEnterEasing_ = nullptr;
        navigationExitEasing_ = nullptr;
        outgoingOpacityAnimation_ = nullptr;
        incomingOpacityAnimation_ = nullptr;
        outgoingTranslationAnimation_ = nullptr;
        incomingTranslationAnimation_ = nullptr;
        outgoingScaleAnimation_ = nullptr;
        incomingScaleAnimation_ = nullptr;
        return false;
    }
}

void MainWindow::prepareNavigationViewportIsolation(bool open) noexcept {
    clearNavigationViewportIsolation();
    try {
        const double currentWidth = PageHost().ActualWidth();
        const double currentHeight = PageHost().ActualHeight();
        if (currentWidth <= 0.0 || currentHeight <= 0.0) {
            return;
        }

        double targetWidth = currentWidth;
        double targetHeight = currentHeight;
        if (navigationWindowResizePrepared_) {
            const UINT dpi = std::max<UINT>(96U, GetDpiForWindow(windowHandle()));
            const double dipPerPixel = 96.0 / static_cast<double>(dpi);
            targetWidth = std::max(
                1.0,
                currentWidth +
                    (static_cast<double>(navigationResizeTarget_.width) -
                     static_cast<double>(navigationResizeStart_.width)) *
                        dipPerPixel);
            targetHeight = std::max(
                1.0,
                currentHeight +
                    (static_cast<double>(navigationResizeTarget_.height) -
                     static_cast<double>(navigationResizeStart_.height)) *
                        dipPerPixel);
        }

        const FrameworkElement outgoing = open
            ? CaptureScrollViewer().as<FrameworkElement>()
            : SettingsScrollViewer().as<FrameworkElement>();
        const FrameworkElement incoming = open
            ? SettingsScrollViewer().as<FrameworkElement>()
            : CaptureScrollViewer().as<FrameworkElement>();
        navigationViewportsIsolated_ = true;
        outgoing.Width(currentWidth);
        outgoing.Height(currentHeight);
        incoming.Width(targetWidth);
        incoming.Height(targetHeight);
    } catch (...) {
        clearNavigationViewportIsolation();
    }
}

void MainWindow::clearNavigationViewportIsolation() noexcept {
    if (!navigationViewportsIsolated_) {
        return;
    }
    try {
        clearExplicitSize(CaptureScrollViewer());
    } catch (...) {
    }
    try {
        clearExplicitSize(SettingsScrollViewer());
    } catch (...) {
    }
    navigationViewportsIsolated_ = false;
}

void MainWindow::beginPageNavigationAnimation(bool open) {
    if (!navigationTransitionRunning_ || navigationTargetOpen_ != open) {
        return;
    }

    const UIElement outgoing = open
        ? CaptureScrollViewer().as<UIElement>()
        : SettingsScrollViewer().as<UIElement>();
    const UIElement incoming = open
        ? SettingsScrollViewer().as<UIElement>()
        : CaptureScrollViewer().as<UIElement>();

    try {
        if (!settingsLayoutPrewarmed_) {
            PageHost().UpdateLayout();
            settingsLayoutPrewarmed_ = true;
        }
        if (!ensureNavigationAnimationTemplates()) {
            completePageNavigation(open);
            return;
        }
        setNavigationCenterPoint(outgoing);
        setNavigationCenterPoint(incoming);
        const auto compositor = Microsoft::UI::Xaml::Media::CompositionTarget::
            GetCompositorForCurrentThread();

        const float outgoingDistance =
            open ? -navigationDistance : navigationDistance;
        const float incomingDistance = -outgoingDistance;
        outgoingTranslationAnimation_.InsertKeyFrame(0.0F, float3{});
        outgoingTranslationAnimation_.InsertKeyFrame(
            1.0F,
            float3{outgoingDistance, 0.0F, 0.0F},
            navigationExitEasing_);
        incomingTranslationAnimation_.InsertKeyFrame(
            0.0F, float3{incomingDistance, 0.0F, 0.0F});
        incomingTranslationAnimation_.InsertKeyFrame(
            1.0F, float3{}, navigationEnterEasing_);

        navigationBatch_ = compositor.CreateScopedBatch(
            CompositionBatchTypes::Animation);
        navigationBatchCompletedToken_ = navigationBatch_.Completed(
            [weak = get_weak(), open](
                [[maybe_unused]] IInspectable const& sender,
                [[maybe_unused]] CompositionBatchCompletedEventArgs const& args) {
                if (const auto self = weak.get();
                    self && self->navigationTransitionRunning_ &&
                    self->navigationTargetOpen_ == open) {
                    self->completePageNavigation(open);
                }
            });
        navigationBatchCompletionSubscribed_ = true;
        outgoing.StartAnimation(outgoingOpacityAnimation_);
        outgoing.StartAnimation(outgoingTranslationAnimation_);
        outgoing.StartAnimation(outgoingScaleAnimation_);
        incoming.StartAnimation(incomingOpacityAnimation_);
        incoming.StartAnimation(incomingTranslationAnimation_);
        incoming.StartAnimation(incomingScaleAnimation_);
        navigationBatch_.End();
        startNavigationWindowResizeAnimation();
    } catch (...) {
        stopPageNavigationAnimation();
        completePageNavigation(open);
    }
}

void MainWindow::stopPageNavigationAnimation() noexcept {
    try {
        if (navigationBatch_ && navigationBatchCompletionSubscribed_) {
            navigationBatch_.Completed(navigationBatchCompletedToken_);
        }
        const UIElement outgoing = navigationTargetOpen_
            ? CaptureScrollViewer().as<UIElement>()
            : SettingsScrollViewer().as<UIElement>();
        const UIElement incoming = navigationTargetOpen_
            ? SettingsScrollViewer().as<UIElement>()
            : CaptureScrollViewer().as<UIElement>();
        if (outgoingOpacityAnimation_) {
            outgoing.StopAnimation(outgoingOpacityAnimation_);
        }
        if (incomingOpacityAnimation_) {
            incoming.StopAnimation(incomingOpacityAnimation_);
        }
        if (outgoingTranslationAnimation_) {
            outgoing.StopAnimation(outgoingTranslationAnimation_);
        }
        if (incomingTranslationAnimation_) {
            incoming.StopAnimation(incomingTranslationAnimation_);
        }
        if (outgoingScaleAnimation_) {
            outgoing.StopAnimation(outgoingScaleAnimation_);
        }
        if (incomingScaleAnimation_) {
            incoming.StopAnimation(incomingScaleAnimation_);
        }
    } catch (...) {
    }
    navigationBatchCompletionSubscribed_ = false;
    navigationBatchCompletedToken_ = {};
    navigationBatch_ = nullptr;
}

} // namespace winrt::Rillshot::WinUI::implementation
