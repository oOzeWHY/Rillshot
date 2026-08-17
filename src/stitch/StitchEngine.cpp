#include "stitch/StitchEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

namespace rillshot::stitch {
namespace {

struct CandidateScore {
    int overlap = 0;
    double ncc = -2.0;
    double varA = 0.0;
    double varB = 0.0;
    std::int64_t samples = 0;
};

bool candidateIsBetter(
    const CandidateScore& left,
    const CandidateScore& right) noexcept {
    if (left.ncc != right.ncc) {
        return left.ncc > right.ncc;
    }
    return left.overlap > right.overlap;
}

struct BetterCandidateFirst {
    bool operator()(
        const CandidateScore& left,
        const CandidateScore& right) const noexcept {
        return candidateIsBetter(left, right);
    }
};

double grayPixel(const std::uint8_t* pixel) noexcept {
    return 0.114 * static_cast<double>(pixel[0]) +
        0.587 * static_cast<double>(pixel[1]) +
        0.299 * static_cast<double>(pixel[2]);
}

int safeStep(int value) noexcept {
    return std::max(1, value);
}

bool isValidDirection(rillshot::core::ScrollDirection direction) noexcept {
    switch (direction) {
    case rillshot::core::ScrollDirection::Down:
    case rillshot::core::ScrollDirection::Up:
        return true;
    }
    return false;
}

bool matchOptionsAreValid(const MatchOptions& options) noexcept {
    return isValidDirection(options.direction) &&
        options.maxOverlapPx >= 0 &&
        options.ignoreTopPx >= 0 &&
        options.ignoreBottomPx >= 0 &&
        std::isfinite(options.lowConfidenceThreshold) &&
        options.lowConfidenceThreshold >= 0.0 &&
        options.lowConfidenceThreshold <= 1.0 &&
        std::isfinite(options.ambiguityThreshold) &&
        options.ambiguityThreshold >= 0.0 &&
        options.ambiguityThreshold <= 2.0 &&
        std::isfinite(options.lowVarianceThreshold) &&
        options.lowVarianceThreshold >= 0.0;
}

CandidateScore scoreOverlap(
    const rillshot::core::Image& previous,
    const rillshot::core::Image& current,
    int overlap,
    const MatchOptions& options) {

    const int xs = safeStep(options.xStep);
    const int ys = safeStep(options.yStep);
    const int prevContentStartY = options.ignoreTopPx;
    const int prevContentEndY = previous.height() - options.ignoreBottomPx;
    const int currContentStartY = options.ignoreTopPx;
    const int currContentEndY = current.height() - options.ignoreBottomPx;
    const bool downward = options.direction == rillshot::core::ScrollDirection::Down;
    const int prevStartY = downward
        ? prevContentEndY - overlap
        : prevContentStartY;
    const int currStartY = downward
        ? currContentStartY
        : currContentEndY - overlap;

    double sumA = 0.0;
    double sumB = 0.0;
    double sumA2 = 0.0;
    double sumB2 = 0.0;
    double sumAB = 0.0;
    long long n = 0;

    // Interleave a half-step lattice with the historical top-left lattice.
    // This keeps the candidate budget fixed while making refinement less
    // sensitive to thin separators and periodic content aligned to x/yStep.
    const int phases = xs == 1 && ys == 1 ? 1 : 2;
    for (int phase = 0; phase < phases; ++phase) {
        const int xOffset = phase == 0 ? 0 : xs / 2;
        const int yOffset = phase == 0 ? 0 : ys / 2;
        for (int dy = yOffset; dy < overlap; dy += ys) {
            const auto* prevRow = previous.row(prevStartY + dy);
            const auto* currRow = current.row(currStartY + dy);
            for (int x = xOffset; x < previous.width(); x += xs) {
                const double a = grayPixel(prevRow + x * 4);
                const double b = grayPixel(currRow + x * 4);
                sumA += a;
                sumB += b;
                sumA2 += a * a;
                sumB2 += b * b;
                sumAB += a * b;
                ++n;
            }
        }
    }

    if (n < 4) {
        return CandidateScore{overlap, -2.0, 0.0, 0.0, n};
    }

    const double dn = static_cast<double>(n);
    const double cov = sumAB - (sumA * sumB / dn);
    const double varA = sumA2 - (sumA * sumA / dn);
    const double varB = sumB2 - (sumB * sumB / dn);
    const double denom = std::sqrt(std::max(0.0, varA) * std::max(0.0, varB));
    const double ncc = denom <= std::numeric_limits<double>::epsilon() ? -2.0 : cov / denom;

    return CandidateScore{overlap, ncc, varA / dn, varB / dn, n};
}

CandidateScore scoreOverlapGrid(
    const rillshot::core::Image& previous,
    const rillshot::core::Image& current,
    int overlap,
    const MatchOptions& options) {

    constexpr int maximumColumns = 24;
    constexpr int maximumRows = 24;
    const int columns = std::min(maximumColumns, previous.width());
    const int rows = std::min(maximumRows, overlap);
    const int prevContentStartY = options.ignoreTopPx;
    const int prevContentEndY = previous.height() - options.ignoreBottomPx;
    const int currContentStartY = options.ignoreTopPx;
    const int currContentEndY = current.height() - options.ignoreBottomPx;
    const bool downward =
        options.direction == rillshot::core::ScrollDirection::Down;
    const int prevStartY = downward
        ? prevContentEndY - overlap
        : prevContentStartY;
    const int currStartY = downward
        ? currContentStartY
        : currContentEndY - overlap;

    double sumA = 0.0;
    double sumB = 0.0;
    double sumA2 = 0.0;
    double sumB2 = 0.0;
    double sumAB = 0.0;
    long long n = 0;
    for (int rowIndex = 0; rowIndex < rows; ++rowIndex) {
        const int dy = static_cast<int>(
            (static_cast<long long>(rowIndex) * 2LL + 1LL) * overlap /
            (static_cast<long long>(rows) * 2LL));
        const auto* prevRow = previous.row(prevStartY + dy);
        const auto* currRow = current.row(currStartY + dy);
        for (int columnIndex = 0; columnIndex < columns; ++columnIndex) {
            const int x = static_cast<int>(
                (static_cast<long long>(columnIndex) * 2LL + 1LL) * previous.width() /
                (static_cast<long long>(columns) * 2LL));
            const double a = grayPixel(prevRow + x * 4);
            const double b = grayPixel(currRow + x * 4);
            sumA += a;
            sumB += b;
            sumA2 += a * a;
            sumB2 += b * b;
            sumAB += a * b;
            ++n;
        }
    }

    if (n < 4) {
        return CandidateScore{overlap, -2.0, 0.0, 0.0, n};
    }
    const double dn = static_cast<double>(n);
    const double cov = sumAB - (sumA * sumB / dn);
    const double varA = sumA2 - (sumA * sumA / dn);
    const double varB = sumB2 - (sumB * sumB / dn);
    const double denom =
        std::sqrt(std::max(0.0, varA) * std::max(0.0, varB));
    const double ncc = denom <= std::numeric_limits<double>::epsilon()
        ? -2.0
        : cov / denom;
    return CandidateScore{overlap, ncc, varA / dn, varB / dn, n};
}

} // namespace

MatchResult StitchEngine::findVerticalOverlap(
    const rillshot::core::Image& previous,
    const rillshot::core::Image& current,
    const MatchOptions& rawOptions) const {

    MatchResult result;

    if (previous.empty() || current.empty()) {
        result.message = "empty image";
        return result;
    }
    if (previous.width() != current.width()) {
        result.message = "image widths differ";
        return result;
    }
    if (!matchOptionsAreValid(rawOptions)) {
        result.message = "invalid match options";
        return result;
    }

    MatchOptions options = rawOptions;
    const int sharedHeight = std::min(previous.height(), current.height());
    options.ignoreTopPx = std::clamp(options.ignoreTopPx, 0, sharedHeight);
    options.ignoreBottomPx = std::clamp(options.ignoreBottomPx, 0, sharedHeight - options.ignoreTopPx);
    options.minOverlapPx = std::max(4, options.minOverlapPx);
    options.candidateStepPx = safeStep(options.candidateStepPx);

    const int prevAvailable = previous.height() - options.ignoreTopPx - options.ignoreBottomPx;
    const int currAvailable = current.height() - options.ignoreTopPx - options.ignoreBottomPx;
    int maxOverlap = std::min(prevAvailable, currAvailable);
    if (options.maxOverlapPx > 0) {
        maxOverlap = std::min(maxOverlap, options.maxOverlapPx);
    }

    if (maxOverlap < options.minOverlapPx) {
        result.message = "not enough overlap search area";
        return result;
    }

    // Evaluate the requested grid without skipping potential seams, but retain
    // only the strongest candidates. This keeps memory bounded for extremely
    // tall, narrow captures without changing one-pixel matching precision.
    constexpr std::size_t maximumCoarseCandidates = 4096;
    const auto overlapRange =
        static_cast<std::int64_t>(maxOverlap) - options.minOverlapPx;
    const auto requestedCandidateCount = static_cast<std::size_t>(
        overlapRange / options.candidateStepPx + 2LL);
    std::vector<CandidateScore> coarseScores;
    coarseScores.reserve(std::min(
        requestedCandidateCount, maximumCoarseCandidates));
    std::priority_queue<
        CandidateScore,
        std::vector<CandidateScore>,
        BetterCandidateFirst> strongestCandidates;
    int coarseCandidatesEvaluated = 0;
    for (int overlap = options.minOverlapPx;;) {
        auto candidate = scoreOverlapGrid(previous, current, overlap, options);
        ++coarseCandidatesEvaluated;
        if (requestedCandidateCount <= maximumCoarseCandidates) {
            coarseScores.push_back(candidate);
        } else if (strongestCandidates.size() < maximumCoarseCandidates) {
            strongestCandidates.push(candidate);
        } else if (candidateIsBetter(candidate, strongestCandidates.top())) {
            strongestCandidates.pop();
            strongestCandidates.push(candidate);
        }
        if (overlap == maxOverlap) {
            break;
        }
        const auto nextOverlap = std::min<std::int64_t>(
            maxOverlap,
            static_cast<std::int64_t>(overlap) + options.candidateStepPx);
        overlap = static_cast<int>(nextOverlap);
    }
    while (!strongestCandidates.empty()) {
        coarseScores.push_back(strongestCandidates.top());
        strongestCandidates.pop();
    }

    constexpr size_t primaryRefinedCandidates = 24;
    constexpr size_t maximumRefinedCandidates = 32;
    const size_t primaryCount =
        std::min(primaryRefinedCandidates, coarseScores.size());
    std::partial_sort(
        coarseScores.begin(),
        coarseScores.begin() + static_cast<std::ptrdiff_t>(primaryCount),
        coarseScores.end(),
        [](const CandidateScore& left, const CandidateScore& right) {
            return candidateIsBetter(left, right);
        });

    std::vector<int> refinedOverlaps;
    refinedOverlaps.reserve(
        std::min(maximumRefinedCandidates, coarseScores.size()));
    for (size_t index = 0; index < primaryCount; ++index) {
        refinedOverlaps.push_back(coarseScores[index].overlap);
    }

    // A broad local peak can otherwise consume the entire refinement budget
    // with adjacent overlap values and hide a distant, ambiguous peak. Keep
    // most slots for the highest coarse scores, then add spatially separated
    // alternatives so the final ambiguity gate remains conservative.
    constexpr int diversityRadiusPx = 8;
    while (refinedOverlaps.size() < maximumRefinedCandidates &&
           refinedOverlaps.size() < coarseScores.size()) {
        const CandidateScore* bestDiverse = nullptr;
        for (const auto& candidate : coarseScores) {
            const bool nearSelected = std::any_of(
                refinedOverlaps.begin(),
                refinedOverlaps.end(),
                [&candidate](int selectedOverlap) {
                    return std::abs(candidate.overlap - selectedOverlap) <=
                        diversityRadiusPx;
                });
            if (nearSelected) {
                continue;
            }
            if (!bestDiverse || candidate.ncc > bestDiverse->ncc ||
                (candidate.ncc == bestDiverse->ncc &&
                 candidate.overlap > bestDiverse->overlap)) {
                bestDiverse = &candidate;
            }
        }
        if (!bestDiverse) {
            break;
        }
        refinedOverlaps.push_back(bestDiverse->overlap);
    }

    std::vector<CandidateScore> scores;
    scores.reserve(refinedOverlaps.size());
    for (const int overlap : refinedOverlaps) {
        scores.push_back(scoreOverlap(
            previous, current, overlap, options));
    }

    CandidateScore best;
    for (const auto& score : scores) {
        if (score.ncc > best.ncc) {
            best = score;
        }
    }

    CandidateScore secondBest;
    constexpr int suppressionRadiusPx = 8;
    for (const auto& score : scores) {
        if (std::abs(score.overlap - best.overlap) <= suppressionRadiusPx) {
            continue;
        }
        if (score.ncc > secondBest.ncc) {
            secondBest = score;
        }
    }

    result.ok = best.ncc > -1.0;
    result.overlapPx = best.overlap;
    if (options.direction == rillshot::core::ScrollDirection::Down) {
        result.newContentStartY = std::clamp(
            options.ignoreTopPx + best.overlap, 0, current.height());
        result.newContentEndYExclusive = std::clamp(
            current.height() - options.ignoreBottomPx,
            result.newContentStartY,
            current.height());
    } else {
        result.newContentStartY = std::clamp(options.ignoreTopPx, 0, current.height());
        result.newContentEndYExclusive = std::clamp(
            current.height() - options.ignoreBottomPx - best.overlap,
            result.newContentStartY,
            current.height());
    }
    result.newContentHeight = result.newContentEndYExclusive - result.newContentStartY;
    result.confidence = std::clamp(best.ncc, -1.0, 1.0);
    result.ambiguity = secondBest.ncc <= -1.0 ? 1.0 : std::max(0.0, best.ncc - secondBest.ncc);
    result.lowInformation = best.varA < options.lowVarianceThreshold || best.varB < options.lowVarianceThreshold;
    result.lowConfidence = result.confidence < options.lowConfidenceThreshold ||
                           result.ambiguity < options.ambiguityThreshold ||
                           result.lowInformation;
    result.coarseCandidateStepPx = options.candidateStepPx;
    result.coarseCandidatesRetained = static_cast<int>(coarseScores.size());
    result.candidatesEvaluated = coarseCandidatesEvaluated;
    result.primaryCandidatesRefined = static_cast<int>(primaryCount);
    result.diverseCandidatesRefined = static_cast<int>(
        refinedOverlaps.size() - primaryCount);
    result.refinedCandidatesEvaluated = static_cast<int>(scores.size());
    for (const auto& score : scores) {
        result.refinedSamplesEvaluated += score.samples;
    }
    result.method = "CoarseToFineMultiPhaseGridNcc";

    if (!result.ok) {
        result.message = "no valid NCC candidate";
    } else if (result.lowConfidence) {
        result.message = "low-confidence vertical overlap";
    } else {
        result.message = "matched";
    }

    return result;
}

} // namespace rillshot::stitch
