#include "cli/CliOptions.h"
#include "core/Json.h"
#include "core/Types.h"
#include "platform/WinDpi.h"
#include "platform/WinUtf.h"
#include "session/CaptureSession.h"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#ifndef RILLSHOT_VERSION
#define RILLSHOT_VERSION "1.2.0"
#endif

namespace {

using rillshot::cli::ParseError;
using rillshot::session::CaptureSession;
using rillshot::session::CaptureSessionResult;

constexpr int exitSuccess = 0;
constexpr int exitCaptureFailed = 1;
constexpr int exitUsage = 2;

struct ComApartment final {
    ~ComApartment() noexcept {
        CoUninitialize();
    }
};

std::string helpText() {
    return
        "Rillshot " RILLSHOT_VERSION " CLI\n\n"
        "Usage:\n"
        "  rillshot-cli.exe capture --region x,y,w,h --out result.png [options]\n"
        "  rillshot-cli.exe --version\n\n"
        "Options:\n"
        "  --region x,y,w,h        Required physical-pixel capture region.\n"
        "  --out path               Required output path, .png or .bmp.\n"
        "  --backend auto|dxgi|gdi  Default: auto.\n"
        "  --driver wheel|keyboard|manual\n"
        "                           Default: wheel.\n"
        "  --direction down|up      Default: down.\n"
        "  --keyboard-key page|space|arrow\n"
        "                           Default: page. Used with --driver keyboard.\n"
        "  --key-repeats n          Default: 1. Used with --driver keyboard.\n"
        "  --scroll-point x,y       Default: center near the direction edge.\n"
        "  --max-frames n           Default: 20.\n"
        "  --wheel-notches n        Default: 5.\n"
        "  --min-wait-ms n          Default: 120.\n"
        "  --max-wait-ms n          Default: 1500.\n"
        "  --sample-interval-ms n   Default: 50.\n"
        "  --stable-samples n       Default: 2.\n"
        "  --diff-threshold f       Default: 0.003. Range: 0.0 to 1.0.\n"
        "  --hard-stitch-confidence-floor f\n"
        "                           Default: 0.30. Range: 0.0 to 1.0.\n"
        "  --max-output-mib n       Default: 512. Also bounded by WIC.\n"
        "  --allow-low-confidence-seams\n"
        "                           Continue after ambiguous seams.\n"
        "  --overwrite              Replace existing output and companion files.\n"
        "                           Existing files are preserved by default.\n"
        "  --ignore-top px          Default: 0. For sticky headers.\n"
        "  --ignore-bottom px       Default: 0. For fixed footers/status bars.\n"
        "  --json                   Write one UTF-8 JSON result object to stdout.\n"
        "  --help                   Show help for the capture command.\n\n"
        "Exit codes:\n"
        "  0  Capture completed with a graceful stop reason.\n"
        "  1  Capture or process initialization failed.\n"
        "  2  Command-line usage error.\n\n"
        "Examples:\n"
        "  rillshot-cli.exe capture --region 100,100,900,700 --out result.png\n"
        "  rillshot-cli.exe capture --region 100,100,900,700 --out result.png --json\n";
}

std::string quoteJson(const std::string& value) {
    return "\"" + rillshot::core::escapeJsonString(value) + "\"";
}

std::string wideJson(const std::wstring& value) {
    return quoteJson(rillshot::platform::wideToUtf8(value));
}

void writeUtf8(DWORD standardHandle, const std::string& text) noexcept {
    const HANDLE output = GetStdHandle(standardHandle);
    if (output == nullptr || output == INVALID_HANDLE_VALUE) {
        return;
    }
    constexpr std::size_t maximumChunk = 32U * 1024U;
    DWORD consoleMode = 0;
    if (GetConsoleMode(output, &consoleMode) != FALSE) {
        try {
            const std::wstring wide = rillshot::platform::utf8ToWide(text);
            std::size_t offset = 0;
            while (offset < wide.size()) {
                const DWORD requested = static_cast<DWORD>(
                    std::min(maximumChunk, wide.size() - offset));
                DWORD written = 0;
                if (WriteConsoleW(
                        output,
                        wide.data() + offset,
                        requested,
                        &written,
                        nullptr) == FALSE || written == 0) {
                    break;
                }
                offset += written;
            }
            return;
        } catch (...) {
            // Fall through to the byte path for a best-effort diagnostic if a
            // system exception supplied malformed UTF-8.
        }
    }
    std::size_t offset = 0;
    while (offset < text.size()) {
        const DWORD requested = static_cast<DWORD>(
            std::min(maximumChunk, text.size() - offset));
        DWORD written = 0;
        if (WriteFile(
                output,
                text.data() + offset,
                requested,
                &written,
                nullptr) == FALSE || written == 0) {
            break;
        }
        offset += written;
    }
}

void writeStdout(const std::string& text) noexcept {
    writeUtf8(STD_OUTPUT_HANDLE, text);
}

void writeStderr(const std::string& text) noexcept {
    writeUtf8(STD_ERROR_HANDLE, text);
}

std::string errorsJson(
    const std::string& command,
    int exitCode,
    const std::vector<ParseError>& errors) {
    std::ostringstream output;
    output << "{\"schemaVersion\":1,\"command\":" << quoteJson(command)
           << ",\"ok\":false,\"exitCode\":" << exitCode << ",\"errors\":[";
    for (std::size_t index = 0; index < errors.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << "{\"code\":" << quoteJson(errors[index].code)
               << ",\"message\":" << quoteJson(errors[index].message) << '}';
    }
    output << "]}\n";
    return output.str();
}

void reportErrors(
    const std::string& command,
    int exitCode,
    const std::vector<ParseError>& errors,
    bool json) {
    if (json) {
        writeStderr(errorsJson(command, exitCode, errors));
        return;
    }
    std::ostringstream output;
    for (const auto& error : errors) {
        output << "Error [" << error.code << "]: " << error.message << '\n';
    }
    output << "Run `rillshot-cli.exe capture --help` for usage.\n";
    writeStderr(output.str());
}

std::string captureResultJson(
    const rillshot::session::CaptureSessionOptions& options,
    const CaptureSessionResult& result,
    int exitCode) {
    const std::wstring diagnosticsPath = options.outPath + L".jsonl";
    std::ostringstream output;
    output << "{\"schemaVersion\":1,\"command\":\"capture\",\"ok\":"
           << (result.ok ? "true" : "false")
           << ",\"exitCode\":" << exitCode
           << ",\"stopReason\":"
           << quoteJson(rillshot::core::toString(result.stopReason))
           << ",\"framesCaptured\":" << result.framesCaptured
           << ",\"seams\":" << result.seams
           << ",\"message\":" << quoteJson(result.message)
           << ",\"output\":{\"saved\":"
           << (result.outputSaved ? "true" : "false")
           << ",\"path\":" << wideJson(options.outPath) << '}'
           << ",\"comparisonFrame\":{\"saved\":"
           << (result.comparisonFrameSaved ? "true" : "false")
           << ",\"path\":";
    if (result.comparisonFrameSaved) {
        output << wideJson(result.comparisonFramePath);
    } else {
        output << "null";
    }
    output << "},\"diagnostics\":{\"saved\":"
           << (result.diagnosticsSaved ? "true" : "false")
           << ",\"path\":" << wideJson(diagnosticsPath) << "}}\n";
    return output.str();
}

std::string captureResultText(
    const rillshot::session::CaptureSessionOptions& options,
    const CaptureSessionResult& result) {
    std::ostringstream output;
    output << "Stop reason: " << rillshot::core::toString(result.stopReason) << '\n'
           << "Frames: " << result.framesCaptured << ", seams: " << result.seams << '\n'
           << "Message: " << result.message << '\n'
           << "Output saved: " << (result.outputSaved ? "yes" : "no") << '\n';
    if (result.outputSaved) {
        output << "Output: " << rillshot::platform::wideToUtf8(options.outPath) << '\n';
    }
    output << "Comparison frame saved: "
           << (result.comparisonFrameSaved ? "yes" : "no") << '\n';
    if (result.comparisonFrameSaved) {
        output << "Comparison frame: "
               << rillshot::platform::wideToUtf8(result.comparisonFramePath) << '\n';
    }
    output << "Diagnostics saved: " << (result.diagnosticsSaved ? "yes" : "no") << '\n';
    if (result.diagnosticsSaved) {
        output << "Diagnostics: "
               << rillshot::platform::wideToUtf8(options.outPath + L".jsonl") << '\n';
    }
    return output.str();
}

bool hasArgument(int argc, wchar_t** argv, const std::wstring& expected) {
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == expected) {
            return true;
        }
    }
    return false;
}

