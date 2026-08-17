#pragma once

#include <string>

namespace rillshot::platform {

std::string wideToUtf8(const std::wstring& input);
std::wstring utf8ToWide(const std::string& input);

} // namespace rillshot::platform
