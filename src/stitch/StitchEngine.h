#pragma once

#include "core/Image.h"
#include "core/Types.h"

#include <cstdint>
#include <string>

namespace rillshot::stitch {

struct MatchOptions {
    rillshot::core::ScrollDirection direction = rillshot::core::ScrollDirection::Down;
    int minOverlapPx = 32;
    int maxOverlapPx = 0;
    int ignoreTopPx = 0;
    int ignoreBottomPx = 0;
    int candidateStepPx = 1;
    int xStep = 8;
    int yStep = 4;
    double lowConfidenceThreshold = 0.70;
    double ambiguityThreshold = 0.015;
    double lowVarianceThreshold = 10.0;
};

struct MatchResult {
    bool ok = false;
    int overlapPx = 0;
    int newContentStartY = 0;
    int newContentEndYExclusive = 0;
    int newContentHeight = 0;
    double confidence = 0.0;
    double ambiguity = 0.0;
    bool lowInformation = false;
    bool lowConfidence = false;
    int coarseCandidateStepPx = 0;
    int coarseCandidatesRetained = 0;
    int candidatesEvaluated = 0;
    int primaryCandidatesRefined = 0;
    int diverseCandidatesRefined = 0;
    int refinedCandidatesEvaluated = 0;
    std::int64_t refinedSamplesEvaluated = 0;
    std::string method = "CoarseToFineMultiPhaseGridNcc";
    std::string message;
};

class StitchEngine {
public:
    [[nodiscard]] MatchResult findVerticalOverlap(
        const rillshot::core::Image& previous,
        const rillshot::core::Image& current,
        const MatchOptions& options = {}) const;
};

} // namespace rillshot::stitch
