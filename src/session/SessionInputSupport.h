#pragma once

#include "core/Types.h"
#include "input/IScrollDriver.h"

namespace rillshot::session::detail {

[[nodiscard]] rillshot::core::Status advanceScrollSafely(
    rillshot::input::IScrollDriver& driver,
    const rillshot::input::ScrollRequest& request);

[[nodiscard]] rillshot::core::StopReason stopReasonForScrollFailure(
    const rillshot::core::Status& status) noexcept;

} // namespace rillshot::session::detail
