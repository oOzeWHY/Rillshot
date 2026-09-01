#pragma once

#include <chrono>

namespace winrt::Rillshot::WinUI::implementation::navigation_motion {

// Fluent uses a fast 167 ms exit and a 250 ms normal entrance. The native
// window resize follows the longer entrance clock so all visible state settles
// together without making the outgoing page linger.
inline constexpr auto outgoingDuration = std::chrono::milliseconds(167);
inline constexpr auto incomingDuration = std::chrono::milliseconds(250);
inline constexpr auto navigationDuration = incomingDuration;
inline constexpr float navigationDistance = 10.0F;
inline constexpr float outgoingScale = 0.992F;
inline constexpr float incomingScale = 0.985F;

} // namespace winrt::Rillshot::WinUI::implementation::navigation_motion