std::string commandNameBestEffort(int argc, wchar_t** argv) noexcept {
    if (argc < 2) {
        return {};
    }
    try {
        return rillshot::platform::wideToUtf8(argv[1]);
    } catch (...) {
        return {};
    }
}

int runCapture(const rillshot::cli::CaptureCommand& command) {
    rillshot::platform::enablePerMonitorDpiV2BestEffort();

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) {
        reportErrors(
            "capture",
            exitCaptureFailed,
            {{"com-initialization-failed", "Windows COM initialization failed"}},
            command.json);
        return exitCaptureFailed;
    }
    [[maybe_unused]] const ComApartment apartment;

    CaptureSession session;
    const auto result = session.run(command.options);

    const int exitCode = result.ok ? exitSuccess : exitCaptureFailed;
    if (command.json) {
        writeStdout(captureResultJson(command.options, result, exitCode));
    } else {
        writeStdout(captureResultText(command.options, result));
    }
    return exitCode;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const bool jsonRequested = hasArgument(argc, argv, L"--json");
    try {
        if (argc < 2 || std::wstring(argv[1]) == L"--help" ||
            std::wstring(argv[1]) == L"help") {
            writeStdout(helpText());
            return exitSuccess;
        }
        if (std::wstring(argv[1]) == L"--version" ||
            std::wstring(argv[1]) == L"version") {
            writeStdout("Rillshot " RILLSHOT_VERSION "\n");
            return exitSuccess;
        }
        if (std::wstring(argv[1]) != L"capture") {
            reportErrors(
                "",
                exitUsage,
                {{"unknown-command", "expected capture, help, or version"}},
                jsonRequested);
            return exitUsage;
        }

        std::vector<std::wstring> arguments;
        arguments.reserve(static_cast<std::size_t>(argc - 2));
        for (int index = 2; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }
        auto parsed = rillshot::cli::parseCaptureArguments(arguments);
        if (parsed.helpRequested) {
            writeStdout(helpText());
            return exitSuccess;
        }
        if (!parsed.command) {
            reportErrors(
                "capture", exitUsage, parsed.errors, parsed.jsonRequested);
            return exitUsage;
        }
        return runCapture(*parsed.command);
    } catch (const std::exception& exception) {
        reportErrors(
            commandNameBestEffort(argc, argv),
            exitCaptureFailed,
            {{"internal-error", exception.what()}},
            jsonRequested);
        return exitCaptureFailed;
    } catch (...) {
        reportErrors(
            commandNameBestEffort(argc, argv),
            exitCaptureFailed,
            {{"internal-error", "unknown internal failure"}},
            jsonRequested);
        return exitCaptureFailed;
    }
}
