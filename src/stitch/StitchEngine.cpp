#include "stitch/StitchEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
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

struct CoarseLumaGrid {
    int rows = 0;
    int columns = 0;
    std::vector<double> values;

    [[nodiscard]] double at(int row, int column) const noexcept {
        return values[static_cast<std::size_t>(row) *
            static_cast<std::size_t>(columns) +
            static_cast<std::size_t>(column)];
    }
};

// The coarse grid is intentionally tiny, but the final NCC refinement still
// visits the same RGB pixels for every retained candidate.  Cache the two
// interleaved luma lattices once per frame pair so refinement only performs
// indexed loads and the six running sums.  Keeping a separate lattice for
// each phase preserves the historical sample coordinates, including odd
// x/y steps, instead of rounding them onto a new grid.
struct RefinedLumaGrid {
    int rows = 0;
    int phaseCount = 1;
    std::array<int, 2> columns{};
    std::array<std::vector<double>, 2> values;

    [[nodiscard]] double at(int phase, int row, int column) const noexcept {
        return values[static_cast<std::size_t>(phase)][
            static_cast<std::size_t>(row) *
                static_cast<std::size_t>(columns[static_cast<std::size_t>(phase)]) +
            static_cast<std::size_t>(column)];
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

std::size_t refinedLumaSampleCount(
    int width,
    int rows,
    int xStep,
    int yStep) noexcept {
    if (width <= 0 || rows <= 0) {
        return 0;
    }
    const int safeXStepValue = safeStep(xStep);
    const int phaseCount = xStep == 1 && yStep == 1 ? 1 : 2;
    std::size_t samples = 0;
    for (int phase = 0; phase < phaseCount; ++phase) {
        const int xOffset = phase == 0 ? 0 : safeXStepValue / 2;
        const int remainingWidth = width - xOffset;
        const auto columns = remainingWidth > 0
            ? static_cast<std::size_t>((remainingWidth - 1) / safeXStepValue + 1)
            : 0U;
        const auto rowSamples = static_cast<std::size_t>(rows);
        if (columns != 0 && rowSamples >
                (std::numeric_limits<std::size_t>::max)() / columns) {
            return (std::numeric_limits<std::size_t>::max)();
        }
        const auto phaseSamples = rowSamples * columns;
        if (samples >
                (std::numeric_limits<std::size_t>::max)() - phaseSamples) {
            return (std::numeric_limits<std::size_t>::max)();
        }
        samples += phaseSamples;
    }
    return samples;
}

CoarseLumaGrid makeCoarseLumaGrid(
    const rillshot::core::Image& image,
    int ignoreTopPx,
    int ignoreBottomPx) {
    constexpr int maximumColumns = 24;
    CoarseLumaGrid grid;
    grid.rows = image.height() - ignoreTopPx - ignoreBottomPx;
    grid.columns = std::min(maximumColumns, image.width());
    grid.values.resize(
        static_cast<std::size_t>(grid.rows) *
        static_cast<std::size_t>(grid.columns));

    for (int y = 0; y < grid.rows; ++y) {
        const auto* row = image.row(ignoreTopPx + y);
        for (int columnIndex = 0;
             columnIndex < grid.columns; ++columnIndex) {
            const int x = static_cast<int>(
                (static_cast<long long>(columnIndex) * 2LL + 1LL) *
                image.width() /
                (static_cast<long long>(grid.columns) * 2LL));
            grid.values[
                static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(grid.columns) +
                static_cast<std::size_t>(columnIndex)] =
                grayPixel(row + x * 4);
        }
    }
    return grid;
}

RefinedLumaGrid makeRefinedLumaGrid(
    const rillshot::core::Image& image,
    int ignoreTopPx,
    int ignoreBottomPx,
    int xStep,
    int yStep) {
    RefinedLumaGrid grid;
    grid.rows = image.height() - ignoreTopPx - ignoreBottomPx;
    grid.phaseCount = xStep == 1 && yStep == 1 ? 1 : 2;
    const int safeXStepValue = safeStep(xStep);
    for (int phase = 0; phase < grid.phaseCount; ++phase) {
        const int xOffset = phase == 0 ? 0 : safeXStepValue / 2;
        const int remainingWidth = image.width() - xOffset;
        const int columns = remainingWidth > 0
            ? (remainingWidth - 1) / safeXStepValue + 1
            : 0;
        grid.columns[static_cast<std::size_t>(phase)] = columns;
        auto& values = grid.values[static_cast<std::size_t>(phase)];
        values.resize(
            static_cast<std::size_t>(grid.rows) *
            static_cast<std::size_t>(columns));
        for (int rowIndex = 0; rowIndex < grid.rows; ++rowIndex) {
            const auto* row = image.row(ignoreTopPx + rowIndex);
            for (int column = 0; column < columns; ++column) {
                const int x = xOffset + column * safeXStepValue;
                values[static_cast<std::size_t>(rowIndex) *
                           static_cast<std::size_t>(columns) +
                       static_cast<std::size_t>(column)] =
                    grayPixel(row + x * 4);
            }
        }
    }
    return grid;
}

bool contentRowsAreExactlyEqual(
    const rillshot::core::Image& previous,
    const rillshot::core::Image& current,
    const MatchOptions& options) noexcept {
    const int previousRows =
        previous.height() - options.ignoreTopPx - options.ignoreBottomPx;
    const int currentRows =
        current.height() - options.ignoreTopPx - options.ignoreBottomPx;
    if (previousRows != currentRows || previousRows <= 0) {
        return false;
    }
    const auto rowBytes = static_cast<std::size_t>(previous.stride());
    for (int y = 0; y < previousRows; ++y) {
        if (std::memcmp(
                previous.row(options.ignoreTopPx + y),
                current.row(options.ignoreTopPx + y),
                rowBytes) != 0) {
            return false;
        }
    }
    return true;
}

bool contentHasAlternativeExactOverlap(
    const rillshot::core::Image& image,
    const MatchOptions& options) {
    const int rows =
        image.height() - options.ignoreTopPx - options.ignoreBottomPx;
    if (rows <= options.minOverlapPx) {
        return false;
    }

    const auto rowBytes = static_cast<std::size_t>(image.stride());
    // KMP over exact rows needs O(rows) comparisons, without hashing every
    // byte up front. memcmp stops at the first differing block on normal
    // content and remains exact for periodic rows and long shared margins.
    // Worst case is still O(rows * rowBytes), with only the prefix table
    // allocated. A proper prefix/suffix is an alternative exact overlap.
    std::vector<int> prefix(static_cast<std::size_t>(rows), 0);
    for (int index = 1; index < rows; ++index) {
        int candidate = prefix[static_cast<std::size_t>(index - 1)];
        const auto* row = image.row(options.ignoreTopPx + index);
        while (true) {
            if (std::memcmp(row, image.row(options.ignoreTopPx + candidate),
                            rowBytes) == 0) {
                ++candidate;
                break;
            }
            if (candidate == 0) {
                break;
            }
            candidate = prefix[static_cast<std::size_t>(candidate - 1)];
        }
        prefix[static_cast<std::size_t>(index)] = candidate;
    }
    return prefix.back() >= options.minOverlapPx;
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

CandidateScore scoreOverlap(
    const RefinedLumaGrid& previous,
    const RefinedLumaGrid& current,
    int overlap,
    const MatchOptions& options) {

    const int ys = safeStep(options.yStep);
    const bool downward = options.direction == rillshot::core::ScrollDirection::Down;
    const int prevStartY = downward ? previous.rows - overlap : 0;
    const int currStartY = downward ? 0 : current.rows - overlap;

    double sumA = 0.0;
    double sumB = 0.0;
    double sumA2 = 0.0;
    double sumB2 = 0.0;
    double sumAB = 0.0;
    long long n = 0;

    const int phases = previous.phaseCount;
    for (int phase = 0; phase < phases; ++phase) {
        const int yOffset = phase == 0 ? 0 : ys / 2;
        const int columns = previous.columns[static_cast<std::size_t>(phase)];
        for (int dy = yOffset; dy < overlap; dy += ys) {
            const int previousRow = prevStartY + dy;
            const int currentRow = currStartY + dy;
            for (int column = 0; column < columns; ++column) {
                const double a = previous.at(phase, previousRow, column);
                const double b = current.at(phase, currentRow, column);
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
    const CoarseLumaGrid& previous,
    const CoarseLumaGrid& current,
    int overlap,
    const MatchOptions& options) {

    constexpr int maximumRows = 24;
    const int columns = previous.columns;
    const int rows = std::min(maximumRows, overlap);
    const bool downward =
        options.direction == rillshot::core::ScrollDirection::Down;
    const int prevStartY = downward
        ? previous.rows - overlap
        : 0;
    const int currStartY = downward
        ? 0
        : current.rows - overlap;

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
        for (int columnIndex = 0; columnIndex < columns; ++columnIndex) {
            const double a = previous.at(
                prevStartY + dy, columnIndex);
            const double b = current.at(
                currStartY + dy, columnIndex);
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
    // Both cached and raw refinement must use the same normalized phases.
    options.xStep = safeStep(options.xStep);
    options.yStep = safeStep(options.yStep);

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

    // Rejected scrolling and end-of-page captures are common. Exact content
    // equality is conclusive, so avoid the complete candidate search when the
    // usable rows are byte-identical and contain enough sampled information.
    if (maxOverlap == prevAvailable && maxOverlap == currAvailable &&
        contentRowsAreExactlyEqual(previous, current, options) &&
        !contentHasAlternativeExactOverlap(previous, options)) {
        const auto exact = scoreOverlap(
            previous, current, maxOverlap, options);
        if (exact.ncc > -1.0 &&
            exact.varA >= options.lowVarianceThreshold &&
            exact.varB >= options.lowVarianceThreshold) {
            result.ok = true;
            result.overlapPx = maxOverlap;
            result.newContentStartY = options.direction ==
                    rillshot::core::ScrollDirection::Down
                ? std::clamp(
                    options.ignoreTopPx + maxOverlap,
                    0,
                    current.height())
                : std::clamp(options.ignoreTopPx, 0, current.height());
            result.newContentEndYExclusive = result.newContentStartY;
            result.newContentHeight = 0;
            result.confidence = std::clamp(exact.ncc, -1.0, 1.0);
            result.ambiguity = 1.0;
            result.lowInformation = false;
            result.lowConfidence =
                result.confidence < options.lowConfidenceThreshold;
            result.candidatesEvaluated = 1;
            result.refinedCandidatesEvaluated = 1;
            result.refinedSamplesEvaluated = exact.samples;
            result.exactContentMatch = true;
            result.method = "ExactContentEquality";
            result.message = "matched identical content";
            return result;
        }
    }

    // RGB-to-luma conversion depends only on source row and fixed x sample.
    // Cache it once per frame pair instead of repeating it for every overlap.
    const auto previousCoarse = makeCoarseLumaGrid(
        previous, options.ignoreTopPx, options.ignoreBottomPx);
    const auto currentCoarse = makeCoarseLumaGrid(
        current, options.ignoreTopPx, options.ignoreBottomPx);
    result.coarseLumaSamplesPrepared = static_cast<std::int64_t>(
        previousCoarse.values.size() + currentCoarse.values.size());

    // Keep the optimization bounded. A pathological 8K region can already
    // consume hundreds of MiB for the two BGRA frames; in that case the
    // original raw-pixel refinement is safer than adding another large cache.
    constexpr std::size_t maximumRefinedLumaCacheBytes = 64U * 1024U * 1024U;
    const auto previousRefinedSampleCount = refinedLumaSampleCount(
        previous.width(),
        prevAvailable,
        options.xStep,
        options.yStep);
    const auto currentRefinedSampleCount = refinedLumaSampleCount(
        current.width(),
        currAvailable,
        options.xStep,
        options.yStep);
    const bool refinedCacheFits =
        previousRefinedSampleCount <=
            (std::numeric_limits<std::size_t>::max)() - currentRefinedSampleCount &&
        previousRefinedSampleCount + currentRefinedSampleCount <=
            maximumRefinedLumaCacheBytes / sizeof(double);
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
        auto candidate = scoreOverlapGrid(
            previousCoarse, currentCoarse, overlap, options);
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
    // Exclusion is monotone: once near a selected peak, always excluded.
    // Update only for newly selected peaks instead of repeating all pairwise
    // distances on each of the eight diversity passes.
    std::vector<unsigned char> excluded(coarseScores.size(), 0);
    const auto excludeNear = [&](int selectedOverlap) {
        for (std::size_t index = 0; index < coarseScores.size(); ++index) {
            if (std::abs(coarseScores[index].overlap - selectedOverlap) <=
                    diversityRadiusPx) {
                excluded[index] = 1;
            }
        }
    };
    for (const int overlap : refinedOverlaps) {
        excludeNear(overlap);
    }
    while (refinedOverlaps.size() < maximumRefinedCandidates &&
           refinedOverlaps.size() < coarseScores.size()) {
        const CandidateScore* bestDiverse = nullptr;
        for (std::size_t index = 0; index < coarseScores.size(); ++index) {
            if (excluded[index]) {
                continue;
            }
            const auto& candidate = coarseScores[index];
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
        excludeNear(bestDiverse->overlap);
    }

    // A single refined candidate does not amortize cache construction. Keep
    // the original path for that small case, while the normal 24+8 budget
    // reuses the cache across all candidates.
    const bool useRefinedCache = refinedCacheFits && refinedOverlaps.size() > 1;
    RefinedLumaGrid previousRefined;
    RefinedLumaGrid currentRefined;
    if (useRefinedCache) {
        previousRefined = makeRefinedLumaGrid(
            previous,
            options.ignoreTopPx,
            options.ignoreBottomPx,
            options.xStep,
            options.yStep);
        currentRefined = makeRefinedLumaGrid(
            current,
            options.ignoreTopPx,
            options.ignoreBottomPx,
            options.xStep,
            options.yStep);
        result.refinedLumaSamplesPrepared = static_cast<std::int64_t>(
            previousRefinedSampleCount + currentRefinedSampleCount);
    }

    std::vector<CandidateScore> scores;
    scores.reserve(refinedOverlaps.size());
    for (const int overlap : refinedOverlaps) {
        scores.push_back(useRefinedCache
            ? scoreOverlap(previousRefined, currentRefined, overlap, options)
            : scoreOverlap(previous, current, overlap, options));
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
    result.method = "CoarseToFineCachedGridNcc";

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
