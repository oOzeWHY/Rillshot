cmake_minimum_required(VERSION 3.23)
project(Rillshot VERSION 1.1.9 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RILLSHOT_BUILD_TESTS "Build platform-neutral tests" ON)
option(RILLSHOT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(_rillshot_windows_apps_default OFF)
if(WIN32)
    set(_rillshot_windows_apps_default ON)
endif()
option(
    RILLSHOT_BUILD_WINDOWS_APPS
    "Build the Win32 capture, CLI, and legacy GUI targets"
    ${_rillshot_windows_apps_default}
)
unset(_rillshot_windows_apps_default)
option(
    RILLSHOT_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer on supported compilers"
    OFF
)

if(RILLSHOT_BUILD_WINDOWS_APPS AND NOT WIN32)
    message(FATAL_ERROR "RILLSHOT_BUILD_WINDOWS_APPS requires a Windows toolchain")
endif()

if(RILLSHOT_ENABLE_SANITIZERS AND MSVC)
    message(FATAL_ERROR "RILLSHOT_ENABLE_SANITIZERS currently supports GCC and Clang")
endif()

function(rillshot_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(RILLSHOT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(RILLSHOT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    if(RILLSHOT_ENABLE_SANITIZERS)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

add_library(rillshot_core STATIC
    src/core/Image.cpp
    src/core/ImageMetrics.cpp
    src/core/Json.cpp
    src/input/ManualDriver.cpp
    src/session/CaptureSessionOptions.cpp
    src/session/SessionInputSupport.cpp
    src/stitch/StitchEngine.cpp
)

target_include_directories(rillshot_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(rillshot_core PUBLIC cxx_std_23)

rillshot_enable_warnings(rillshot_core)

add_library(rillshot_gui_model STATIC
    src/gui/CaptureSummary.cpp
    src/gui/CaptureWorkflow.cpp
    src/gui/GuiConfig.cpp
    src/gui/GuiStrings.cpp
)

target_include_directories(rillshot_gui_model PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(rillshot_gui_model PUBLIC rillshot_core)
target_compile_features(rillshot_gui_model PUBLIC cxx_std_23)
rillshot_enable_warnings(rillshot_gui_model)

if(RILLSHOT_BUILD_WINDOWS_APPS)
    add_library(rillshot_windows STATIC
        src/capture/GdiCaptureBackend.cpp
        src/capture/DxgiCaptureBackend.cpp
        src/input/WheelDriver.cpp
        src/input/KeyboardDriver.cpp
        src/output/WicImageWriter.cpp
        src/session/CaptureSession.cpp
        src/session/SessionCaptureSupport.cpp
        src/session/SessionDiagnostics.cpp
        src/session/SessionOutputSupport.cpp
        src/platform/AppPaths.cpp
        src/platform/StartupDiagnostics.cpp
        src/platform/WinDpi.cpp
        src/platform/WinUtf.cpp
    )

    target_include_directories(rillshot_windows PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(rillshot_windows
        PUBLIC rillshot_core
        PRIVATE
        d3d11
        dxgi
        gdi32
        user32
        windowscodecs
        ole32
        shell32
        shcore
    )

    target_compile_definitions(rillshot_windows PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )

    target_compile_features(rillshot_windows PUBLIC cxx_std_23)
    rillshot_enable_warnings(rillshot_windows)

    add_executable(rillshot
        apps/rillshot_cli/main.cpp
        apps/rillshot_cli/rillshot_cli.manifest
    )

    target_link_libraries(rillshot PRIVATE rillshot_windows)
    rillshot_enable_warnings(rillshot)

    add_executable(rillshot_gui WIN32
        apps/rillshot_gui/main.cpp
        apps/rillshot_gui/rillshot_gui.manifest
        src/gui/CaptureController.cpp
        src/gui/MainWindow.cpp
        src/gui/MainWindowActions.cpp
        src/gui/MainWindowControls.cpp
        src/gui/SelectionOverlay.cpp
        src/gui/SelectionOverlay.Rendering.cpp
        src/gui/SelectionOverlay.Snapshot.cpp
        src/gui/Win32ControlUtils.cpp
        src/gui/WindowsShellUtils.cpp
    )

    target_link_libraries(rillshot_gui PRIVATE
        rillshot_windows
        rillshot_gui_model
        dwmapi
        ole32
        shell32
        uuid
    )

    rillshot_enable_warnings(rillshot_gui)

    if(MSVC)
        set_property(TARGET rillshot PROPERTY VS_GLOBAL_EnableDpiAwareness "PerMonitorHighDPIAware")
        set_property(TARGET rillshot_gui PROPERTY VS_GLOBAL_EnableDpiAwareness "PerMonitorHighDPIAware")
    endif()
endif()

if(RILLSHOT_BUILD_TESTS)
    enable_testing()
    add_executable(stitch_engine_test tests/stitch_engine_test.cpp)
    target_link_libraries(stitch_engine_test PRIVATE rillshot_core rillshot_gui_model)
    rillshot_enable_warnings(stitch_engine_test)
    add_test(NAME stitch_engine_test COMMAND stitch_engine_test)

    add_executable(stitch_engine_sticky_test tests/stitch_engine_sticky_test.cpp)
    target_link_libraries(stitch_engine_sticky_test PRIVATE rillshot_core)
    rillshot_enable_warnings(stitch_engine_sticky_test)
    add_test(NAME stitch_engine_sticky_test COMMAND stitch_engine_sticky_test)

    add_executable(session_contract_test tests/session_contract_test.cpp)
    target_link_libraries(session_contract_test PRIVATE rillshot_core rillshot_gui_model)
    rillshot_enable_warnings(session_contract_test)
    add_test(NAME session_contract_test COMMAND session_contract_test)

    add_executable(image_metrics_test tests/image_metrics_test.cpp)
    target_link_libraries(image_metrics_test PRIVATE rillshot_core rillshot_gui_model)
    rillshot_enable_warnings(image_metrics_test)
    add_test(NAME image_metrics_test COMMAND image_metrics_test)

    add_executable(windows_macro_compat_test tests/windows_macro_compat_test.cpp)
    target_link_libraries(windows_macro_compat_test PRIVATE rillshot_core)
    rillshot_enable_warnings(windows_macro_compat_test)
    add_test(NAME windows_macro_compat_test COMMAND windows_macro_compat_test)

    add_executable(selection_and_hotkey_test tests/selection_and_hotkey_test.cpp)
    target_link_libraries(selection_and_hotkey_test PRIVATE rillshot_gui_model)
    rillshot_enable_warnings(selection_and_hotkey_test)
    add_test(NAME selection_and_hotkey_test COMMAND selection_and_hotkey_test)

    add_executable(window_geometry_test tests/window_geometry_test.cpp)
    target_link_libraries(window_geometry_test PRIVATE rillshot_gui_model)
    rillshot_enable_warnings(window_geometry_test)
    add_test(NAME window_geometry_test COMMAND window_geometry_test)

    add_executable(input_boundary_test tests/input_boundary_test.cpp)
    target_link_libraries(input_boundary_test PRIVATE rillshot_core)
    rillshot_enable_warnings(input_boundary_test)
    add_test(NAME input_boundary_test COMMAND input_boundary_test)
endif()
