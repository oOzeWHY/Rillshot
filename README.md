<p align="center">
  <img src="apps/rillshot_winui/Assets/BrandLockup.svg" width="520" alt="Rillshot 标志" />
</p>

<p align="center">面向 Windows 的本地长截图工具</p>

<p align="center">
  <a href="../../releases/latest">下载</a> ·
  <a href="#使用方法">使用方法</a> ·
  <a href="#从源码构建">从源码构建</a> ·
  <a href="CONTRIBUTING.md">参与贡献</a>
</p>

# Rillshot

Rillshot 可以把需要滚动查看的页面保存为一张长图。选择截图区域和页面中的滚动位置后，Rillshot 会自动滚动、等待画面稳定，并根据相邻画面的重叠内容完成拼接。截图和处理过程均在本机完成。

> [!NOTE]
> 当前仓库对应 Rillshot 1.1.9。可运行的正式构建仅通过本仓库的 [Releases](../../releases) 页面提供；请勿从非官方来源下载所谓的 Rillshot 安装包。

## 主要功能

- **双向长截图**：支持向下或向上滚动，可用于网页、文档、聊天记录等纵向内容。
- **自由选择范围**：分别选择截图区域和滚动位置，不要求目标应用提供专用接口。
- **多种滚动方式**：支持鼠标滚轮、Page Up/Down、Space/Shift+Space 和方向键。
- **固定页头处理**：可以忽略重复出现的固定页头，使页头在最终图片中只保留一次。
- **自动选择捕获方式**：优先使用高性能捕获；遇到兼容性问题时可切换到**兼容模式**。
- **谨慎处理异常结果**：如果相邻画面无法可靠对齐，Rillshot 会停止继续拼接，并尽可能保留已经完成的部分和用于排查的对照图，避免输出明显错位的长图。
- **本地保存**：支持 PNG 和 BMP；默认不覆盖已有文件，也不会自动上传截图。
- **Windows 桌面体验**：支持浅色、深色和系统主题，适配高 DPI、多显示器和常见屏幕尺寸，并提供可自定义的全局快捷键。

## 获取和运行

Rillshot 首个公开二进制版本将以 x64 Portable 压缩包发布。发布后可按以下步骤使用：

1. 在 [Releases](../../releases/latest) 页面下载最新的 `Rillshot-*-win-x64-portable.zip` 和 `SHA256SUMS.txt`。
2. 可选：在 PowerShell 中运行以下命令，并将结果与 `SHA256SUMS.txt` 对照。

   ```powershell
   Get-FileHash .\Rillshot-*-win-x64-portable.zip -Algorithm SHA256
   ```

3. 将压缩包完整解压到具有写入权限的目录。不要只从压缩包预览窗口直接运行程序。
4. 双击解压目录中的 `Rillshot.cmd`。

Portable 版本会把默认截图、设置和启动日志保存在程序目录的 `app` 子目录中。移动或删除程序目录前，请先备份需要保留的截图。

### 系统要求

- 64 位 Windows 10 1809（内部版本 17763）或更高版本，包括 Windows 11；
- x64 处理器；
- 目标内容能够显示在 Windows 桌面上，并能响应所选的鼠标或键盘滚动方式。

## 使用方法

1. 打开需要截图的页面，并滚动到希望作为长图起点的位置。
2. 在 Rillshot 中选择**截图区域**。尽量只包含需要保存的内容，避免把滚动条、悬浮按钮等无关元素框入区域。
3. 选择**滚动点**。该位置应位于能够响应滚动的正文区域内。
4. 如有需要，在**设置**中调整滚动方向、滚动方式、最多捕获帧数、每次滚动步数和固定页头高度。
5. 选择保存位置，然后点击**开始截图**。也可以在 Rillshot 窗口中按 `Ctrl+Enter`。
6. 截图期间不要移动目标窗口或遮挡截图区域。使用键盘滚动时，请确保目标页面保持焦点。
7. 完成后可直接打开图片、在文件资源管理器中定位文件，或复制文件路径。

默认全局快捷键为 `Ctrl+Alt+S`，用于开始选择截图区域。可以在设置中更改或关闭该快捷键。

## 如果截图提前停止

以下情况可能使 Rillshot 无法继续生成可靠的长图：

- 页面没有继续滚动，或滚动点不在可滚动区域；
- 页面包含持续播放的视频、动画、自动刷新的内容或不断变化的广告；
- 相邻画面的重叠区域太少，或大量重复纹理使对齐位置无法确定；
- 固定页头、悬浮工具栏等元素反复遮挡正文；
- 目标窗口权限高于 Rillshot，或内容受到系统保护。

