export function checkWinUI(context) {
  const { fs, path, projectRoot, read, requireFile, requireMatch, failures } = context;
  const appXaml = read("apps/rillshot_winui/App.xaml");
  const mainXaml = read("apps/rillshot_winui/MainWindow.xaml");
  const project = read("apps/rillshot_winui/Rillshot.WinUI.vcxproj");
  const implementation = context.walk(
    path.join(projectRoot, "apps", "rillshot_winui"),
    (file) => /MainWindow.*\.(?:cpp|h)$/iu.test(file),
  ).map((file) => fs.readFileSync(file, "utf8")).join("\n");

  for (const file of [
    "apps/rillshot_winui/App.xaml", "apps/rillshot_winui/MainWindow.xaml",
    "apps/rillshot_winui/MainWindow.idl", "apps/rillshot_winui/MainWindow.xaml.cpp",
    "apps/rillshot_winui/MainWindow.xaml.h",
  ]) requireFile(file);

  const handlerPattern = /(?:Click|Loaded|SelectionChanged|SizeChanged|Toggled|ValueChanged)="([A-Za-z0-9_]+)"/gu;
  for (const match of mainXaml.matchAll(handlerPattern)) context.xamlHandlers.add(match[1]);
  for (const handler of context.xamlHandlers) {
    if (!implementation.includes(`${handler}(`)) failures.push(`XAML 事件缺少 C++ 实现：${handler}`);
  }

  const declaredKeys = new Set(
    [...appXaml.matchAll(/x:Key="([^"]+)"/gu)].map((match) => match[1]),
  );
  const builtInStyles = new Set(["AccentButtonStyle", "CaptionTextBlockStyle"]);
  for (const match of mainXaml.matchAll(/\{StaticResource ([^}]+)\}/gu)) {
    if (!declaredKeys.has(match[1]) && !builtInStyles.has(match[1])) {
      failures.push(`XAML 引用了未定义的静态资源：${match[1]}`);
    }
  }

  requireMatch("设置操作按钮未满足 40 epx 最小触控高度", appXaml,
    /x:Key="SettingActionButtonStyle"[\s\S]*<Setter Property="MinHeight" Value="40"\/>/u);
  requireMatch("紧凑数字框未满足 40 epx 最小触控高度", appXaml,
    /x:Key="CompactNumberBoxStyle"[\s\S]*<Setter Property="MinHeight" Value="40"\/>/u);
  const undersizedTargets = [...mainXaml.matchAll(/MinHeight="(\d+)"/gu)]
    .filter((match) => Number(match[1]) < 40);
  if (undersizedTargets.length > 0) failures.push("WinUI 仍含低于 40 epx 的显式交互高度");
  requireMatch("主窗口缺少响应式视觉状态", mainXaml,
    /VisualStateManager\.VisualStateGroups[\s\S]*AdaptiveTrigger/u);
  requireMatch("主窗口缺少系统主题入口", mainXaml, /Theme_SelectionChanged/u);
  requireMatch("主窗口缺少辅助技术实时状态", mainXaml,
    /AutomationProperties\.LiveSetting/u);

  const cppFiles = context.walk(
    path.join(projectRoot, "apps", "rillshot_winui"),
    (file) => /\.cpp$/iu.test(file),
  );
  for (const file of cppFiles) {
    const relative = path.relative(path.join(projectRoot, "apps", "rillshot_winui"), file)
      .replaceAll(path.sep, "\\");
    if (!project.includes(`Include="${relative}"`)) {
      failures.push(`WinUI 工程未包含实现文件：${relative}`);
    }
  }
}
