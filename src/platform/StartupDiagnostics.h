#pragma once

#include <string>
#include <string_view>

namespace rillshot::platform {

void writeStartupLog(std::wstring_view message) noexcept;
[[nodiscard]] std::wstring startupLogPath() noexcept;

} // namespace rillshot::platform
