#include <windows.h>

#include <string>
#include <vector>

namespace {

void showLaunchError(const std::wstring& detail, const DWORD errorCode = 0) noexcept {
    std::wstring message = detail;
    if (errorCode != 0) {
        LPWSTR systemMessage = nullptr;
        const auto length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            errorCode,
            0,
            reinterpret_cast<LPWSTR>(&systemMessage),
            0,
            nullptr);
        if (length != 0 && systemMessage != nullptr) {
            message.append(L"\n\n");
            message.append(systemMessage, length);
            LocalFree(systemMessage);
        }
    }
    MessageBoxW(
        nullptr,
        message.c_str(),
        L"Rillshot",
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::vector<wchar_t> modulePathBuffer(32'768);
    const auto modulePathLength = GetModuleFileNameW(
        nullptr,
        modulePathBuffer.data(),
        static_cast<DWORD>(modulePathBuffer.size()));
    if (modulePathLength == 0 ||
        modulePathLength >= static_cast<DWORD>(modulePathBuffer.size())) {
        showLaunchError(L"无法确定 Rillshot 的安装位置。", GetLastError());
        return 2;
    }

    std::wstring rootPath(modulePathBuffer.data(), modulePathLength);
    const auto separator = rootPath.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        showLaunchError(L"Rillshot 的安装路径无效。");
        return 2;
    }
    rootPath.resize(separator);

    const auto appDirectory = rootPath + L"\\app";
    const auto executablePath = appDirectory + L"\\Rillshot.WinUI.exe";
    const auto attributes = GetFileAttributesW(executablePath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        showLaunchError(
            L"Rillshot 运行文件不完整。请重新解压整个 Portable 压缩包。\n\n缺少：" +
            executablePath);
        return 2;
    }

    std::wstring commandLine = L"\"" + executablePath + L"\"";
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    if (CreateProcessW(
            executablePath.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            appDirectory.c_str(),
            &startupInfo,
            &processInfo) == FALSE) {
        showLaunchError(L"Rillshot 无法启动。", GetLastError());
        return 2;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return 0;
}
