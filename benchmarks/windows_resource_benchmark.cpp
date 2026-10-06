// Non-gating measurement. --capture reads a small live desktop ROI only;
// --save writes a synthetic 128 MiB image in an isolated process.
#include "capture/DxgiCaptureBackend.h"
#include "output/WicImageWriter.h"
#include "stitch/StitchedImageBuilder.h"
#include <Windows.h>
#include <objbase.h>
#include <psapi.h>
#ifndef RILLSHOT_BENCHMARK_BASELINE
#include "capture/GdiCaptureBackend.h"
#include "gui/CaptureDimmer.h"
#include <dwmapi.h>
#endif
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

using Clock = std::chrono::steady_clock;

static double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

static void memory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    std::cout << ",\"peakWorkingSetMiB\":" <<
        static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0)
        << ",\"peakCommitMiB\":" <<
        static_cast<double>(counters.PeakPagefileUsage) / (1024.0 * 1024.0);
}

#ifndef RILLSHOT_BENCHMARK_BASELINE
static bool changed = false;
static LRESULT CALLBACK fixtureProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        const auto brush = CreateSolidBrush(changed ? RGB(40, 100, 210) : RGB(210, 190, 170));
        FillRect(dc, &paint.rcPaint, brush);
        DeleteObject(brush);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

static bool equalRgb(const rillshot::core::Image& a, const rillshot::core::Image& b) {
    if (a.empty() || a.width() != b.width() || a.height() != b.height()) return false;
    for (std::size_t offset = 0; offset < a.bytes().size(); ++offset) {
        if (offset % 4 != 3 && a.bytes()[offset] != b.bytes()[offset]) return false;
    }
    return true;
}

