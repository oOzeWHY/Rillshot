#include "session/SessionDiagnostics.h"

#include "core/Json.h"

#include <filesystem>
#include <limits>
#include <system_error>

namespace rillshot::session::detail {

JsonlLogger::JsonlLogger(const std::wstring& outPath, bool allowOverwrite) {
    path_ = outPath + L".jsonl";
    const auto parentPath = std::filesystem::path(path_).parent_path();
    if (!parentPath.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parentPath, ec);
        if (ec) {
            error_ = "could not create diagnostics directory: " + ec.message();
            return;
        }
    }
    file_ = CreateFileW(
        path_.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        allowOverwrite ? CREATE_ALWAYS : CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        error_ = "could not create diagnostics file: " +
            std::system_category().message(static_cast<int>(error));
    }
}

JsonlLogger::~JsonlLogger() {
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
    }
}

void JsonlLogger::event(const std::string& name, const std::string& fields) {
    if (file_ == INVALID_HANDLE_VALUE) {
        return;
    }

    std::string line = "{\"event\":\"" + rillshot::core::escapeJsonString(name) + "\"";
    if (!fields.empty()) {
        line += "," + fields;
    }
    line += "}\n";

    if (line.size() > (std::numeric_limits<DWORD>::max)()) {
        error_ = "diagnostics event exceeds the Windows write limit";
        return;
    }

    const DWORD bytesToWrite = static_cast<DWORD>(line.size());
    DWORD written = 0;
    if (!WriteFile(file_, line.data(), bytesToWrite, &written, nullptr)) {
        const DWORD error = GetLastError();
        error_ = "could not write diagnostics file: " +
            std::system_category().message(static_cast<int>(error));
        return;
    }
    if (written != bytesToWrite) {
        error_ = "diagnostics file write was incomplete";
        return;
    }
    if (!FlushFileBuffers(file_)) {
        const DWORD error = GetLastError();
        error_ = "could not flush diagnostics file: " +
            std::system_category().message(static_cast<int>(error));
    }
}

bool JsonlLogger::ok() const noexcept {
    return file_ != INVALID_HANDLE_VALUE && error_.empty();
}

const std::string& JsonlLogger::error() const noexcept {
    return error_;
}

std::string q(const std::string& value) {
    return "\"" + rillshot::core::escapeJsonString(value) + "\"";
}

std::string backendName(BackendChoice choice) {
    switch (choice) {
    case BackendChoice::Auto: return "auto";
    case BackendChoice::Dxgi: return "dxgi";
    case BackendChoice::Gdi: return "gdi";
    }
    return "unknown";
}

std::string driverName(DriverChoice choice) {
    switch (choice) {
    case DriverChoice::Wheel: return "wheel";
    case DriverChoice::Keyboard: return "keyboard";
    case DriverChoice::Manual: return "manual";
    }
    return "unknown";
}

std::string keyboardKeyName(rillshot::input::KeyboardKey key) {
    using rillshot::input::KeyboardKey;
    switch (key) {
    case KeyboardKey::Page: return "page";
    case KeyboardKey::Space: return "space";
    case KeyboardKey::Arrow: return "arrow";
    }
    return "unknown";
}

} // namespace rillshot::session::detail
