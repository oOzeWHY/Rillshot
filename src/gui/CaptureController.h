#pragma once

#include "session/CaptureSession.h"

#include <Windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace rillshot::gui {

inline constexpr UINT captureCompleteMessage = WM_APP + 1U;

struct CaptureCompletion {
    rillshot::session::CaptureSessionResult result;
    std::wstring outputPath;
};

class CaptureController final {
public:
    using CompletionNotifier = std::function<void()>;

    CaptureController() = default;
    ~CaptureController();

    CaptureController(const CaptureController&) = delete;
    CaptureController& operator=(const CaptureController&) = delete;

    [[nodiscard]] bool start(
        HWND notificationWindow,
        rillshot::session::CaptureSessionOptions options);
    [[nodiscard]] bool start(
        HWND hiddenCaptureWindow,
        rillshot::session::CaptureSessionOptions options,
        CompletionNotifier completionNotifier);
    // The notifier is invoked once on the worker thread after the completion
    // value is published. It must only enqueue/post work to the owning UI thread;
    // it must not read XAML/Win32 controls or synchronously join this controller.
    void requestStop() noexcept;
    void joinCompleted();
    [[nodiscard]] std::optional<CaptureCompletion> takeCompletion();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] bool hasWorker() const noexcept { return worker_.joinable(); }

private:
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex completionMutex_;
    std::optional<CaptureCompletion> completion_;
};

} // namespace rillshot::gui
