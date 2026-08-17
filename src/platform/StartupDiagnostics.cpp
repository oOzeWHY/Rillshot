#include "platform/StartupDiagnostics.h"
#include "platform/AppPaths.h"

#include <Windows.h>

#include <array>
#include <cwchar>
#include <filesystem>
#include <limits>
#include <mutex>

namespace rillshot::platform {
namespace {

std::filesystem::path resolveLogPath() {
    const auto directory = applicationDirectory(L"logs");
    if (directory.empty()) {
        return {};
    }
    return directory / L"startup.log";
}

std::string utf8FromWide(std::wstring_view value) {
    if (value.empty() ||
        value.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return {};
    }
    const int sourceLength = static_cast<int>(value.size());
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), sourceLength, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string output(static_cast<size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), sourceLength,
            output.data(), required, nullptr, nullptr) != required) {
        return {};
    }
    return output;
}

} // namespace

std::wstring startupLogPath() noexcept {
    try {
        return resolveLogPath().wstring();
    } catch (...) {
        return {};
    }
}

void writeStartupLog(std::wstring_view message) noexcept {
    try {
        static std::mutex logMutex;
        const std::scoped_lock lock(logMutex);

        const auto path = resolveLogPath();
        if (path.empty()) {
            return;
        }

        SYSTEMTIME now{};
        GetLocalTime(&now);
        std::array<wchar_t, 64> prefix{};
        _snwprintf_s(
            prefix.data(), prefix.size(), _TRUNCATE,
            L"%04u-%02u-%02u %02u:%02u:%02u.%03u [pid=%lu] ",
            static_cast<unsigned int>(now.wYear),
            static_cast<unsigned int>(now.wMonth),
            static_cast<unsigned int>(now.wDay),
            static_cast<unsigned int>(now.wHour),
            static_cast<unsigned int>(now.wMinute),
            static_cast<unsigned int>(now.wSecond),
            static_cast<unsigned int>(now.wMilliseconds),
            static_cast<unsigned long>(GetCurrentProcessId()));

        std::wstring line(prefix.data());
        line.append(message);
        line.append(L"\r\n");
        const std::string utf8 = utf8FromWide(line);
        if (utf8.empty() || utf8.size() > static_cast<size_t>((std::numeric_limits<DWORD>::max)())) {
            return;
        }

        const HANDLE file = CreateFileW(
            path.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }
        DWORD written = 0;
        WriteFile(
            file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        CloseHandle(file);
    } catch (...) {
        // Startup diagnostics must never become a startup failure.
    }
}

} // namespace rillshot::platform
