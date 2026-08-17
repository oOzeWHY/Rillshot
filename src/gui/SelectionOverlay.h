#pragma once

#include "core/Types.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace rillshot::gui {

enum class SelectionExitReason {
    Accepted,
    UserCancelled,
    ForegroundChanged,
    DisplayChanged,
    InitializationFailed,
    MessageLoopFailed,
    WindowClosed
};

template <typename T>
struct SelectionResult {
    std::optional<T> value;
    SelectionExitReason reason = SelectionExitReason::InitializationFailed;
    DWORD systemError = ERROR_SUCCESS;

    [[nodiscard]] bool accepted() const noexcept {
        return reason == SelectionExitReason::Accepted && value.has_value();
    }
};

[[nodiscard]] const wchar_t* selectionExitReasonCode(
    SelectionExitReason reason) noexcept;

class SelectionOverlay final {
public:
    [[nodiscard]] static SelectionResult<rillshot::core::PointI> pickPoint(
        HWND owner,
        std::optional<rillshot::core::RectI> allowedRegion = std::nullopt);
    [[nodiscard]] static SelectionResult<rillshot::core::PointI> pickHorizontalBoundary(
        HWND owner,
        rillshot::core::RectI allowedRegion);
    [[nodiscard]] static SelectionResult<rillshot::core::RectI> pickRegion(HWND owner);

private:
    enum class Mode {
        Point,
        HorizontalBoundary,
        Region
    };

    explicit SelectionOverlay(
        Mode mode,
        std::optional<rillshot::core::RectI> allowedRegion = std::nullopt)
        : mode_(mode), allowedRegion_(allowedRegion) {}
    ~SelectionOverlay();

    [[nodiscard]] bool run(HWND owner);
    [[nodiscard]] bool captureDesktopSnapshot();
    void releaseDesktopSnapshot() noexcept;
    void restoreOwner();
    void finish(SelectionExitReason reason);
    void cancelDrag();
    void paint();
    void invalidatePointer(const POINT& point);
    void invalidateSelectionOutline(const POINT& first, const POINT& second);
    void invalidateSelectionFillDelta(
        const POINT& start,
        const POINT& oldCurrent,
        const POINT& newCurrent);
    void invalidateBoundaryLine(const POINT& point);
    void invalidateAllowedOutline();
    [[nodiscard]] bool pointAllowed(const POINT& point) const noexcept;
    [[nodiscard]] bool cursorBackdropIsLight() const noexcept;
    [[nodiscard]] bool foregroundBelongsToCurrentProcess() const noexcept;
    [[nodiscard]] LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    [[nodiscard]] static bool registerWindowClass();

    Mode mode_;
    std::optional<rillshot::core::RectI> allowedRegion_;
    HWND window_ = nullptr;
    HWND owner_ = nullptr;
    RECT virtualScreen_{};
    int virtualScreenWidth_ = 0;
    int virtualScreenHeight_ = 0;
    HDC snapshotDc_ = nullptr;
    HDC dimmedSnapshotDc_ = nullptr;
    HBITMAP snapshotBitmap_ = nullptr;
    HBITMAP dimmedSnapshotBitmap_ = nullptr;
    HGDIOBJ snapshotPreviousBitmap_ = nullptr;
    HGDIOBJ dimmedSnapshotPreviousBitmap_ = nullptr;
    const std::uint8_t* snapshotPixels_ = nullptr;
    const std::uint8_t* dimmedSnapshotPixels_ = nullptr;
    std::size_t snapshotStrideBytes_ = 0;
    bool ownerWasVisible_ = false;
    bool ownerWasEnabled_ = false;
    bool ownerPlacementValid_ = false;
    WINDOWPLACEMENT ownerPlacement_{sizeof(WINDOWPLACEMENT)};
    SelectionExitReason exitReason_ = SelectionExitReason::InitializationFailed;
    DWORD systemError_ = ERROR_SUCCESS;
    bool foregroundTransitionArmed_ = false;
    bool finishing_ = false;
    bool dragging_ = false;
    bool rejectedPoint_ = false;
    POINT startScreen_{};
    POINT currentScreen_{};
    std::optional<rillshot::core::PointI> point_;
    std::optional<rillshot::core::RectI> region_;
};

} // namespace rillshot::gui
