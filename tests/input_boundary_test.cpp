#include "TestSupport.h"

#include "input/IScrollDriver.h"
#include "input/ManualDriver.h"
#include "session/SessionInputSupport.h"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <sstream>

namespace {

class ThrowingDriver final : public rillshot::input::IScrollDriver {
public:
    rillshot::core::Status advance(
        const rillshot::input::ScrollRequest&) override {
        throw std::runtime_error("synthetic input failure");
    }
};

class UnknownThrowingDriver final : public rillshot::input::IScrollDriver {
public:
    rillshot::core::Status advance(
        const rillshot::input::ScrollRequest&) override {
        throw 7;
    }
};

class RejectingDriver final : public rillshot::input::IScrollDriver {
public:
    rillshot::core::Status advance(
        const rillshot::input::ScrollRequest&) override {
        return rillshot::core::Status::failure(
            "synthetic-rejection", "driver rejected input");
    }
};

int testStandardExceptionBecomesFailure() {
    ThrowingDriver driver;
    const auto status = rillshot::session::detail::advanceScrollSafely(
        driver, rillshot::input::ScrollRequest{});
    if (status.ok || status.code != "scroll-driver-exception" ||
        status.message.find("synthetic input failure") == std::string::npos) {
        return fail("standard input exceptions must become structured failures");
    }
    return EXIT_SUCCESS;
}

int testUnknownExceptionBecomesFailure() {
    UnknownThrowingDriver driver;
    const auto status = rillshot::session::detail::advanceScrollSafely(
        driver, rillshot::input::ScrollRequest{});
    if (status.ok || status.code != "scroll-driver-exception" ||
        status.message != "scroll driver raised an unknown exception") {
        return fail("unknown input exceptions must become structured failures");
    }
    return EXIT_SUCCESS;
}

int testExistingFailureIsPreserved() {
    RejectingDriver driver;
    const auto status = rillshot::session::detail::advanceScrollSafely(
        driver, rillshot::input::ScrollRequest{});
    if (status.ok || status.code != "synthetic-rejection" ||
        status.message != "driver rejected input") {
        return fail("ordinary driver failures must pass through unchanged");
    }
    return EXIT_SUCCESS;
}

int testManualInputConfirmation() {
    std::wistringstream input{L"\n"};
    std::wostringstream output;
    const auto status = rillshot::input::detail::awaitManualAdvance(input, output);
    if (!status.ok || output.str().empty()) {
        return fail("manual confirmation must continue after a readable line");
    }
    return EXIT_SUCCESS;
}

int testManualUserStop() {
    std::wistringstream input{L"q\n"};
    std::wostringstream output;
    const auto status = rillshot::input::detail::awaitManualAdvance(input, output);
    if (status.ok || status.code != "manual-user-stopped" ||
        rillshot::session::detail::stopReasonForScrollFailure(status) !=
            rillshot::core::StopReason::UserStopped) {
        return fail("manual q must remain an explicit graceful user stop");
    }
    return EXIT_SUCCESS;
}

int testClosedManualInputIsRejected() {
    std::wistringstream input;
    std::wostringstream output;
    const auto status = rillshot::input::detail::awaitManualAdvance(input, output);
    if (status.ok || status.code != "manual-input-closed" ||
        rillshot::session::detail::stopReasonForScrollFailure(status) !=
            rillshot::core::StopReason::ScrollRejected) {
        return fail("closed manual input must be a non-graceful input failure");
    }
    return EXIT_SUCCESS;
}

int testManualDriverExceptionIsNotUserStop() {
    const auto status = rillshot::core::Status::failure(
        "scroll-driver-exception", "synthetic manual exception");
    if (rillshot::session::detail::stopReasonForScrollFailure(status) !=
        rillshot::core::StopReason::ScrollRejected) {
        return fail("manual driver exceptions must not masquerade as user stops");
    }
    return EXIT_SUCCESS;
}

} // namespace

int main() {
    if (testStandardExceptionBecomesFailure() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (testUnknownExceptionBecomesFailure() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (testExistingFailureIsPreserved() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (testManualInputConfirmation() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (testManualUserStop() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (testClosedManualInputIsRejected() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (testManualDriverExceptionIsNotUserStop() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
