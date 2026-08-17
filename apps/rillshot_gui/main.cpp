#include "gui/MainWindow.h"
#include "platform/WinDpi.h"

#include <Windows.h>
#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    rillshot::platform::enablePerMonitorDpiV2BestEffort();

    const HRESULT comResult = CoInitializeEx(
        nullptr,
        COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool comInitialized = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        MessageBoxW(nullptr, L"无法初始化 Windows 对话框服务。", L"Rillshot", MB_OK | MB_ICONERROR);
        return 1;
    }

    rillshot::gui::MainWindow mainWindow(instance);
    if (!mainWindow.create(showCommand)) {
        MessageBoxW(nullptr, L"无法创建 Rillshot 主窗口。", L"Rillshot", MB_OK | MB_ICONERROR);
        if (comInitialized) {
            CoUninitialize();
        }
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (comInitialized) {
        CoUninitialize();
    }
    return static_cast<int>(message.wParam);
}
