export function checkProjectIdentity(context) {
  const {
    fs, path, projectRoot, read, requireFile, requireMatch, requireNoMatch,
    walk, failures,
  } = context;
  const cmake = read("CMakeLists.txt");
  const solution = read("apps/rillshot_winui/Rillshot.WinUI.sln");
  const winuiProject = read("apps/rillshot_winui/Rillshot.WinUI.vcxproj");
  const launcherProject = read("apps/rillshot_launcher/Rillshot.Launcher.vcxproj");
  const releaseScript = read("tools/build-release.ps1");

  const required = [
    "apps/rillshot_winui/Rillshot.WinUI.sln",
    "apps/rillshot_winui/Rillshot.WinUI.vcxproj",
    "apps/rillshot_winui/HybridCRT.props",
    "apps/rillshot_launcher/main.cpp",
    "apps/rillshot_launcher/Rillshot.Launcher.vcxproj",
    "apps/rillshot_launcher/Rillshot.Launcher.rc",
    "apps/rillshot_launcher/launcher.manifest",
    "apps/rillshot_cli/rillshot_cli.manifest",
    "apps/rillshot_gui/rillshot_gui.manifest",
  ];
  required.forEach(requireFile);

  requireMatch("CMake 产品版本不是 1.1.9", cmake,
    /project\(Rillshot VERSION 1\.1\.9 LANGUAGES CXX\)/u);
  requireMatch("发布脚本产品版本不是 1.1.9", releaseScript,
    /\$version = "1\.1\.9"/u);
  requireMatch("源码身份未更新", releaseScript,
    /\$sourceIdentity = "1\.1\.9-source-ready-reviewed-r2"/u);
  requireMatch("解决方案缺少 WinUI 工程", solution, /Rillshot\.WinUI\.vcxproj/u);
  requireMatch("解决方案缺少原生 Portable 启动器", solution,
    /rillshot_launcher\\Rillshot\.Launcher\.vcxproj/u);
  requireMatch("启动器输出名称不是 Rillshot.exe", launcherProject,
    /<TargetName>Rillshot<\/TargetName>/u);
  requireMatch("WinUI 工程未声明分发模式", winuiProject, /<RillshotDistribution/u);
  requireNoMatch("WinUI 工程不应引用 Windows App SDK 聚合包", winuiProject,
    /<PackageReference Include="Microsoft\.WindowsAppSDK" Version=/u);
  requireNoMatch("WinUI 工程不应直接使用预发布依赖", winuiProject,
    /<PackageReference[^>]+Version="[^"]*-(?:experimental|preview|rc)[^"]*"/iu);

  for (const [label, file] of [
    ["CLI", "apps/rillshot_cli/rillshot_cli.manifest"],
    ["Win32 GUI", "apps/rillshot_gui/rillshot_gui.manifest"],
    ["WinUI", "apps/rillshot_winui/app.manifest"],
    ["Launcher", "apps/rillshot_launcher/launcher.manifest"],
  ]) {
    requireMatch(`${label} 清单版本不是 1.1.9.0`, read(file),
      /version="1\.1\.9\.0"/u);
  }
  requireMatch("MSIX 包版本不是 1.1.9.0",
    read("apps/rillshot_winui/Package.appxmanifest"), /Version="1\.1\.9\.0"/u);
  for (const [label, file] of [
    ["WinUI", "apps/rillshot_winui/Rillshot.rc"],
    ["Launcher", "apps/rillshot_launcher/Rillshot.Launcher.rc"],
  ]) {
    requireMatch(`${label} 资源版本不是 1.1.9.0`, read(file),
      /FILEVERSION 1,1,9,0[\s\S]*PRODUCTVERSION 1,1,9,0/u);
  }

  const legacyName = ["Scroll", "Stitch"].join("");
  const activeFiles = walk(projectRoot, (file) =>
    /\.(?:cpp|h|idl|xaml|vcxproj|sln|manifest|ps1|mjs)$/iu.test(file));
  for (const file of activeFiles) {
    const content = fs.readFileSync(file, "utf8");
    if (content.toLowerCase().includes(legacyName.toLowerCase())) {
      failures.push(`活动工程仍含旧产品身份：${path.relative(projectRoot, file)}`);
    }
  }
}
