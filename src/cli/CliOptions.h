#pragma once

#include "session/CaptureSession.h"

#include <optional>
#include <string>
#include <vector>

namespace rillshot::cli {

struct ParseError {
    std::string code;
    std::string message;
};

struct CaptureCommand {
    rillshot::session::CaptureSessionOptions options;
    bool json = false;
};

struct CaptureParseResult {
    std::optional<CaptureCommand> command;
    std::vector<ParseError> errors;
    bool helpRequested = false;
    bool jsonRequested = false;
};

// Parses arguments following the `capture` command. The parser is kept
// platform-neutral so the CLI contract can be tested without invoking capture.
[[nodiscard]] CaptureParseResult parseCaptureArguments(
    const std::vector<std::wstring>& arguments);

} // namespace rillshot::cli
