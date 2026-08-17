#include "core/Image.h"
#include "core/ImageMetrics.h"
#include "core/Types.h"
#include "gui/CaptureSummary.h"
#include "gui/CaptureWorkflow.h"
#include "gui/GuiConfig.h"
#include "gui/GuiStrings.h"
#include "gui/SelectionVisuals.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
#include <string_view>

namespace {

int fail(const char* message) {
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

int testNearBlackDetection() {
    rillshot::core::Image black(128, 96);
    for (int y = 0; y < black.height(); ++y) {
        std::fill(black.row(y), black.row(y) + black.stride(), std::uint8_t{0});
    }
    const auto summary = rillshot::core::summarizeImage(black);
    if (!summary.suspiciouslyNearBlack || summary.meanLuma != 0.0 || summary.darkPixelRatio != 1.0) {
        return fail("uniform black frame should be marked suspiciously near-black");
    }
    return EXIT_SUCCESS;
}

int testContentFrameNotBlank() {
    rillshot::core::Image image(128, 96);
    image.fillTestPattern();
    const auto summary = rillshot::core::summarizeImage(image);
    if (summary.suspiciouslyNearBlack || summary.meanLuma <= 0.05 || summary.lumaStdDev <= 0.01) {
        return fail("content test frame should not be classified as near-black");
    }
    return EXIT_SUCCESS;
}

int testDifferenceOptionsHandleExtremeBorders() {
    rillshot::core::Image dark(8, 8);
    rillshot::core::Image light(8, 8);
    std::fill(dark.bytes().begin(), dark.bytes().end(), std::uint8_t{0});
    std::fill(light.bytes().begin(), light.bytes().end(), std::uint8_t{255});

    rillshot::core::DifferenceOptions options;
    options.ignoreBottomPx = (std::numeric_limits<int>::min)();
    const double fullDifference =
        rillshot::core::meanAbsDiffRatio(dark, light, options);
    if (fullDifference != 1.0) {
        return fail("a negative bottom exclusion should clamp to zero without overflow");
    }

    options.ignoreTopPx = (std::numeric_limits<int>::max)();
    options.ignoreBottomPx = (std::numeric_limits<int>::max)();
    if (rillshot::core::meanAbsDiffRatio(dark, light, options) != 0.0) {
        return fail("fully excluded image rows should produce an empty sampled difference");
    }
    return EXIT_SUCCESS;
}

int testDifferenceSamplingUsesASecondPhase() {
    rillshot::core::Image first(24, 18);
    rillshot::core::Image second(24, 18);
    std::fill(first.bytes().begin(), first.bytes().end(), std::uint8_t{0});
    std::fill(second.bytes().begin(), second.bytes().end(), std::uint8_t{0});

    // Deliberately change only the half-step lattice. A single top-left phase
    // misses every changed pixel; the production two-phase sampler must not.
    for (int y = 3; y < second.height(); y += 6) {
        auto* row = second.row(y);
        for (int x = 6; x < second.width(); x += 12) {
            row[x * 4 + 0] = 255;
            row[x * 4 + 1] = 255;
            row[x * 4 + 2] = 255;
        }
    }

    rillshot::core::DifferenceOptions options;
    options.xStep = 12;
    options.yStep = 6;
    options.samplePhases = 1;
    if (rillshot::core::meanAbsDiffRatio(first, second, options) != 0.0) {
        return fail("single-phase control should miss the offset lattice");
    }
    options.samplePhases = 2;
    if (rillshot::core::meanAbsDiffRatio(first, second, options) <= 0.0) {
        return fail("two-phase stability sampling must detect offset-lattice changes");
    }
    return EXIT_SUCCESS;
}

int testAdaptiveSelectionCursorTone() {
    constexpr int width = 24;
    constexpr int height = 24;
    constexpr std::size_t stride = static_cast<std::size_t>(width) * 4U;
    std::vector<std::uint8_t> pixels(stride * static_cast<std::size_t>(height), 255U);

    using rillshot::gui::selection::CursorOuterTone;
    using rillshot::gui::selection::chooseCursorOuterTone;
    if (chooseCursorOuterTone(pixels, width, height, stride, 12, 12) !=
        CursorOuterTone::Black) {
        return fail("cursor should choose a black outer stroke on a light backdrop");
    }

    for (std::size_t offset = 0; offset < pixels.size(); offset += 4U) {
        pixels[offset] = 8U;
        pixels[offset + 1U] = 8U;
        pixels[offset + 2U] = 8U;
        pixels[offset + 3U] = 255U;
    }
    if (chooseCursorOuterTone(pixels, width, height, stride, 12, 12) !=
        CursorOuterTone::White) {
        return fail("cursor should choose a white outer stroke on a dark backdrop");
    }

    for (int y = 6; y <= 18; ++y) {
        for (int x = 6; x <= 18; ++x) {
            const std::size_t offset =
                static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4U;
            pixels[offset] = 244U;
            pixels[offset + 1U] = 244U;
            pixels[offset + 2U] = 244U;
        }
    }
    if (chooseCursorOuterTone(pixels, width, height, stride, 12, 12) !=
        CursorOuterTone::Black) {
        return fail("cursor tone should follow the local backdrop instead of the whole screen");
    }

    for (std::size_t offset = 0; offset < pixels.size(); offset += 4U) {
        pixels[offset] = 200U;
        pixels[offset + 1U] = 200U;
        pixels[offset + 2U] = 200U;
    }
    if (chooseCursorOuterTone(pixels, width, height, stride, 12, 12) !=
        CursorOuterTone::Black) {
        return fail("an undimmed medium-light backdrop should choose a black outer stroke");
    }
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4U) {
        pixels[offset] = static_cast<std::uint8_t>(pixels[offset] * 62U / 100U);
        pixels[offset + 1U] = static_cast<std::uint8_t>(pixels[offset + 1U] * 62U / 100U);
        pixels[offset + 2U] = static_cast<std::uint8_t>(pixels[offset + 2U] * 62U / 100U);
    }
    if (chooseCursorOuterTone(pixels, width, height, stride, 12, 12) !=
        CursorOuterTone::White) {
        return fail("cursor tone should use the actually displayed dimmed backdrop");
    }

    if (chooseCursorOuterTone({}, width, height, stride, 12, 12) !=
        CursorOuterTone::White) {
        return fail("missing snapshot data should use the fail-visible white cursor tone");
    }
    if (chooseCursorOuterTone(
            {}, 1, 2, (std::numeric_limits<std::size_t>::max)(), 0, 0) !=
        CursorOuterTone::White) {
        return fail("overflowing snapshot dimensions should fail safely");
    }
    return EXIT_SUCCESS;
}

int testSafeGuiDefaultsAndChineseLabels() {
    const rillshot::gui::GuiConfig config;
    if (config.wheelNotches != 1 || config.maxFrames != 12) {
        return fail("GUI should default to a conservative one-notch scroll and 12 frames");
    }
    if (std::wstring_view(rillshot::gui::stopReasonZh(
            rillshot::core::StopReason::StitchUnreliable)).empty()) {
        return fail("Chinese stop-reason label should not be empty");
    }
    if (rillshot::gui::sessionMessageZh(
            "stitch confidence below hard floor; partial result preserved").empty()) {
        return fail("known stitch failure should have a Chinese explanation");
    }
    if (rillshot::gui::sessionMessageZh(
            "initial frame remained unstable; reliable stitching was not started").empty() ||
        rillshot::gui::sessionMessageZh(
            "frame remained unstable after scrolling; partial result preserved").empty() ||
        rillshot::gui::sessionMessageZh(
            "stitched image memory limit reached; reliable partial result preserved").empty()) {
        return fail("unstable-frame rejection should have Chinese explanations");
    }
    return EXIT_SUCCESS;
}

int testCaptureSummaryPreservesReleaseEvidence() {
    rillshot::session::CaptureSessionResult result;
    result.ok = false;
    result.outputSaved = true;
    result.diagnosticsSaved = true;
    result.comparisonFrameSaved = true;
    result.stopReason = rillshot::core::StopReason::StitchUnreliable;
    result.framesCaptured = 2;
    result.seams = 0;
    result.message = "low-confidence seam rejected; partial result preserved";
    result.comparisonFramePath = L"result.png.comparison.png";

    const auto summary = rillshot::gui::buildCaptureSummaryZh(result, L"result.png");
    for (const std::wstring_view required : {
             L"需要检查结果", L"2 帧", L"0 条接缝", L"result.png",
             L"result.png.comparison.png", L"result.png.jsonl", L"建议"}) {
        if (summary.find(required) == std::wstring::npos) {
            return fail("capture summary omitted release-relevant evidence");
        }
    }
    return EXIT_SUCCESS;
}

int testGracefulCancellationWithoutOutputIsNotReportedComplete() {
    rillshot::session::CaptureSessionResult result;
    result.ok = true;
    result.outputSaved = false;
    result.stopReason = rillshot::core::StopReason::UserStopped;
    result.message = "capture cancelled before the first frame";

    const auto summary = rillshot::gui::buildCaptureSummaryZh(result, L"result.png");
    if (summary.find(L"截图已完成") != std::wstring::npos ||
        summary.find(L"未保存输出图像") == std::wstring::npos) {
        return fail("a graceful cancellation without output must not be presented as completed");
    }
    return EXIT_SUCCESS;
}

int testGuiValidationIncludesPointerTarget() {
    rillshot::gui::GuiConfig config;
    if (!rillshot::gui::validateGuiConfig(config).ok) {
        return fail("default GUI configuration should validate");
    }
    config.allowOverwrite = true;
    if (!config.toSessionOptions().allowOverwrite) {
        return fail("explicit GUI overwrite confirmation should reach the shared session options");
    }
    config.scrollPoint = {config.region.x + config.region.width, config.region.y};
    const auto validation = rillshot::gui::validateGuiConfig(config);
    if (validation.ok || validation.code != "invalid-scroll-point") {
        return fail("GUI validation should reject a scroll point outside the capture region");
    }
    if (rillshot::gui::validationMessageZh(validation).find(L"滚轮作用点") == std::wstring::npos) {
        return fail("invalid scroll point should have a specific Chinese message");
    }
    return EXIT_SUCCESS;
}

int testCaptureWorkflowTransitions() {
    using rillshot::gui::CaptureUiStage;
    rillshot::gui::CaptureWorkflowModel workflow;

    if (workflow.stage() != CaptureUiStage::Ready || !workflow.beginCapture().ok ||
        workflow.stage() != CaptureUiStage::Capturing) {
        return fail("capture workflow should enter Capturing from a valid Ready state");
    }
    if (!workflow.requestStop().ok || !workflow.stopPending()) {
        return fail("capture workflow should expose a cooperative stop request");
    }
    if (workflow.updateConfig({}).ok || workflow.reset().ok) {
        return fail("capture workflow should reject mutation and reset during capture");
    }

    rillshot::session::CaptureSessionResult success;
    success.ok = true;
    success.outputSaved = true;
    success.diagnosticsSaved = true;
    success.stopReason = rillshot::core::StopReason::UserStopped;
    success.framesCaptured = 4;
    success.seams = 3;
    success.message = "user stopped capture";
    if (!workflow.completeCapture(success, L"result.png").ok ||
        workflow.stage() != CaptureUiStage::Result || workflow.stopPending() ||
        workflow.summary().find(L"result.png") == std::wstring::npos) {
        return fail("saved graceful capture should enter Result with an evidence summary");
    }
    if (!workflow.reset().ok || workflow.stage() != CaptureUiStage::Ready ||
        !workflow.summary().empty()) {
        return fail("capture workflow reset should return to a clean Ready state");
    }

    auto invalid = workflow.config();
    invalid.outPath = L"result.jpg";
    if (!workflow.updateConfig(invalid).ok || workflow.beginCapture().ok ||
        workflow.stage() != CaptureUiStage::Ready) {
        return fail("invalid configuration should keep the workflow in Ready");
    }

    invalid.outPath = L"result.png";
    if (!workflow.updateConfig(invalid).ok || !workflow.beginCapture().ok) {
        return fail("capture workflow should restart after configuration is corrected");
    }
    rillshot::session::CaptureSessionResult failure;
    failure.ok = false;
    failure.outputSaved = true;
    failure.stopReason = rillshot::core::StopReason::StitchUnreliable;
    failure.message = "low-confidence seam rejected; partial result preserved";
    if (!workflow.completeCapture(failure, L"result.png").ok ||
        workflow.stage() != CaptureUiStage::Recovery) {
        return fail("non-graceful capture should enter Recovery even when partial output is saved");
    }
    return EXIT_SUCCESS;
}

} // namespace

int main() {
    if (testNearBlackDetection() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testContentFrameNotBlank() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testDifferenceOptionsHandleExtremeBorders() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testDifferenceSamplingUsesASecondPhase() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testAdaptiveSelectionCursorTone() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testSafeGuiDefaultsAndChineseLabels() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testCaptureSummaryPreservesReleaseEvidence() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testGracefulCancellationWithoutOutputIsNotReportedComplete() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testGuiValidationIncludesPointerTarget() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testCaptureWorkflowTransitions() != EXIT_SUCCESS) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
