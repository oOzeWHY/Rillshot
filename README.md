<p align="center">
  <img src="apps/rillshot_winui/Assets/BrandLockup.svg" width="460" alt="Rillshot 标志" />
</p>

<p align="center">适用于 Windows 的本地长截图工具</p>

<p align="center">
  <a href="../../releases/latest">下载</a> ·
  <a href="#使用方法">使用方法</a> ·
  <a href="#从源码构建">从源码构建</a> ·
  <a href="CONTRIBUTING.md">参与贡献</a>
</p>

# Rillshot

Rillshot 用于把需要滚动查看的页面保存为一张长图。用户选择截图区域和滚动位置后，程序自动滚动页面、等待画面稳定，并根据相邻画面的重叠内容完成拼接。截图、设置和诊断信息均保存在本机。

## 功能

- 支持向下和向上长截图；
- 支持鼠标滚轮、Page Up/Down、Space/Shift+Space 和方向键；
- 支持 PNG、BMP 和固定页头处理；
- 自动使用 DXGI 捕获，并提供 GDI 兼容模式；
- 接缝置信度不足时停止拼接，并保留可用结果和诊断信息；
- 默认不覆盖已有文件，不上传截图，不收集遥测；
- 提供浅色、深色、系统主题和可配置的全局快捷键。

## 系统要求

- 64 位 Windows 11；
- x64 处理器；
- 目标内容能够在 Windows 桌面显示，并能响应所选滚动方式。

Windows App SDK 的技术兼容范围不等同于 Rillshot 的测试范围。其他 Windows 版本需完成真机验证后再列入支持范围。

## 安装

1. 从 [Releases](../../releases/latest) 下载 x64 Portable 压缩包。
2. 将压缩包完整解压到具有写入权限的目录。
3. 运行根目录中的 `Rillshot.exe`。

Portable 版本将截图、设置和启动日志保存在解压目录的 `app` 子目录中。移动或删除程序前，请先备份需要保留的截图。

## 使用方法

1. 打开目标页面，并滚动到长图起点。
2. 在 Rillshot 中选择截图区域。
3. 选择正文中的滚动位置。
4. 按需调整滚动方向、滚动方式、捕获帧数、滚动步数和固定页头高度。
5. 选择保存位置，点击“开始截图”或按 `Ctrl+Enter`。
6. 截图完成后，可打开图片、定位文件或复制文件路径。

默认全局快捷键为 `Ctrl+Alt+S`，用于开始选择截图区域。截图期间不要移动目标窗口或遮挡截图区域；使用键盘滚动时，应保持目标页面处于前台。

## 常见问题

截图提前停止通常由以下原因引起：页面没有继续滚动、滚动点不在正文区域、视频或广告持续变化、重叠内容不足、固定元素遮挡正文，或目标窗口的权限高于 Rillshot。

可以依次尝试：减小滚动步数、重新选择滚动点、暂停动态内容、设置固定页头高度、切换兼容模式，或缩小截图区域。

输出目录可能包含以下辅助文件：

| 文件 | 说明 |
|---|---|
| `*.partial.png` | 停止前已完成的部分 |
| `*.comparison.png` | 最后一个未通过接缝检查的画面 |
| `*.jsonl` | 捕获过程、参数和停止原因 |

诊断文件可能包含本地路径或页面内容。提交问题前请先检查文件内容。

## 从源码构建

### 环境

- Visual Studio 2026 18.7 或更高版本；
- “使用 C++ 的桌面开发”和“Windows 应用开发”工作负载；
- Windows SDK、MSBuild C++ 工具和 CMake；
- Node.js 24；
- CMake 4.2 或更高版本。

将源码放在较短的纯 ASCII 路径，例如 `C:\Users\msi\Desktop\Rillshot`。在源码根目录运行：

```bat
build-release.cmd
```

命令会执行源码检查、核心测试、WinUI 构建、依赖检查和启动冒烟，并在 `artifacts\release` 生成未签名的 Preview Portable 包。源码 ZIP 不含 `.git` 时也可以构建 Preview；Stable 构建要求有提交且工作树干净。

只运行平台无关测试时，可在 Developer PowerShell for Visual Studio 中执行：

```powershell
node .\tools\check_project_consistency.mjs
node .\tools\check_human_docs_language.mjs
cmake -S . -B out\build\core-msvc -A x64 `
  -DRILLSHOT_BUILD_TESTS=ON `
  -DRILLSHOT_BUILD_WINDOWS_APPS=OFF `
  -DRILLSHOT_WARNINGS_AS_ERRORS=ON
cmake --build out\build\core-msvc --config Release --parallel
ctest --test-dir out\build\core-msvc -C Release --output-on-failure
```

## 工程结构

| 路径 | 内容 |
|---|---|
| `src/core`、`src/stitch` | 图像数据、指标和拼接算法 |
| `src/session` | 捕获流程、停止语义和诊断 |
| `src/capture`、`src/input`、`src/output` | Windows 捕获、输入和图片写入 |
| `src/gui` | 共享界面状态和 Windows 交互 |
| `apps/rillshot_winui` | C++/WinRT WinUI 3 前端 |
| `apps/rillshot_launcher` | Portable 原生启动器 |
| `tests` | 核心算法与行为测试 |
| `tools` | 检查、构建、打包和签名脚本 |

## 参与贡献

缺陷报告、测试、文档修正和范围明确的代码改进均可提交。开始前请阅读 [贡献指南](CONTRIBUTING.md) 和 [行为准则](CODE_OF_CONDUCT.md)。安全问题请按 [安全政策](SECURITY.md) 报告。

用户可见的变化应同步更新 [变更记录](CHANGELOG.md)。

## 许可证

除第三方文件或另有标注外，Rillshot 社区版采用 [`GPL-3.0-only`](LICENSE) 许可证。分发二进制时，还应遵守 [对应源码说明](SOURCE_OFFER.md) 和 [第三方通知](THIRD_PARTY_NOTICES.txt)。商业授权信息见 [商业许可说明](COMMERCIAL-LICENSE.md)，名称和图形标志的使用规则见 [商标说明](TRADEMARKS.md)。
