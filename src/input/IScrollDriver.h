#pragma once

#include "core/Types.h"

namespace rillshot::input {

enum class KeyboardKey {
    Page,
    Space,
    Arrow
};

struct ScrollRequest {
    rillshot::core::PointI point;
    int notches = 5;
    int keyRepeats = 1;
    bool down = true;
    KeyboardKey keyboardKey = KeyboardKey::Page;
};

class IScrollDriver {
public:
    virtual ~IScrollDriver() = default;
    [[nodiscard]] virtual rillshot::core::Status advance(const ScrollRequest& request) = 0;
};

} // namespace rillshot::input
