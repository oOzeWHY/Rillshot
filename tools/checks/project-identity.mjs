export function checkProjectIdentity(context) {
  const {
    projectRoot, read, requireFile, requireMatch, requireNoMatch, walk, failures,
  } = context;

  const cmake = read("CMakeLists.txt");
  const solution = read("apps/rillshot_winui/Rillshot.WinUI.sln");
  const project = read("apps/rillshot_winui/Rillshot.WinUI.vcxproj");
  const hybridCrt = read("apps/rillshot_winui/HybridCRT.props");
  const cliManifest = read("apps/rillshot_cli/rillshot_cli.manifest");
  const guiManifest = read("apps/rillshot_gui/rillshot_gui.manifest");
  const appManifest = read("apps/rillshot_winui/app.manifest");
  const packageManifest = read("apps/rillshot_winui/Package.appxmanifest");
  const releaseScript = read("tools/build-release.ps1");
  const cli = read("apps/rillshot_cli/main.cpp");
  const winuiResources = read("apps/rillshot_winui/Rillshot.rc");

  requireMatch("CMake 产品版本不是 Rillshot 1.1.9", cmake,
    /project\(Rillshot VERSION 1\.1\.9 LANGUAGES CXX\)/u);
  requireMatch("CMake 测试开关未迁移到 RILLSHOT_*", cmake,
    /option\(RILLSHOT_BUILD_TESTS/u);
  requireMatch("CMake 缺少 Rillshot WinUI 之外的共享核心目标", cmake,
    /add_library\(rillshot_core STATIC/u);

  requireMatch("解决方案仍未指向 Rillshot.WinUI.vcxproj", solution,
    /"Rillshot\.WinUI\.vcxproj"/u);
  requireMatch("WinUI ProjectName 不一致", project,
    /<ProjectName>Rillshot\.WinUI<\/ProjectName>/u);
  requireMatch("WinUI RootNamespace 不一致", project,
    /<RootNamespace>Rillshot\.WinUI<\/RootNamespace>/u);
  requireMatch("Portable/MSIX 分发属性未迁移", project,
    /<RillshotDistribution/u);
  requireMatch("新增快照实现未加入 WinUI 工程", project,
    /SelectionOverlay\.Snapshot\.cpp/u);
  requireMatch("输出支持模块未加入 WinUI 工程", project,
    /SessionOutputSupport\.cpp/u);
  requireMatch("输入异常边界模块未加入 WinUI 工程", project,
    /SessionInputSupport\.cpp/u);
  requireMatch("导航合成模块未加入 WinUI 工程", project,
    /MainWindow\.Navigation\.Animation\.cpp/u);
  requireMatch("自适应窗口尺寸策略未加入 WinUI 工程", project,
    /MainWindow\.WindowSizing\.h/u);
  requireMatch("偏好持久化模块未加入 WinUI 工程", project,
    /MainWindow\.PreferencePersistence\.cpp/u);
  requireMatch("WinUI 工程未使用精简的 Windows App SDK WinUI 组件包", project,
    /<PackageReference Include="Microsoft\.WindowsAppSDK\.WinUI" Version="2\.3\.0" \/>/u);
  requireMatch("WinUI 工程未固定稳定 Foundation 组件以规避 2.3.0 错误依赖元数据", project,
    /<PackageReference Include="Microsoft\.WindowsAppSDK\.Foundation" Version="2\.3\.5" \/>/u);
  requireMatch("WinUI 工程未固定稳定 InteractiveExperiences 组件", project,
    /<PackageReference Include="Microsoft\.WindowsAppSDK\.InteractiveExperiences" Version="2\.1\.3" \/>/u);
  requireMatch("WinUI 工程未显式引用 Windows App SDK Runtime 组件包", project,
    /<PackageReference Include="Microsoft\.WindowsAppSDK\.Runtime" Version="2\.3\.1" \/>/u);
  requireNoMatch("WinUI 工程仍引用会带入 AI\/ML\/Widgets 的聚合 Windows App SDK 包", project,
    /<PackageReference Include="Microsoft\.WindowsAppSDK" Version=/u);
  requireNoMatch("WinUI 工程直接引用了预发布组件", project,
    /<PackageReference[^>]+Version="[^"]*-(?:experimental|preview|rc)[^"]*"/iu);
  requireMatch("Portable C++ 工程未导入自包含部署所需的 Hybrid CRT 配置", project,
    /<Import Project="HybridCRT\.props" Condition="'\$\(RillshotDistribution\)'=='Portable'" \/>/u);
  requireMatch("Hybrid CRT Release 配置未静态链接 MSVC 运行库", hybridCrt,
    /Condition="'\$\(Configuration\)'=='Release'"[\s\S]*<RuntimeLibrary>MultiThreaded<\/RuntimeLibrary>/u);

  for (const [label, content] of [
    ["CLI", cliManifest], ["Win32 GUI", guiManifest], ["WinUI", appManifest],
  ]) {
    requireMatch(`${label} 清单版本不是 1.1.9.0`, content,
      /version="1\.1\.9\.0"/u);
  }
  requireMatch("MSIX 包版本不是 1.1.9.0", packageManifest,
    /Version="1\.1\.9\.0"/u);
  requireMatch("MSIX 显示名不是 Rillshot", packageManifest,
    /<DisplayName>Rillshot<\/DisplayName>/u);
  requireMatch("发布脚本版本不是 1.1.9", releaseScript,
    /\$version = "1\.1\.9"/u);
  requireMatch("发布脚本没有使用新解决方案", releaseScript,
    /Rillshot\.WinUI\.sln/u);
  requireMatch("Portable 压缩包未使用 Rillshot 名称", releaseScript,
    /Rillshot-\$artifactVersion-win-\$Platform-portable/u);
  requireMatch("CLI 帮助版本不是 1.1.9", cli,
    /Rillshot 1\.1\.9 CLI/u);
  requireMatch("WinUI EXE 缺少 1.1.9 文件和产品版本资源", winuiResources,
    /FILEVERSION 1,1,9,0[\s\S]*?PRODUCTVERSION 1,1,9,0[\s\S]*?"FileVersion", "1\.1\.9\.0"[\s\S]*?"ProductVersion", "1\.1\.9\.0"/u);
  requireMatch("WinUI EXE 版权资源未使用实际权利人集合称呼", winuiResources,
    /Copyright © 2026 Rillshot contributors/u);

  const required = [
    "apps/rillshot_winui/Rillshot.WinUI.sln",
    "apps/rillshot_winui/Rillshot.WinUI.vcxproj",
    "apps/rillshot_winui/HybridCRT.props",
    "apps/rillshot_cli/rillshot_cli.manifest",
    "apps/rillshot_gui/rillshot_gui.manifest",
    "src/gui/SelectionOverlay.Snapshot.cpp",
    "apps/rillshot_winui/MainWindow.Navigation.Animation.cpp",
    "apps/rillshot_winui/MainWindow.Navigation.Motion.h",
    "apps/rillshot_winui/MainWindow.WindowSizing.h",
    "apps/rillshot_winui/MainWindow.PreferencePersistence.cpp",
  ];
  required.forEach((file) => requireFile(file));

  const activeFiles = walk(projectRoot, (file) =>
    /\.(?:cpp|h|idl|xaml|xml|vcxproj|sln|manifest|ps1|cmd|mjs)$/iu.test(file) &&
    !file.includes(`${context.path.sep}docs${context.path.sep}archive${context.path.sep}`));
  const legacyProduct = ["Scroll", "Stitch"].join("");
  const legacyTokens = [legacyProduct, legacyProduct.toLowerCase(),
    legacyProduct.toUpperCase()];
  for (const file of activeFiles) {
    const content = context.fs.readFileSync(file, "utf8");
    if (legacyTokens.some((token) => content.includes(token))) {
      failures.push(`活动工程仍含旧产品身份：${context.path.relative(projectRoot, file)}`);
    }
  }
  requireNoMatch("活动 CMake 仍含 0.x 版本", cmake, /VERSION 0\./u);
}
