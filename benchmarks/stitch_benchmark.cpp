#include "StitchTestFixtures.h"

#include "stitch/StitchEngine.h"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace {

using Clock = std::chrono::steady_clock;

double milliseconds(Clock::time_point started, int iterations) {
    const auto elapsed = std::chrono::duration<double, std::milli>(
        Clock::now() - started).count();
    return elapsed / static_cast<double>(iterations);
}

} // namespace

int main() {
    constexpr int width = 1200;
    constexpr int frameHeight = 2160;
    constexpr int scrollDelta = 713;
    constexpr int iterations = 30;
    auto document = makeDocument(width, 4000);
    const auto previous = cropDocumentWindow(document, 500, frameHeight);
    const auto current = cropDocumentWindow(
        document, 500 + scrollDelta, frameHeight);

    rillshot::stitch::MatchOptions options;
    options.minOverlapPx = 64;
    options.maxOverlapPx = frameHeight;
    options.xStep = 8;
    options.yStep = 3;
    rillshot::stitch::StitchEngine engine;

    auto match = engine.findVerticalOverlap(previous, current, options);
    if (!match.ok || match.overlapPx != frameHeight - scrollDelta) {
        std::cerr << "benchmark seam validation failed\n";
        return EXIT_FAILURE;
    }

    const auto seamStarted = Clock::now();
    for (int iteration = 0; iteration < iterations; ++iteration) {
        match = engine.findVerticalOverlap(previous, current, options);
    }
    const double seamAverageMs = milliseconds(seamStarted, iterations);
    const auto seamMethod = match.method;

    constexpr int identicalIterations = 100;
    const auto identicalStarted = Clock::now();
    for (int iteration = 0; iteration < identicalIterations; ++iteration) {
        match = engine.findVerticalOverlap(previous, previous, options);
    }
    const double identicalAverageMs = milliseconds(
        identicalStarted, identicalIterations);

    std::cout << std::fixed << std::setprecision(3)
              << "{\"seamAverageMs\":" << seamAverageMs
              << ",\"identicalAverageMs\":" << identicalAverageMs
              << ",\"seamMethod\":\"" << seamMethod
              << "\",\"identicalMethod\":\"" << match.method
              << "\"}\n";
    return EXIT_SUCCESS;
}
