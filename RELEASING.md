# 发布 Rillshot 1.2.0

本文说明 Rillshot 1.2.0 的 GitHub Release 流程。当前流程不要求代码签名，也不生成或上传 SHA256 文件。

## 1. 检查仓库

在源码根目录打开 PowerShell：

```powershell
git switch main
git pull --ff-only origin main
git status --short
```

最后一条命令应没有输出。公开仓库会公开完整 Git 历史；确认其中没有密码、访问令牌、私钥、个人文件、截图或构建产物。如果凭据曾经提交过，应先撤销凭据并清理历史。

确认 1.2.0 的代码和文档已经提交并推送：

```powershell
git add -A
git diff --cached --check
git diff --cached
git commit -m "Prepare Rillshot 1.2.0"
git push origin main
```

如果没有待提交内容，可直接继续。等待仓库 `Actions` 页的 CI 通过。

## 2. 构建 Stable Portable 包

```powershell
./tools/build-release.ps1 `
  -Mode Portable `
  -Platform x64 `
  -Configuration Release `
  -ReleaseStage Stable `
  -Clean `
  -SmokeTest
```

脚本运行核心测试，构建 CLI 和 WinUI，检查 CLI 版本并执行 WinUI 启动测试。成功后生成：

```text
artifacts\release\Rillshot-1.2.0-win-x64-portable.zip
```

将 ZIP 解压到新的可写目录，并完成以下检查：

1. 运行根目录的 `Rillshot.exe`，完成一次普通长截图。
2. 运行 `./rillshot-cli.exe --version`，确认输出为 `Rillshot 1.2.0`。
3. 使用 `rillshot-cli.exe capture ... --json` 完成一次截图，确认标准输出为单个 JSON 对象。
4. 确认默认情况下不会覆盖已有输出。

## 3. 创建标签

确认当前提交就是构建所用提交：

```powershell
git status --short
git log -1 --oneline
```

工作树应为空。随后创建并推送标签：

```powershell
git tag -a v1.2.0 -m "Rillshot 1.2.0"
git push origin v1.2.0
```

如果标签已经存在，不要强制覆盖。需要修正已发布版本时，应发布新的补丁版本。

## 4. 建立 Release 草稿

1. 打开 GitHub 仓库的 `Releases` 页面，选择 `Draft a new release`。
2. 选择标签 `v1.2.0`。
3. 将标题设为 `Rillshot 1.2.0`。
4. 上传 `artifacts\release\Rillshot-1.2.0-win-x64-portable.zip`。
5. 不上传构建目录、日志、缓存、单独的源码 ZIP 或 SHA256 文件。GitHub 会按标签提供 Source code 归档。
6. 保存草稿，不要立即发布。

发行说明应只列出已经实现并验证的用户可见变化。例如：

```markdown
Rillshot 1.2.0 是适用于 Windows 11 x64 的长截图工具。

主要变化：
- 提供图形界面和命令行接口；
- 支持向上和向下长截图；
- 支持滚轮、翻页键、空格键和方向键滚动；
- 提供 PNG/BMP 输出、固定页头处理和 DXGI/GDI 捕获；
- 优化主页面与设置页的切换动画。

下载 Portable ZIP 并完整解压。图形界面运行 Rillshot.exe；命令行调用 rillshot-cli.exe。
```

## 5. 公开仓库

首个公开版本应在 Release 草稿准备完成后再修改仓库可见性：

1. 打开仓库 `Settings` 的 `General` 页面。
2. 在 `Danger Zone` 中选择 `Change repository visibility`。
3. 选择 `Make public` 并按提示确认。

如果没有该选项，需要仓库所有者或组织管理员处理。仓库公开后，所有提交、公开 Issue 和标签均可见。GitHub Actions Secrets 不会直接公开，但 Git 历史中的凭据会公开。

## 6. 发布并复查

1. 重新打开 Release 草稿。
2. 确认标签为 `v1.2.0`，附件为正确的 Portable ZIP，说明与实际功能一致。
3. Stable 版本不勾选 `Set as a pre-release`。
4. 选择 `Publish release`。
5. 从公开 Release 页面重新下载 ZIP，在新目录中复查图形界面和 CLI。

不要替换已发布标签下的附件。后续修复应更新版本号，通过 CI，再创建新的标签和 Release。
