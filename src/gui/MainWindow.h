#pragma once

#include "gui/CaptureController.h"
#include "gui/GuiConfig.h"

#include <Windows.h>

#include <optional>
#include <string>
#include <vector>

namespace rillshot::gui {

class MainWindow final {
public:
    explicit MainWindow(HINSTANCE instance) : instance_(instance) {}
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    [[nodiscard]] bool create(int showCommand);
    [[nodiscard]] HWND handle() const noexcept { return window_; }

private:
    struct LayoutItem {
        HWND control = nullptr;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    [[nodiscard]] LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    [[nodiscard]] bool createControls();
    HWND addControl(
        DWORD extendedStyle,
        const wchar_t* className,
        const wchar_t* text,
        DWORD style,
        int x,
        int y,
        int width,
        int height,
        int id = 0);
    void layoutControls(UINT dpi);
    void refreshFont(UINT dpi);
    void loadConfig(const GuiConfig& config);
    [[nodiscard]] std::optional<GuiConfig> readConfig(std::wstring& error) const;
    void pickRegion();
    void pickScrollPoint();
    void browseOutput();
    void startCapture();
    void stopCapture();
    void captureCompleted(CaptureCompletion completion);
    void restoreAfterCapture();
    void setRunningUi(bool running);
    void setStatus(const std::wstring& status);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    bool pendingClose_ = false;
    bool captureWindowPlacementValid_ = false;
    WINDOWPLACEMENT captureWindowPlacement_{sizeof(WINDOWPLACEMENT)};
    std::wstring confirmedOverwritePath_;

    HWND regionX_ = nullptr;
    HWND regionY_ = nullptr;
    HWND regionWidth_ = nullptr;
    HWND regionHeight_ = nullptr;
    HWND scrollX_ = nullptr;
    HWND scrollY_ = nullptr;
    HWND outputPath_ = nullptr;
    HWND backend_ = nullptr;
    HWND maxFrames_ = nullptr;
    HWND wheelNotches_ = nullptr;
    HWND ignoreTop_ = nullptr;
    HWND ignoreBottom_ = nullptr;
    HWND pickRegionButton_ = nullptr;
    HWND pickPointButton_ = nullptr;
    HWND browseButton_ = nullptr;
    HWND startButton_ = nullptr;
    HWND stopButton_ = nullptr;
    HWND status_ = nullptr;

    std::vector<LayoutItem> layoutItems_;
    std::vector<HWND> configurationControls_;
    CaptureController captureController_;
};

} // namespace rillshot::gui
