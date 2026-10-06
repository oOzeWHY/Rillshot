# 发布 Rillshot 1.2.1

本文说明 Rillshot 1.2.1 的 GitHub Release 流程。当前流程不要求代码签名，也不生成或上传 SHA256 文件。

## 1. 检查仓库

在源码根目录打开 PowerShell：

```powershell
git switch main
git pull --ff-only origin main
git status --short
```

最后一条命令应没有输出。公开仓库会公开完整 Git 历史；确认其中没有密码、访问令牌、私钥、个人文件、截图或构建产物。如果凭据曾经提交过，应先撤销凭据并清理历史。

确认 1.2.1 的代码和文档已经提交并推送：

```powershell
git add -A
git diff --cached --check
git diff --cached
git commit -m "Prepare Rillshot 1.2.1"
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
artifacts\release\Rillshot-1.2.1-win-x64-portable.zip
```

将 ZIP 解压到新的可写目录，并完成以下检查：

1. 运行根目录的 `Rillshot.exe`，完成一次普通长截图。
2. 运行 `./rillshot-cli.exe --version`，确认输出为 `Rillshot 1.2.1`。
3. 使用 `rillshot-cli.exe capture ... --json` 完成一次截图，确认标准输出为单个 JSON 对象。
4. 确认默认情况下不会覆盖已有输出。
5. 分别完成向下、向上、固定页头、固定页脚和页面到底五类截图，检查接缝、首尾固定区域及停止原因。
6. 检查 JSONL 中包含捕获、稳定、匹配、组装、恢复编码、最终编码和会话总耗时；长会话的恢复检查点不应逐接缝编码。

## 3. 创建标签

确认当前提交就是构建所用提交：

```powershell
git status --short
git log -1 --oneline
```

工作树应为空。随后创建并推送标签：

```powershell
git tag -a v1.2.1 -m "Rillshot 1.2.1"
git push origin v1.2.1
```

如果标签已经存在，不要强制覆盖。需要修正已发布版本时，应发布新的补丁版本。

## 4. 建立 Release 草稿

1. 打开 GitHub 仓库的 `Releases` 页面，选择 `Draft a new release`。
2. 选择标签 `v1.2.1`。
3. 将标题设为 `Rillshot 1.2.1`。
4. 上传 `artifacts\release\Rillshot-1.2.1-win-x64-portable.zip`。
5. 不上传构建目录、日志、缓存、单独的源码 ZIP 或 SHA256 文件。GitHub 会按标签提供 Source code 归档。
6. 保存草稿，不要立即发布。

发行说明应只列出已经实现并验证的用户可见变化。例如：

```markdown
Rillshot 1.2.1 是适用于 Windows 11 x64 的长截图工具。

主要变化：
- DXGI 只复制截图选区，减少不需要的桌面纹理传输；
- 接缝粗筛缓存亮度采样，并加速页面没有继续滚动时的判断；
- 向上截图使用分块组装，避免反复移动已经拼接的像素；
- 降低恢复检查点和诊断刷盘对长截图热路径的影响；
- 增加分阶段耗时诊断，便于定位不同设备上的瓶颈。

下载 Portable ZIP 并完整解压。图形界面运行 Rillshot.exe；命令行调用 rillshot-cli.exe。
```

## 5. 性能与回归门禁

在同一台 Windows 11 设备、相同显示缩放和相同页面上，对 1.2.0 与 1.2.1 各运行至少 5 次：

1. 使用相同区域和滚动参数，记录成功率、停止原因、总耗时及各阶段耗时中位数。
2. 逐像素或以明确的接缝检查确认 1.2.1 没有新增重复行、缺行、固定区域重复或顺序错误。
3. DXGI 与 GDI 各完成至少一次；DXGI 失败回退仍应保存明确诊断。
4. 页面到底或滚动未生效时，应正常停止且不新增正文。
5. Windows/MSVC 构建、上述正确性检查或输出所有权策略任一失败，均不得发布 Stable。

Linux 合成微基准只用于比较接缝实现，不可替代这一门禁，也不可单独作为对外速度主张。

## 6. 发布并复查

1. 重新打开 Release 草稿。
2. 确认标签为 `v1.2.1`，附件为正确的 Portable ZIP，说明与实际功能一致。
3. Stable 版本不勾选 `Set as a pre-release`。
4. 选择 `Publish release`。
5. 从公开 Release 页面重新下载 ZIP，在新目录中复查图形界面和 CLI。

不要替换已发布标签下的附件。后续修复应更新版本号，通过 CI，再创建新的标签和 Release。

## 2026-09-17 增量验收

本轮状态、性能口径和复验命令见 [审查报告](PERFORMANCE-REVIEW-ZH.md)。Windows CMake 测试包括模拟后端 `stabilization_test`，它不代表真实捕获或输入验证。

- 核对正常稳定需三帧、运动重置计数、等待耗尽后不再调用后端、捕获中取消优先于稳定提交。
- 对比滚轮/键盘的 `scrollElapsedMs` 和会话总时间；检查 120/50 ms 默认值及两次稳定比较未降低。
- 平滑滚动、延迟响应和到底场景须检查是否过早判断无滚动；基线与候选各至少五次，保留逐会话数据再计算 P95。
- 本轮 WinUI Build 的 MIDL FileTracker 出现 E_ACCESSDENIED；在正常构建环境重新完成 Release/Portable 和对应真实页面矩阵之前，不发布 Stable。
