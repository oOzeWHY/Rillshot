#include "platform/AppPaths.h"

#include <Windows.h>
#include <appmodel.h>
#include <ShlObj.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace rillshot::platform {
namespace {

constexpr std::size_t initialPathCapacity = 512;
constexpr std::size_t maximumPathCapacity = 32768;

bool isSafeDirectoryName(std::wstring_view value) noexcept {
    return !value.empty() &&
           value != L"." &&
           value != L".." &&
           value.find_first_of(L"\\/") == std::wstring_view::npos;
}

bool hasPackageIdentity() noexcept {
    UINT32 length = 0;
    return GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
}

std::filesystem::path packagedApplicationDataRoot() {
    UINT32 familyNameLength = 0;
    const LONG probe = GetCurrentPackageFamilyName(&familyNameLength, nullptr);
    if (probe != ERROR_INSUFFICIENT_BUFFER || familyNameLength <= 1) {
        return {};
    }

    std::vector<wchar_t> familyName(familyNameLength);
    if (GetCurrentPackageFamilyName(&familyNameLength, familyName.data()) != ERROR_SUCCESS ||
        familyNameLength <= 1 || familyName.front() == L'\0') {
        return {};
    }

    PWSTR localAppDataRaw = nullptr;
    if (FAILED(SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_DEFAULT,
            nullptr,
            &localAppDataRaw)) ||
        !localAppDataRaw) {
        CoTaskMemFree(localAppDataRaw);
        return {};
    }

    const std::filesystem::path localAppData(localAppDataRaw);
    CoTaskMemFree(localAppDataRaw);
    return localAppData /
        L"Packages" /
        std::wstring(familyName.data(), familyNameLength - 1U) /
        L"LocalState";
}

} // namespace

std::filesystem::path executableDirectory() noexcept {
    try {
        std::vector<wchar_t> buffer(initialPathCapacity);
        while (buffer.size() <= maximumPathCapacity &&
               buffer.size() <= static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())) {
            const DWORD length = GetModuleFileNameW(
                nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) {
                return {};
            }
            if (length < buffer.size()) {
                return std::filesystem::path(
                    std::wstring(buffer.data(), static_cast<std::size_t>(length))).parent_path();
            }
            if (buffer.size() == maximumPathCapacity) {
                return {};
            }
            buffer.resize((std::min)(buffer.size() * 2U, maximumPathCapacity));
        }
    } catch (...) {
    }
    return {};
}

std::filesystem::path applicationDirectory(std::wstring_view directoryName) noexcept {
    try {
        if (!isSafeDirectoryName(directoryName)) {
            return {};
        }
        const auto root = hasPackageIdentity()
            ? packagedApplicationDataRoot()
            : executableDirectory();
        if (root.empty()) {
            return {};
        }
        const auto directory = root / directoryName;
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error || !std::filesystem::is_directory(directory, error) || error) {
            return {};
        }
        return directory;
    } catch (...) {
        return {};
    }
}

} // namespace rillshot::platform
