#include "gui/MainWindow.h"

#include "gui/GuiResourceIds.h"
#include "gui/GuiStrings.h"
#include "gui/Win32ControlUtils.h"
#include "gui/WindowsShellUtils.h"
#include "platform/WinUtf.h"

#include <algorithm>
#include <array>

namespace rillshot::gui {

HWND MainWindow::addControl(
    DWORD extendedStyle,
    const wchar_t* className,
    const wchar_t* text,
    DWORD style,
    int x,
    int y,
    int width,
    int height,
    int id) {

    HWND control = CreateWindowExW(
        extendedStyle,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        win32::scaled(x, dpi_),
        win32::scaled(y, dpi_),
        win32::scaled(width, dpi_),
        win32::scaled(height, dpi_),
        window_,
        id == 0 ? nullptr : reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance_,
        nullptr);
    if (control) {
        layoutItems_.push_back(LayoutItem{control, x, y, width, height});
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    return control;
}

bool MainWindow::createControls() {
    refreshFont(dpi_);

    addControl(0, L"STATIC", L"Rillshot", SS_LEFT, 22, 18, 520, 24);
    addControl(0, L"STATIC", L"滚动长截图", SS_LEFT, 22, 45, 710, 22);

    addControl(0, L"BUTTON", L"截图区域", BS_GROUPBOX, 18, 76, 724, 108);
    addControl(0, L"STATIC", L"X", SS_LEFT, 34, 111, 18, 22);
    regionX_ = addControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL, 54, 106, 92, 26);
    addControl(0, L"STATIC", L"Y", SS_LEFT, 162, 111, 18, 22);
    regionY_ = addControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL, 182, 106, 92, 26);
    addControl(0, L"STATIC", L"宽", SS_LEFT, 300, 111, 32, 22);
    regionWidth_ = addControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, 338, 106, 92, 26);
    addControl(0, L"STATIC", L"高", SS_LEFT, 458, 111, 32, 22);
    regionHeight_ = addControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, 496, 106, 92, 26);
    pickRegionButton_ = addControl(0, L"BUTTON", L"框选区域…", BS_PUSHBUTTON, 606, 103, 116, 32, pickRegionId);

    addControl(0, L"BUTTON", L"滚动位置", BS_GROUPBOX, 18, 194, 724, 82);
    addControl(0, L"STATIC", L"X", SS_LEFT, 34, 229, 18, 22);
    scrollX_ = addControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL, 54, 224, 110, 26);
    addControl(0, L"STATIC", L"Y", SS_LEFT, 182, 229, 18, 22);
    scrollY_ = addControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL, 202, 224, 110, 26);
    pickPointButton_ = addControl(0, L"BUTTON", L"选择坐标…", BS_PUSHBUTTON, 332, 221, 116, 32, pickPointId);

    addControl(0, L"BUTTON", L"截图选项", BS_GROUPBOX, 18, 286, 724, 164);
    addControl(0, L"STATIC", L"输出", SS_LEFT, 34, 320, 54, 22);
    outputPath_ = addControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL, 90, 315, 500, 26);
    browseButton_ = addControl(0, L"BUTTON", L"浏览…", BS_PUSHBUTTON, 606, 312, 116, 32, browseOutputId);

    addControl(0, L"STATIC", L"后端", SS_LEFT, 34, 365, 60, 22);
    backend_ = addControl(0, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 98, 359, 122, 180, backendId);
    SendMessageW(backend_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动"));
    SendMessageW(backend_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DXGI"));
    SendMessageW(backend_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"GDI"));

    addControl(0, L"STATIC", L"最多帧数", SS_LEFT, 242, 365, 78, 22);
    maxFrames_ = addControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, 322, 359, 72, 26);
    addControl(0, L"STATIC", L"每次滚轮格数", SS_LEFT, 410, 365, 98, 22);
    wheelNotches_ = addControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, 512, 359, 72, 26);

    addControl(0, L"STATIC", L"固定页头(px)", SS_LEFT, 34, 407, 92, 22);
    ignoreTop_ = addControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, 130, 401, 72, 26);
    addControl(0, L"STATIC", L"固定页脚(px)", SS_LEFT, 224, 407, 100, 22);
    ignoreBottom_ = addControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, 328, 401, 72, 26);

    startButton_ = addControl(0, L"BUTTON", L"开始滚动截图", BS_DEFPUSHBUTTON, 18, 466, 190, 38, startCaptureId);
    stopButton_ = addControl(0, L"BUTTON", L"停止", BS_PUSHBUTTON, 220, 466, 100, 38, stopCaptureId);
    status_ = addControl(WS_EX_CLIENTEDGE, L"EDIT", L"就绪",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 18, 520, 724, 122);

    const std::array<HWND, 15> configuration = {
        regionX_, regionY_, regionWidth_, regionHeight_, scrollX_, scrollY_, outputPath_, backend_,
        maxFrames_, wheelNotches_, ignoreTop_, ignoreBottom_, pickRegionButton_, pickPointButton_, browseButton_};
    configurationControls_.assign(configuration.begin(), configuration.end());
    if (std::any_of(configuration.begin(), configuration.end(), [](HWND control) { return control == nullptr; }) ||
        !startButton_ || !stopButton_ || !status_) {
        return false;
    }

    GuiConfig defaults;
    defaults.outPath = win32::defaultOutputPath();
    loadConfig(defaults);
    setRunningUi(false);
    return true;
}

