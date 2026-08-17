#include "StitchTestFixtures.h"
#include "TestSupport.h"

#include "stitch/StitchEngine.h"

#include <algorithm>
#include <cstdlib>

using rillshot::stitch::MatchOptions;
using rillshot::stitch::StitchEngine;

static int testStickyHeaderIgnoreTop() {
    constexpr int width = 300;
    constexpr int frameHeight = 260;
    constexpr int stickyHeight = 36;
    constexpr int scrollDelta = 64;
    auto document = makeDocument(width, 1200);
    auto previous = cropWithStickyHeader(
        document, 200, frameHeight, stickyHeight);
    auto current = cropWithStickyHeader(
        document, 200 + scrollDelta, frameHeight, stickyHeight);

    StitchEngine engine;
    MatchOptions options;
    options.minOverlapPx = 32;
    options.maxOverlapPx = frameHeight;
    options.ignoreTopPx = stickyHeight;
    options.xStep = 4;
    options.yStep = 2;

    const auto match = engine.findVerticalOverlap(previous, current, options);
    const int expectedOverlap = frameHeight - stickyHeight - scrollDelta;
    if (!match.ok ||
        expectNear(match.overlapPx, expectedOverlap, 1, "sticky overlap") != EXIT_SUCCESS ||
        expectNear(match.newContentHeight, scrollDelta, 1,
                   "sticky newContentHeight") != EXIT_SUCCESS) {
        return fail("sticky-header overlap is incorrect");
    }

    Image assembled = std::move(previous);
    assembled.appendRowsFrom(
        current, match.newContentStartY, match.newContentEndYExclusive);
    if (assembled.height() != frameHeight + scrollDelta) {
        return fail("sticky-header assembly height is incorrect");
    }

    int stickyRows = 0;
    for (int y = 0; y < assembled.height(); ++y) {
        const auto* row = assembled.row(y);
        bool isSticky = true;
        for (int x = 0; x < assembled.width(); ++x) {
            if (row[x * 4 + 0] != 32 || row[x * 4 + 1] != 32 ||
                row[x * 4 + 2] != 32 || row[x * 4 + 3] != 255) {
                isSticky = false;
                break;
            }
        }
        stickyRows += isSticky ? 1 : 0;
    }
    if (stickyRows != stickyHeight) {
        return fail("sticky header must be retained exactly once");
    }
    return EXIT_SUCCESS;
}

static int testStickyFooterIgnoreBottom() {
    constexpr int width = 300;
    constexpr int frameHeight = 260;
    constexpr int stickyHeight = 36;
    constexpr int scrollDelta = 64;
    auto document = makeDocument(width, 1200);
    auto previous = cropWithStickyFooter(
        document, 200, frameHeight, stickyHeight);
    auto current = cropWithStickyFooter(
        document, 200 + scrollDelta, frameHeight, stickyHeight);

    StitchEngine engine;
    MatchOptions options;
    options.minOverlapPx = 32;
    options.maxOverlapPx = frameHeight;
    options.ignoreBottomPx = stickyHeight;
    options.xStep = 4;
    options.yStep = 2;

    const auto match = engine.findVerticalOverlap(previous, current, options);
    const int expectedOverlap = frameHeight - stickyHeight - scrollDelta;
    if (!match.ok ||
        expectNear(match.overlapPx, expectedOverlap, 1,
                   "sticky-footer overlap") != EXIT_SUCCESS ||
        match.newContentEndYExclusive != frameHeight - stickyHeight ||
        expectNear(match.newContentHeight, scrollDelta, 1,
                   "sticky-footer newContentHeight") != EXIT_SUCCESS) {
        return fail("sticky-footer match bounds are incorrect");
    }

    Image assembled(width, frameHeight - stickyHeight);
    for (int y = 0; y < assembled.height(); ++y) {
        std::copy(previous.row(y), previous.row(y) + previous.stride(),
                  assembled.row(y));
    }
    assembled.appendRowsFrom(
        current, match.newContentStartY, match.newContentEndYExclusive);
    assembled.appendRowsFrom(current, frameHeight - stickyHeight, frameHeight);

    if (assembled.height() != frameHeight + scrollDelta) {
        return fail("sticky-footer assembly height is incorrect");
    }
    const auto* firstAppended = assembled.row(frameHeight - stickyHeight);
    const auto* expectedContent = document.row(200 + frameHeight - stickyHeight);
    if (!std::equal(firstAppended, firstAppended + assembled.stride(),
                    expectedContent)) {
        return fail("sticky footer was inserted between content rows");
    }
    const auto* finalRow = assembled.row(assembled.height() - 1);
    if (finalRow[0] != 48 || finalRow[1] != 48 || finalRow[2] != 48) {
        return fail("sticky footer should be retained once at the bottom edge");
    }
    return EXIT_SUCCESS;
}

