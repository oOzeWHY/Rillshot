#pragma once

#include "session/CaptureSession.h"

#include <string>

namespace rillshot::gui {

// Builds the user-facing capture summary independently from a specific UI
// framework. Win32 and a possible WinUI 3 shell should present the same facts,
// especially partial-output and diagnostic availability.
[[nodiscard]] std::wstring buildCaptureSummaryZh(
    const rillshot::session::CaptureSessionResult& result,
    const std::wstring& outputPath,
    const std::wstring& diagnosticMessage = {});

} // namespace rillshot::gui
