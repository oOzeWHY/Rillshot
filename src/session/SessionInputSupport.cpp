#include "session/SessionInputSupport.h"

#include <exception>
#include <string>

namespace rillshot::session::detail {

rillshot::core::Status advanceScrollSafely(
    rillshot::input::IScrollDriver& driver,
    const rillshot::input::ScrollRequest& request) {
    try {
        return driver.advance(request);
    } catch (const std::exception& exception) {
        return rillshot::core::Status::failure(
            "scroll-driver-exception",
            std::string("scroll driver raised an exception: ") +
                exception.what());
    } catch (...) {
        return rillshot::core::Status::failure(
            "scroll-driver-exception",
            "scroll driver raised an unknown exception");
    }
}

rillshot::core::StopReason stopReasonForScrollFailure(
    const rillshot::core::Status& status) noexcept {
    return status.code == "manual-user-stopped"
        ? rillshot::core::StopReason::UserStopped
        : rillshot::core::StopReason::ScrollRejected;
}

} // namespace rillshot::session::detail
