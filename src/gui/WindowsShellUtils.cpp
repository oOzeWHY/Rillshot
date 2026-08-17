#include "gui/WindowsShellUtils.h"
#include "platform/AppPaths.h"

#include <ShlObj.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <system_error>

namespace rillshot::gui::win32 {
namespace {

using Microsoft::WRL::ComPtr;

struct CoTaskMemDeleter {
    void operator()(void* value) const noexcept {
        CoTaskMemFree(value);
    }
};

std::filesystem::path outputDirectory() {
    return rillshot::platform::applicationDirectory(L"captures");
}

bool hasBmpExtension(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    for (wchar_t& character : extension) {
        if (character >= L'A' && character <= L'Z') {
            character = static_cast<wchar_t>(character + (L'a' - L'A'));
        }
    }
    return extension == L".bmp";
}

} // namespace

bool protectWindowFromCapture(HWND window) noexcept {
    // WDA_EXCLUDEFROMCAPTURE is available from Windows 10 version 2004. Use
    // the documented numeric value so older SDK headers can still build; on an
    // older OS this degrades to WDA_MONITOR behavior.
    constexpr DWORD excludeFromCaptureAffinity = 0x00000011UL;
    return window && IsWindow(window) &&
        SetWindowDisplayAffinity(window, excludeFromCaptureAffinity) != FALSE;
}

void clearWindowCaptureProtection(HWND window) noexcept {
    if (window && IsWindow(window)) {
        [[maybe_unused]] const BOOL cleared =
            SetWindowDisplayAffinity(window, WDA_NONE);
    }
}

bool hideWindowForCapture(HWND window) noexcept {
    if (!window || !IsWindow(window)) {
        return false;
    }

    // Keep this affinity scoped to the selection/capture boundary. Permanent
    // exclusion would make the ordinary app window disappear from meetings and
    // third-party screenshots even while Rillshot is idle.
    [[maybe_unused]] const bool captureProtectionEnabled =
        protectWindowFromCapture(window);
    ShowWindow(window, SW_HIDE);
    if (IsWindowVisible(window) != FALSE) {
        return false;
    }

    // The first flush retires our queued surface update. Waiting for two
    // ordinary 60 Hz frames lets the exposed target repaint before the second
    // fence. The capture worker adds a longer cancellable settle period.
    const HRESULT firstFlush = DwmFlush();
    Sleep(34);
    const HRESULT secondFlush = DwmFlush();
    return SUCCEEDED(firstFlush) && SUCCEEDED(secondFlush) &&
        IsWindow(window) && IsWindowVisible(window) == FALSE;
}

std::wstring defaultOutputPath() {
    try {
        SYSTEMTIME now{};
        GetLocalTime(&now);

        const auto processId = static_cast<unsigned long>(GetCurrentProcessId());
        std::array<wchar_t, 112> fileName{};
        _snwprintf_s(
            fileName.data(),
            fileName.size(),
            _TRUNCATE,
            L"Rillshot-%04u%02u%02u-%02u%02u%02u-%03u-%lu.png",
            static_cast<unsigned int>(now.wYear),
            static_cast<unsigned int>(now.wMonth),
            static_cast<unsigned int>(now.wDay),
            static_cast<unsigned int>(now.wHour),
            static_cast<unsigned int>(now.wMinute),
            static_cast<unsigned int>(now.wSecond),
            static_cast<unsigned int>(now.wMilliseconds),
            processId);

        const auto directory = outputDirectory();
        if (directory.empty()) {
            return {};
        }
        auto candidate = directory / fileName.data();
        std::error_code error;
        for (unsigned int suffix = 1;
             std::filesystem::exists(candidate, error) && !error;
             ++suffix) {
            std::array<wchar_t, 128> suffixedName{};
            _snwprintf_s(
                suffixedName.data(),
                suffixedName.size(),
                _TRUNCATE,
                L"Rillshot-%04u%02u%02u-%02u%02u%02u-%03u-%lu-%u.png",
                static_cast<unsigned int>(now.wYear),
                static_cast<unsigned int>(now.wMonth),
                static_cast<unsigned int>(now.wDay),
                static_cast<unsigned int>(now.wHour),
                static_cast<unsigned int>(now.wMinute),
                static_cast<unsigned int>(now.wSecond),
                static_cast<unsigned int>(now.wMilliseconds),
                processId,
                suffix);
            candidate = directory / suffixedName.data();
            error.clear();
        }
        return candidate.wstring();
    } catch (...) {
        return {};
    }
}

std::optional<std::wstring> chooseOutputPath(HWND owner, const std::wstring& currentPath) {
    try {
        ComPtr<IFileSaveDialog> dialog;
        HRESULT result = CoCreateInstance(
            CLSID_FileSaveDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog));
        if (FAILED(result)) {
            return std::nullopt;
        }

        FILEOPENDIALOGOPTIONS options{};
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            dialog->SetOptions(
                options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                FOS_OVERWRITEPROMPT | FOS_NOREADONLYRETURN | FOS_NOCHANGEDIR);
        }

