#include "StitchTestFixtures.h"
#include "TestSupport.h"

#include "stitch/StitchEngine.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

using rillshot::stitch::MatchOptions;
using rillshot::stitch::StitchEngine;

static int testBasicVerticalOverlap() {
    constexpr int width = 320;
    constexpr int frameHeight = 240;
    constexpr int scrollDelta = 73;
    auto document = makeDocument(width, 1000);
    auto previous = cropDocumentWindow(document, 100, frameHeight);
    auto current = cropDocumentWindow(document, 100 + scrollDelta, frameHeight);

    StitchEngine engine;
    MatchOptions options;
    options.minOverlapPx = 32;
    options.maxOverlapPx = frameHeight;
    options.xStep = 4;
    options.yStep = 2;
    options.lowConfidenceThreshold = 0.65;

    const auto match = engine.findVerticalOverlap(previous, current, options);
    const int expectedOverlap = frameHeight - scrollDelta;
    if (!match.ok) {
        return fail("expected match.ok");
    }
    if (expectNear(match.overlapPx, expectedOverlap, 1, "overlap") != EXIT_SUCCESS ||
        expectNear(match.newContentHeight, scrollDelta, 1, "newContentHeight") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (match.confidence < 0.90) {
        std::cerr << "confidence too low: " << match.confidence << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int testBasicUpwardOverlapAndPrepend() {
    constexpr int width = 320;
    constexpr int frameHeight = 240;
    constexpr int scrollDelta = 73;
    constexpr int previousTop = 300;
    auto document = makeDocument(width, 1000);
    auto previous = cropDocumentWindow(document, previousTop, frameHeight);
    auto current = cropDocumentWindow(
        document, previousTop - scrollDelta, frameHeight);

    StitchEngine engine;
    MatchOptions options;
    options.direction = rillshot::core::ScrollDirection::Up;
    options.minOverlapPx = 32;
    options.maxOverlapPx = frameHeight;
    options.xStep = 4;
    options.yStep = 2;
    options.lowConfidenceThreshold = 0.65;

    const auto match = engine.findVerticalOverlap(previous, current, options);
    const int expectedOverlap = frameHeight - scrollDelta;
    if (!match.ok) {
        return fail("expected upward match.ok");
    }
    if (expectNear(match.overlapPx, expectedOverlap, 1, "upward overlap") != EXIT_SUCCESS ||
        expectNear(match.newContentHeight, scrollDelta, 1, "upward newContentHeight") != EXIT_SUCCESS ||
        match.newContentStartY != 0 ||
        expectNear(match.newContentEndYExclusive, scrollDelta, 1,
                   "upward newContentEndY") != EXIT_SUCCESS) {
        return fail("upward match bounds are incorrect");
    }

    Image assembled = std::move(previous);
    assembled.prependRowsFrom(
        current, match.newContentStartY, match.newContentEndYExclusive);
    if (assembled.height() != frameHeight + scrollDelta) {
        return fail("upward assembly height is incorrect");
    }
    for (int y = 0; y < assembled.height(); ++y) {
        const auto* expected = document.row(previousTop - scrollDelta + y);
        if (!std::equal(
                assembled.row(y), assembled.row(y) + assembled.stride(), expected)) {
            return fail("upward assembly row order is incorrect");
        }
    }
    return EXIT_SUCCESS;
}

static int testNoMotionAppendsNothing() {
    constexpr int frameHeight = 240;
    auto document = makeDocument(320, 1000);
    auto frame = cropDocumentWindow(document, 100, frameHeight);

    StitchEngine engine;
    MatchOptions options;
    options.minOverlapPx = 32;
    options.maxOverlapPx = frameHeight;
    options.xStep = 4;
    options.yStep = 2;

    const auto match = engine.findVerticalOverlap(frame, frame, options);
    if (!match.ok || match.newContentHeight > 1) {
        return fail("no motion should append little or no content");
    }
    return EXIT_SUCCESS;
}

static int testLowInformationRejected() {
    auto previous = makeSolid(160, 160, 128);
    auto current = makeSolid(160, 160, 128);

    StitchEngine engine;
    MatchOptions options;
    options.minOverlapPx = 16;
    options.maxOverlapPx = 160;
    if (engine.findVerticalOverlap(previous, current, options).ok) {
        return fail("solid low-information frames should not produce a valid NCC match");
    }
    return EXIT_SUCCESS;
}

static int testRepeatedRowsMarkedLowConfidence() {
    constexpr int width = 240;
    constexpr int frameHeight = 160;
    constexpr int scrollDelta = 32;
    auto document = makeRepeatingRows(width, 800, 16);
    auto previous = cropDocumentWindow(document, 96, frameHeight);
    auto current = cropDocumentWindow(document, 96 + scrollDelta, frameHeight);

    StitchEngine engine;
    MatchOptions options;
    options.minOverlapPx = 16;
    options.maxOverlapPx = frameHeight;
    options.xStep = 4;
    options.yStep = 2;

    const auto match = engine.findVerticalOverlap(previous, current, options);
    if (!match.ok || !match.lowConfidence) {
        return fail("repeated rows should remain matchable but low-confidence");
    }
    return EXIT_SUCCESS;
}

static int testTallFrameUsesBoundedRefinement() {
    constexpr int width = 1200;
    constexpr int frameHeight = 2160;
    constexpr int scrollDelta = 713;
    auto document = makeDocument(width, 4000);
    auto previous = cropDocumentWindow(document, 500, frameHeight);
    auto current = cropDocumentWindow(document, 500 + scrollDelta, frameHeight);

    StitchEngine engine;
    MatchOptions options;
    options.minOverlapPx = 64;
    options.maxOverlapPx = frameHeight;
    options.xStep = 8;
    options.yStep = 3;

    const auto match = engine.findVerticalOverlap(previous, current, options);
    if (!match.ok ||
        expectNear(match.overlapPx, frameHeight - scrollDelta, 1,
                   "tall-frame overlap") != EXIT_SUCCESS) {
        return fail("coarse-to-fine matcher lost the tall-frame seam");
    }
    if (match.candidatesEvaluated <= match.refinedCandidatesEvaluated ||
        match.primaryCandidatesRefined != 24 ||
        match.diverseCandidatesRefined != 8 ||
        match.refinedCandidatesEvaluated != 32 ||
        match.refinedSamplesEvaluated <= 0 ||
        match.method != "CoarseToFineMultiPhaseGridNcc") {
        return fail("tall-frame matcher did not keep refinement bounded");
    }
    return EXIT_SUCCESS;
}

static int testExtremeHeightUsesBoundedCoarseRetention() {
    constexpr int width = 32;
    constexpr int frameHeight = 10000;
    // Deliberately choose an overlap that is not aligned with the adaptive
    // coarse step, so the regression also covers bounded-resolution error.
    constexpr int scrollDelta = 1999;
    auto document = makeDocument(width, 16000);
    auto previous = cropDocumentWindow(document, 1000, frameHeight);
    auto current = cropDocumentWindow(
        document, 1000 + scrollDelta, frameHeight);

    MatchOptions options;
    options.minOverlapPx = 32;
    options.maxOverlapPx = frameHeight;
    const auto match = StitchEngine{}.findVerticalOverlap(
        previous, current, options);
    if (!match.ok || match.candidatesEvaluated <= 4096 ||
        match.coarseCandidatesRetained != 4096 ||
        match.coarseCandidatesRetained <= match.refinedCandidatesEvaluated ||
        match.coarseCandidateStepPx != 1 ||
        expectNear(
            match.overlapPx,
            frameHeight - scrollDelta,
            1,
            "bounded coarse overlap") != EXIT_SUCCESS) {
        return fail("extreme-height coarse retention exceeded its budget or lost the seam");
    }
    return EXIT_SUCCESS;
}

static int testRefinementUsesSecondSamplingPhase() {
    constexpr int width = 32;
    constexpr int height = 16;
    auto previous = makeSolid(width, height, 128);
    auto current = makeSolid(width, height, 128);
    for (int y = 1; y < height; y += 2) {
        for (int x = 2; x < width; x += 4) {
            const std::uint8_t value = ((x / 4 + y) % 2 == 0) ? 20 : 235;
            for (auto* image : {&previous, &current}) {
                auto* pixel = image->row(y) + x * 4;
                pixel[0] = value;
                pixel[1] = value;
                pixel[2] = value;
            }
        }
    }

    MatchOptions options;
    options.minOverlapPx = height;
    options.maxOverlapPx = height;
    options.xStep = 4;
    options.yStep = 2;
    const auto match = StitchEngine{}.findVerticalOverlap(
        previous, current, options);
    if (!match.ok || match.confidence < 0.99 || match.lowInformation ||
        match.refinedSamplesEvaluated <= 0) {
        return fail("refinement must recover texture on the half-step lattice");
    }
    return EXIT_SUCCESS;
}

static int testWidthMismatchRejected() {
    auto first = makeDocument(160, 160);
    auto second = makeDocument(161, 160);
    StitchEngine engine;
    if (engine.findVerticalOverlap(first, second).ok) {
        return fail("width mismatch should be rejected");
    }
    return EXIT_SUCCESS;
}

static int testInvalidMatchOptionsRejected() {
    auto document = makeDocument(160, 400);
    auto previous = cropDocumentWindow(document, 0, 160);
    auto current = cropDocumentWindow(document, 40, 160);
    StitchEngine engine;

    MatchOptions invalid;
    invalid.direction = static_cast<rillshot::core::ScrollDirection>(999);
    auto result = engine.findVerticalOverlap(previous, current, invalid);
    if (result.ok || result.message != "invalid match options") {
        return fail("unknown stitch direction should be rejected explicitly");
    }

    invalid = {};
    invalid.lowConfidenceThreshold =
        std::numeric_limits<double>::quiet_NaN();
    result = engine.findVerticalOverlap(previous, current, invalid);
    if (result.ok || result.message != "invalid match options") {
        return fail("NaN stitch thresholds should be rejected explicitly");
    }
    return EXIT_SUCCESS;
}

int main() {
    if (testBasicVerticalOverlap() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testBasicUpwardOverlapAndPrepend() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testNoMotionAppendsNothing() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testLowInformationRejected() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testRepeatedRowsMarkedLowConfidence() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testTallFrameUsesBoundedRefinement() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testExtremeHeightUsesBoundedCoarseRetention() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testRefinementUsesSecondSamplingPhase() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testWidthMismatchRejected() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testInvalidMatchOptionsRejected() != EXIT_SUCCESS) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
