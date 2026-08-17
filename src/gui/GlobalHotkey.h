#pragma once

#include "gui/HotkeyBinding.h"

#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace rillshot::gui {

enum class HotkeyRegistrationStatus {
    Disabled,
    Registered,
    Unchanged,
    Invalid,
    Conflict,
    SystemError
};

struct HotkeyRegistrationResult {
    HotkeyRegistrationStatus status = HotkeyRegistrationStatus::Disabled;
    DWORD systemError = ERROR_SUCCESS;

    [[nodiscard]] bool usable() const noexcept {
        return status == HotkeyRegistrationStatus::Registered ||
               status == HotkeyRegistrationStatus::Unchanged;
    }
};

class GlobalHotkey final {
public:
    GlobalHotkey() = default;
    ~GlobalHotkey();

    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

    [[nodiscard]] bool initialize(std::function<void()> callback) noexcept;
    [[nodiscard]] HotkeyRegistrationResult apply(
        bool enabled,
        HotkeyBinding binding) noexcept;
    void unregister() noexcept;
    void shutdown() noexcept;

    [[nodiscard]] static constexpr int messageId() noexcept { return 0x6A01; }

private:
    struct ApplyRequest;

    [[nodiscard]] static LRESULT CALLBACK windowProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) noexcept;
    void receiverThreadMain() noexcept;
    [[nodiscard]] HotkeyRegistrationResult applyOnReceiverThread(
        bool enabled,
        HotkeyBinding binding) noexcept;
    void unregisterOnReceiverThread() noexcept;

    std::thread receiverThread_;
    std::atomic<bool> shutdownRequested_{false};
    std::mutex stateMutex_;
    std::condition_variable initializationChanged_;
    HWND receiverWindow_ = nullptr;
    DWORD receiverThreadId_ = 0;
    bool initializationComplete_ = false;
    bool initializationSucceeded_ = false;
    std::function<void()> callback_;
    std::optional<HotkeyBinding> binding_;
};

} // namespace rillshot::gui