static int testCombinedStickyBorders() {
    constexpr int width = 300;
    constexpr int frameHeight = 280;
    constexpr int headerHeight = 36;
    constexpr int footerHeight = 24;
    constexpr int scrollDelta = 64;
    constexpr int contentHeight = frameHeight - headerHeight - footerHeight;
    auto document = makeDocument(width, 1400);
    auto previous = cropWithStickyHeaderAndFooter(
        document, 200, frameHeight, headerHeight, footerHeight);
    auto current = cropWithStickyHeaderAndFooter(
        document, 200 + scrollDelta, frameHeight, headerHeight, footerHeight);

    StitchEngine engine;
    MatchOptions options;
    options.minOverlapPx = 32;
    options.maxOverlapPx = frameHeight;
    options.ignoreTopPx = headerHeight;
    options.ignoreBottomPx = footerHeight;
    options.xStep = 4;
    options.yStep = 2;

    const auto match = engine.findVerticalOverlap(previous, current, options);
    if (!match.ok ||
        expectNear(match.overlapPx, contentHeight - scrollDelta, 1,
                   "combined-border overlap") != EXIT_SUCCESS ||
        match.newContentStartY != headerHeight + contentHeight - scrollDelta ||
        match.newContentEndYExclusive != frameHeight - footerHeight) {
        return fail("combined sticky borders produced incorrect append bounds");
    }

    Image assembled(width, frameHeight - footerHeight);
    for (int y = 0; y < assembled.height(); ++y) {
        std::copy(previous.row(y), previous.row(y) + previous.stride(),
                  assembled.row(y));
    }
    assembled.appendRowsFrom(
        current, match.newContentStartY, match.newContentEndYExclusive);
    assembled.appendRowsFrom(current, frameHeight - footerHeight, frameHeight);
    if (assembled.height() != frameHeight + scrollDelta ||
        assembled.row(0)[0] != 24 ||
        assembled.row(assembled.height() - 1)[0] != 56) {
        return fail("combined sticky-border assembly is incorrect");
    }
    return EXIT_SUCCESS;
}

static int testUpwardCombinedStickyBorders() {
    constexpr int width = 300;
    constexpr int frameHeight = 280;
    constexpr int headerHeight = 36;
    constexpr int footerHeight = 24;
    constexpr int scrollDelta = 64;
    constexpr int previousContentTop = 400;
    auto document = makeDocument(width, 1400);
    auto previous = cropWithStickyHeaderAndFooter(
        document, previousContentTop, frameHeight, headerHeight, footerHeight);
    auto current = cropWithStickyHeaderAndFooter(
        document, previousContentTop - scrollDelta,
        frameHeight, headerHeight, footerHeight);

    StitchEngine engine;
    MatchOptions options;
    options.direction = rillshot::core::ScrollDirection::Up;
    options.minOverlapPx = 32;
    options.maxOverlapPx = frameHeight;
    options.ignoreTopPx = headerHeight;
    options.ignoreBottomPx = footerHeight;
    options.xStep = 4;
    options.yStep = 2;

    const auto match = engine.findVerticalOverlap(previous, current, options);
    if (!match.ok ||
        expectNear(match.newContentHeight, scrollDelta, 1,
                   "upward sticky newContentHeight") != EXIT_SUCCESS ||
        match.newContentStartY != headerHeight ||
        expectNear(match.newContentEndYExclusive, headerHeight + scrollDelta, 1,
                   "upward sticky newContentEndY") != EXIT_SUCCESS) {
        return fail("upward sticky-border bounds are incorrect");
    }

    Image assembled(width, frameHeight - headerHeight);
    for (int y = headerHeight; y < frameHeight; ++y) {
        std::copy(previous.row(y), previous.row(y) + previous.stride(),
                  assembled.row(y - headerHeight));
    }
    assembled.prependRowsFrom(
        current, match.newContentStartY, match.newContentEndYExclusive);
    assembled.prependRowsFrom(current, 0, headerHeight);

    if (assembled.height() != frameHeight + scrollDelta ||
        assembled.row(0)[0] != 24 ||
        assembled.row(assembled.height() - 1)[0] != 56) {
        return fail("upward sticky-border assembly is incorrect");
    }
    const auto* firstContent = assembled.row(headerHeight);
    const auto* expectedFirst = document.row(previousContentTop - scrollDelta);
    const auto* lastContent = assembled.row(
        assembled.height() - footerHeight - 1);
    const auto* expectedLast = document.row(
        previousContentTop + frameHeight - headerHeight - footerHeight - 1);
    if (!std::equal(firstContent, firstContent + assembled.stride(), expectedFirst) ||
        !std::equal(lastContent, lastContent + assembled.stride(), expectedLast)) {
        return fail("upward sticky-border content order is incorrect");
    }
    return EXIT_SUCCESS;
}

int main() {
    if (testStickyHeaderIgnoreTop() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testStickyFooterIgnoreBottom() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testCombinedStickyBorders() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testUpwardCombinedStickyBorders() != EXIT_SUCCESS) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
