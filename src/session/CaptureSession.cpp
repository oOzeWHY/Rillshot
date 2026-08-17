#include "session/CaptureSession.h"

#include "capture/ICaptureBackend.h"
#include "output/WicImageWriter.h"
#include "platform/WinUtf.h"
#include "session/SessionCaptureSupport.h"
#include "session/SessionDiagnostics.h"
#include "session/SessionInputSupport.h"
#include "session/SessionOutputSupport.h"
#include "stitch/StitchEngine.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace rillshot::session {
namespace {

using rillshot::capture::CaptureFrame;
using rillshot::core::Image;
using rillshot::core::Status;
using rillshot::core::StopReason;
using rillshot::input::ScrollRequest;
using rillshot::output::ImageFormat;
using rillshot::output::WicImageWriter;
using rillshot::stitch::MatchOptions;
using rillshot::stitch::StitchEngine;
using detail::backendName;
using detail::advanceScrollSafely;
using detail::cancellationRequested;
using detail::driverName;
using detail::JsonlLogger;
using detail::keyboardKeyName;
using detail::makeBackends;
using detail::makeDriver;
using detail::comparisonFramePathFor;
using detail::formatFromPath;
using detail::initialResultFrom;
using detail::partialPathFor;
using detail::q;
using detail::writeImageNoThrow;
using detail::waitForStableFrame;
using detail::stopReasonForScrollFailure;

} // namespace

rillshot::core::Status validateCaptureOutputCollisionPolicy(
    const CaptureSessionOptions& options) {
    return detail::validateOutputCollisionPolicy(options);
}

