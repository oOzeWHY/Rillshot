#include "pch.h"

#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "platform/StartupDiagnostics.h"

#include <atomic>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Rillshot::WinUI::implementation {
namespace {

void showStartupFailure(const std::wstring& detail) noexcept {
    static std::atomic_bool shown = false;
    if (shown.exchange(true)) {
        return;
    }
    rillshot::platform::writeStartupLog(L"Fatal startup failure: " + detail);

    try {
        std::wstring message =
            L"Rillshot 无法打开主窗口。\r\n\r\n" + detail;
        const auto logPath = rillshot::platform::startupLogPath();
        if (!logPath.empty()) {
            message += L"\r\n\r\n启动日志：\r\n" + logPath;
        }
        MessageBoxW(nullptr, message.c_str(), L"Rillshot 启动失败", MB_OK | MB_ICONERROR);
    } catch (...) {
        MessageBoxW(
            nullptr,
            L"Rillshot 无法打开主窗口。请检查应用数据目录中的启动日志。",
            L"Rillshot 启动失败",
            MB_OK | MB_ICONERROR);
    }
}

} // namespace

App::App() {
    rillshot::platform::writeStartupLog(L"Application object constructed.");
    UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& eventArgs) {
        const std::wstring detail = eventArgs.Message().c_str();
        rillshot::platform::writeStartupLog(L"Unhandled XAML exception: " + detail);
        if (IsDebuggerPresent()) {
            __debugbreak();
        } else {
            showStartupFailure(detail);
        }
    });
#if defined(_DEBUG) && !defined(DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION)
    if (IsDebuggerPresent()) {
        rillshot::platform::writeStartupLog(L"Debugger attached.");
    }
#endif
}

void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& eventArgs) {
    rillshot::platform::writeStartupLog(L"OnLaunched entered.");
    try {
        window_ = make<MainWindow>();
        window_.Activate();
        rillshot::platform::writeStartupLog(L"Main window activated.");
    } catch (const hresult_error& error) {
        showStartupFailure(error.message().c_str());
        throw;
    } catch (...) {
        showStartupFailure(L"Unknown native exception.");
        throw;
    }
}

} // namespace winrt::Rillshot::WinUI::implementation
