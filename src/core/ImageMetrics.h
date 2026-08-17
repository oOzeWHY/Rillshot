#pragma once

#include "core/Image.h"

namespace rillshot::core {

struct DifferenceOptions {
    int ignoreTopPx = 0;
    int ignoreBottomPx = 0;
    int xStep = 8;
    int yStep = 4;
    // Two interleaved lattice phases reduce fixed-grid aliasing from thin
    // cursors, one-pixel separators, and periodic UI patterns. Values outside
    // 1..2 are clamped at this low-level diagnostic boundary.
    int samplePhases = 2;
};

// Cheap sampled statistics used for diagnostics and capture-backend fallback.
// A near-black frame is suspicious, not necessarily invalid: callers may try
// another backend but must not silently discard the only available frame.
struct ImageSummary {
    double meanLuma = 0.0;
    double lumaStdDev = 0.0;
    double darkPixelRatio = 0.0;
    bool suspiciouslyNearBlack = false;
};

[[nodiscard]] double meanAbsDiffRatio(const Image& a, const Image& b, const DifferenceOptions& options = {});
[[nodiscard]] ImageSummary summarizeImage(const Image& image, int xStep = 16, int yStep = 8);

} // namespace rillshot::core
