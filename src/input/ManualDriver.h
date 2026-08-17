#pragma once

#include "input/IScrollDriver.h"

#include <iosfwd>

namespace rillshot::input {

class ManualDriver final : public IScrollDriver {
public:
    [[nodiscard]] rillshot::core::Status advance(const ScrollRequest& request) override;
};

namespace detail {

[[nodiscard]] rillshot::core::Status awaitManualAdvance(
    std::wistream& input,
    std::wostream& output);

} // namespace detail

} // namespace rillshot::input