void MainWindow::layoutControls(UINT dpi) {
    dpi_ = dpi;
    for (const auto& item : layoutItems_) {
        MoveWindow(item.control, win32::scaled(item.x, dpi_), win32::scaled(item.y, dpi_),
            win32::scaled(item.width, dpi_), win32::scaled(item.height, dpi_), TRUE);
    }
    refreshFont(dpi_);
}

void MainWindow::refreshFont(UINT dpi) {
    HFONT newFont = CreateFontW(-MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (!newFont) {
        return;
    }
    HFONT oldFont = font_;
    font_ = newFont;
    for (const auto& item : layoutItems_) {
        SendMessageW(item.control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    if (oldFont) {
        DeleteObject(oldFont);
    }
}

void MainWindow::loadConfig(const GuiConfig& config) {
    win32::setIntegerControl(regionX_, config.region.x);
    win32::setIntegerControl(regionY_, config.region.y);
    win32::setIntegerControl(regionWidth_, config.region.width);
    win32::setIntegerControl(regionHeight_, config.region.height);
    win32::setIntegerControl(scrollX_, config.scrollPoint.x);
    win32::setIntegerControl(scrollY_, config.scrollPoint.y);
    SetWindowTextW(outputPath_, config.outPath.c_str());
    confirmedOverwritePath_ = config.allowOverwrite ? config.outPath : L"";
    SendMessageW(backend_, CB_SETCURSEL, static_cast<WPARAM>(config.backend), 0);
    win32::setIntegerControl(maxFrames_, config.maxFrames);
    win32::setIntegerControl(wheelNotches_, config.wheelNotches);
    win32::setIntegerControl(ignoreTop_, config.ignoreTopPx);
    win32::setIntegerControl(ignoreBottom_, config.ignoreBottomPx);
}

std::optional<GuiConfig> MainWindow::readConfig(std::wstring& error) const {
    GuiConfig config;
    if (!win32::parseIntegerControl(regionX_, config.region.x) ||
        !win32::parseIntegerControl(regionY_, config.region.y) ||
        !win32::parseIntegerControl(regionWidth_, config.region.width) ||
        !win32::parseIntegerControl(regionHeight_, config.region.height) ||
        !win32::parseIntegerControl(scrollX_, config.scrollPoint.x) ||
        !win32::parseIntegerControl(scrollY_, config.scrollPoint.y) ||
        !win32::parseIntegerControl(maxFrames_, config.maxFrames) ||
        !win32::parseIntegerControl(wheelNotches_, config.wheelNotches) ||
        !win32::parseIntegerControl(ignoreTop_, config.ignoreTopPx) ||
        !win32::parseIntegerControl(ignoreBottom_, config.ignoreBottomPx)) {
        error = L"坐标和数值选项都必须是整数。";
        return std::nullopt;
    }

    config.outPath = win32::controlText(outputPath_);
    config.allowOverwrite =
        !confirmedOverwritePath_.empty() && config.outPath == confirmedOverwritePath_;
    switch (SendMessageW(backend_, CB_GETCURSEL, 0, 0)) {
    case 0: config.backend = rillshot::session::BackendChoice::Auto; break;
    case 1: config.backend = rillshot::session::BackendChoice::Dxgi; break;
    case 2: config.backend = rillshot::session::BackendChoice::Gdi; break;
    default:
        error = L"请选择截图后端。";
        return std::nullopt;
    }

    const auto validation = validateGuiConfig(config);
    if (!validation.ok) {
        error = validationMessageZh(validation);
        return std::nullopt;
    }
    return config;
}

} // namespace rillshot::gui
