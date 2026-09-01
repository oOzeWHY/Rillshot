export function checkCore(context) {
  const { path, projectRoot, read, requireFile, requireMatch, failures } = context;
  const cmake = read("CMakeLists.txt");
  const presets = read("CMakePresets.json");

  requireMatch("CMake 缺少共享核心库", cmake, /add_library\(rillshot_core STATIC/u);
  requireMatch("CMake 缺少严格警告开关", cmake, /RILLSHOT_WARNINGS_AS_ERRORS/u);
  requireMatch("CMake 缺少 ASan/UBSan 开关", cmake,
    /RILLSHOT_ENABLE_SANITIZERS[\s\S]*-fsanitize=address,undefined/u);
  requireMatch("预设缺少严格 Release 核心构建", presets,
    /core-release[\s\S]*RILLSHOT_WARNINGS_AS_ERRORS[\s\S]*ON/u);
  requireMatch("预设缺少 sanitizer 构建", presets,
    /core-sanitize[\s\S]*RILLSHOT_ENABLE_SANITIZERS[\s\S]*ON/u);

  const tests = [
    "stitch_engine_test", "stitch_engine_sticky_test", "session_contract_test",
    "image_metrics_test", "selection_and_hotkey_test", "window_geometry_test",
    "windows_macro_compat_test", "input_boundary_test",
  ];
  for (const name of tests) {
    const file = `tests/${name}.cpp`;
    requireFile(file);
    if (!cmake.includes(`add_executable(${name} ${file})`) ||
        !cmake.includes(`add_test(NAME ${name} COMMAND ${name})`)) {
      failures.push(`CMake 未完整注册测试：${name}`);
    }
  }

  const requiredBoundaries = [
    "src/core/Image.cpp", "src/core/ImageMetrics.cpp", "src/stitch/StitchEngine.cpp",
    "src/session/CaptureSession.cpp", "src/session/SessionCaptureSupport.cpp",
    "src/session/SessionInputSupport.cpp", "src/session/SessionOutputSupport.cpp",
    "src/gui/WindowGeometry.h", "src/platform/AppPaths.cpp",
  ];
  requiredBoundaries.forEach(requireFile);
  for (const file of requiredBoundaries.filter((value) => value.endsWith(".cpp"))) {
    if (!cmake.includes(file)) failures.push(`共享核心目标未引用职责模块：${file}`);
  }

  const generated = context.walk(projectRoot, (file) =>
    /\.(?:o|obj|exe|pdb|ilk|log|jsonl)$/iu.test(file) ||
    (path.dirname(file) === projectRoot && /^cc.+\.(?:s|res|c|o)$/iu.test(path.basename(file))));
  for (const file of generated) {
    failures.push(`交付源码含本机构建产物：${path.relative(projectRoot, file)}`);
  }
}
