#include "core/ImageMetrics.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rillshot::core {

static int safeStep(int value) noexcept {
    return std::max(1, value);
}

double meanAbsDiffRatio(const Image& a, const Image& b, const DifferenceOptions& options) {
    if (a.width() != b.width() || a.height() != b.height()) {
        throw std::invalid_argument("meanAbsDiffRatio requires equal image dimensions");
    }
    if (a.empty()) {
        return 0.0;
    }

    const int y0 = std::clamp(options.ignoreTopPx, 0, a.height());
    // Clamp the raw bottom exclusion before subtracting it. Subtracting an
    // untrusted INT_MIN value from the image height would otherwise overflow
    // before std::clamp had a chance to repair the range.
    const int ignoredBottom = std::clamp(
        options.ignoreBottomPx, 0, a.height() - y0);
    const int y1 = a.height() - ignoredBottom;
    const int xs = safeStep(options.xStep);
    const int ys = safeStep(options.yStep);
    const int requestedPhases = std::clamp(options.samplePhases, 1, 2);
    const int phases = xs == 1 && ys == 1 ? 1 : requestedPhases;

    double sum = 0.0;
    long long count = 0;

    for (int phase = 0; phase < phases; ++phase) {
        const int xOffset = phase == 0 ? 0 : xs / 2;
        const int yOffset = phase == 0 ? 0 : ys / 2;
        for (int y = y0 + yOffset; y < y1; y += ys) {
            const auto* ar = a.row(y);
            const auto* br = b.row(y);
            for (int x = xOffset; x < a.width(); x += xs) {
                const int i = x * 4;
                const double db = std::abs(static_cast<int>(ar[i + 0]) - static_cast<int>(br[i + 0]));
                const double dg = std::abs(static_cast<int>(ar[i + 1]) - static_cast<int>(br[i + 1]));
                const double dr = std::abs(static_cast<int>(ar[i + 2]) - static_cast<int>(br[i + 2]));
                sum += (db + dg + dr) / (3.0 * 255.0);
                ++count;
            }
        }
    }

    if (count == 0) {
        return 0.0;
    }
    return sum / static_cast<double>(count);
}

ImageSummary summarizeImage(const Image& image, int xStep, int yStep) {
    ImageSummary summary;
    if (image.empty()) {
        return summary;
    }

    const int xs = safeStep(xStep);
    const int ys = safeStep(yStep);
    double sum = 0.0;
    double sumSquared = 0.0;
    long long darkCount = 0;
    long long count = 0;

    for (int y = 0; y < image.height(); y += ys) {
        const auto* row = image.row(y);
        for (int x = 0; x < image.width(); x += xs) {
            const int i = x * 4;
            const double b = static_cast<double>(row[i + 0]);
            const double g = static_cast<double>(row[i + 1]);
            const double r = static_cast<double>(row[i + 2]);
            const double luma = (0.0722 * b + 0.7152 * g + 0.2126 * r) / 255.0;
            sum += luma;
            sumSquared += luma * luma;
            if (luma <= 0.02) {
                ++darkCount;
            }
            ++count;
        }
    }

    if (count == 0) {
        return summary;
    }

    const double sampleCount = static_cast<double>(count);
    summary.meanLuma = sum / sampleCount;
    const double variance = std::max(0.0, sumSquared / sampleCount - summary.meanLuma * summary.meanLuma);
    summary.lumaStdDev = std::sqrt(variance);
    summary.darkPixelRatio = static_cast<double>(darkCount) / sampleCount;
    summary.suspiciouslyNearBlack = summary.meanLuma <= 0.02 &&
                                    summary.lumaStdDev <= 0.01 &&
                                    summary.darkPixelRatio >= 0.995;
    return summary;
}

} // namespace rillshot::core
