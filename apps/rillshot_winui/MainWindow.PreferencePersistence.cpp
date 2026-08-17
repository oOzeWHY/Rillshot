#include "pch.h"

#include "MainWindow.xaml.h"

#include "platform/UserPreferences.h"

#include <algorithm>
#include <cstdint>
#include <mutex>

using namespace winrt;
using namespace Windows::System::Threading;

namespace winrt::Rillshot::WinUI::implementation {

struct MainWindow::PreferenceSaveState {
    std::mutex stateMutex;
    std::mutex writerMutex;
    rillshot::platform::UserPreferences latest{};
    std::uint64_t latestGeneration = 0;
    std::uint64_t savedGeneration = 0;
    bool workerScheduled = false;
    bool closed = false;
};

void MainWindow::initializePreferencePersistence() {
    preferenceSaveState_ = std::make_shared<PreferenceSaveState>();
}

void MainWindow::queuePreferenceSave() noexcept {
    const auto state = preferenceSaveState_;
    if (!state) {
        return;
    }

    bool startWorker = false;
    {
        const std::scoped_lock lock(state->stateMutex);
        if (state->closed) {
            return;
        }
        state->latest = preferences_;
        ++state->latestGeneration;
        if (!state->workerScheduled) {
            state->workerScheduled = true;
            startWorker = true;
        }
    }
    if (!startWorker) {
        return;
    }

    try {
        const auto dispatcher = DispatcherQueue();
        const auto weak = get_weak();
        [[maybe_unused]] const auto workItem = ThreadPool::RunAsync(
            [state, dispatcher, weak](
                [[maybe_unused]] Windows::Foundation::IAsyncAction const& action) {
                while (true) {
                    rillshot::platform::UserPreferences snapshot;
                    std::uint64_t generation = 0;
                    {
                        const std::scoped_lock lock(state->stateMutex);
                        if (state->closed) {
                            state->workerScheduled = false;
                            return;
                        }
                        snapshot = state->latest;
                        generation = state->latestGeneration;
                    }

                    bool saved = false;
                    {
                        const std::scoped_lock writerLock(state->writerMutex);
                        bool stillCurrent = false;
                        {
                            const std::scoped_lock stateLock(state->stateMutex);
                            stillCurrent = !state->closed &&
                                generation == state->latestGeneration;
                        }
                        if (stillCurrent) {
                            saved = rillshot::platform::saveUserPreferences(snapshot);
                        }
                    }

                    bool repeat = false;
                    {
                        const std::scoped_lock lock(state->stateMutex);
                        if (saved) {
                            state->savedGeneration = std::max(
                                state->savedGeneration, generation);
                        }
                        repeat = !state->closed &&
                            generation != state->latestGeneration;
                        if (!repeat) {
                            state->workerScheduled = false;
                        }
                    }

                    try {
                        [[maybe_unused]] const bool completionQueued =
                            dispatcher.TryEnqueue(
                                Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
                                [weak, generation, saved] {
                                    if (const auto self = weak.get()) {
                                        self->completePreferenceSave(generation, saved);
                                    }
                                });
                    } catch (...) {
                    }
                    if (!repeat) {
                        return;
                    }
                }
            });
    } catch (...) {
        rillshot::platform::UserPreferences snapshot;
        std::uint64_t generation = 0;
        {
            const std::scoped_lock lock(state->stateMutex);
            state->workerScheduled = false;
            snapshot = state->latest;
            generation = state->latestGeneration;
        }
        // A thread-pool scheduling failure is rare; preserve correctness with
        // a synchronous fallback instead of silently losing the preference.
        const bool saved = rillshot::platform::saveUserPreferences(snapshot);
        if (saved) {
            const std::scoped_lock lock(state->stateMutex);
            state->savedGeneration = std::max(
                state->savedGeneration, generation);
        }
        completePreferenceSave(generation, saved);
    }
}

void MainWindow::completePreferenceSave(
    std::uint64_t generation,
    bool saved) noexcept {
    const auto state = preferenceSaveState_;
    if (!state) {
        return;
    }
    {
        const std::scoped_lock lock(state->stateMutex);
        if (state->closed || generation != state->latestGeneration) {
            return;
        }
    }
    try {
        PreferencePersistenceText().Text(saved
            ? L"偏好已保存到应用数据目录"
            : L"无法写入应用数据目录；本次运行仍然有效");
    } catch (...) {
    }
}

void MainWindow::flushPreferenceSave() noexcept {
    const auto state = preferenceSaveState_;
    if (!state) {
        return;
    }

    {
        const std::scoped_lock lock(state->stateMutex);
        state->closed = true;
    }
    const std::scoped_lock writerLock(state->writerMutex);

    rillshot::platform::UserPreferences snapshot;
    std::uint64_t generation = 0;
    {
        const std::scoped_lock lock(state->stateMutex);
        if (state->latestGeneration == 0 ||
            state->savedGeneration >= state->latestGeneration) {
            return;
        }
        snapshot = state->latest;
        generation = state->latestGeneration;
    }
    if (rillshot::platform::saveUserPreferences(snapshot)) {
        const std::scoped_lock lock(state->stateMutex);
        state->savedGeneration = std::max(
            state->savedGeneration, generation);
    }
}

} // namespace winrt::Rillshot::WinUI::implementation
