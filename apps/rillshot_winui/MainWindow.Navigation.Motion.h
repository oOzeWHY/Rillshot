#pragma once

#include <chrono>

namespace winrt::Rillshot::WinUI::implementation::navigation_motion {

inline constexpr auto navigationDuration = std::chrono::milliseconds(220);
inline constexpr auto navigationResizeFallbackInterval = std::chrono::microseconds(16'667);
inline constexpr auto navigationResizeMinimumInterval = std::chrono::microseconds(4'000);
inline constexpr auto navigationResizeMaximumInterval = std::chrono::microseconds(16'667);
inline constexpr float navigationDistance = 10.0F;
inline constexpr float outgoingScale = 0.992F;
inline constexpr float incomingScale = 0.985F;

} // namespace winrt::Rillshot::WinUI::implementation::navigation_motion
