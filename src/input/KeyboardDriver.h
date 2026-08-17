#pragma once

#include "input/IScrollDriver.h"

namespace rillshot::input {

class KeyboardDriver final : public IScrollDriver {
public:
    [[nodiscard]] rillshot::core::Status advance(const ScrollRequest& request) override;
};

} // namespace rillshot::input
