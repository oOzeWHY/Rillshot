function pngDimensions(buffer) {
  if (buffer.length < 24 || buffer.toString("ascii", 1, 4) !== "PNG") return null;
  return { width: buffer.readUInt32BE(16), height: buffer.readUInt32BE(20) };
}

function icoSizes(buffer) {
  if (buffer.length < 6 || buffer.readUInt16LE(0) !== 0 ||
      buffer.readUInt16LE(2) !== 1) return null;
  const count = buffer.readUInt16LE(4);
  if (buffer.length < 6 + count * 16) return null;
  const sizes = [];
  for (let index = 0; index < count; ++index) {
    const offset = 6 + index * 16;
    const width = buffer[offset] || 256;
    const height = buffer[offset + 1] || 256;
    sizes.push(`${width}x${height}`);
  }
  return sizes;
}

export function checkWinUI(context) {
  const { read, readBuffer, requireMatch, requireNoMatch, requireFile, failures } = context;
  const appXaml = read("apps/rillshot_winui/App.xaml");
  const windowXaml = read("apps/rillshot_winui/MainWindow.xaml");
  const header = read("apps/rillshot_winui/MainWindow.xaml.h");
  const navigationMotion = read("apps/rillshot_winui/MainWindow.Navigation.Motion.h");
  const windowSizing = read("apps/rillshot_winui/MainWindow.WindowSizing.h");
  const navigationState = read("apps/rillshot_winui/MainWindow.Navigation.cpp");
  const navigationAnimation = read("apps/rillshot_winui/MainWindow.Navigation.Animation.cpp");
  const navigation = `${navigationMotion}\n${windowSizing}\n${navigationState}\n${navigationAnimation}`;
  const preferences = read("apps/rillshot_winui/MainWindow.Preferences.cpp");
  const preferencePersistence = read("apps/rillshot_winui/MainWindow.PreferencePersistence.cpp");
  const presentation = read("apps/rillshot_winui/MainWindow.Presentation.cpp");
  const mainSelection = read("apps/rillshot_winui/MainWindow.Selection.cpp");
  const window = read("apps/rillshot_winui/MainWindow.xaml.cpp");
  const selection = read("src/gui/SelectionOverlay.cpp");
  const selectionRendering = read("src/gui/SelectionOverlay.Rendering.cpp");
  const selectionSnapshot = read("src/gui/SelectionOverlay.Snapshot.cpp");
  const project = read("apps/rillshot_winui/Rillshot.WinUI.vcxproj");

  const xamlDependentHeaders = [...project.matchAll(
    /<ClInclude\s+Include="([^"]+)"[^>]*>([\s\S]*?)<\/ClInclude>/gu,
  )].flatMap((match) => {
    const dependent = match[2].match(/<DependentUpon>([^<]+)<\/DependentUpon>/u);
    return dependent ? [{ header: match[1], dependent: dependent[1] }] : [];
  });
  const mainWindowDependentHeaders = xamlDependentHeaders.filter(
    (item) => item.dependent.toLowerCase() === "mainwindow.xaml",
  );
  if (mainWindowDependentHeaders.length !== 1 ||
      mainWindowDependentHeaders[0].header !== "MainWindow.xaml.h") {
    failures.push(
      "MainWindow.xaml 必须且只能由 MainWindow.xaml.h 声明 DependentUpon；辅助头不能重复占用 XAML 类到头文件映射",
    );
  }

  requireMatch("准备态次级命令没有共享统一样式", appXaml,
    /x:Key="ReadyCommandButtonStyle"[\s\S]*?MinHeight" Value="64"/u);
  const sharedStyleUses = windowXaml.match(
    /Style="\{StaticResource ReadyCommandButtonStyle\}"/gu) ?? [];
  if (sharedStyleUses.length !== 2) {
    failures.push("滚动点与设置必须且只能共同使用两次 ReadyCommandButtonStyle");
  }
  requireMatch("设置入口缺少完整语义", windowXaml,
    /x:Name="OpenSettingsButton"[\s\S]*?AutomationProperties\.Name="打开设置"/u);
  requireMatch("设置消息没有留在设置视图内", windowXaml,
    /x:Name="SettingsView"[\s\S]*?x:Name="ShellInfoBar"/u);
  requireNoMatch("主页面重新引入了顶部通知占位", windowXaml,
    /x:Name="ReadyView"[\s\S]{0,800}x:Name="ShellInfoBar"/u);

  requireNoMatch("设置切换仍使用 UI 线程逐帧回调", navigation,
    /CompositionTarget::Rendering/u);
  requireNoMatch("设置窗口动画重新使用了无节流 SetWindowPos 或 AppWindow.Resize", navigation,
    /SetWindowPos|AppWindow\(\)\.Resize/u);
  requireMatch("设置页面未直接使用合成线程动画", navigation,
    /GetCompositorForCurrentThread[\s\S]*?CreateScopedBatch[\s\S]*?StartAnimation/u);
  const compositorGetterCalls = navigationAnimation.match(
    /CompositionTarget::\s*GetCompositorForCurrentThread\(\)/gu) ?? [];
  const qualifiedCompositorGetterCalls = navigationAnimation.match(
    /Microsoft::UI::Xaml::Media::CompositionTarget::\s*GetCompositorForCurrentThread\(\)/gu) ?? [];
  if (compositorGetterCalls.length !== 2 ||
      qualifiedCompositorGetterCalls.length !== compositorGetterCalls.length) {
    failures.push("合成器获取调用必须显式限定为 Microsoft::UI::Xaml::Media::CompositionTarget");
  }
  requireMatch("设置页面切换未使用 220ms 协同动效", navigation,
    /navigationDuration = std::chrono::milliseconds\(220\)/u);
  requireNoMatch("窗口几何仍使用会被 idle 工作饿死的 DispatcherQueueTimer", navigation,
    /DispatcherQueueTimer|\.CreateTimer\(\)/u);
  requireMatch("窗口几何没有使用 4–16.667ms 刷新率感知脉冲", navigation,
    /navigationResizeMinimumInterval = std::chrono::microseconds\(4'000\)[\s\S]*?navigationResizeMaximumInterval = std::chrono::microseconds\(16'667\)[\s\S]*?EnumDisplaySettingsW[\s\S]*?CreatePeriodicTimer/u);
  requireMatch("窗口几何脉冲没有合并为最多一个待处理 UI 更新", navigation,
    /dispatchPending[\s\S]*?compare_exchange_strong[\s\S]*?DispatcherQueuePriority::High[\s\S]*?advanceNavigationWindowResize/u);
  requireMatch("设置窗口没有按 DPI、工作区和占用比例计算自适应目标尺寸", navigation,
    /settingsWidthDip = 1000\.0[\s\S]*?settingsHeightDip = 760\.0[\s\S]*?maximumWorkAreaWidthPercent = 88[\s\S]*?maximumWorkAreaHeightPercent = 82[\s\S]*?fitAdaptiveWindowSizeAroundCenter/u);
  requireMatch("跨 DPI 显示器返回主页面时没有保持紧凑窗口的有效尺寸", navigationState,
    /compactWindowDpi_[\s\S]*?scalePixelDimensionForDpi/u);
  requireMatch("首次窗口没有使用与设置页一致的自适应工作区策略", window,
    /fitAdaptiveWindowSizeAroundCenter[\s\S]*?maximumWorkAreaWidthPercent[\s\S]*?maximumWorkAreaHeightPercent/u);
  requireMatch("设置窗口没有使用平滑插值后的原子移动缩放", navigation,
    /smootherstepProgress[\s\S]*?MoveAndResize/u);
  requireMatch("进入动效未使用 Fluent Fast Out, Slow In 曲线", navigation,
    /float2\{0\.0F, 0\.0F\}, float2\{0\.0F, 1\.0F\}/u);
  requireMatch("退出动效未使用 Fluent Slow Out, Fast In 曲线", navigation,
    /float2\{1\.0F, 0\.0F\}, float2\{1\.0F, 1\.0F\}/u);
  requireMatch("设置页首次布局成本没有与合成动画隔离", navigation,
    /DispatcherQueuePriority::Low[\s\S]*?beginPageNavigationAnimation[\s\S]*?UpdateLayout/u);
  requireMatch("主页面与设置页没有使用独立滚动视口", windowXaml,
    /x:Name="CaptureScrollViewer"[\s\S]*?x:Name="SettingsScrollViewer"/u);
  requireMatch("窗口缩放期间没有固定页面视口尺寸以隔离整树重排", navigationAnimation,
    /prepareNavigationViewportIsolation[\s\S]*?outgoing\.Width\(currentWidth\)[\s\S]*?incoming\.Width\(targetWidth\)[\s\S]*?clearNavigationViewportIsolation/u);
  requireMatch("设置页没有在首次交互前空闲预热布局与合成模板",
    `${window}\n${navigationAnimation}`,
    /DispatcherQueuePriority::Low[\s\S]*?prewarmNavigationExperience[\s\S]*?ensureNavigationAnimationTemplates[\s\S]*?UpdateLayout/u);
  requireMatch("设置页预热没有等待首个已呈现帧", window,
    /CompositionTarget::Rendered[\s\S]*?DispatcherQueuePriority::Low[\s\S]*?prewarmNavigationExperience/u);
  requireMatch("设置页首帧观察器没有一次性解除或关闭清理",
    `${window}\n${preferences}`,
    /stopNavigationPrewarmObservation[\s\S]*?CompositionTarget::Rendered/u);
  requireMatch("设置导航没有复用合成模板", navigationAnimation,
    /navigationTemplatesReady_[\s\S]*?CreateScalarKeyFrameAnimation[\s\S]*?CreateVector3KeyFrameAnimation/u);
  requireMatch("设置缩放没有围绕可见内容中心", navigationAnimation,
    /setNavigationCenterPoint[\s\S]*?CenterPoint/u);
  requireMatch("快速设置往返没有合并为最新目标", navigationState,
    /navigationPendingTarget_ = open[\s\S]*?pendingTarget[\s\S]*?navigateToSettings\(\*pendingTarget\)/u);
  requireMatch("设置页面没有在同一合成批次交叉淡化、位移与缩放", navigation,
    /Target\(L"Opacity"\)[\s\S]*?Target\(L"Translation"\)[\s\S]*?Target\(L"Scale"\)[\s\S]*?CompositionBatchTypes::Animation/u);
  requireMatch("合成批次完成事件没有收口导航终态", navigation,
    /navigationBatch_\.Completed[\s\S]*?completePageNavigation/u);
  requireMatch("减少动画边界未保留", navigation,
    /navigationUiSettings_[\s\S]*?AnimationsEnabled\(\)/u);
  requireNoMatch("XAML 仍保留与合成属性冲突的 Storyboard 或 RenderTransform",
    windowXaml, /<Storyboard\b|<StackPanel\.RenderTransform>/u);
  requireNoMatch("主页面 Ready 卡片仍叠加独立进入过渡",
    windowXaml, /x:Name="ReadyPanel"[\s\S]{0,240}<EntranceThemeTransition/u);
  requireMatch("截图区域未选择状态没有简洁操作提示", windowXaml,
    /x:Name="RegionSummaryText" Text="点击选择区域"/u);
  requireMatch("滚动点未选择状态没有简洁操作提示", windowXaml,
    /x:Name="ScrollSummaryText" Text="点击选择滚动点"/u);
  requireNoMatch("截图区域坐标仍包含演示默认值", windowXaml,
    /x:Name="Region(?:X|Y|Width|Height)Box"[^>]*\sValue=/u);
  requireNoMatch("滚动点坐标仍包含演示默认值", windowXaml,
    /x:Name="Scroll(?:X|Y)Box"[^>]*\sValue=/u);
  requireMatch("空坐标输入没有明确未选择占位", windowXaml,
    /x:Name="RegionXBox"[^>]*PlaceholderText="未选择"[\s\S]*?x:Name="ScrollYBox"[^>]*PlaceholderText="未选择"/u);
  requireMatch("重新选择区域后没有清除旧滚动点", mainSelection,
    /quiet_NaN\(\)[\s\S]*?ScrollXBox\(\)\.Value\(unsetValue\)[\s\S]*?ScrollYBox\(\)\.Value\(unsetValue\)/u);
  requireNoMatch("框选区域仍自动生成滚动点", mainSelection,
    /defaultScrollPoint/u);
  requireNoMatch("切换滚动方向仍自动改写滚动点", presentation,
    /ScrollDirection_SelectionChanged[\s\S]{0,900}defaultScrollPoint/u);
  requireMatch("合成批次晚到回调未核对当前导航状态", navigation,
    /navigationBatch_\.Completed[\s\S]*?navigationTransitionRunning_[\s\S]*?navigationTargetOpen_ == open/u);
  requireMatch("交互界面没有显示 GPL 适当法律通知", windowXaml,
    /关于与许可[\s\S]*?Copyright © 2026 Rillshot contributors[\s\S]*?不提供任何担保/u);
  requireMatch("交互界面没有提供 GPL 全文入口", windowXaml,
    /NavigateUri="https:\/\/www\.gnu\.org\/licenses\/gpl-3\.0\.html"/u);
  requireMatch("初始窗口工作区尺寸缺少受检坐标跨度", window,
    /tryPositiveCoordinateSpan[\s\S]*?rcWork\.left[\s\S]*?rcWork\.right/u);
  requireMatch("选择覆盖层虚拟屏幕边界缺少受检偏移", selection,
    /tryCoordinateOffset\(left, width, right\)[\s\S]*?tryCoordinateOffset\(top, height, bottom\)/u);
  requireMatch("选择快照没有复用已验证的虚拟屏幕尺寸", selectionSnapshot,
    /virtualScreenWidth_[\s\S]*?virtualScreenHeight_/u);
  requireMatch("选择绘制缺少饱和屏幕到客户区坐标转换", selectionRendering,
    /saturatedCoordinateDelta/u);
  requireNoMatch("选择快照重新引入 32 位虚拟屏幕减法", selectionSnapshot,
    /virtualScreen_\.right\s*-\s*virtualScreen_\.left/u);

  requireMatch("快捷键切换没有延后到低优先级队列", preferences,
    /DispatcherQueuePriority::Low/u);
  requireMatch("快捷键连续切换没有合并最终状态", preferences,
    /hotkeyApplyQueued_/u);
  requireMatch("快捷键延后失败没有同步回退", preferences,
    /if \(!queued\)[\s\S]*?applyGlobalHotkey/u);
  requireMatch("主题切换没有延后并合并昂贵的主题失效", preferences,
    /themeApplyQueued_[\s\S]*?DispatcherQueuePriority::Low[\s\S]*?applyThemePreference/u);
  requireMatch("偏好持久化没有移出 UI 事件线程", preferencePersistence,
    /ThreadPool::RunAsync[\s\S]*?saveUserPreferences/u);
  requireMatch("连续偏好写入没有按代次合并最新状态", preferencePersistence,
    /latestGeneration[\s\S]*?workerScheduled[\s\S]*?generation != state->latestGeneration/u);
  requireMatch("窗口关闭没有补写最后一份偏好", preferencePersistence,
    /flushPreferenceSave[\s\S]*?savedGeneration[\s\S]*?saveUserPreferences/u);
  requireNoMatch("偏好交互处理器仍直接同步写磁盘", preferences,
    /saveUserPreferences/u);
  requireMatch("普通成功通知未保持安静", presentation,
    /severity == InfoBarSeverity::Success[\s\S]*?return;/u);

  const handlerExpression = /(?:Click|Toggled|SelectionChanged|KeyDown|TextChanged)="([A-Za-z0-9_]+)"/gu;
  for (const match of windowXaml.matchAll(handlerExpression)) {
    context.xamlHandlers.add(match[1]);
  }
  for (const handler of context.xamlHandlers) {
    if (!new RegExp(`\\b${handler}\\s*\\(`, "u").test(header)) {
      failures.push(`XAML 事件 ${handler} 未在 MainWindow.xaml.h 声明`);
    }
  }

  const assets = new Map([
    ["Square44x44Logo.png", [44, 44]],
    ["Square44x44Logo.scale-200.png", [88, 88]],
    ["Square44x44Logo.scale-400.png", [176, 176]],
    ["Square44x44Logo.targetsize-16.png", [16, 16]],
    ["Square44x44Logo.targetsize-24.png", [24, 24]],
    ["Square44x44Logo.targetsize-32.png", [32, 32]],
    ["Square44x44Logo.targetsize-48.png", [48, 48]],
    ["Square44x44Logo.targetsize-256.png", [256, 256]],
    ["Square150x150Logo.png", [150, 150]],
    ["Square150x150Logo.scale-200.png", [300, 300]],
    ["Square150x150Logo.scale-400.png", [600, 600]],
    ["StoreLogo.png", [50, 50]],
    ["Wide310x150Logo.png", [310, 150]],
    ["SplashScreen.png", [620, 300]],
  ]);
  requireFile("apps/rillshot_winui/Assets/AppIcon.svg");
  requireFile("apps/rillshot_winui/Assets/Rillshot.ico");
  requireFile("apps/rillshot_winui/Rillshot.rc");
  requireFile("apps/rillshot_winui/Assets/BrandLockup.svg");
  const icon = read("apps/rillshot_winui/Assets/AppIcon.svg");
  requireMatch("新图标没有长内容接缝母题", icon, /id="seamColumn"/u);
  requireMatch("新图标没有对角捕获边界", icon, /id="captureBounds"/u);
  requireNoMatch("主应用图标重新引入了可见字母或字标", icon, /<text\b/u);
  requireNoMatch("主应用图标重新引入了旧 ribbon R", icon, /id="ribbon"/u);
  requireMatch("Portable EXE 没有编译图标资源", project,
    /<ResourceCompile Include="Rillshot\.rc"/u);
  requireMatch("MSIX 未包含社区许可、对应源码说明与第三方通知", project,
    /<Link>Legal\\LICENSE\.txt<\/Link>[\s\S]*?<Link>Legal\\COPYRIGHT\.md<\/Link>[\s\S]*?<Link>Legal\\SOURCE_OFFER\.md<\/Link>[\s\S]*?<Link>Legal\\THIRD_PARTY_NOTICES\.txt<\/Link>/u);
  requireMatch("WinUI 窗口没有设置大小图标", window,
    /WM_SETICON[\s\S]*?ICON_BIG[\s\S]*?ICON_SMALL/u);
  requireMatch("窗口图标局部变量未避开 Windows SDK 的 small 标识符", window,
    /HICON largeIcon[\s\S]*?HICON smallIcon/u);
  requireNoMatch("窗口图标代码重新使用了会与 Windows SDK 冲突的 small 名称", window,
    /HICON\s+small\b/u);
  const expectedIcoSizes = ["16x16", "24x24", "32x32", "48x48", "64x64", "128x128", "256x256"];
  const actualIcoSizes = icoSizes(readBuffer("apps/rillshot_winui/Assets/Rillshot.ico"));
  if (!actualIcoSizes || expectedIcoSizes.some((size) => !actualIcoSizes.includes(size))) {
    failures.push("Rillshot.ico 缺少 16/24/32/48/64/128/256 方形帧");
  }
  for (const [name, expected] of assets) {
    const relative = `apps/rillshot_winui/Assets/${name}`;
    if (!requireFile(relative)) continue;
    const dimensions = pngDimensions(readBuffer(relative));
    if (!dimensions || dimensions.width !== expected[0] ||
        dimensions.height !== expected[1]) {
      failures.push(`${name} 尺寸不是 ${expected[0]}×${expected[1]}`);
    }
  }
}
