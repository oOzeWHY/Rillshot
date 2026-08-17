#pragma once

#include "core/Types.h"

#include <string>

namespace rillshot::gui {

[[nodiscard]] std::wstring validationMessageZh(const rillshot::core::Status& status);
[[nodiscard]] std::wstring sessionMessageZh(const std::string& message);
[[nodiscard]] const wchar_t* stopReasonZh(rillshot::core::StopReason reason) noexcept;
[[nodiscard]] const wchar_t* stopGuidanceZh(rillshot::core::StopReason reason) noexcept;

} // namespace rillshot::gui
