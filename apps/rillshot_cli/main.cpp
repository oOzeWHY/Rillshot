#include "core/Types.h"
#include "platform/WinDpi.h"
#include "session/CaptureSession.h"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <cwchar>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using rillshot::core::PointI;
using rillshot::core::RectI;
using rillshot::core::ScrollDirection;
using rillshot::session::BackendChoice;
using rillshot::session::CaptureSession;
using rillshot::session::CaptureSessionOptions;
using rillshot::input::KeyboardKey;
using rillshot::session::DriverChoice;

namespace {

void printHelp() {
    std::wcout << LR"(Rillshot 1.1.9 CLI

Usage:
  rillshot capture --region x,y,w,h --out result.png [options]

Options:
  --region x,y,w,h        Required physical-pixel capture region.
  --out path             Required output path, .png or .bmp.
  --backend auto|dxgi|gdi Default: auto.
  --driver wheel|keyboard|manual
                          Default: wheel.
  --direction down|up     Default: down.
  --keyboard-key page|space|arrow
                          Default: page. Used with --driver keyboard.
  --key-repeats n         Default: 1. Used with --driver keyboard.
  --scroll-point x,y      Default: center near the selected direction edge.
  --max-frames n          Default: 20.
  --wheel-notches n       Default: 5.
  --min-wait-ms n         Default: 120.
  --max-wait-ms n         Default: 1500.
  --sample-interval-ms n  Default: 50.
  --stable-samples n      Default: 2.
  --diff-threshold f      Default: 0.003. Range: 0.0 to 1.0.
  --hard-stitch-confidence-floor f
                          Default: 0.30. Range: 0.0 to 1.0.
  --max-output-mib n      Default: 512. Range is further limited by WIC.
  --allow-low-confidence-seams
                          Continue after ambiguous/low-information seams.
                          Default is conservative: stop and preserve partial output.
  --overwrite             Replace existing output and companion files.
                          Without this flag, existing files are preserved.
  --ignore-top px         Default: 0. Useful for sticky headers.
  --ignore-bottom px      Default: 0. Useful for fixed footer/status bars.

Examples:
  rillshot capture --region 100,100,900,700 --scroll-point 550,760 --out result.png
  rillshot capture --region 100,100,900,700 --direction up --driver keyboard --keyboard-key page --out result-up.png
  rillshot capture --region 100,100,900,700 --driver manual --out result-manual.png
)";
}

struct OptionSpec {
    bool requiresValue = true;
};

struct ParsedOptions {
    std::map<std::wstring, std::wstring> values;
    std::vector<std::wstring> errors;
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
    };
    return specs;
}

ParsedOptions parseOptions(int argc, wchar_t** argv, int start) {
    ParsedOptions parsed;
    const auto& specs = optionSpecs();
    for (int i = start; i < argc; ++i) {
        const std::wstring key = argv[i];
        if (!key.starts_with(L"--")) {
            parsed.errors.push_back(L"Unexpected positional argument: " + key);
            continue;
        }

        const auto specIt = specs.find(key);
        if (specIt == specs.end()) {
            parsed.errors.push_back(L"Unknown option: " + key);
            continue;
        }

        if (!specIt->second.requiresValue) {
            parsed.values[key] = L"true";
            continue;
        }

        if (i + 1 >= argc || std::wstring(argv[i + 1]).starts_with(L"--")) {
            parsed.errors.push_back(L"Missing value for option: " + key);
            continue;
        }

        parsed.values[key] = argv[++i];
    }
    return parsed;
}

