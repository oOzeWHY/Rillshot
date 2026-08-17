#include "TestSupport.h"

#include "core/Image.h"
#include "core/Json.h"
#include "core/Types.h"
#include "gui/GuiConfig.h"
#include "session/CaptureSession.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using rillshot::core::Image;
using rillshot::core::RectI;

static int testRectRejectsCoordinateOverflow() {
    RectI rectangle;
    rectangle.x = 2147483640;
    rectangle.y = 10;
    rectangle.width = 100;
    rectangle.height = 100;
    if (rectangle.isValid()) {
        return fail("RectI should reject right-edge overflow");
    }
    return EXIT_SUCCESS;
}

static int testImageRejectsInvalidDimensions() {
    try {
        Image invalid(-1, 20);
        (void)invalid;
    } catch (const std::invalid_argument&) {
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "unexpected exception type: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
    return fail("negative image dimensions should throw invalid_argument");
}

static int testStopReasonClassification() {
    using rillshot::core::StopReason;
    using rillshot::core::isGracefulStop;

    if (!isGracefulStop(StopReason::MaxFramesReached) ||
        !isGracefulStop(StopReason::UserStopped) ||
        !isGracefulStop(StopReason::NoVisualProgress)) {
        return fail("normal completion reasons should be graceful");
    }
    if (isGracefulStop(StopReason::ScrollRejected) ||
        isGracefulStop(StopReason::CaptureFailed) ||
        isGracefulStop(StopReason::StitchUnreliable) ||
        isGracefulStop(StopReason::UnstableTooLong) ||
        isGracefulStop(StopReason::OutputLimitReached)) {
        return fail("failure stop reasons should be non-graceful");
    }
    return EXIT_SUCCESS;
}

static int testJsonStringEscaping() {
    std::string input;
    input += '"';
    input += '\\';
    input += '\b';
    input += '\f';
    input += '\n';
    input += '\r';
    input += '\t';
    input += static_cast<char>(0x00);
    input += static_cast<char>(0x01);
    input += static_cast<char>(0x1F);
    input += " UTF-8: \xE4\xB8\xAD";

    const std::string expected =
        "\\\"\\\\\\b\\f\\n\\r\\t\\u0000\\u0001\\u001f UTF-8: \xE4\xB8\xAD";
    if (rillshot::core::escapeJsonString(input) != expected) {
        return fail("JSON escape output is incorrect");
    }
    return EXIT_SUCCESS;
}

static rillshot::session::CaptureSessionOptions makeValidSessionOptions() {
    rillshot::session::CaptureSessionOptions options;
    options.region = RectI{100, 100, 320, 240};
    options.scrollPoint = rillshot::core::PointI{260, 220};
    options.outPath = L"result.png";
    return options;
}

static int testSessionOptionsValidation() {
    using rillshot::session::BackendChoice;
    using rillshot::session::validateCaptureSessionOptions;

    const auto valid = makeValidSessionOptions();
    if (!validateCaptureSessionOptions(valid).ok) {
        return fail("default session options should validate");
    }

    auto invalid = valid;
    invalid.outPath = L"result.jpg";
    if (validateCaptureSessionOptions(invalid).ok) {
        return fail("unsupported output extensions must be rejected");
    }

    invalid = valid;
    invalid.scrollPoint = rillshot::core::PointI{420, 100};
    const auto invalidScrollPoint = validateCaptureSessionOptions(invalid);
    if (invalidScrollPoint.ok ||
        invalidScrollPoint.code != "invalid-scroll-point") {
        return fail("scroll points outside the capture region must be rejected");
    }

    invalid = valid;
    invalid.maxFrames = 0;
    if (validateCaptureSessionOptions(invalid).ok) {
        return fail("maxFrames=0 must be rejected");
    }

    invalid = valid;
    invalid.diffThreshold = std::numeric_limits<double>::quiet_NaN();
    if (validateCaptureSessionOptions(invalid).ok) {
        return fail("NaN thresholds must be rejected");
    }

    invalid = valid;
    invalid.ignoreTopPx = 120;
    invalid.ignoreBottomPx = 112;
    if (validateCaptureSessionOptions(invalid).ok) {
        return fail("ignored borders must leave enough content");
    }

    invalid = valid;
    invalid.backend = static_cast<BackendChoice>(999);
    if (validateCaptureSessionOptions(invalid).ok) {
        return fail("unknown backend values must be rejected");
    }

    invalid = valid;
    invalid.direction = static_cast<rillshot::core::ScrollDirection>(999);
    if (validateCaptureSessionOptions(invalid).ok) {
        return fail("unknown direction values must be rejected");
    }

    invalid = valid;
    invalid.maxAssembledImageBytes = 1024;
    const auto invalidBudget = validateCaptureSessionOptions(invalid);
    if (invalidBudget.ok || invalidBudget.code != "invalid-output-budget") {
        return fail("output budget must fit at least one complete capture frame");
    }
    return EXIT_SUCCESS;
}

static int testImageByteBudgetArithmetic() {
    using rillshot::session::imageSizeFitsByteBudget;
    if (!imageSizeFitsByteBudget(320, 240, 320ULL * 240ULL * 4ULL) ||
        imageSizeFitsByteBudget(320, 241, 320ULL * 240ULL * 4ULL) ||
        imageSizeFitsByteBudget(-1, 240, 1024) ||
        imageSizeFitsByteBudget(
            (std::numeric_limits<int>::max)(),
            (std::numeric_limits<std::int64_t>::max)(),
            (std::numeric_limits<std::uint64_t>::max)())) {
        return fail("image byte budget arithmetic must be exact and overflow-safe");
    }
    return EXIT_SUCCESS;
}

static int testPartialCheckpointOverwriteOwnership() {
    using rillshot::session::mayOverwritePartialCheckpoint;
    if (mayOverwritePartialCheckpoint(false, false)) {
        return fail("a session must not overwrite a pre-existing checkpoint");
    }
    if (!mayOverwritePartialCheckpoint(true, false) ||
        !mayOverwritePartialCheckpoint(false, true)) {
        return fail("authorized or session-owned checkpoints must be refreshable");
    }
    return EXIT_SUCCESS;
}

static int testPartialCheckpointCleanupPolicy() {
    using rillshot::core::StopReason;
    using rillshot::session::shouldDeletePartialCheckpoint;
    if (!shouldDeletePartialCheckpoint(true, true, StopReason::MaxFramesReached) ||
        !shouldDeletePartialCheckpoint(true, true, StopReason::UserStopped) ||
        !shouldDeletePartialCheckpoint(true, true, StopReason::NoVisualProgress)) {
        return fail("a graceful final output should remove its session checkpoint");
    }
    if (shouldDeletePartialCheckpoint(false, true, StopReason::MaxFramesReached) ||
        shouldDeletePartialCheckpoint(true, false, StopReason::MaxFramesReached) ||
        shouldDeletePartialCheckpoint(true, true, StopReason::StitchUnreliable) ||
        shouldDeletePartialCheckpoint(true, true, StopReason::CaptureFailed)) {
        return fail("foreign, failed, or recovery checkpoints must be preserved");
    }
    return EXIT_SUCCESS;
}

static int testGuiConfigMapsToSession() {
    rillshot::gui::GuiConfig config;
    config.region = RectI{-1200, 80, 900, 700};
    config.scrollPoint = rillshot::core::PointI{-750, 720};
    config.outPath = L"gui-result.bmp";
    config.backend = rillshot::session::BackendChoice::Gdi;
    config.driver = rillshot::session::DriverChoice::Keyboard;
    config.keyboardKey = rillshot::input::KeyboardKey::Space;
    config.direction = rillshot::core::ScrollDirection::Up;
    config.maxFrames = 31;
    config.wheelNotches = 7;
    config.ignoreTopPx = 42;
    config.ignoreBottomPx = 18;

    const auto options = config.toSessionOptions();
    if (options.region.x != -1200 || options.region.y != 80 ||
        options.region.width != 900 || options.region.height != 700 ||
        options.scrollPoint.x != -750 || options.scrollPoint.y != 720) {
        return fail("GUI coordinate mapping is incorrect");
    }
    if (options.outPath != L"gui-result.bmp" ||
        options.backend != rillshot::session::BackendChoice::Gdi ||
        options.driver != rillshot::session::DriverChoice::Keyboard ||
        options.keyboardKey != rillshot::input::KeyboardKey::Space ||
        options.direction != rillshot::core::ScrollDirection::Up ||
        options.maxFrames != 31 || options.wheelNotches != 7 ||
        options.keyRepeats != 7 || options.ignoreTopPx != 42 ||
        options.ignoreBottomPx != 18) {
        return fail("GUI option mapping is incorrect");
    }
    if (!rillshot::session::validateCaptureSessionOptions(options).ok) {
        return fail("mapped GUI options should pass session validation");
    }
    return EXIT_SUCCESS;
}

static int testDefaultScrollPointFollowsDirection() {
    const RectI region{100, 100, 900, 700};
    const auto down = rillshot::core::defaultScrollPoint(
        region, rillshot::core::ScrollDirection::Down);
    const auto up = rillshot::core::defaultScrollPoint(
        region, rillshot::core::ScrollDirection::Up);
    if (down.x != 550 || down.y != 760 || up.x != 550 || up.y != 140) {
        return fail("default scroll point does not follow direction");
    }

    const RectI shortRegion{5, 10, 40, 50};
    const auto shortDown = rillshot::core::defaultScrollPoint(
        shortRegion, rillshot::core::ScrollDirection::Down);
    const auto shortUp = rillshot::core::defaultScrollPoint(
        shortRegion, rillshot::core::ScrollDirection::Up);
    if (shortUp.y >= shortDown.y || shortUp.y < shortRegion.y ||
        shortDown.y >= shortRegion.bottom()) {
        return fail("short-region default points left their valid region");
    }

    const RectI twoRowRegion{0, 0, 2, 2};
    const auto twoRowDown = rillshot::core::defaultScrollPoint(
        twoRowRegion, rillshot::core::ScrollDirection::Down);
    const auto twoRowUp = rillshot::core::defaultScrollPoint(
        twoRowRegion, rillshot::core::ScrollDirection::Up);
    if (twoRowUp.y != 0 || twoRowDown.y != 1) {
        return fail("two-row default points must stay on opposite valid edges");
    }

    const RectI extremeInvalidRegion{
        (std::numeric_limits<int>::max)(),
        (std::numeric_limits<int>::max)(),
        (std::numeric_limits<int>::max)(),
        (std::numeric_limits<int>::max)()};
    const auto extremePoint = rillshot::core::defaultScrollPoint(
        extremeInvalidRegion, rillshot::core::ScrollDirection::Down);
    if (extremePoint.x != (std::numeric_limits<int>::max)() ||
        extremePoint.y != (std::numeric_limits<int>::max)()) {
        return fail("default scroll-point arithmetic should saturate invalid extreme input");
    }
    return EXIT_SUCCESS;
}

int main() {
    if (testRectRejectsCoordinateOverflow() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testImageRejectsInvalidDimensions() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testStopReasonClassification() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testJsonStringEscaping() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testSessionOptionsValidation() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testImageByteBudgetArithmetic() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testPartialCheckpointOverwriteOwnership() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testPartialCheckpointCleanupPolicy() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testGuiConfigMapsToSession() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testDefaultScrollPointFollowsDirection() != EXIT_SUCCESS) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