        constexpr COMDLG_FILTERSPEC filters[] = {
            {L"PNG 图像 (*.png)", L"*.png"},
            {L"位图 (*.bmp)", L"*.bmp"},
        };
        dialog->SetFileTypes(2U, filters);

        const std::filesystem::path current(currentPath);
        const bool bmp = hasBmpExtension(current);
        dialog->SetFileTypeIndex(bmp ? 2U : 1U);
        dialog->SetDefaultExtension(bmp ? L"bmp" : L"png");
        if (!current.filename().empty()) {
            dialog->SetFileName(current.filename().c_str());
        }

        const auto parent = current.parent_path();
        std::error_code fileSystemError;
        if (!parent.empty() &&
            std::filesystem::is_directory(parent, fileSystemError) &&
            !fileSystemError) {
            ComPtr<IShellItem> folder;
            if (SUCCEEDED(SHCreateItemFromParsingName(
                    parent.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder.Get());
            }
        }

        result = dialog->Show(owner);
        if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED) || FAILED(result)) {
            return std::nullopt;
        }

        ComPtr<IShellItem> selected;
        if (FAILED(dialog->GetResult(&selected))) {
            return std::nullopt;
        }

        PWSTR rawPath = nullptr;
        if (FAILED(selected->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || !rawPath) {
            CoTaskMemFree(rawPath);
            return std::nullopt;
        }

        [[maybe_unused]] const std::unique_ptr<wchar_t, CoTaskMemDeleter> pathOwner(rawPath);
        std::filesystem::path selectedPath(rawPath);

        if (selectedPath.extension().empty()) {
            UINT selectedType = 1;
            dialog->GetFileTypeIndex(&selectedType);
            selectedPath.replace_extension(selectedType == 2 ? L".bmp" : L".png");
        }
        return selectedPath.wstring();
    } catch (...) {
        return std::nullopt;
    }
}

bool openOutputFile(HWND owner, const std::wstring& path) noexcept {
    try {
        std::error_code error;
        if (path.empty() || !std::filesystem::is_regular_file(path, error) || error) {
            return false;
        }
        const auto result = reinterpret_cast<std::intptr_t>(
            ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        return result > 32;
    } catch (...) {
        return false;
    }
}

bool revealOutputFile([[maybe_unused]] HWND owner, const std::wstring& path) noexcept {
    try {
        std::error_code error;
        if (path.empty() || !std::filesystem::is_regular_file(path, error) || error) {
            return false;
        }

        PIDLIST_ABSOLUTE itemId = nullptr;
        const HRESULT parsed = SHParseDisplayName(path.c_str(), nullptr, &itemId, 0, nullptr);
        if (FAILED(parsed) || !itemId) {
            CoTaskMemFree(itemId);
            return false;
        }
        const HRESULT opened = SHOpenFolderAndSelectItems(itemId, 0, nullptr, 0);
        CoTaskMemFree(itemId);
        return SUCCEEDED(opened);
    } catch (...) {
        return false;
    }
}

bool copyTextToClipboard(HWND owner, std::wstring_view text) noexcept {
    if (text.empty() || text.size() >= std::numeric_limits<std::size_t>::max() / sizeof(wchar_t)) {
        return false;
    }

    const std::size_t bytes = (text.size() + 1U) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        return false;
    }
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(destination, text.data(), text.size() * sizeof(wchar_t));
    static_cast<wchar_t*>(destination)[text.size()] = L'\0';
    GlobalUnlock(memory);

    if (!OpenClipboard(owner)) {
        GlobalFree(memory);
        return false;
    }
    const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    CloseClipboard();
    if (!copied) {
        GlobalFree(memory);
    }
    return copied;
}

} // namespace rillshot::gui::win32
