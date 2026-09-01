#include "cli/CliOptions.h"

#include "core/Types.h"

#include <map>
#include <optional>
#include <type_traits>
#include <utility>

namespace rillshot::cli {
namespace {

struct OptionSpec {
    bool requiresValue = true;
};

struct ParsedOptions {
    std::map<std::wstring, std::wstring> values;
    std::vector<ParseError> errors;
    bool jsonRequested = false;
};

const std::map<std::wstring, OptionSpec>& optionSpecs() {
    static const std::map<std::wstring, OptionSpec> specs = {
        {L"--region", {true}},
        {L"--out", {true}},
        {L"--backend", {true}},
        {L"--driver", {true}},
        {L"--direction", {true}},
        {L"--keyboard-key", {true}},
        {L"--key-repeats", {true}},
        {L"--scroll-point", {true}},
        {L"--max-frames", {true}},
        {L"--wheel-notches", {true}},
        {L"--min-wait-ms", {true}},
        {L"--max-wait-ms", {true}},
        {L"--sample-interval-ms", {true}},
        {L"--stable-samples", {true}},
        {L"--diff-threshold", {true}},
        {L"--hard-stitch-confidence-floor", {true}},
        {L"--max-output-mib", {true}},
        {L"--allow-low-confidence-seams", {false}},
        {L"--overwrite", {false}},
        {L"--ignore-top", {true}},
        {L"--ignore-bottom", {true}},
        {L"--json", {false}},
    };
    return specs;
}

std::string narrowOption(const std::wstring& value) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        result.push_back(character >= 0x20 && character <= 0x7e
            ? static_cast<char>(character)
            : '?');
    }
    return result;
}

ParsedOptions parseOptions(const std::vector<std::wstring>& arguments) {
    ParsedOptions parsed;
    const auto& specs = optionSpecs();
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::wstring& key = arguments[index];
        if (!key.starts_with(L"--")) {
            parsed.errors.push_back({
                "unexpected-argument",
                "unexpected positional argument: " + narrowOption(key)});
            continue;
        }

        const auto specIt = specs.find(key);
        if (specIt == specs.end()) {
            parsed.errors.push_back({
                "unknown-option", "unknown option: " + narrowOption(key)});
            continue;
        }
        if (!specIt->second.requiresValue) {
            if (!parsed.values.emplace(key, L"true").second) {
                parsed.errors.push_back({
                    "duplicate-option",
                    "option specified more than once: " + narrowOption(key)});
            }
            if (key == L"--json") {
                parsed.jsonRequested = true;
            }
            continue;
        }

        if (index + 1 >= arguments.size() || arguments[index + 1].starts_with(L"--")) {
            parsed.errors.push_back({
                "missing-option-value", "missing value for option: " + narrowOption(key)});
            continue;
        }
        const std::wstring& value = arguments[++index];
        if (!parsed.values.emplace(key, value).second) {
            parsed.errors.push_back({
                "duplicate-option",
                "option specified more than once: " + narrowOption(key)});
        }
    }
    return parsed;
}