CaptureSessionResult CaptureSession::run(const CaptureSessionOptions& options) {
    CaptureSessionResult result;

    const auto validation = validateCaptureSessionOptions(options);
    if (!validation.ok) {
        result.message = validation.message;
        result.stopReason = StopReason::CaptureFailed;
        return result;
    }

    const auto collisionValidation = validateCaptureOutputCollisionPolicy(options);
    if (!collisionValidation.ok) {
        result.message = collisionValidation.message;
        result.stopReason = StopReason::CaptureFailed;
        return result;
    }

    JsonlLogger logger(options.outPath, options.allowOverwrite);

    if (!logger.ok()) {
        std::cerr << "Warning: JSONL diagnostics unavailable: " << logger.error() << "\n";
    }

    {
        std::ostringstream fields;
        fields << "\"region\":{"
               << "\"x\":" << options.region.x
               << ",\"y\":" << options.region.y
               << ",\"width\":" << options.region.width
               << ",\"height\":" << options.region.height
               << "},\"scrollPoint\":{"
               << "\"x\":" << options.scrollPoint.x
               << ",\"y\":" << options.scrollPoint.y
               << "},\"backend\":" << q(backendName(options.backend))
               << ",\"driver\":" << q(driverName(options.driver))
               << ",\"direction\":" << q(rillshot::core::toString(options.direction))
               << ",\"maxFrames\":" << options.maxFrames
               << ",\"ignoreTopPx\":" << options.ignoreTopPx
               << ",\"ignoreBottomPx\":" << options.ignoreBottomPx
               << ",\"hardStitchConfidenceFloor\":" << std::fixed << std::setprecision(6) << options.hardStitchConfidenceFloor
               << ",\"maxAssembledImageBytes\":" << options.maxAssembledImageBytes
               << ",\"stopOnLowConfidenceSeams\":" << (options.stopOnLowConfidenceSeams ? "true" : "false");
        logger.event("session_start", fields.str());
    }

    auto backends = makeBackends(options.backend);
    auto driver = makeDriver(options.driver);

    CaptureFrame first;
    // The GUI minimizes immediately before the session starts. Capturing the
    // first frame without stabilization can preserve an unpainted/blank
    // transition frame and then reject the first real frame as an unreliable
    // seam. Stabilize the baseline exactly as we stabilize post-scroll frames.
    auto status = waitForStableFrame(backends, options, first, logger, "initial");
    if (!status.ok) {
        result.message = status.message;
        result.stopReason = status.code == "session-cancelled"
            ? StopReason::UserStopped
            : StopReason::CaptureFailed;
        result.ok = rillshot::core::isGracefulStop(result.stopReason);
        logger.event("stop", "\"reason\":" + q(rillshot::core::toString(result.stopReason)) + ",\"message\":" + q(result.message));
        result.diagnosticsSaved = logger.ok();
        return result;
    }

    const bool initialFrameUnstable = first.unstable;
    Image previousFrame = std::move(first.image);
    WicImageWriter writer;
    Image assembled;
    try {
        assembled = initialResultFrom(
            previousFrame,
            options.ignoreTopPx,
            options.ignoreBottomPx,
            options.direction);
    } catch (const std::exception& ex) {
        result.stopReason = StopReason::CaptureFailed;
        result.message = std::string("could not initialize stitched image: ") + ex.what();
        logger.event("assemble_failed", "\"message\":" + q(result.message));
        const auto recoveryStatus = writeImageNoThrow(
            writer,
            previousFrame,
            options.outPath,
            formatFromPath(options.outPath),
            options.allowOverwrite);
        result.outputSaved = recoveryStatus.ok;
        if (!recoveryStatus.ok) {
            logger.event("write_failed", "\"message\":" + q(recoveryStatus.message));
        }
        logger.event(
            "stop",
            "\"reason\":" + q(rillshot::core::toString(result.stopReason)) +
                ",\"message\":" + q(result.message) +
                ",\"ok\":false" +
                ",\"outputSaved\":" + (result.outputSaved ? "true" : "false") +
                ",\"framesCaptured\":1,\"seams\":0");
        result.framesCaptured = 1;
        result.diagnosticsSaved = logger.ok();
        return result;
    }
    result.framesCaptured = 1;

    if (initialFrameUnstable) {
        result.stopReason = StopReason::UnstableTooLong;
        result.message =
            "initial frame remained unstable; reliable stitching was not started";
        logger.event(
            "unstable_frame_rejected",
            "\"phase\":\"initial\",\"message\":" + q(result.message));
    }

    StitchEngine stitcher;

    const auto preserveComparisonFrame = [&](const Image& image) {
        const auto path = comparisonFramePathFor(options.outPath);
        const auto comparisonStatus = writeImageNoThrow(
            writer, image, path, ImageFormat::Png, options.allowOverwrite);
        if (comparisonStatus.ok) {
            result.comparisonFrameSaved = true;
            result.comparisonFramePath = path;
            try {
                logger.event("comparison_frame_saved", "\"path\":" + q(rillshot::platform::wideToUtf8(path)));
            } catch (const std::exception& ex) {
                logger.event("path_encoding_failed", "\"message\":" + q(ex.what()));
            }
        } else {
            logger.event("comparison_frame_write_failed", "\"message\":" + q(comparisonStatus.message));
        }
    };

    const int finalFixedRows = options.direction == rillshot::core::ScrollDirection::Down
        ? options.ignoreBottomPx
        : options.ignoreTopPx;
    bool partialWrittenBySession = false;
    for (int frameIndex = 1;
         !initialFrameUnstable && frameIndex < options.maxFrames;
         ++frameIndex) {
        if (cancellationRequested(options)) {
            result.stopReason = StopReason::UserStopped;
            result.message = "user stopped capture";
            logger.event("cancel_requested");
            break;
        }

        ScrollRequest request;
        request.point = options.scrollPoint;
        request.notches = options.wheelNotches;
        request.keyRepeats = options.keyRepeats;
        request.keyboardKey = options.keyboardKey;
        request.down = options.direction == rillshot::core::ScrollDirection::Down;

        status = advanceScrollSafely(*driver, request);
        if (!status.ok) {
            result.stopReason = stopReasonForScrollFailure(status);
            result.message = status.message;
            logger.event("scroll_failed", "\"code\":" + q(status.code) + ",\"message\":" + q(status.message));
            break;
        }
        {
            std::ostringstream fields;
            fields << "\"driver\":" << q(driverName(options.driver));
            fields << ",\"direction\":" << q(rillshot::core::toString(options.direction));
            if (options.driver == DriverChoice::Keyboard) {
                fields << ",\"key\":" << q(keyboardKeyName(options.keyboardKey))
                       << ",\"repeats\":" << options.keyRepeats;
            } else if (options.driver == DriverChoice::Wheel) {
                fields << ",\"notches\":" << options.wheelNotches;
            }
            logger.event("scroll_sent", fields.str());
        }

        CaptureFrame current;
        status = waitForStableFrame(backends, options, current, logger, "after_scroll");
        if (!status.ok) {
            result.stopReason = status.code == "session-cancelled"
                ? StopReason::UserStopped
                : StopReason::CaptureFailed;
            result.message = status.message;
            break;
        }
        ++result.framesCaptured;

        if (current.unstable) {
            result.stopReason = StopReason::UnstableTooLong;
            result.message =
                "frame remained unstable after scrolling; partial result preserved";
            logger.event(
                "unstable_frame_rejected",
                "\"phase\":\"after_scroll\",\"message\":" + q(result.message));
            preserveComparisonFrame(current.image);
            break;
        }

        MatchOptions matchOptions;
        matchOptions.direction = options.direction;
        matchOptions.minOverlapPx = std::max(16, options.region.height / 8);
        matchOptions.maxOverlapPx = options.region.height;
        matchOptions.ignoreTopPx = options.ignoreTopPx;
        matchOptions.ignoreBottomPx = options.ignoreBottomPx;
        matchOptions.xStep = 8;
        matchOptions.yStep = 3;

        rillshot::stitch::MatchResult match;
        try {
            match = stitcher.findVerticalOverlap(previousFrame, current.image, matchOptions);
        } catch (const std::exception& ex) {
            result.stopReason = StopReason::StitchUnreliable;
            result.message = std::string("stitch matcher raised an exception: ") + ex.what();
            logger.event("stitch_failed", "\"message\":" + q(result.message));
            break;
        } catch (...) {
            result.stopReason = StopReason::StitchUnreliable;
            result.message = "stitch matcher raised an unknown exception";
            logger.event("stitch_failed", "\"message\":" + q(result.message));
            break;
        }
        {
            std::ostringstream fields;
            fields << "\"frame\":" << frameIndex
                   << ",\"ok\":" << (match.ok ? "true" : "false")
                   << ",\"overlapPx\":" << match.overlapPx
                   << ",\"newContentStartY\":" << match.newContentStartY
                   << ",\"newContentEndYExclusive\":" << match.newContentEndYExclusive
                   << ",\"newContentHeight\":" << match.newContentHeight
                   << ",\"confidence\":" << std::fixed << std::setprecision(6) << match.confidence
                   << ",\"ambiguity\":" << std::fixed << std::setprecision(6) << match.ambiguity
                   << ",\"lowInformation\":" << (match.lowInformation ? "true" : "false")
                   << ",\"lowConfidence\":" << (match.lowConfidence ? "true" : "false")
                   << ",\"coarseCandidateStepPx\":" << match.coarseCandidateStepPx
                   << ",\"coarseCandidatesRetained\":" << match.coarseCandidatesRetained
                   << ",\"candidatesEvaluated\":" << match.candidatesEvaluated
                   << ",\"primaryCandidatesRefined\":" << match.primaryCandidatesRefined
                   << ",\"diverseCandidatesRefined\":" << match.diverseCandidatesRefined
                   << ",\"refinedCandidatesEvaluated\":" << match.refinedCandidatesEvaluated
                   << ",\"refinedSamplesEvaluated\":" << match.refinedSamplesEvaluated
                   << ",\"method\":" << q(match.method)
                   << ",\"unstable\":" << (current.unstable ? "true" : "false")
                   << ",\"message\":" << q(match.message);
            logger.event("seam", fields.str());
        }

        if (!match.ok || match.confidence < options.hardStitchConfidenceFloor) {
            result.stopReason = StopReason::StitchUnreliable;
            result.message = "stitch confidence below hard floor; partial result preserved";
            preserveComparisonFrame(current.image);
            break;
        }

        if (options.stopOnLowConfidenceSeams && match.lowConfidence) {
            result.stopReason = StopReason::StitchUnreliable;
            result.message = "low-confidence seam rejected; partial result preserved";
            preserveComparisonFrame(current.image);
            break;
        }

        if (match.newContentHeight <= 2) {
            result.stopReason = StopReason::NoVisualProgress;
            result.message = "no visual progress detected";
            break;
        }

        const auto projectedHeight =
            static_cast<std::int64_t>(assembled.height()) +
            static_cast<std::int64_t>(match.newContentHeight) +
            static_cast<std::int64_t>(finalFixedRows);
        if (!imageSizeFitsByteBudget(
                assembled.width(), projectedHeight,
                options.maxAssembledImageBytes)) {
            result.stopReason = StopReason::OutputLimitReached;
            result.message =
                "stitched image memory limit reached; reliable partial result preserved";
            logger.event(
                "output_budget_reached",
                "\"projectedHeight\":" + std::to_string(projectedHeight) +
                    ",\"maxAssembledImageBytes\":" +
                    std::to_string(options.maxAssembledImageBytes));
            preserveComparisonFrame(current.image);
            break;
        }

        try {
            if (options.direction == rillshot::core::ScrollDirection::Down) {
                assembled.appendRowsFrom(
                    current.image,
                    match.newContentStartY,
                    match.newContentEndYExclusive);
            } else {
                assembled.prependRowsFrom(
                    current.image,
                    match.newContentStartY,
                    match.newContentEndYExclusive);
            }
        } catch (const std::exception& ex) {
            result.stopReason = StopReason::CaptureFailed;
            result.message = std::string("could not assemble stitched rows: ") + ex.what();
            logger.event("assemble_rows_failed", "\"message\":" + q(result.message));
            break;
        }
        previousFrame = std::move(current.image);
        ++result.seams;

        const auto partialPath = partialPathFor(options.outPath);
        const auto partialStatus = writeImageNoThrow(
            writer,
            assembled,
            partialPath,
            ImageFormat::Png,
            mayOverwritePartialCheckpoint(
                options.allowOverwrite, partialWrittenBySession));
        if (!partialStatus.ok) {
            logger.event("partial_write_failed", "\"message\":" + q(partialStatus.message));
        } else {
            partialWrittenBySession = true;
        }
    }

    if (result.message.empty()) {
        result.stopReason = StopReason::MaxFramesReached;
        result.message = "max frames reached";
    }

    if (finalFixedRows > 0) {
        try {
            if (options.direction == rillshot::core::ScrollDirection::Down) {
                assembled.appendRowsFrom(
                    previousFrame,
                    previousFrame.height() - options.ignoreBottomPx,
                    previousFrame.height());
            } else {
                assembled.prependRowsFrom(previousFrame, 0, options.ignoreTopPx);
            }
        } catch (const std::exception& ex) {
            result.stopReason = StopReason::CaptureFailed;
            result.message = std::string("could not restore fixed border rows: ") + ex.what();
            logger.event("finalize_fixed_rows_failed", "\"message\":" + q(result.message));
        }
    }

    const auto writeStatus = writeImageNoThrow(
        writer,
        assembled,
        options.outPath,
        formatFromPath(options.outPath),
        options.allowOverwrite);
    if (!writeStatus.ok) {
        result.ok = false;
        result.outputSaved = false;
        result.stopReason = StopReason::CaptureFailed;
        result.message = writeStatus.message;
        logger.event("write_failed", "\"message\":" + q(writeStatus.message));
    } else {
        result.outputSaved = true;
        result.ok = rillshot::core::isGracefulStop(result.stopReason);
        try {
            logger.event("write_done", "\"path\":" + q(rillshot::platform::wideToUtf8(options.outPath)));
        } catch (const std::exception& ex) {
            logger.event("path_encoding_failed", "\"message\":" + q(ex.what()));
        }
    }

    if (shouldDeletePartialCheckpoint(
            partialWrittenBySession, result.outputSaved, result.stopReason)) {
        std::error_code cleanupError;
        const bool removed = std::filesystem::remove(
            std::filesystem::path(partialPathFor(options.outPath)), cleanupError);
        if (cleanupError) {
            logger.event(
                "partial_cleanup_failed",
                "\"message\":" + q(cleanupError.message()));
        } else if (removed) {
            logger.event("partial_cleanup_done");
        }
    }

    logger.event(
        "stop",
        "\"reason\":" + q(rillshot::core::toString(result.stopReason)) +
            ",\"message\":" + q(result.message) +
            ",\"ok\":" + (result.ok ? "true" : "false") +
            ",\"outputSaved\":" + (result.outputSaved ? "true" : "false") +
            ",\"comparisonFrameSaved\":" + (result.comparisonFrameSaved ? "true" : "false") +
            ",\"framesCaptured\":" + std::to_string(result.framesCaptured) +
            ",\"seams\":" + std::to_string(result.seams));

    result.diagnosticsSaved = logger.ok();

    return result;
}

} // namespace rillshot::session