std::optional<int> parseInt(const std::wstring& text) {
    try {
        size_t used = 0;
        const int value = std::stoi(text, &used, 10);
        if (used != text.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> parseDouble(const std::wstring& text) {
    try {
        size_t used = 0;
        const double value = std::stod(text, &used);
        if (used != text.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<std::wstring> splitCommaSeparated(const std::wstring& text) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(L',', start);
        if (comma == std::wstring::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, comma - start));
        start = comma + 1;
    }
    return parts;
}

std::optional<RectI> parseRect(const std::wstring& text) {
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
    RectI r{*x, *y, *width, *height};
    if (!r.isValid()) {
        return std::nullopt;
    }
    return r;
}

std::optional<PointI> parsePoint(const std::wstring& text) {
    const auto parts = splitCommaSeparated(text);
    if (parts.size() != 2) {
        return std::nullopt;
    }
    const auto x = parseInt(parts[0]);
    const auto y = parseInt(parts[1]);
    if (!x || !y) {
        return std::nullopt;
    }
    return PointI{*x, *y};
}

std::optional<BackendChoice> parseBackend(const std::wstring& value) {
    if (value == L"auto") return BackendChoice::Auto;
    if (value == L"dxgi") return BackendChoice::Dxgi;
    if (value == L"gdi") return BackendChoice::Gdi;
    return std::nullopt;
}

std::optional<DriverChoice> parseDriver(const std::wstring& value) {
    if (value == L"wheel") return DriverChoice::Wheel;
    if (value == L"keyboard") return DriverChoice::Keyboard;
    if (value == L"manual") return DriverChoice::Manual;
    return std::nullopt;
}

std::optional<ScrollDirection> parseDirection(const std::wstring& value) {
    if (value == L"down") return ScrollDirection::Down;
    if (value == L"up") return ScrollDirection::Up;
    return std::nullopt;
}

std::optional<KeyboardKey> parseKeyboardKey(const std::wstring& value) {
    if (value == L"page" || value == L"pagedown" || value == L"page-down" || value == L"pageup" || value == L"page-up" || value == L"pgdn" || value == L"pgup") return KeyboardKey::Page;
    if (value == L"space") return KeyboardKey::Space;
    if (value == L"arrow" || value == L"down" || value == L"up") return KeyboardKey::Arrow;
    return std::nullopt;
}

std::wstring widenAscii(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring widenAscii(const char* value) {
    std::string s = value ? value : "";
    return widenAscii(s);
}

template <typename T>
bool setIfPresent(const std::map<std::wstring, std::wstring>& opts, const std::wstring& key, T& target) {
    const auto it = opts.find(key);
    if (it == opts.end()) {
        return true;
    }
    if constexpr (std::is_same_v<T, int>) {
        if (auto parsed = parseInt(it->second)) {
            target = *parsed;
            return true;
        }
    } else if constexpr (std::is_same_v<T, double>) {
        if (auto parsed = parseDouble(it->second)) {
            target = *parsed;
            return true;
        }
    }
    return false;
}

std::wstring lowerExtension(const std::wstring& path) {
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return {};
    }
    std::wstring ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) {
        return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
    });
    return ext;
}

bool isSupportedOutputPath(const std::wstring& path) {
    const auto ext = lowerExtension(path);
    return ext == L"png" || ext == L"bmp";
}

int runCapture(int argc, wchar_t** argv) {
    const auto parsed = parseOptions(argc, argv, 2);
    if (!parsed.errors.empty()) {
        for (const auto& error : parsed.errors) {
            std::wcerr << error << L"\n";
        }
        printHelp();
        return 2;
    }

    const auto& opts = parsed.values;
    CaptureSessionOptions sessionOptions;
    int maxOutputMiB = static_cast<int>(
        sessionOptions.maxAssembledImageBytes / (1024ULL * 1024ULL));

    const auto regionIt = opts.find(L"--region");
    const auto outIt = opts.find(L"--out");
    if (regionIt == opts.end() || outIt == opts.end()) {
        printHelp();
        return 2;
    }

    const auto region = parseRect(regionIt->second);
    if (!region) {
        std::wcerr << L"Invalid --region. Expected x,y,w,h with positive width/height and no coordinate overflow.\n";
        return 2;
    }

    if (outIt->second.empty() || !isSupportedOutputPath(outIt->second)) {
        std::wcerr << L"Invalid --out. Expected a non-empty .png or .bmp path.\n";
        return 2;
    }

    sessionOptions.region = *region;
    sessionOptions.outPath = outIt->second;

    if (const auto it = opts.find(L"--direction"); it != opts.end()) {
        const auto direction = parseDirection(it->second);
        if (!direction) {
            std::wcerr << L"Invalid --direction. Expected down or up.\n";
            return 2;
        }
        sessionOptions.direction = *direction;
    }
    sessionOptions.scrollPoint =
        rillshot::core::defaultScrollPoint(sessionOptions.region, sessionOptions.direction);

    if (const auto it = opts.find(L"--scroll-point"); it != opts.end()) {
        const auto point = parsePoint(it->second);
        if (!point) {
            std::wcerr << L"Invalid --scroll-point. Expected x,y with no trailing text.\n";
            return 2;
        }
        sessionOptions.scrollPoint = *point;
    }

    if (const auto it = opts.find(L"--backend"); it != opts.end()) {
        const auto backend = parseBackend(it->second);
        if (!backend) {
            std::wcerr << L"Invalid --backend. Expected auto, dxgi, or gdi.\n";
            return 2;
        }
        sessionOptions.backend = *backend;
    }
    if (const auto it = opts.find(L"--driver"); it != opts.end()) {
        const auto driver = parseDriver(it->second);
        if (!driver) {
            std::wcerr << L"Invalid --driver. Expected wheel, keyboard, or manual.\n";
            return 2;
        }
        sessionOptions.driver = *driver;
    }
    if (const auto it = opts.find(L"--keyboard-key"); it != opts.end()) {
        const auto key = parseKeyboardKey(it->second);
        if (!key) {
            std::wcerr << L"Invalid --keyboard-key. Expected page, space, or arrow.\n";
            return 2;
        }
        sessionOptions.keyboardKey = *key;
    }
    if (opts.contains(L"--allow-low-confidence-seams")) {
        sessionOptions.stopOnLowConfidenceSeams = false;
    }
    if (opts.contains(L"--overwrite")) {
        sessionOptions.allowOverwrite = true;
    }

    if (!setIfPresent(opts, L"--max-frames", sessionOptions.maxFrames) ||
        !setIfPresent(opts, L"--wheel-notches", sessionOptions.wheelNotches) ||
        !setIfPresent(opts, L"--key-repeats", sessionOptions.keyRepeats) ||
        !setIfPresent(opts, L"--min-wait-ms", sessionOptions.minWaitMs) ||
        !setIfPresent(opts, L"--max-wait-ms", sessionOptions.maxWaitMs) ||
        !setIfPresent(opts, L"--sample-interval-ms", sessionOptions.sampleIntervalMs) ||
        !setIfPresent(opts, L"--stable-samples", sessionOptions.stableSamplesRequired) ||
        !setIfPresent(opts, L"--diff-threshold", sessionOptions.diffThreshold) ||
        !setIfPresent(opts, L"--hard-stitch-confidence-floor", sessionOptions.hardStitchConfidenceFloor) ||
        !setIfPresent(opts, L"--max-output-mib", maxOutputMiB) ||
        !setIfPresent(opts, L"--ignore-top", sessionOptions.ignoreTopPx) ||
        !setIfPresent(opts, L"--ignore-bottom", sessionOptions.ignoreBottomPx)) {
        std::wcerr << L"Invalid numeric option value.\n";
        return 2;
    }
    if (maxOutputMiB <= 0) {
        std::wcerr << L"Invalid --max-output-mib. Expected a positive integer.\n";
        return 2;
    }
    sessionOptions.maxAssembledImageBytes =
        static_cast<std::uint64_t>(maxOutputMiB) * 1024ULL * 1024ULL;

    const auto validation = rillshot::session::validateCaptureSessionOptions(sessionOptions);
    if (!validation.ok) {
        std::wcerr << L"Invalid capture options: " << widenAscii(validation.message) << L".\n";
        return 2;
    }

    CaptureSession session;
    const auto result = session.run(sessionOptions);

    std::wcout << L"StopReason: " << widenAscii(rillshot::core::toString(result.stopReason)) << L"\n";
    std::wcout << L"Frames: " << result.framesCaptured << L", seams: " << result.seams << L"\n";
    std::wcout << L"Message: " << widenAscii(result.message) << L"\n";
    std::wcout << L"Output saved: " << (result.outputSaved ? L"yes" : L"no") << L"\n";
    if (result.outputSaved) {
        std::wcout << L"Output: " << sessionOptions.outPath << L"\n";
    }
    std::wcout << L"Comparison frame saved: " << (result.comparisonFrameSaved ? L"yes" : L"no") << L"\n";
    if (result.comparisonFrameSaved) {
        std::wcout << L"Comparison frame: " << result.comparisonFramePath << L"\n";
    }
    std::wcout << L"Diagnostics saved: " << (result.diagnosticsSaved ? L"yes" : L"no") << L"\n";
    if (result.diagnosticsSaved) {
        std::wcout << L"Diagnostics: " << sessionOptions.outPath << L".jsonl\n";
    }

    return result.ok ? 0 : 1;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    rillshot::platform::enablePerMonitorDpiV2BestEffort();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool coInitialized = SUCCEEDED(hr);

    int exitCode = 0;
    if (argc < 2 || std::wstring(argv[1]) == L"--help" || std::wstring(argv[1]) == L"help") {
        printHelp();
    } else if (std::wstring(argv[1]) == L"capture") {
        exitCode = runCapture(argc, argv);
    } else {
        printHelp();
        exitCode = 2;
    }

    if (coInitialized) {
        CoUninitialize();
    }
    return exitCode;
}