可以依次尝试以下方法：

1. 将每次滚动步数调小，并重新选择正文内的滚动点。
2. 暂停视频和动画，关闭会自动变化的悬浮内容。
3. 为重复出现的页头设置合适的固定页头高度。
4. 将捕获方式改为**兼容模式**，或改用另一种滚动方式。
5. 缩小截图区域，确保连续两次捕获之间保留足够的相同内容。

发生失败或提前停止时，输出目录中可能同时出现以下文件：

| 文件 | 用途 |
|---|---|
| `*.partial.png` | 截止到停止位置时已经完成的部分 |
| `*.comparison.png` | 最后一次未能通过检查的画面，便于比较问题位置 |
| `*.jsonl` | 捕获过程和停止原因；提交问题前请检查并删除敏感路径或坐标 |

## 隐私与限制

Rillshot 1.1.9 不包含账号系统、广告、云上传、遥测、OCR 或图片编辑器。应用不会主动把截图、设置或诊断信息发送到网络。

截图和诊断文件可能包含个人信息、工作内容或本地路径。分享这些文件或提交 Issue 前，请先检查并脱敏。Rillshot 不适合捕获受数字版权保护的内容、Windows 安全桌面或权限更高的窗口；这些场景可能得到黑屏、空白内容或明确的捕获失败提示。

## 从源码构建

### Windows 桌面应用

构建 WinUI 3 应用需要：

- Visual Studio 2026 18.7 或更高版本；
- **使用 C++ 的桌面开发**、**Windows 应用开发**、CMake 工具和 Windows SDK；
- Node.js 24；
- CMake 4.2 或更高版本（可使用 Visual Studio 随附版本）。

将仓库放在较短的纯 ASCII 路径下，例如 `D:\Dev\Rillshot`。克隆仓库后，在根目录运行：

```bat
build-release.cmd
```

该命令会生成一个未签名的内部 Preview，并运行源码检查、核心测试、WinUI 构建、依赖检查和启动冒烟。输出位于 `artifacts\release`。未签名的 Preview 仅用于本地开发和测试，不应公开分发。

### 平台无关核心

核心算法和测试可在提供 C++23、CMake、Ninja 与 Node.js 的环境中构建：

```bash
node tools/check_project_consistency.mjs
node tools/check_human_docs_language.mjs
cmake --preset core-release
cmake --build --preset core-release
ctest --preset core-release
```

需要运行 AddressSanitizer 和 UndefinedBehaviorSanitizer 时，将 `core-release` 替换为 `core-sanitize`。

## 工程结构

| 路径 | 内容 |
|---|---|
| `src/core`、`src/stitch` | 图像数据、采样指标和长图拼接算法 |
| `src/session` | 捕获流程、稳定判断、停止原因和诊断信息 |
| `src/capture`、`src/input`、`src/output` | Windows 屏幕捕获、滚动输入和图片写入 |
| `src/gui` | 共享界面状态、选择器和全局快捷键 |
| `apps/rillshot_winui` | C++/WinRT WinUI 3 桌面界面 |
| `tests` | 核心算法、输入边界、窗口尺寸和会话行为测试 |
| `tools` | 工程检查、Windows 构建、打包和签名脚本 |

## 参与贡献

欢迎提交缺陷复现、测试、文档修正和范围明确的代码改进。开始前请阅读 [贡献指南](CONTRIBUTING.md) 和 [行为准则](CODE_OF_CONDUCT.md)。

- 缺陷报告或功能建议：使用 [Issue 模板](../../issues/new/choose)。
- 安全漏洞：不要公开披露利用细节，请按照 [安全政策](SECURITY.md) 报告。
- 用户可见的变化：同时更新 [变更记录](CHANGELOG.md)。

## 许可证

除第三方文件或另有标注外，Rillshot 社区版按 [`GPL-3.0-only`](LICENSE) 提供，不附带任何担保。分发二进制时，必须同时履行 GPL 对应源码义务，详见 [对应源码说明](SOURCE_OFFER.md) 和 [第三方通知](THIRD_PARTY_NOTICES.txt)。

需要在不适用 GPL 的专有产品或内部环境中使用 Rillshot，可查看 [商业许可说明](COMMERCIAL-LICENSE.md)。商业许可是版权持有人另行提供的替代授权，不改变社区版的开源许可证。Rillshot 名称和图形标志不随源码许可证一并授权，详见 [商标说明](TRADEMARKS.md)。
