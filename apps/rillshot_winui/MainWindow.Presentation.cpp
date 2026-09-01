#include "pch.h"

#include "MainWindow.xaml.h"

#include "gui/GuiConfig.h"
#include "gui/GuiStrings.h"

#include <cmath>
#include <filesystem>
#include <limits>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Rillshot::WinUI::implementation {

std::optional<int> MainWindow::checkedInt(double value) noexcept {
    if (!std::isfinite(value) ||
        std::trunc(value) != value ||
        value < static_cast<double>((std::numeric_limits<int>::min)()) ||
        value > static_cast<double>((std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

std::optional<rillshot::gui::GuiConfig> MainWindow::readConfig() {
    const double values[] = {
        RegionXBox().Value(), RegionYBox().Value(),
        RegionWidthBox().Value(), RegionHeightBox().Value(),
        ScrollXBox().Value(), ScrollYBox().Value(),
        MaxFramesBox().Value(), WheelNotchesBox().Value(),
        HeaderHeightBox().Value()
    };
    for (const double value : values) {
        if (!checkedInt(value)) {
            return std::nullopt;
        }
    }

    rillshot::gui::GuiConfig config;
    config.region.x = static_cast<int>(values[0]);
    config.region.y = static_cast<int>(values[1]);
    config.region.width = static_cast<int>(values[2]);
    config.region.height = static_cast<int>(values[3]);
    config.scrollPoint.x = static_cast<int>(values[4]);
    config.scrollPoint.y = static_cast<int>(values[5]);
    config.maxFrames = static_cast<int>(values[6]);
    config.wheelNotches = static_cast<int>(values[7]);
    config.ignoreTopPx = static_cast<int>(values[8]);
    config.ignoreBottomPx = 0;
    config.outPath = OutputPathBox().Text().c_str();
    config.allowOverwrite =
        !confirmedOverwritePath_.empty() && config.outPath == confirmedOverwritePath_;

    switch (ScrollDirectionBox().SelectedIndex()) {
    case 0: config.direction = rillshot::core::ScrollDirection::Down; break;
    case 1: config.direction = rillshot::core::ScrollDirection::Up; break;
    default: return std::nullopt;
    }
    switch (BackendBox().SelectedIndex()) {
    case 0: config.backend = rillshot::session::BackendChoice::Auto; break;
    case 1: config.backend = rillshot::session::BackendChoice::Dxgi; break;
    case 2: config.backend = rillshot::session::BackendChoice::Gdi; break;
    default: return std::nullopt;
    }
    switch (ScrollModeBox().SelectedIndex()) {
    case 0:
        config.driver = rillshot::session::DriverChoice::Wheel;
        break;
    case 1:
        config.driver = rillshot::session::DriverChoice::Keyboard;
        config.keyboardKey = rillshot::input::KeyboardKey::Page;
        break;
    case 2:
        config.driver = rillshot::session::DriverChoice::Keyboard;
        config.keyboardKey = rillshot::input::KeyboardKey::Space;
        break;
    case 3:
        config.driver = rillshot::session::DriverChoice::Keyboard;
        config.keyboardKey = rillshot::input::KeyboardKey::Arrow;
        break;
    default:
        return std::nullopt;
    }
    return config;
}

std::optional<rillshot::core::RectI> MainWindow::selectedRegion() {
    const auto x = checkedInt(RegionXBox().Value());
    const auto y = checkedInt(RegionYBox().Value());
    const auto width = checkedInt(RegionWidthBox().Value());
    const auto height = checkedInt(RegionHeightBox().Value());
    if (!x || !y || !width || !height) {
        return std::nullopt;
    }
    const rillshot::core::RectI region{*x, *y, *width, *height};
    return region.isValid()
        ? std::optional<rillshot::core::RectI>{region}
        : std::nullopt;
}

std::optional<rillshot::core::PointI> MainWindow::selectedScrollPoint(
    const rillshot::core::RectI& region) {
    const auto x = checkedInt(ScrollXBox().Value());
    const auto y = checkedInt(ScrollYBox().Value());
    if (!x || !y ||
        *x < region.x || static_cast<long long>(*x) >= region.right() ||
        *y < region.y || static_cast<long long>(*y) >= region.bottom()) {
        return std::nullopt;
    }
    return rillshot::core::PointI{*x, *y};
}

void MainWindow::ConfigurationNumber_ValueChanged(
    [[maybe_unused]] NumberBox const& sender,
    [[maybe_unused]] NumberBoxValueChangedEventArgs const& eventArgs) {
    if (initialized_) {
        refreshConfigurationSummary();
    }
}

void MainWindow::OutputPath_TextChanged(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] TextChangedEventArgs const& eventArgs) {
    if (std::wstring(OutputPathBox().Text().c_str()) != confirmedOverwritePath_) {
        confirmedOverwritePath_.clear();
    }
    if (initialized_) {
        refreshConfigurationSummary();
    }
}

void MainWindow::CaptureBehavior_SelectionChanged(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] SelectionChangedEventArgs const& eventArgs) {
    if (initialized_) {
        refreshConfigurationSummary();
    }
}

void MainWindow::ScrollDirection_SelectionChanged(
    [[maybe_unused]] IInspectable const& sender,
    [[maybe_unused]] SelectionChangedEventArgs const& eventArgs) {
    if (initialized_) {
        // Direction is a capture behavior, not a coordinate authoring action.
        // Preserve an explicitly selected point and leave an empty point empty.
        refreshConfigurationSummary();
    }
}

void MainWindow::renderStage() {
    using rillshot::gui::CaptureUiStage;
    const auto stage = workflow_.stage();
    const bool ready = stage == CaptureUiStage::Ready;
    const bool capturing = stage == CaptureUiStage::Capturing;
    const bool outcome = stage == CaptureUiStage::Result || stage == CaptureUiStage::Recovery;

    if (!ready) {
        settingsOpen_ = false;
    }
    renderPage();
    ReadyPanel().Visibility(ready ? Visibility::Visible : Visibility::Collapsed);
    CapturingPanel().Visibility(capturing ? Visibility::Visible : Visibility::Collapsed);
    OutcomePanel().Visibility(outcome ? Visibility::Visible : Visibility::Collapsed);
    StopCaptureButton().IsEnabled(capturing && !workflow_.stopPending());
    updateOutcomeActions(lastOutputSaved_, lastOutputPath_);

    switch (stage) {
    case CaptureUiStage::Ready:
        refreshConfigurationSummary();
        break;
    case CaptureUiStage::Capturing:
        CaptureStateText().Text(
            workflow_.stopPending()
                ? L"正在停止"
                : (ScrollDirectionBox().SelectedIndex() == 1 ? L"正在向上截图" : L"正在向下截图"));
        break;
    case CaptureUiStage::Result:
        OutcomeTitle().Text(L"截图已完成");
        OutcomeSubtitle().Text(L"结果已保存");
        OutcomeGlyph().Glyph(L"\xE73E");
        OutcomeSummary().Text(workflow_.summary());
        break;
    case CaptureUiStage::Recovery:
        OutcomeTitle().Text(L"已安全停止");
        OutcomeSubtitle().Text(lastOutputSaved_
            ? L"已保存可用结果"
            : L"未生成图像");
        OutcomeGlyph().Glyph(L"\xE7BA");
        OutcomeSummary().Text(workflow_.summary());
        break;
    }
}

void MainWindow::refreshConfigurationSummary() {
    if (!initialized_) {
        return;
    }

    const auto headerHeight = checkedInt(HeaderHeightBox().Value());
    if (headerHeight && *headerHeight >= 0) {
        HeaderSummaryText().Text(
            *headerHeight == 0
                ? L"未设置"
                : std::to_wstring(*headerHeight) + L" 像素 · 只保留一次");
    } else {
        HeaderSummaryText().Text(L"请输入有效的页头高度");
    }
    ClearHeaderButton().IsEnabled(headerHeight && *headerHeight > 0);

    try {
        const std::filesystem::path path(OutputPathBox().Text().c_str());
        const std::wstring fileName = path.filename().wstring();
        OutputSummaryText().Text(
            fileName.empty() ? L"尚未选择输出文件" : fileName);
    } catch (...) {
        OutputSummaryText().Text(L"输出路径无法解析");
    }

    const bool ready = workflow_.stage() == rillshot::gui::CaptureUiStage::Ready;
    const auto region = selectedRegion();
    PickScrollPointButton().IsEnabled(region.has_value());
    PickHeaderBoundaryButton().IsEnabled(region.has_value());
    if (!region) {
        RegionSummaryText().Text(L"点击选择区域");
        ScrollSummaryText().Text(L"请先选择区域");
        ExactPositionSummaryText().Text(L"尚未选择截图区域与滚动点");
        StartCaptureButton().IsEnabled(false);
        if (ready) {
            ReadyStateText().Text(L"请先选择截图区域");
            ReadyStateGlyph().Glyph(L"\xE7BA");
        }
        return;
    }

    RegionSummaryText().Text(
        std::to_wstring(region->width) + L" × " +
        std::to_wstring(region->height));
    const auto scrollX = checkedInt(ScrollXBox().Value());
    const auto scrollY = checkedInt(ScrollYBox().Value());
    const auto scrollPoint = selectedScrollPoint(*region);
    const std::wstring regionCoordinates =
        L"区域 (" + std::to_wstring(region->x) + L", " +
        std::to_wstring(region->y) + L")";
    if (!scrollPoint) {
        const bool coordinatesPresent = scrollX.has_value() && scrollY.has_value();
        ScrollSummaryText().Text(
            coordinatesPresent ? L"滚动点不在区域内" : L"点击选择滚动点");
        ExactPositionSummaryText().Text(
            regionCoordinates + (coordinatesPresent
                ? L" · 滚动点不在区域内"
                : L" · 滚动点未选择"));
        StartCaptureButton().IsEnabled(false);
        if (ready) {
            ReadyStateText().Text(
                coordinatesPresent ? L"请重新选择滚动点" : L"请选择滚动点");
            ReadyStateGlyph().Glyph(L"\xE7BA");
        }
        return;
    }

    ScrollSummaryText().Text(
        std::to_wstring(scrollPoint->x) + L", " +
        std::to_wstring(scrollPoint->y));
    ExactPositionSummaryText().Text(
        regionCoordinates + L" · 滚动点 (" +
        std::to_wstring(scrollPoint->x) + L", " +
        std::to_wstring(scrollPoint->y) + L")");

    const auto config = readConfig();
    if (!config) {
        CaptureBehaviorSummaryText().Text(L"请输入有效的捕获数值");
        StartCaptureButton().IsEnabled(false);
        if (ready) {
            ReadyStateText().Text(L"请输入有效的截图设置");
            ReadyStateGlyph().Glyph(L"\xE7BA");
        }
        return;
    }
    const wchar_t* backend = L"自动";
    switch (config->backend) {
    case rillshot::session::BackendChoice::Auto: backend = L"自动"; break;
    case rillshot::session::BackendChoice::Dxgi: backend = L"高性能"; break;
    case rillshot::session::BackendChoice::Gdi: backend = L"兼容模式"; break;
    }
    const wchar_t* scrollMode = L"鼠标滚轮";
    if (config->driver == rillshot::session::DriverChoice::Keyboard) {
        switch (config->keyboardKey) {
        case rillshot::input::KeyboardKey::Page:
            scrollMode = config->direction == rillshot::core::ScrollDirection::Up
                ? L"Page Up" : L"Page Down";
            break;
        case rillshot::input::KeyboardKey::Space:
            scrollMode = config->direction == rillshot::core::ScrollDirection::Up
                ? L"Shift + 空格" : L"空格键";
            break;
        case rillshot::input::KeyboardKey::Arrow:
            scrollMode = config->direction == rillshot::core::ScrollDirection::Up
                ? L"方向上键" : L"方向下键";
            break;
        }
    }
    const wchar_t* direction = config->direction == rillshot::core::ScrollDirection::Up
        ? L"向上" : L"向下";
    CaptureBehaviorSummaryText().Text(
        std::wstring(backend) + L" · " + direction + L" · " + scrollMode + L" · " +
        std::to_wstring(config->maxFrames) + L" 帧 · " +
        std::to_wstring(config->wheelNotches) + L" 步");

    const auto validation = rillshot::gui::validateGuiConfig(*config);
    StartCaptureButton().IsEnabled(ready && validation.ok);
    if (validation.ok) {
        if (ready) {
            ReadyStateText().Text(L"可以开始截图");
            ReadyStateGlyph().Glyph(L"\xE73E");
        }
    } else if (ready) {
        ReadyStateText().Text(rillshot::gui::validationMessageZh(validation));
        ReadyStateGlyph().Glyph(L"\xE7BA");
    }
}

void MainWindow::updateOutcomeActions(bool outputSaved, const std::wstring& outputPath) {
    OpenOutputButton().IsEnabled(outputSaved);
    RevealOutputButton().IsEnabled(outputSaved);
    CopyOutputPathButton().IsEnabled(outputSaved);
    OutcomePathText().Text(outputSaved ? outputPath : L"未生成可用图像");
}

void MainWindow::showInfo(
    InfoBarSeverity severity,
    const std::wstring& title,
    const std::wstring& message) {
    if (!settingsOpen_ && !navigationTargetOpen_) {
        // The compact capture surface must remain stable: routine success and
        // progress already have visible state in their owning controls, while
        // warnings/errors reuse the current stage's status line without
        // inserting another row above the page.
        ShellInfoBar().IsOpen(false);
        if (severity == InfoBarSeverity::Success ||
            severity == InfoBarSeverity::Informational) {
            return;
        }

        using rillshot::gui::CaptureUiStage;
        const std::wstring detail = message.empty()
            ? title
            : title + L" · " + message;
        switch (workflow_.stage()) {
        case CaptureUiStage::Ready:
            ReadyStateText().Text(detail);
            ReadyStateGlyph().Glyph(L"\xE7BA");
            break;
        case CaptureUiStage::Capturing:
            CaptureStateText().Text(title);
            CaptureDestinationText().Text(message);
            break;
        case CaptureUiStage::Result:
        case CaptureUiStage::Recovery:
            OutcomeSubtitle().Text(detail);
            OutcomeGlyph().Glyph(L"\xE7BA");
            break;
        }
        return;
    }

    ShellInfoBar().Severity(severity);
    ShellInfoBar().Title(title);
    ShellInfoBar().Message(message);
    ShellInfoBar().IsOpen(true);
}

} // namespace winrt::Rillshot::WinUI::implementation
