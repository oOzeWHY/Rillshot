#include "platform/WinUtf.h"

#include <Windows.h>

#include <limits>
#include <stdexcept>

namespace rillshot::platform {

std::string wideToUtf8(const std::wstring& input) {
    if (input.empty()) {
        return {};
    }
    if (input.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        throw std::length_error("UTF-16 input exceeds the Windows conversion limit");
    }
    const int sourceLength = static_cast<int>(input.size());
    const int needed = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), sourceLength,
        nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        throw std::runtime_error("WideCharToMultiByte failed");
    }
    std::string output(static_cast<size_t>(needed), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), sourceLength,
        output.data(), needed, nullptr, nullptr);
    if (written != needed) {
        throw std::runtime_error("WideCharToMultiByte returned an incomplete result");
    }
    return output;
}

std::wstring utf8ToWide(const std::string& input) {
    if (input.empty()) {
        return {};
    }
    if (input.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        throw std::length_error("UTF-8 input exceeds the Windows conversion limit");
    }
    const int sourceLength = static_cast<int>(input.size());
    const int needed = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), sourceLength, nullptr, 0);
    if (needed <= 0) {
        throw std::runtime_error("MultiByteToWideChar failed");
    }
    std::wstring output(static_cast<size_t>(needed), L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), sourceLength,
        output.data(), needed);
    if (written != needed) {
        throw std::runtime_error("MultiByteToWideChar returned an incomplete result");
    }
    return output;
}

} // namespace rillshot::platform