std::optional<int> parseInt(const std::wstring& text) {
    try {
        std::size_t used = 0;
        const int value = std::stoi(text, &used, 10);
        if (used == text.size()) {
            return value;
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<double> parseDouble(const std::wstring& text) {
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used == text.size()) {
            return value;
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::vector<std::wstring> splitCommaSeparated(const std::wstring& text) {
    std::vector<std::wstring> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(L',', start);
        if (comma == std::wstring::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, comma - start));
        start = comma + 1;
    }
    return parts;
}

std::optional<rillshot::core::RectI> parseRect(const std::wstring& text) {
    const auto parts = splitCommaSeparated(text);
    if (parts.size() != 4) {
        return std::nullopt;
    }
    const auto x = parseInt(parts[0]);
    const auto y = parseInt(parts[1]);
    const auto width = parseInt(parts[2]);
    const auto height = parseInt(parts[3]);
    if (!x || !y || !width || !height) {
        return std::nullopt;
    }
    const rillshot::core::RectI region{*x, *y, *width, *height};
    return region.isValid()
        ? std::optional<rillshot::core::RectI>{region}
        : std::nullopt;
}

std::optional<rillshot::core::PointI> parsePoint(const std::wstring& text) {
    const auto parts = splitCommaSeparated(text);
    if (parts.size() != 2) {
        return std::nullopt;
    }
    const auto x = parseInt(parts[0]);
    const auto y = parseInt(parts[1]);
    if (!x || !y) {
        return std::nullopt;
    }
    return rillshot::core::PointI{*x, *y};
}

std::optional<rillshot::session::BackendChoice> parseBackend(
    const std::wstring& value) {
    using rillshot::session::BackendChoice;
    if (value == L"auto") return BackendChoice::Auto;
    if (value == L"dxgi") return BackendChoice::Dxgi;
    if (value == L"gdi") return BackendChoice::Gdi;
    return std::nullopt;
}

std::optional<rillshot::session::DriverChoice> parseDriver(
    const std::wstring& value) {
    using rillshot::session::DriverChoice;
    if (value == L"wheel") return DriverChoice::Wheel;
    if (value == L"keyboard") return DriverChoice::Keyboard;
    if (value == L"manual") return DriverChoice::Manual;
    return std::nullopt;
}

std::optional<rillshot::core::ScrollDirection> parseDirection(
    const std::wstring& value) {
    using rillshot::core::ScrollDirection;
    if (value == L"down") return ScrollDirection::Down;
    if (value == L"up") return ScrollDirection::Up;
    return std::nullopt;
}

std::optional<rillshot::input::KeyboardKey> parseKeyboardKey(
    const std::wstring& value) {
    using rillshot::input::KeyboardKey;
    if (value == L"page" || value == L"pagedown" || value == L"page-down" ||
        value == L"pageup" || value == L"page-up" || value == L"pgdn" ||
        value == L"pgup") {
        return KeyboardKey::Page;
    }
    if (value == L"space") return KeyboardKey::Space;
    if (value == L"arrow" || value == L"down" || value == L"up") {
        return KeyboardKey::Arrow;
    }
    return std::nullopt;
}

template <typename T>
bool setNumericOption(
    const std::map<std::wstring, std::wstring>& values,
    const std::wstring& key,
    T& target,
    std::vector<ParseError>& errors) {
    const auto it = values.find(key);
    if (it == values.end()) {
        return true;
    }

    std::optional<T> parsed;
    if constexpr (std::is_same_v<T, int>) {
        parsed = parseInt(it->second);
    } else if constexpr (std::is_same_v<T, double>) {
        parsed = parseDouble(it->second);
    }
    if (!parsed) {
        errors.push_back({
            "invalid-option-value", "invalid numeric value for " + narrowOption(key)});
        return false;
    }
    target = *parsed;
    return true;
}

void addMissingOption(
    std::vector<ParseError>& errors,
    const std::wstring& option) {
    errors.push_back({
        "missing-required-option", "required option is missing: " + narrowOption(option)});
}

} // namespace

CaptureParseResult parseCaptureArguments(
    const std::vector<std::wstring>& arguments) {
    CaptureParseResult result;
    if (arguments.size() == 1 && arguments.front() == L"--help") {
        result.helpRequested = true;
        return result;
    }

    ParsedOptions parsed = parseOptions(arguments);
    result.errors = std::move(parsed.errors);
    result.jsonRequested = parsed.jsonRequested;
    const auto& values = parsed.values;

    CaptureCommand command;
    command.json = result.jsonRequested;
    auto& options = command.options;

    const auto regionIt = values.find(L"--region");
    if (regionIt == values.end()) {
        addMissingOption(result.errors, L"--region");
    } else if (const auto region = parseRect(regionIt->second)) {
        options.region = *region;
    } else {
        result.errors.push_back({
            "invalid-region", "--region must be x,y,width,height with positive dimensions"});
    }

    const auto outIt = values.find(L"--out");
    if (outIt == values.end()) {
        addMissingOption(result.errors, L"--out");
    } else {
        options.outPath = outIt->second;
    }

    if (const auto it = values.find(L"--direction"); it != values.end()) {
        if (const auto direction = parseDirection(it->second)) {
            options.direction = *direction;
        } else {
            result.errors.push_back({
                "invalid-direction", "--direction must be down or up"});
        }
    }
    if (options.region.isValid()) {
        options.scrollPoint =
            rillshot::core::defaultScrollPoint(options.region, options.direction);
    }

    if (const auto it = values.find(L"--scroll-point"); it != values.end()) {
        if (const auto point = parsePoint(it->second)) {
            options.scrollPoint = *point;
        } else {
            result.errors.push_back({
                "invalid-scroll-point", "--scroll-point must be x,y"});
        }
    }
    if (const auto it = values.find(L"--backend"); it != values.end()) {
        if (const auto backend = parseBackend(it->second)) {
            options.backend = *backend;
        } else {
            result.errors.push_back({
                "invalid-backend", "--backend must be auto, dxgi, or gdi"});
        }
    }
    if (const auto it = values.find(L"--driver"); it != values.end()) {
        if (const auto driver = parseDriver(it->second)) {
            options.driver = *driver;
        } else {
            result.errors.push_back({
                "invalid-driver", "--driver must be wheel, keyboard, or manual"});
        }
    }
    if (const auto it = values.find(L"--keyboard-key"); it != values.end()) {
        if (const auto key = parseKeyboardKey(it->second)) {
            options.keyboardKey = *key;
        } else {
            result.errors.push_back({
                "invalid-keyboard-key", "--keyboard-key must be page, space, or arrow"});
        }
    }

    options.stopOnLowConfidenceSeams =
        !values.contains(L"--allow-low-confidence-seams");
    options.allowOverwrite = values.contains(L"--overwrite");

    int maxOutputMiB = static_cast<int>(
        options.maxAssembledImageBytes / (1024ULL * 1024ULL));
    setNumericOption(values, L"--max-frames", options.maxFrames, result.errors);
    setNumericOption(values, L"--wheel-notches", options.wheelNotches, result.errors);
    setNumericOption(values, L"--key-repeats", options.keyRepeats, result.errors);
    setNumericOption(values, L"--min-wait-ms", options.minWaitMs, result.errors);
    setNumericOption(values, L"--max-wait-ms", options.maxWaitMs, result.errors);
    setNumericOption(
        values, L"--sample-interval-ms", options.sampleIntervalMs, result.errors);
    setNumericOption(
        values, L"--stable-samples", options.stableSamplesRequired, result.errors);
    setNumericOption(values, L"--diff-threshold", options.diffThreshold, result.errors);
    setNumericOption(
        values,
        L"--hard-stitch-confidence-floor",
        options.hardStitchConfidenceFloor,
        result.errors);
    setNumericOption(values, L"--max-output-mib", maxOutputMiB, result.errors);
    setNumericOption(values, L"--ignore-top", options.ignoreTopPx, result.errors);
    setNumericOption(values, L"--ignore-bottom", options.ignoreBottomPx, result.errors);

    if (maxOutputMiB <= 0) {
        result.errors.push_back({
            "invalid-output-budget", "--max-output-mib must be a positive integer"});
    } else {
        options.maxAssembledImageBytes =
            static_cast<std::uint64_t>(maxOutputMiB) * 1024ULL * 1024ULL;
    }

    if (result.errors.empty()) {
        const auto validation =
            rillshot::session::validateCaptureSessionOptions(options);
        if (!validation.ok) {
            result.errors.push_back({validation.code, validation.message});
        }
    }
    if (result.errors.empty()) {
        result.command = std::move(command);
    }
    return result;
}

} // namespace rillshot::cli
