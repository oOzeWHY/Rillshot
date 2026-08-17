#include "platform/WinDpi.h"

#include <Windows.h>

namespace rillshot::platform {

void enablePerMonitorDpiV2BestEffort() {
    // The application manifest is the primary DPI declaration. This call is a
    // best-effort fallback and must run before creating any HWND.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

} // namespace rillshot::platform
