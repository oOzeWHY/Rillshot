#include "StitchTestFixtures.h"
#include "TestSupport.h"

#include "stitch/StitchedImageBuilder.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <utility>

using rillshot::core::Image;
using rillshot::core::ScrollDirection;
using rillshot::stitch::StitchedImageBuilder;

static bool rowsEqual(
    const Image& actual,
    int actualY,
    const Image& expected,
    int expectedY) {
    return std::equal(
        actual.row(actualY),
        actual.row(actualY) + actual.stride(),
        expected.row(expectedY));
}

static int testDownwardChunksMaterializeInCaptureOrder() {
    constexpr int width = 80;
    auto document = makeDocument(width, 300);
    auto initial = cropDocumentWindow(document, 40, 80);
    auto next = cropDocumentWindow(document, 120, 40);
    auto final = cropDocumentWindow(document, 160, 24);

    StitchedImageBuilder builder(std::move(initial), ScrollDirection::Down);
    builder.addRowsFrom(next, 0, next.height());
    builder.addRowsFrom(final, 0, final.height());
    const auto output = builder.materialize();

    if (builder.height() != 144 || builder.chunkCount() != 3 ||
        output.height() != 144) {
        return fail("downward builder dimensions or chunk count are wrong");
    }
    for (int y = 0; y < output.height(); ++y) {
        if (!rowsEqual(output, y, document, 40 + y)) {
            return fail("downward builder changed row order");
        }
    }
    return EXIT_SUCCESS;
}

static int testUpwardChunksMaterializeWithoutRepeatedPrepend() {
    constexpr int width = 80;
    auto document = makeDocument(width, 300);
    auto initial = cropDocumentWindow(document, 120, 80);
    auto previousChunk = cropDocumentWindow(document, 80, 40);
    auto firstChunk = cropDocumentWindow(document, 56, 24);

    StitchedImageBuilder builder(std::move(initial), ScrollDirection::Up);
    builder.addRowsFrom(previousChunk, 0, previousChunk.height());
    builder.addRowsFrom(firstChunk, 0, firstChunk.height());
    const auto output = builder.materialize();

    if (builder.height() != 144 || builder.chunkCount() != 3 ||
        output.height() != 144) {
        return fail("upward builder dimensions or chunk count are wrong");
    }
    for (int y = 0; y < output.height(); ++y) {
        if (!rowsEqual(output, y, document, 56 + y)) {
            return fail("upward builder changed row order");
        }
    }
    return EXIT_SUCCESS;
}

static int testRejectedRangesAndWidthsFailClosed() {
    auto first = makeDocument(32, 32);
    StitchedImageBuilder builder(std::move(first), ScrollDirection::Down);
    auto otherWidth = makeDocument(33, 32);
    try {
        builder.addRowsFrom(otherWidth, 0, 1);
        return fail("builder accepted a mismatched width");
    } catch (const std::invalid_argument&) {
    }

    auto source = makeDocument(32, 32);
    builder.addRowsFrom(source, 20, 10);
    if (builder.chunkCount() != 1 || builder.height() != 32) {
        return fail("an empty clamped range should not create a chunk");
    }
    return EXIT_SUCCESS;
}

int main() {
    if (testDownwardChunksMaterializeInCaptureOrder() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (testUpwardChunksMaterializeWithoutRepeatedPrepend() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (testRejectedRangesAndWidthsFailClosed() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
