export function checkCore(context) {
  const {
    read, requireMatch, requireNoMatch, requireFile,
    checkLineBudget, walk, projectRoot,
  } = context;
  const cmake = read("CMakeLists.txt");
  const stitch = read("src/stitch/StitchEngine.cpp");
  const session = read("src/session/CaptureSession.cpp");
  const options = read("src/session/CaptureSessionOptions.cpp");
  const captureSupport = read("src/session/SessionCaptureSupport.cpp");
  const inputSupport = read("src/session/SessionInputSupport.cpp");
  const image = read("src/core/Image.cpp");
  const types = read("src/core/Types.h");
  const winUtf = read("src/platform/WinUtf.cpp");
  const windowGeometry = read("src/gui/WindowGeometry.h");
  const dxgiCapture = read("src/capture/DxgiCaptureBackend.cpp");
  const gdiCapture = read("src/capture/GdiCaptureBackend.cpp");
  const captureController = read("src/gui/CaptureController.cpp");
  const globalHotkey = read("src/gui/GlobalHotkey.cpp");
  const globalHotkeyHeader = read("src/gui/GlobalHotkey.h");
  const appPaths = read("src/platform/AppPaths.cpp");
  const cursorGuard = read("src/input/CursorPositionGuard.h");
  const keyboardDriver = read("src/input/KeyboardDriver.cpp");
  const manualDriver = read("src/input/ManualDriver.cpp");
  const wheelDriver = read("src/input/WheelDriver.cpp");
  const guiStrings = read("src/gui/GuiStrings.cpp");
  const presets = read("CMakePresets.json");

  requireMatch("CMake 缺少平台无关的 Windows 应用开关", cmake,
    /RILLSHOT_BUILD_WINDOWS_APPS[\s\S]*?requires a Windows toolchain/u);
  requireMatch("CMake 缺少 GCC\/Clang 消毒器开关", cmake,
    /RILLSHOT_ENABLE_SANITIZERS[\s\S]*?-fsanitize=address,undefined/u);
  requireMatch("CMake presets 缺少严格核心构建", presets,
    /core-release[\s\S]*?RILLSHOT_WARNINGS_AS_ERRORS[\s\S]*?ON/u);
  requireMatch("CMake presets 缺少消毒器构建", presets,
    /core-sanitize[\s\S]*?RILLSHOT_ENABLE_SANITIZERS[\s\S]*?ON/u);
  requireMatch("窗口脉冲间隔缺少平台无关刷新率边界", windowGeometry,
    /navigationPulseIntervalMicros[\s\S]*?minimumInterval = 4'000[\s\S]*?maximumInterval = 16'667/u);

  requireMatch("拼接器缺少粗筛/精排预算", stitch,
    /primaryRefinedCandidates\s*=\s*24/u);
  requireMatch("拼接器缺少多样候选精排预算", stitch,
    /maximumRefinedCandidates\s*=\s*32/u);
  requireMatch("拼接器缺少粗筛候选硬上限", stitch,
    /maximumCoarseCandidates\s*=\s*4096/u);
  requireMatch("拼接器没有在保留上限外继续精确评估候选", stitch,
    /strongestCandidates[\s\S]*?candidateIsBetter\(candidate, strongestCandidates\.top\(\)\)/u);
  requireMatch("拼接器没有拒绝非法枚举或非有限阈值", stitch,
    /matchOptionsAreValid[\s\S]*?isfinite/u);
  requireMatch("拼接候选循环缺少溢出安全的终止条件", stitch,
    /nextOverlap[\s\S]*?static_cast<std::int64_t>\(overlap\)/u);
  requireMatch("拼接器缺少向上方向分支", stitch,
    /const bool downward[\s\S]*?ScrollDirection::Down/u);
  requireMatch("核心方向枚举缺少向上捕获", types,
    /enum class ScrollDirection[\s\S]*?Up/u);
  requireMatch("会话没有根据方向选择前置/追加组装", session,
    /prependRowsFrom/u);
  requireMatch("稳定超时帧仍可能进入接缝搜索", session,
    /UnstableTooLong[\s\S]*?unstable_frame_rejected/u);
  requireMatch("会话缺少部分图检查点保护", session,
    /mayOverwritePartialCheckpoint/u);
  requireMatch("会话仍直接调用可能抛异常的输入驱动", session,
    /advanceScrollSafely\(\*driver, request\)/u);
  requireMatch("输入驱动异常没有转为结构化失败", inputSupport,
    /catch \(const std::exception& exception\)[\s\S]*?catch \(\.\.\.\)[\s\S]*?scroll-driver-exception/u);
  requireMatch("输入驱动异常包装器没有加入核心目标", cmake,
    /src\/session\/SessionInputSupport\.cpp/u);
  requireMatch("平台无关的人工输入仍未加入核心测试目标", cmake,
    /add_library\(rillshot_core STATIC[\s\S]*?src\/input\/ManualDriver\.cpp[\s\S]*?\)/u);
  requireMatch("人工输入关闭仍可能被当作成功确认", manualDriver,
    /if \(!std::getline\(input, line\)\)[\s\S]*?manual-input-closed/u);
  requireMatch("人工输入失败仍按驱动类型全部视为用户停止", inputSupport,
    /status\.code == "manual-user-stopped"[\s\S]*?StopReason::UserStopped[\s\S]*?StopReason::ScrollRejected/u);
  requireNoMatch("捕获会话仍把人工模式的所有失败视为用户停止", session,
    /options\.driver == DriverChoice::Manual/u);
  requireNoMatch("共享停止原因仍把所有输入失败写成滚轮失败", guiStrings,
    /ScrollRejected:\s*return L"滚轮输入/u);
  requireMatch("鼠标位置缺少异常安全的作用域恢复", cursorGuard,
    /class CursorPositionGuard[\s\S]*?~CursorPositionGuard\(\) noexcept[\s\S]*?SetCursorPos/u);
  for (const [label, driver] of [["键盘", keyboardDriver], ["滚轮", wheelDriver]]) {
    requireMatch(`${label}输入没有使用鼠标位置作用域保护`, driver,
      /CursorPositionGuard cursorPositionGuard/u);
    requireNoMatch(`${label}输入仍依赖可能抛异常的 random_device`, driver,
      /random_device/u);
  }
  requireMatch("会话选项未拒绝非法方向", options,
    /invalid-scroll-direction/u);
  requireMatch("图像缓冲区缺少尺寸溢出防护", image,
    /numeric_limits<int>::max\(\)[\s\S]*?maxSize \/ strideSize/u);
  const imageMetrics = read("src/core/ImageMetrics.cpp");
  requireMatch("图像差异忽略区仍在夹紧前做减法", imageMetrics,
    /ignoredBottom\s*=\s*std::clamp[\s\S]*?a\.height\(\) - ignoredBottom/u);
  requireMatch("稳定差异检测缺少交错采样相位", imageMetrics,
    /samplePhases[\s\S]*?xOffset[\s\S]*?yOffset/u);
  requireMatch("稳定等待上限仍未覆盖最小等待和后端阻塞", captureSupport,
    /const auto deadline[\s\S]*?now >= deadline[\s\S]*?steady_clock::now\(\) >= deadline/u);
  requireMatch("捕获回退仍在后端之间复用可污染的输出帧", captureSupport,
    /CaptureFrame candidate[\s\S]*?backend->capture\(region, candidate\)/u);
  requireMatch("会话缺少编码前的拼接图像内存预算", session,
    /imageSizeFitsByteBudget[\s\S]*?OutputLimitReached/u);
  requireFile("src/session/SessionOutputSupport.cpp");
  requireFile("src/session/SessionOutputSupport.h");
  requireMatch("UTF-16 转 UTF-8 缺少输入长度防护", winUtf,
    /input\.size\(\)[\s\S]*?numeric_limits<int>::max/u);
  requireMatch("UTF-8 解码没有拒绝非法字节序列", winUtf,
    /MultiByteToWideChar\([\s\S]*?MB_ERR_INVALID_CHARS/u);
  requireMatch("窗口坐标差缺少 64 位中间值", windowGeometry,
    /coordinateDelta[\s\S]*?static_cast<std::int64_t>\(end\)[\s\S]*?static_cast<std::int64_t>\(start\)/u);
  requireMatch("窗口坐标插值缺少浮点宽化", windowGeometry,
    /static_cast<double>\(end\)[\s\S]*?static_cast<double>\(start\)/u);
  requireMatch("窗口坐标跨度缺少收窄前检查", windowGeometry,
    /tryPositiveCoordinateSpan[\s\S]*?wideSpan <= 0[\s\S]*?wideSpan > maximum/u);
  requireMatch("渲染坐标缺少饱和收窄", windowGeometry,
    /saturatedCoordinateDelta[\s\S]*?clampCoordinate/u);
  requireMatch("DIP 缩放缺少非有限值和上限保护", windowGeometry,
    /pixelsForDip[\s\S]*?!std::isfinite\(dip\)[\s\S]*?!std::isfinite\(scaled\)/u);
  requireMatch("DXGI 捕获缺少已获取帧的作用域释放", dxgiCapture,
    /class AcquiredFrameGuard[\s\S]*?ReleaseFrame\(\)/u);
  requireMatch("DXGI 捕获缺少映射纹理的作用域释放", dxgiCapture,
    /class MappedTextureGuard[\s\S]*?Unmap\(/u);
  requireMatch("DXGI 捕获没有验证桌面复制像素格式", dxgiCapture,
    /DXGI_FORMAT_B8G8R8A8_UNORM/u);
  requireMatch("DXGI 捕获坐标边界缺少 64 位中间值", dxgiCapture,
    /const auto relativeX[\s\S]*?static_cast<std::int64_t>\(region\.x\)[\s\S]*?const auto relativeBottom/u);
  requireMatch("DXGI 捕获没有按真实 RowPitch 验证源缓冲区", dxgiCapture,
    /mapped\.RowPitch[\s\S]*?finalSourceRow[\s\S]*?mapped\.RowPitch/u);
  requireMatch("GDI 位图像素地址失败时缺少清理", gdiCapture,
    /if \(!bitmap \|\| !bits\)[\s\S]*?DeleteObject\(bitmap\)/u);
  requireMatch("GDI 选择位图失败时缺少清理", gdiCapture,
    /if \(!old \|\| old == HGDI_ERROR\)[\s\S]*?DeleteObject\(bitmap\)/u);
  requireMatch("CaptureController 覆盖了调用方取消回调", captureController,
    /callerShouldStop\s*=\s*std::move\(options\.shouldStop\)[\s\S]*?callerShouldStop && callerShouldStop\(\)/u);
  requireMatch("全局快捷键初始化超时缺少消息队列外的停止状态", globalHotkeyHeader,
    /std::atomic<bool> shutdownRequested_/u);
  requireMatch("全局快捷键初始化失败没有在 join 前持久请求停止", globalHotkey,
    /if \(!initialized\)[\s\S]*?shutdownRequested_\.store\(true\)[\s\S]*?receiverThread_\.join/u);
  requireMatch("快捷键接收线程仍可能在超时后进入阻塞消息循环", globalHotkey,
    /PeekMessageW[\s\S]*?if \(shutdownRequested_\.load\(\)\)[\s\S]*?return;[\s\S]*?GetMessageW/u);
  requireMatch("快捷键窗口创建与初始化超时竞态没有二次收口", globalHotkey,
    /startupCancelled = shutdownRequested_\.load\(\)[\s\S]*?receiver != nullptr && !startupCancelled/u);
  requireMatch("MSIX 应用数据没有路由到包 LocalState", appPaths,
    /GetCurrentPackageFamilyName[\s\S]*?FOLDERID_LocalAppData[\s\S]*?L"Packages"[\s\S]*?L"LocalState"/u);
  requireMatch("Portable 应用数据没有保留程序旁目录", appPaths,
    /const auto root = hasPackageIdentity\(\)[\s\S]*?\? packagedApplicationDataRoot\(\)[\s\S]*?: executableDirectory\(\)/u);

  const tests = [
    "tests/stitch_engine_test.cpp",
    "tests/stitch_engine_sticky_test.cpp",
    "tests/session_contract_test.cpp",
    "tests/image_metrics_test.cpp",
    "tests/selection_and_hotkey_test.cpp",
    "tests/window_geometry_test.cpp",
    "tests/windows_macro_compat_test.cpp",
    "tests/input_boundary_test.cpp",
  ];
  tests.forEach((file) => requireFile(file));
  requireMatch("输入边界测试缺少人工输入关闭回归",
    read("tests/input_boundary_test.cpp"), /testClosedManualInputIsRejected/u);
  requireMatch("窗口几何测试缺少 60–240Hz 脉冲映射回归",
    read("tests/window_geometry_test.cpp"),
    /navigationPulseIntervalMicros\(60\)[\s\S]*?navigationPulseIntervalMicros\(240\)/u);
  for (const test of ["stitch_engine_test", "stitch_engine_sticky_test",
    "session_contract_test", "image_metrics_test", "selection_and_hotkey_test",
    "window_geometry_test", "windows_macro_compat_test", "input_boundary_test"]) {
    requireMatch(`CMake 缺少 ${test}`, cmake,
      new RegExp(`add_test\\(NAME ${test} COMMAND ${test}\\)`, "u"));
  }

  const implementationFiles = walk(projectRoot, (file) =>
    /\.(?:cpp|h)$/u.test(file) &&
    !file.includes(`${context.path.sep}docs${context.path.sep}`));
  checkLineBudget(
    implementationFiles.map((file) => context.path.relative(projectRoot, file)), 500);
  checkLineBudget([
    "tests/stitch_engine_test.cpp",
    "tests/stitch_engine_sticky_test.cpp",
    "tests/session_contract_test.cpp",
    "tests/StitchTestFixtures.h",
  ], 340);
}
