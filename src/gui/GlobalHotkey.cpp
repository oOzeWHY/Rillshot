#include "gui/GlobalHotkey.h"

#include <chrono>
#include <mutex>
#include <utility>

namespace rillshot::gui {
namespace {

constexpr int probeHotkeyId = 0x6A02;
constexpr wchar_t receiverClassName[] = L"Rillshot.GlobalHotkeyReceiver.v2";
constexpr UINT applyHotkeyMessage = WM_APP + 0x321U;

UINT nativeModifiers(const HotkeyBinding& binding) noexcept {
    return static_cast<UINT>(binding.modifiers) | MOD_NOREPEAT;
}

HotkeyRegistrationStatus failureStatus(DWORD error) noexcept {
    return error == ERROR_HOTKEY_ALREADY_REGISTERED
        ? HotkeyRegistrationStatus::Conflict
        : HotkeyRegistrationStatus::SystemError;
}

} // namespace

struct GlobalHotkey::ApplyRequest {
    bool enabled = false;
    HotkeyBinding binding{};
    HotkeyRegistrationResult result{
        HotkeyRegistrationStatus::SystemError,
        ERROR_GEN_FAILURE};
};

GlobalHotkey::~GlobalHotkey() {
    shutdown();
}

bool GlobalHotkey::initialize(std::function<void()> callback) noexcept {
    shutdown();
    shutdownRequested_.store(false);
    {
        const std::scoped_lock lock(stateMutex_);
        callback_ = std::move(callback);
        initializationComplete_ = false;
        initializationSucceeded_ = false;
    }

    try {
        receiverThread_ = std::thread([this] { receiverThreadMain(); });
    } catch (...) {
        const std::scoped_lock lock(stateMutex_);
        callback_ = {};
        initializationComplete_ = true;
        return false;
    }

    std::unique_lock lock(stateMutex_);
    const bool completed = initializationChanged_.wait_for(
        lock,
        std::chrono::seconds(5),
        [this] { return initializationComplete_; });
    const bool initialized = completed && initializationSucceeded_;
    if (!initialized) {
        // The receiver may not have created its message queue before the
        // timeout, so PostThreadMessage alone cannot guarantee that it stops.
        // Preserve an out-of-band request that the thread checks before it can
        // enter the blocking GetMessage loop.
        shutdownRequested_.store(true);
    }
    const DWORD threadId = receiverThreadId_;
    lock.unlock();

    if (!initialized) {
        if (threadId != 0) {
            PostThreadMessageW(threadId, WM_QUIT, 0, 0);
        }
        try {
            if (receiverThread_.joinable()) {
                receiverThread_.join();
            }
        } catch (...) {
        }
        const std::scoped_lock cleanupLock(stateMutex_);
        callback_ = {};
    }
    return initialized;
}

HotkeyRegistrationResult GlobalHotkey::apply(
    bool enabled,
    HotkeyBinding binding) noexcept {
    HWND receiver = nullptr;
    {
        const std::scoped_lock lock(stateMutex_);
        receiver = receiverWindow_;
    }
    if (!receiver || !IsWindow(receiver)) {
        return {HotkeyRegistrationStatus::SystemError, ERROR_INVALID_WINDOW_HANDLE};
    }

    ApplyRequest request{enabled, binding};
    SetLastError(ERROR_SUCCESS);
    const LRESULT processed = SendMessageW(
        receiver,
        applyHotkeyMessage,
        0,
        reinterpret_cast<LPARAM>(&request));
    if (processed == 0) {
        const DWORD error = GetLastError();
        return {
            HotkeyRegistrationStatus::SystemError,
            error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error};
    }
    return request.result;
}

void GlobalHotkey::unregister() noexcept {
    [[maybe_unused]] const auto result = apply(false, HotkeyBinding{});
}

void GlobalHotkey::shutdown() noexcept {
    shutdownRequested_.store(true);
    HWND receiver = nullptr;
    DWORD threadId = 0;
    {
        const std::scoped_lock lock(stateMutex_);
        receiver = receiverWindow_;
        threadId = receiverThreadId_;
    }
    if (receiver && IsWindow(receiver)) {
        SendMessageW(receiver, WM_CLOSE, 0, 0);
    } else if (threadId != 0) {
        PostThreadMessageW(threadId, WM_QUIT, 0, 0);
    }
    try {
        if (receiverThread_.joinable()) {
            receiverThread_.join();
        }
    } catch (...) {
    }
    const std::scoped_lock lock(stateMutex_);
    receiverWindow_ = nullptr;
    receiverThreadId_ = 0;
    initializationComplete_ = false;
    initializationSucceeded_ = false;
    callback_ = {};
    binding_.reset();
}

void GlobalHotkey::receiverThreadMain() noexcept {
    const HINSTANCE module = GetModuleHandleW(nullptr);
    {
        const std::scoped_lock lock(stateMutex_);
        receiverThreadId_ = GetCurrentThreadId();
    }

    MSG seed{};
    PeekMessageW(&seed, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    if (shutdownRequested_.load()) {
        {
            const std::scoped_lock lock(stateMutex_);
            initializationSucceeded_ = false;
            initializationComplete_ = true;
            receiverThreadId_ = 0;
        }
        initializationChanged_.notify_all();
        return;
    }

    static std::once_flag registrationOnce;
    static bool classRegistered = false;
    std::call_once(registrationOnce, [module] {
        if (!module) {
            return;
        }
        WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
        windowClass.lpfnWndProc = &GlobalHotkey::windowProc;
        windowClass.hInstance = module;
        windowClass.lpszClassName = receiverClassName;
        SetLastError(ERROR_SUCCESS);
        classRegistered =
            RegisterClassExW(&windowClass) != 0 ||
            GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    });

    HWND receiver = nullptr;
    if (classRegistered) {
        receiver = CreateWindowExW(
            0,
            receiverClassName,
            L"",
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nullptr,
            module,
            this);
    }

    bool startupCancelled = false;
    {
        const std::scoped_lock lock(stateMutex_);
        startupCancelled = shutdownRequested_.load();
        receiverWindow_ = receiver;
        initializationSucceeded_ = receiver != nullptr && !startupCancelled;
        initializationComplete_ = true;
    }
    initializationChanged_.notify_all();
    if (!receiver || startupCancelled) {
        if (receiver) {
            DestroyWindow(receiver);
        }
        const std::scoped_lock lock(stateMutex_);
        receiverWindow_ = nullptr;
        receiverThreadId_ = 0;
        return;
    }

    MSG message{};
    while (true) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (IsWindow(receiver)) {
        unregisterOnReceiverThread();
        DestroyWindow(receiver);
    }
    const std::scoped_lock lock(stateMutex_);
    receiverWindow_ = nullptr;
    receiverThreadId_ = 0;
}

HotkeyRegistrationResult GlobalHotkey::applyOnReceiverThread(
    bool enabled,
    HotkeyBinding binding) noexcept {
    if (!enabled) {
        unregisterOnReceiverThread();
        return {HotkeyRegistrationStatus::Disabled, ERROR_SUCCESS};
    }
    if (!receiverWindow_ || !IsWindow(receiverWindow_) ||
        !isValidHotkeyBinding(binding)) {
        return {HotkeyRegistrationStatus::Invalid, ERROR_INVALID_PARAMETER};
    }
    if (binding_ && *binding_ == binding) {
        return {HotkeyRegistrationStatus::Unchanged, ERROR_SUCCESS};
    }

    SetLastError(ERROR_SUCCESS);
    if (!RegisterHotKey(
            receiverWindow_,
            probeHotkeyId,
            nativeModifiers(binding),
            static_cast<UINT>(binding.virtualKey))) {
        const DWORD error = GetLastError();
        return {failureStatus(error), error};
    }
    SetLastError(ERROR_SUCCESS);
    if (!UnregisterHotKey(receiverWindow_, probeHotkeyId)) {
        const DWORD error = GetLastError();
        UnregisterHotKey(receiverWindow_, probeHotkeyId);
        return {HotkeyRegistrationStatus::SystemError, error};
    }

    const auto previousBinding = binding_;
    unregisterOnReceiverThread();
    SetLastError(ERROR_SUCCESS);
    if (RegisterHotKey(
            receiverWindow_,
            messageId(),
            nativeModifiers(binding),
            static_cast<UINT>(binding.virtualKey))) {
        binding_ = binding;
        return {HotkeyRegistrationStatus::Registered, ERROR_SUCCESS};
    }

    const DWORD error = GetLastError();
    if (previousBinding && RegisterHotKey(
            receiverWindow_,
            messageId(),
            nativeModifiers(*previousBinding),
            static_cast<UINT>(previousBinding->virtualKey))) {
        binding_ = previousBinding;
    }
    return {failureStatus(error), error};
}

void GlobalHotkey::unregisterOnReceiverThread() noexcept {
    if (receiverWindow_ && IsWindow(receiverWindow_)) {
        UnregisterHotKey(receiverWindow_, messageId());
    }
    binding_.reset();
}

LRESULT CALLBACK GlobalHotkey::windowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) noexcept {
    GlobalHotkey* self = reinterpret_cast<GlobalHotkey*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<GlobalHotkey*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && message == applyHotkeyMessage) {
        auto* request = reinterpret_cast<ApplyRequest*>(lParam);
        if (!request) {
            return 0;
        }
        request->result = self->applyOnReceiverThread(
            request->enabled,
            request->binding);
        return 1;
    }
    if (self && message == WM_HOTKEY &&
        static_cast<int>(wParam) == messageId()) {
        try {
            if (self->callback_) {
                self->callback_();
            }
        } catch (...) {
        }
        return 0;
    }
    if (message == WM_CLOSE) {
        if (self) {
            self->unregisterOnReceiverThread();
        }
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace rillshot::gui
