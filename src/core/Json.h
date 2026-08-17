#pragma once

#include <string>

namespace rillshot::core {

// Escapes one UTF-8 byte string for use inside a JSON string literal.
// The returned value does not include surrounding quotation marks.
[[nodiscard]] std::string escapeJsonString(const std::string& input);

} // namespace rillshot::core
