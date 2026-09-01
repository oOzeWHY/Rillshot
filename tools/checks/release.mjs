export function checkRelease(context) {
  const { fs, path, projectRoot, read, requireFile, requireMatch, requireNoMatch, failures } = context;
  const releaseScript = read("tools/build-release.ps1");
  const payloadScript = read("tools/release/PortablePayload.ps1");
  const bundleScript = read("tools/release/ReleaseBundle.ps1");
  const sourceRevisionScript = read("tools/release/SourceRevision.ps1");
  const signingScript = read("tools/release/Signing.ps1");
  const ci = read(".github/workflows/ci.yml");
  const readme = read("README.md");

  [
    "README.md", "SOURCE_REVISION.txt", "CHANGELOG.md", "LICENSE",
    "THIRD_PARTY_NOTICES.txt", "CONTRIBUTING.md", "SECURITY.md",
    "tools/build-release.ps1", "tools/check_powershell_syntax.ps1",
    "tools/check_winui_restore.ps1", "tools/release/PortablePayload.ps1",
    "tools/release/ReleaseBundle.ps1", "tools/release/SourceRevision.ps1",
    "tools/release/Signing.ps1", "tools/test_source_revision.ps1",
    ".github/workflows/ci.yml", ".github/dependabot.yml",
  ].forEach(requireFile);

  requireMatch("README 未声明 GPL-3.0-only", readme, /GPL-3\.0-only/u);
  requireMatch("README 未使用原生 Portable 启动入口", readme, /Rillshot\.exe/u);
  requireNoMatch("README 仍要求 PowerShell/cmd 启动 Portable", readme,
    /Rillshot\.cmd|ExecutionPolicy Bypass/u);

  if ((ci.match(/actions\/checkout@[0-9a-f]{40}/gu) ?? []).length !== 3) {
    failures.push("三个 checkout 步骤必须固定到完整提交 SHA");
  }
  requireMatch("setup-node 未固定到完整提交 SHA", ci,
    /actions\/setup-node@[0-9a-f]{40}/u);
  if ((ci.match(/persist-credentials:\s*false/gu) ?? []).length !== 3) {
    failures.push("三个 checkout 步骤必须禁用持久化凭据");
  }
  requireMatch("Windows CI 未执行实际 WinUI Portable 构建", ci,
    /build-release\.ps1[\s\S]*-Mode Portable[\s\S]*-SkipCoreTests/u);

  requireMatch("发布脚本未执行平台无关测试", releaseScript, /Invoke-Logged \$ctest/u);
  requireMatch("发布脚本未通过独立模块解析源码身份", releaseScript,
    /SourceRevision\.ps1[\s\S]*Resolve-RillshotSourceRevision/u);
  requireMatch("解压源码 Preview 未回退到随包源码身份", sourceRevisionScript,
    /-not \(Test-Path -LiteralPath \$gitMetadataPath\)[\s\S]*ReleaseStage -eq "Stable"[\s\S]*return \$PackagedSourceIdentity/u);
  requireMatch("Git 探测失败仍可能被 ErrorActionPreference=Stop 提升为构建错误",
    sourceRevisionScript,
    /\$ErrorActionPreference = "SilentlyContinue"[\s\S]*2>\$null[\s\S]*\$ErrorActionPreference = \$previousErrorActionPreference/u);
  requireMatch("Stable 未要求可验证的 Git 提交", sourceRevisionScript,
    /Stable releases require a Git repository with at least one commit/u);
  requireMatch("Windows CI 未覆盖源码身份解析测试", ci,
    /test_source_revision\.ps1/u);
  requireMatch("发布脚本未执行 WinUI Restore", releaseScript,
    /\$solutionPath[\s\S]*"\/t:Restore"/u);
  requireMatch("发布脚本未验证 WinUI 构建日志", releaseScript,
    /Assert-MSBuildLogHasNoReportedErrors/u);
  requireMatch("发布脚本未暂存原生 Rillshot.exe 启动器", releaseScript,
    /launcherExecutable[\s\S]*Join-Path \$portableRoot "Rillshot\.exe"/u);
  requireMatch("Portable 布局未要求原生启动器", bundleScript,
    /"Rillshot\.exe"[\s\S]*"app\\Rillshot\.WinUI\.exe"/u);
  requireNoMatch("发布流程仍依赖 Rillshot.cmd", releaseScript + bundleScript,
    /Rillshot\.cmd/u);
  requireMatch("Portable 根目录启动器和应用 EXE 未统一签名", releaseScript,
    /foreach \(\$portableExecutable in @\([\s\S]*Rillshot\.exe[\s\S]*Rillshot\.WinUI\.exe[\s\S]*Invoke-RillshotSignAndVerify/u);
  requireMatch("签名未固定 SHA-256 与 RFC 3161 时间戳", signingScript,
    /"\/fd", "SHA256"[\s\S]*"\/tr"[\s\S]*"\/td", "SHA256"/u);
  requireMatch("签名后未执行 Authenticode 验证", signingScript,
    /"verify", "\/pa", "\/all", "\/v"/u);

  requireMatch("Portable 复制未核对文件路径、数量和大小", payloadScript,
    /sourceFiles\.Count -ne \$destinationFiles\.Count[\s\S]*sourceFile\.Length -ne \$destinationFile\.Length/u);
  requireNoMatch("同一磁盘上的 Portable 复制仍重复计算 SHA-256", payloadScript,
    /Get-FileHash/u);
  requireMatch("发布日志未提供本地归档摘要供上传后比对", releaseScript,
    /Local archive SHA-256/u);
  requireMatch("Stable Direct 未生成单一最终制品校验清单", bundleScript,
    /function Write-StableDirectChecksumManifest[\s\S]*\$ReleaseStage -ne "Stable" -or \$DistributionChannel -ne "Direct"[\s\S]*"SHA256SUMS\.txt"/u);
  requireMatch("校验清单未限制为公开可分发制品", bundleScript,
    /\$packageExtensions = @\("\.zip", "\.msix", "\.appx", "\.msixbundle", "\.appxbundle"\)/u);
  requireMatch("校验清单没有在所有打包完成后生成", releaseScript,
    /switch \(\$Mode\)[\s\S]*Write-StableDirectChecksumManifest[\s\S]*Build pipeline completed successfully/u);

  const reviewLines = read("THIRD_PARTY_NOTICES.txt").split(/\r?\n/u)
    .filter((line) => line.startsWith("Binary payload review:"));
  if (reviewLines.length !== 1 ||
      !/^Binary payload review: (?:PENDING|COMPLETE)$/u.test(reviewLines[0])) {
    failures.push("第三方通知必须包含且仅包含一个有效的二进制载荷复核状态");
  }

  if (fs.existsSync(path.join(projectRoot, "prompt-engineering"))) {
    failures.push("AI 提示词工程应与可编译源码分包");
  }
  if (fs.existsSync(path.join(projectRoot, "artifacts"))) {
    failures.push("交付源码不应包含本机构建 artifacts 目录");
  }
}
