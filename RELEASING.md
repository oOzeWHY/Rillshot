# 发布 Rillshot 1.1.9

本文说明如何把当前私有仓库公开，并发布第一个 GitHub Release。当前流程不要求代码签名，也不生成或上传 SHA256 文件。

## 一、发布前准备

在 `C:\Users\msi\Desktop\Rillshot` 打开 PowerShell：

```powershell
Set-Location C:\Users\msi\Desktop\Rillshot
git switch main
git pull --ff-only origin main
git status --short
```

最后一条命令应没有输出。公开仓库会公开全部 Git 历史；确认历史中没有密码、访问令牌、私钥、个人文件、截图或构建产物。如果凭据曾经提交过，仅删除当前文件还不够，应先撤销该凭据并清理历史。

确认本轮修改已经提交并推送：

```powershell
git add -A
git diff --cached --check
git diff --cached
git commit -m "Simplify the Rillshot 1.1.9 release workflow"
git push origin main
```

如果 `git commit` 提示没有可提交内容，说明修改已经提交，可继续。等待仓库 `Actions` 页的 CI 通过。

## 二、构建正式 Portable 包

在源码根目录运行：

```powershell
.\tools\build-release.ps1 `
  -Mode Portable `
  -Platform x64 `
  -Configuration Release `
  -ReleaseStage Stable `
  -Clean `
  -SmokeTest
```

脚本会运行核心测试、Restore/Build WinUI、生成 Portable 包，并做一次启动冒烟。成功后应得到：

```text
artifacts\release\Rillshot-1.1.9-win-x64-portable.zip
```

把该 ZIP 解压到一个新的可写目录，双击根目录的 `Rillshot.exe`，至少完成一次普通长截图。确认图片能够保存，程序可以正常退出。

## 三、创建版本标签

确认当前提交就是刚才构建的提交：

```powershell
git status --short
git log -1 --oneline
```

工作树应为空。随后创建并推送标签：

```powershell
git tag -a v1.1.9 -m "Rillshot 1.1.9"
git push origin v1.1.9
```

如果 Git 提示 `v1.1.9` 已存在，不要强制覆盖。先到 GitHub 确认现有标签是否正确；需要修正已公开版本时，应发布新的补丁版本。

## 四、建立 Release 草稿

1. 打开 GitHub 仓库首页。
2. 在右侧选择 `Releases`，再选择 `Draft a new release`。
3. 在 `Choose a tag` 中选择 `v1.1.9`。
4. `Release title` 填写 `Rillshot 1.1.9`。
5. 将 `artifacts\release\Rillshot-1.1.9-win-x64-portable.zip` 拖到附件区域。
6. 不上传 `artifacts` 目录、日志、NuGet 缓存、单独的源码 ZIP 或 SHA256 文件。GitHub 会根据标签自动提供 Source code 归档。
7. 暂时选择 `Save draft`，不要立即发布。

发行说明可以使用下面的简短格式，并按实际变化调整：

```markdown
Rillshot 1.1.9 是首个公开版本。

主要内容：
- 支持向上和向下长截图；
- 支持滚轮、Page、Space 和方向键滚动；
- 提供 PNG/BMP 输出、固定页头和 DXGI/GDI 捕获；
- 提供 Windows 11 x64 Portable 版本。

使用方法：下载 Portable ZIP，完整解压后运行根目录的 Rillshot.exe。
```

## 五、将仓库改为公开

建议在 Release 草稿准备完成后再改可见性：

1. 打开仓库的 `Settings`。
2. 在 `General` 页面滚动到 `Danger Zone`。
3. 选择 `Change repository visibility`。
4. 选择 `Make public`，按 GitHub 提示输入仓库名称并确认。

如果看不到该选项，通常是当前账号没有仓库管理员权限，或组织策略禁止成员修改可见性；此时需要仓库所有者或组织管理员处理。

仓库公开后，任何人都可以查看和克隆代码，也能看到公开的 Issue、提交和标签。GitHub Actions 中保存的 Secrets 不会因为仓库公开而直接显示，但曾经提交进 Git 历史的凭据会被公开。

## 六、发布 Release

1. 回到仓库的 `Releases` 页面并打开刚才的草稿。
2. 再次确认标签为 `v1.1.9`、附件只有正确的 Portable ZIP、说明与实际功能一致。
3. 不勾选 `Set as a pre-release`。
4. 选择 `Publish release`。
5. 从公开 Release 页面重新下载 ZIP，在新的目录解压并启动一次。

至此，仓库和 Release 都已公开。以后修复问题时更新版本号，提交并通过 CI，再按相同步骤创建新标签和新 Release；不要替换已发布标签下的附件。

## 日常开发命令

后续修改建议使用短分支：

```powershell
git switch main
git pull --ff-only origin main
git switch -c fix/<简短名称>

# 修改并完成相关构建或测试后
git add -A
git diff --cached
git commit -m "Describe the change"
git push -u origin fix/<简短名称>
```

然后在 GitHub 创建 Pull Request，等待 CI 通过后合并。只有改动实际涉及发布内容时，才再次执行完整 Stable 构建与 Release 流程。
