#pragma once

#include "session/CaptureSession.h"

#include <string>

#include <windows.h>

namespace rillshot::session::detail {

class JsonlLogger final {
public:
    explicit JsonlLogger(const std::wstring& outPath, bool allowOverwrite);
    ~JsonlLogger();

    JsonlLogger(const JsonlLogger&) = delete;
    JsonlLogger& operator=(const JsonlLogger&) = delete;

    void event(const std::string& name, const std::string& fields = {});
    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const std::string& error() const noexcept;

private:
    std::wstring path_;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    std::string error_;
};

[[nodiscard]] std::string q(const std::string& value);
[[nodiscard]] std::string backendName(BackendChoice choice);
[[nodiscard]] std::string driverName(DriverChoice choice);
[[nodiscard]] std::string keyboardKeyName(rillshot::input::KeyboardKey key);

} // namespace rillshot::session::detail
