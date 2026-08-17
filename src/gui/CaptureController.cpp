#include "gui/CaptureController.h"

#include <objbase.h>

#include <chrono>
#include <exception>
#include <utility>

namespace rillshot::gui {

CaptureController::~CaptureController() {
    requestStop();
    joinCompleted();
}

bool CaptureController::start(
    HWND notificationWindow,
    rillshot::session::CaptureSessionOptions options) {

    if (!notificationWindow || !IsWindow(notificationWindow)) {
        return false;
    }
    return start(
        notificationWindow,
        std::move(options),
        [notificationWindow]() noexcept {
            PostMessageW(notificationWindow, captureCompleteMessage, 0, 0);
        });
}

bool CaptureController::start(
    HWND hiddenCaptureWindow,
    rillshot::session::CaptureSessionOptions options,
    CompletionNotifier completionNotifier) {

    // A minimized window still carries WS_VISIBLE and can remain in a DWM
    // transition. The capture boundary only accepts a fully hidden live HWND.
    if (!hiddenCaptureWindow || !IsWindow(hiddenCaptureWindow) ||
        IsWindowVisible(hiddenCaptureWindow) != FALSE) {
        return false;
    }

    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return false;
    }
    if (worker_.joinable()) {
        running_.store(false);
        return false;
    }
    {
        const std::scoped_lock lock(completionMutex_);
        completion_.reset();
    }

    stopRequested_.store(false);
    try {
        worker_ = std::thread([
            this,
            hiddenCaptureWindow,
            options = std::move(options),
            completionNotifier = std::move(completionNotifier)]() mutable {
            CaptureCompletion completion;
            completion.outputPath = options.outPath;

            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const bool comInitialized = SUCCEEDED(comResult);

            auto callerShouldStop = std::move(options.shouldStop);
            options.shouldStop = [
                this,
                callerShouldStop = std::move(callerShouldStop)]() noexcept {
                if (stopRequested_.load() ||
                    (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
                    return true;
                }
                try {
                    return callerShouldStop && callerShouldStop();
                } catch (...) {
                    // A cancellation callback is a boundary supplied by the
                    // caller. Fail closed instead of terminating the worker.
                    return true;
                }
            };

            // Let the exposed target repaint after the application's window was
            // hidden and passed the compositor fence.
            for (int elapsedMs = 0; elapsedMs < 350 && !options.shouldStop(); elapsedMs += 25) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }

            try {
                if (FAILED(comResult)) {
                    completion.result.stopReason = rillshot::core::StopReason::CaptureFailed;
                    completion.result.message = "could not initialize COM on the capture worker";
                } else if (!IsWindow(hiddenCaptureWindow) ||
                           IsWindowVisible(hiddenCaptureWindow) != FALSE) {
                    completion.result.stopReason = rillshot::core::StopReason::CaptureFailed;
                    completion.result.message =
                        "application window became visible before the first frame";
                } else if (options.shouldStop()) {
                    completion.result.ok = true;
                    completion.result.stopReason = rillshot::core::StopReason::UserStopped;
                    completion.result.message = "capture cancelled before the first frame";
                } else {
                    rillshot::session::CaptureSession session;
                    completion.result = session.run(options);
                }
            } catch (const std::exception& ex) {
                completion.result.stopReason = rillshot::core::StopReason::CaptureFailed;
                completion.result.message = std::string("capture worker exception: ") + ex.what();
            } catch (...) {
                completion.result.stopReason = rillshot::core::StopReason::CaptureFailed;
                completion.result.message = "capture worker raised an unknown exception";
            }

            if (comInitialized) {
                CoUninitialize();
            }

            {
                const std::scoped_lock lock(completionMutex_);
                completion_ = std::move(completion);
            }
            running_.store(false);
            try {
                if (completionNotifier) {
                    completionNotifier();
                }
            } catch (...) {
                // Completion is already safely published. A failed UI enqueue must
                // not unwind across the worker thread or destroy captured output.
            }
        });
    } catch (...) {
        running_.store(false);
        return false;
    }
    return true;
}

void CaptureController::requestStop() noexcept {
    stopRequested_.store(true);
}

void CaptureController::joinCompleted() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::optional<CaptureCompletion> CaptureController::takeCompletion() {
    const std::scoped_lock lock(completionMutex_);
    auto completion = std::move(completion_);
    completion_.reset();
    return completion;
}

} // namespace rillshot::gui