static int verifyCapture() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW type{};
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpfnWndProc = fixtureProc;
    type.lpszClassName = L"Rillshot.ResourceFixture";
    if (!RegisterClassW(&type)) return EXIT_FAILURE;
    const int x = 40, y = 40;
    struct WindowGuard {
        HWND window;
        ~WindowGuard() { if (window) DestroyWindow(window); }
    } fixture{CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        type.lpszClassName, L"", WS_POPUP, x, y, 360, 260, nullptr, nullptr, type.hInstance, nullptr)};
    if (!fixture.window) return EXIT_FAILURE;
    SetWindowPos(fixture.window, HWND_TOPMOST, x, y, 360, 260, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateWindow(fixture.window);
    DwmFlush();
    const rillshot::core::RectI region{x + 20, y + 20, 256, 160};
    rillshot::capture::GdiCaptureBackend gdi;
    rillshot::capture::DxgiCaptureBackend dxgi;
    rillshot::capture::CaptureFrame reference, actual, outsideBefore, outsideAfter;
    if (!gdi.capture(region, reference).ok) return EXIT_FAILURE;
    const auto initialDeadline = Clock::now() + std::chrono::seconds(1);
    do {
        if (!dxgi.capture(region, actual).ok) return EXIT_FAILURE;
        if (equalRgb(actual.image, reference.image)) break;
        Sleep(10);
    } while (Clock::now() < initialDeadline);
    if (!equalRgb(actual.image, reference.image)) {
        std::cerr << "initial DXGI/GDI synthetic fixture pixels differ\n";
        return EXIT_FAILURE;
    }
    for (int sample = 0; sample < 30; ++sample) {
        if (!dxgi.capture(region, actual).ok || !equalRgb(actual.image, reference.image)) {
            std::cerr << "DXGI reused capture changed RGB pixels\n";
            return EXIT_FAILURE;
        }
    }
    if (dxgi.statistics().deviceInitializations != 1 || dxgi.statistics().stagingAllocations != 1) {
        std::cerr << "DXGI did not reuse its device and ROI texture\n";
        return EXIT_FAILURE;
    }
    changed = true;
    InvalidateRect(fixture.window, nullptr, FALSE);
    UpdateWindow(fixture.window);
    DwmFlush();
    if (!gdi.capture(region, reference).ok) return EXIT_FAILURE;
    const auto deadline = Clock::now() + std::chrono::seconds(1);
    do {
        if (!dxgi.capture(region, actual).ok) return EXIT_FAILURE;
        if (equalRgb(actual.image, reference.image)) break;
        Sleep(10);
    } while (Clock::now() < deadline);
    if (!equalRgb(actual.image, reference.image)) {
        std::cerr << "DXGI kept stale pixels after a desktop update\n";
        return EXIT_FAILURE;
    }
    const rillshot::core::RectI outside{x + 2, y + 2, 8, 8};
    if (!gdi.capture(outside, outsideBefore).ok) return EXIT_FAILURE;
    rillshot::gui::CaptureDimmer dimmer;
    const auto framesBeforeMask = dxgi.statistics().framesAcquired;
    if (!dimmer.show(region)) return EXIT_FAILURE;
    DwmFlush();
    if (!gdi.capture(region, actual).ok || !equalRgb(actual.image, reference.image) ||
        !gdi.capture(outside, outsideAfter).ok ||
        outsideAfter.image.bytes()[0] >= outsideBefore.image.bytes()[0]) {
        std::cerr << "dimmer changed crop pixels or failed to darken outside\n";
        return EXIT_FAILURE;
    }
    bool croppedPixelsMatch = false;
    const auto dimmerDeadline = Clock::now() + std::chrono::seconds(1);
    do {
        if (!dxgi.capture(region, actual).ok) return EXIT_FAILURE;
        croppedPixelsMatch = dxgi.statistics().framesAcquired > framesBeforeMask &&
            equalRgb(actual.image, reference.image);
        if (croppedPixelsMatch) break;
        Sleep(10);
    } while (Clock::now() < dimmerDeadline);
    if (!croppedPixelsMatch) return EXIT_FAILURE;
    dimmer.hide();
    DwmFlush();
    auto shifted = region;
    ++shifted.x;
    if (!gdi.capture(shifted, reference).ok) return EXIT_FAILURE;
    const auto shiftedDeadline = Clock::now() + std::chrono::seconds(1);
    do {
        if (!dxgi.capture(shifted, actual).ok) return EXIT_FAILURE;
        if (equalRgb(actual.image, reference.image)) break;
        Sleep(10);
    } while (Clock::now() < shiftedDeadline);
    if (!equalRgb(actual.image, reference.image) || dxgi.statistics().deviceInitializations != 2) {
        std::cerr << "changed capture region reused stale resources\n";
        return EXIT_FAILURE;
    }
    std::cout << "{\"mode\":\"verify-capture\",\"rgbPixelsMatch\":true,"
        "\"outsideDarkened\":true,\"devicesFor30Samples\":1,\"regionResetVerified\":true}\n";
    return EXIT_SUCCESS;
}
#endif

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: windows_resource_benchmark --capture|--save|--verify-capture\n";
        return EXIT_FAILURE;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::cout << std::fixed << std::setprecision(3);
    if (std::string(argv[1]) == "--verify-capture") {
#ifndef RILLSHOT_BENCHMARK_BASELINE
        const int result = verifyCapture();
        CoUninitialize();
        return result;
#else
        return EXIT_FAILURE;
#endif
    } else if (std::string(argv[1]) == "--capture") {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const rillshot::core::RectI region{0, 0,
            (std::min)(1280, GetSystemMetrics(SM_CXSCREEN)),
            (std::min)(720, GetSystemMetrics(SM_CYSCREEN))};
        rillshot::capture::DxgiCaptureBackend backend;
        rillshot::capture::CaptureFrame frame;
        constexpr int samples = 30;
        const auto started = Clock::now();
        for (int index = 0; index < samples; ++index) {
            const auto status = backend.capture(region, frame);
            if (!status.ok) {
                std::cerr << status.message << '\n';
                return EXIT_FAILURE;
            }
        }
        std::cout << "{\"mode\":\"capture\",\"samples\":" << samples
            << ",\"averageMs\":" << elapsed(started) / samples;
#ifndef RILLSHOT_BENCHMARK_BASELINE
        const auto stats = backend.statistics();
        std::cout << ",\"devicesCreated\":" << stats.deviceInitializations
            << ",\"stagingTexturesCreated\":" << stats.stagingAllocations
            << ",\"unchangedSamples\":" << stats.unchangedFrames;
#endif
        memory();
        std::cout << "}\n";
    } else if (std::string(argv[1]) == "--save") {
        constexpr int width = 1024, chunkHeight = 1024, chunks = 32;
        rillshot::core::Image chunk(width, chunkHeight);
        chunk.fillTestPattern();
        rillshot::stitch::StitchedImageBuilder image(chunk, rillshot::core::ScrollDirection::Down);
        for (int index = 1; index < chunks; ++index) image.addRowsFrom(chunk, 0, chunkHeight);
        const auto path = std::filesystem::temp_directory_path() /
            (L"rillshot-resource-" + std::to_wstring(GetCurrentProcessId()) + L".png");
        rillshot::output::WicImageWriter writer;
        const auto started = Clock::now();
#ifdef RILLSHOT_BENCHMARK_BASELINE
        const auto output = image.materialize();
        const auto status = writer.write(output, path.wstring(), rillshot::output::ImageFormat::Png);
#else
        const auto status = writer.write(image, path.wstring(), rillshot::output::ImageFormat::Png);
#endif
        if (!status.ok) {
            std::cerr << status.message << '\n';
            return EXIT_FAILURE;
        }
        std::cout << "{\"mode\":\"save\",\"imageMiB\":128,\"elapsedMs\":" << elapsed(started);
        memory();
        std::cout << "}\n";
        std::filesystem::remove(path);
    } else {
        return EXIT_FAILURE;
    }
    CoUninitialize();
    return EXIT_SUCCESS;
}
