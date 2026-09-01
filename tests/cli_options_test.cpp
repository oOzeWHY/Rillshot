#include "TestSupport.h"

#include "cli/CliOptions.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace {

using rillshot::cli::parseCaptureArguments;

int testMinimumCaptureCommand() {
    const auto parsed = parseCaptureArguments({
        L"--region", L"100,120,800,600", L"--out", L"result.png", L"--json"});
    if (!parsed.command || !parsed.errors.empty() || !parsed.jsonRequested) {
        return fail("minimum capture command should parse");
    }
    const auto& command = *parsed.command;
    if (!command.json || command.options.region.x != 100 ||
        command.options.region.y != 120 || command.options.region.width != 800 ||
        command.options.region.height != 600 ||
        command.options.outPath != L"result.png") {
        return fail("minimum capture command values were not preserved");
    }
    const auto expectedPoint = rillshot::core::defaultScrollPoint(
        command.options.region, command.options.direction);
    if (command.options.scrollPoint.x != expectedPoint.x ||
        command.options.scrollPoint.y != expectedPoint.y) {
        return fail("default scroll point must be derived after region parsing");
    }
    return EXIT_SUCCESS;
}

int testUpDirectionChangesDefaultScrollPoint() {
    const auto parsed = parseCaptureArguments({
        L"--region", L"-900,40,800,600", L"--direction", L"up",
        L"--out", L"result.bmp"});
    if (!parsed.command) {
        return fail("upward capture with negative monitor coordinates should parse");
    }
    const auto& options = parsed.command->options;
    const auto expectedPoint =
        rillshot::core::defaultScrollPoint(options.region, options.direction);
    if (options.scrollPoint.x != expectedPoint.x ||
        options.scrollPoint.y != expectedPoint.y) {
        return fail("direction must be applied before deriving the scroll point");
    }
    return EXIT_SUCCESS;
}

int testDuplicateAndUnknownOptionsAreRejected() {
    const auto parsed = parseCaptureArguments({
        L"--region", L"0,0,800,600", L"--region", L"0,0,640,480",
        L"--out", L"result.png", L"--future-option"});
    bool duplicateFound = false;
    bool unknownFound = false;
    for (const auto& error : parsed.errors) {
        duplicateFound = duplicateFound || error.code == "duplicate-option";
        unknownFound = unknownFound || error.code == "unknown-option";
    }
    if (parsed.command || !duplicateFound || !unknownFound) {
        return fail("duplicate and unknown options must fail predictably");
    }
    return EXIT_SUCCESS;
}

int testInvalidBoundsUseSessionValidation() {
    const auto parsed = parseCaptureArguments({
        L"--region", L"0,0,800,600", L"--scroll-point", L"801,300",
        L"--out", L"result.png", L"--max-frames", L"0"});
    if (parsed.command || parsed.errors.empty()) {
        return fail("out-of-range capture options must fail");
    }
    bool rangeFailureFound = false;
    for (const auto& error : parsed.errors) {
        rangeFailureFound = rangeFailureFound ||
            error.code == "invalid-scroll-point" ||
            error.code == "invalid-max-frames";
    }
    if (!rangeFailureFound) {
        return fail("session validation must preserve a stable failure code");
    }
    return EXIT_SUCCESS;
}

int testOverwriteIsExplicit() {
    const auto defaultCommand = parseCaptureArguments({
        L"--region", L"0,0,800,600", L"--out", L"result.png"});
    const auto overwriteCommand = parseCaptureArguments({
        L"--region", L"0,0,800,600", L"--out", L"result.png", L"--overwrite"});
    if (!defaultCommand.command || defaultCommand.command->options.allowOverwrite ||
        !overwriteCommand.command || !overwriteCommand.command->options.allowOverwrite) {
        return fail("overwrite must remain opt-in");
    }
    return EXIT_SUCCESS;
}

int testHelpIsSideEffectFree() {
    const auto parsed = parseCaptureArguments({L"--help"});
    if (!parsed.helpRequested || parsed.command || !parsed.errors.empty()) {
        return fail("capture help must not require capture options");
    }
    return EXIT_SUCCESS;
}

} // namespace

int main() {
    if (testMinimumCaptureCommand() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testUpDirectionChangesDefaultScrollPoint() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testDuplicateAndUnknownOptionsAreRejected() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testInvalidBoundsUseSessionValidation() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testOverwriteIsExplicit() != EXIT_SUCCESS) return EXIT_FAILURE;
    if (testHelpIsSideEffectFree() != EXIT_SUCCESS) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
