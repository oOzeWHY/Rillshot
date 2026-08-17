export function checkRelease(context) {
  const {
    fs, path, projectRoot, read, requireFile, requireMatch,
    requireNoMatch, checkLineBudget, failures,
  } = context;
  const releaseScript = read("tools/build-release.ps1");
  const portablePayloadScript = read("tools/release/PortablePayload.ps1");
  const releaseBundleScript = read("tools/release/ReleaseBundle.ps1");
  const signingScript = read("tools/release/Signing.ps1");
  const buildToolDiscovery = read("tools/release/BuildToolDiscovery.ps1");
  const powershellSyntaxCheck = read("tools/check_powershell_syntax.ps1");
  const restoreCheckScript = read("tools/check_winui_restore.ps1");
  const releaseCommand = read("build-release.cmd");
  const rootReadme = read("README.md");
  const sourceIdentity = read("SOURCE_REVISION.txt");
  const ciWorkflow = read(".github/workflows/ci.yml");
  const license = read("LICENSE");
  const thirdPartyNotices = read("THIRD_PARTY_NOTICES.txt");
  const bugReportForm = read(".github/ISSUE_TEMPLATE/bug_report.yml");
  const commercialLicenseForm = read(".github/ISSUE_TEMPLATE/commercial_license.yml");
  const publicChinese = [
    rootReadme,
    read("CONTRIBUTING.md"),
    read("SECURITY.md"),
    read("COMMERCIAL-LICENSE.md"),
    bugReportForm,
    commercialLicenseForm,
    read("tools/release/FEEDBACK-TEMPLATE-ZH.md"),
    read("tools/release/README-FIRST-PREVIEW-ZH.txt"),
    read("tools/release/README-FIRST-STABLE-ZH.txt"),
  ].join("\n");

  for (const file of [
    "README.md",
    "SOURCE_REVISION.txt",
    "tools/release/PortablePayload.ps1",
    "tools/release/ReleaseBundle.ps1",
    "tools/release/Signing.ps1",
    "tools/release/BuildToolDiscovery.ps1",
    "tools/check_powershell_syntax.ps1",
    "tools/check_winui_restore.ps1",
    "tools/release/README-FIRST-PREVIEW-ZH.txt",
    "tools/release/README-FIRST-STABLE-ZH.txt",
    "tools/release/FEEDBACK-TEMPLATE-ZH.md",
    "CHANGELOG.md",
    "LICENSE",
    "COMMERCIAL-LICENSE.md",
    "COPYRIGHT.md",
    "SOURCE_OFFER.md",
    "CONTRIBUTING.md",
    "SECURITY.md",
    "TRADEMARKS.md",
    "CODE_OF_CONDUCT.md",
    "THIRD_PARTY_NOTICES.txt",
    "CMakePresets.json",
    ".github/workflows/ci.yml",
    ".github/dependabot.yml",
    ".github/pull_request_template.md",
    ".github/ISSUE_TEMPLATE/bug_report.yml",
    ".github/ISSUE_TEMPLATE/feature_request.yml",
    ".github/ISSUE_TEMPLATE/commercial_license.yml",
    ".github/ISSUE_TEMPLATE/config.yml",
  ]) requireFile(file);

  requireMatch("README 未声明 Rillshot 1.1.9", rootReadme,
    /当前仓库对应 Rillshot 1\.1\.9/u);
  requireMatch("README 未声明 GPL-3.0-only", rootReadme,
    /GPL-3\.0-only/u);
  requireNoMatch("公开 README 不得链接维护者私有 docs 目录", rootReadme,
    /\]\((?:\.\/)?docs\//u);
  requireNoMatch("公开 README 仍含不自然的接缝审查措辞", rootReadme,
    /静默接受可疑接缝/u);
  requireMatch("公开 README 未用普通用户能理解的方式说明拼接拒绝行为", rootReadme,
    /无法可靠对齐[\s\S]*?避免输出明显错位的长图/u);
  requireMatch("README 未提供发布前后都成立的正式构建入口", rootReadme,
    /可运行的正式构建仅通过本仓库的 \[Releases\]/u);
  requireNoMatch("README 仍把不存在的一般问题表单写入反馈入口", rootReadme,
    /一般问题或功能建议/u);
  requireMatch("README 未准确指向缺陷与功能建议表单", rootReadme,
    /缺陷报告或功能建议[：:]使用 \[Issue 模板\]/u);
  requireMatch("缺陷报告表单的版本示例不是 1.1.9", bugReportForm,
    /placeholder:\s*1\.1\.9/u);
  requireNoMatch("公开中文仍含抽象或翻译腔式的信息处理指令", publicChinese,
    /脱敏截图|已脱敏证据|非机密(?:的)?(?:需求)?概要|后续私密联系渠道|按 SECURITY\.md 私密报告/u);
  requireMatch("缺陷报告表单未具体说明附件公开前应删除的内容", bugReportForm,
    /删除账号、工作内容、本地路径/u);
  requireMatch("商业授权表单未明确说明 Issue 公开范围和填写边界", commercialLicenseForm,
    /内容会公开显示[\s\S]*?预算、客户名称、未公开代码/u);
  requireMatch("安全政策未给出 GitHub 私密漏洞报告的实际入口", read("SECURITY.md"),
    /Security → Advisories[\s\S]*?Report a vulnerability[\s\S]*?只有报告者和仓库维护者能够查看/u);
  requireMatch("LICENSE 不是完整 GNU GPL 第 3 版文本", license,
    /GNU GENERAL PUBLIC LICENSE[\s\S]*?Version 3, 29 June 2007[\s\S]*?END OF TERMS AND CONDITIONS/u);
  requireNoMatch("LICENSE 仍包含 GNU Affero GPL 文本", license,
    /GNU AFFERO GENERAL PUBLIC LICENSE/u);
  const checkoutCredentialGuards = ciWorkflow.match(/persist-credentials:\s*false/gu) ?? [];
  if (checkoutCredentialGuards.length !== 3) {
    failures.push("三个 CI checkout 步骤必须全部禁用持久化写凭据");
  }
  requireMatch("商业授权文档未区分 GPL 社区版和独立书面协议",
    read("COMMERCIAL-LICENSE.md"),
    /GPL-3\.0-only[\s\S]*?单独的书面协议/u);
  requireMatch("版权说明未准确区分集合称呼、实际权利人和商业再许可权",
    read("COPYRIGHT.md"), /集合性称呼[\s\S]*?商业再许可权/u);
  requireMatch("对应源码说明未覆盖同一 tag、等价访问与长期有效性",
    read("SOURCE_OFFER.md"), /同一 GitHub Release[\s\S]*?tag[\s\S]*?持续有效/u);
  requireMatch("贡献指南未防止未授权代码破坏商业再许可权利链",
    read("CONTRIBUTING.md"), /CLA 未提供或未完成[\s\S]*?不会合并/u);
  const payloadReviewLines = thirdPartyNotices.split(/\r?\n/u)
    .filter((line) => line.startsWith("Binary payload review:"));
  if (payloadReviewLines.length !== 1 ||
      !/^Binary payload review: (?:PENDING|COMPLETE)$/u.test(payloadReviewLines[0])) {
    failures.push(
      "第三方通知必须且只能包含一个二进制载荷复核状态：PENDING 或 COMPLETE",
    );
  }
  if (fs.existsSync(path.join(projectRoot, "LICENSE-SELECTION-REQUIRED.md"))) {
    failures.push("已选定许可证后不应保留 LICENSE-SELECTION-REQUIRED.md");
  }
  requireMatch("SOURCE_REVISION 未声明当前源码身份", sourceIdentity,
    /Source identity: 1\.1\.9-source-ready/u);
  requireNoMatch("当前源码身份仍使用旧的 release-candidate-rN 格式",
    sourceIdentity, /release-candidate-r\d+/u);
  requireNoMatch("README 仍把 0.x 迭代当作当前说明", rootReadme,
    /当前版本[^\n]*0\.7|提示词[^\n]*v0\.9/u);
  requireMatch("发布脚本未启用严格核心构建", releaseScript,
    /-DRILLSHOT_WARNINGS_AS_ERRORS=ON/u);
  requireMatch("发布脚本未生成 SHA256SUMS", releaseScript,
    /SHA256SUMS\.txt/u);
  requireMatch("发布哈希未覆盖 NuGet 依赖清单", releaseScript,
    /nuget-packages-\*\.txt/u);
  requireMatch("发布脚本未在加载模块前执行全树 PowerShell 语法检查", releaseScript,
    /check_powershell_syntax\.ps1[\s\S]*?SourceRoot \$projectRoot[\s\S]*?release\\BuildToolDiscovery\.ps1/u);
  requireMatch("PowerShell 语法门禁未使用官方解析器逐文件检查", powershellSyntaxCheck,
    /System\.Management\.Automation\.Language\.Parser[\s\S]*?ParseFile/u);
  requireMatch("PowerShell 语法门禁会扫描构建缓存或第三方生成脚本", powershellSyntaxCheck,
    /topLevelName[\s\S]*?"artifacts"[\s\S]*?"out"[\s\S]*?Generated Files/u);
  requireMatch("Windows CI 未执行 PowerShell 语法门禁", ciWorkflow,
    /windows-native:[\s\S]*?check_powershell_syntax\.ps1/u);
  const powershellFiles = context.walk(projectRoot, (file) => /\.ps1$/iu.test(file));
  const ambiguousVariableBeforeColon =
    /\$(?!env:|script:|global:|local:|private:|using:)[A-Za-z_][A-Za-z0-9_]*:/u;
  for (const file of powershellFiles) {
    const content = fs.readFileSync(file, "utf8");
    if (ambiguousVariableBeforeColon.test(content)) {
      failures.push(`PowerShell 可展开字符串可能含未分隔的变量冒号：${path.relative(projectRoot, file)}`);
    }
  }
  requireMatch("构建工具发现没有优先使用所选 Visual Studio 的 MSBuild", buildToolDiscovery,
    /Join-Path \$VisualStudioRoot "MSBuild[\s\S]*?Test-Path[\s\S]*?Get-Command msbuild\.exe/u);
  requireMatch("构建工具发现没有优先使用所选 Visual Studio 的 CMake", buildToolDiscovery,
    /Join-Path \$VisualStudioRoot[\s\S]*?Microsoft\\CMake\\CMake[\s\S]*?Test-Path[\s\S]*?Get-Command cmake\.exe/u);
  requireMatch("发布脚本未在配置前验证 CMake 生成器能力", releaseScript,
    /Assert-CMakeGeneratorAvailable \$cmake \$CMakeGenerator[\s\S]*?Invoke-Logged \$cmake/u);
  requireMatch("CMake 生成器预检未读取 capabilities", buildToolDiscovery,
    /-E capabilities[\s\S]*?generators[\s\S]*?Generator/u);
  requireMatch("发布日志未记录所选 Visual Studio 与 MSBuild 身份", releaseScript,
    /Visual Studio: \$visualStudioRoot[\s\S]*?MSBuild: \$msbuild \(\$msbuildVersion\)/u);
  requireMatch("发布日志未记录 CMake 和 SignTool 路径/版本", buildToolDiscovery,
    /CMake: \$CMakePath[\s\S]*?SignTool: \$\(\$candidate\.FullName\)/u);
  requireMatch("SignTool 发现仍未按 SDK 版本对象排序", buildToolDiscovery,
    /\[version\]::TryParse[\s\S]*?Descending = \$true/u);
  requireNoMatch("SignTool 发现仍只按路径字符串选择 SDK", buildToolDiscovery,
    /Sort-Object FullName -Descending/u);
  requireMatch("发布脚本未验证 Portable 运行时载荷", releaseScript,
    /Assert-PortableRuntimePayload/u);
  requireMatch("发布脚本未加载可核验的 Portable 复制模块", releaseScript,
    /release\\PortablePayload\.ps1/u);
  requireMatch("发布脚本没有输出可辨认的源码身份", releaseScript,
    /\$sourceRevision\s*=\s*"1\.1\.9-source-ready"[\s\S]*Source identity: \$sourceRevision/u);
  requireMatch("Restore 未把关键 NuGet 解析警告提升为错误", releaseScript,
    /warnaserror:NU1603;NU1605;NU1608/u);
  requireMatch("NuGet 资产门禁未拒绝预发布包", restoreCheckScript,
    /prereleaseLibraries[\s\S]*NuGet 资产图混入预发布包/u);
  requireMatch("NuGet 资产门禁未拒绝组件多版本污染", restoreCheckScript,
    /duplicateComponentFamilies[\s\S]*同一 Windows App SDK 组件的多个版本/u);
  requireMatch("发布脚本未使用逐文件核验的 Portable 暂存", releaseScript,
    /Copy-PortablePayloadVerified/u);
  requireNoMatch("发布脚本仍使用无法逐文件核验的 bin 通配符复制", releaseScript,
    /Copy-Item\s+-Path\s+\(Join-Path\s+\$binRoot\s+"\*"\)/u);
  requireMatch("Portable 复制模块未枚举包含隐藏文件的完整载荷",
    portablePayloadScript, /Get-ChildItem[^\n]*-LiteralPath[^\n]*-Recurse[^\n]*-File[^\n]*-Force/su);
  requireMatch("Portable 复制模块未核对源/目标文件数量", portablePayloadScript,
    /\$sourceFiles\.Count\s+-ne\s+\$destinationFiles\.Count/u);
  requireMatch("Portable 复制模块未核对关键文件 SHA-256", portablePayloadScript,
    /Get-FileHash[^\n]*SHA256/u);
  requireMatch("Portable 复制模块未在复制前排除开发文件", portablePayloadScript,
    /PortablePayloadExcludedExtensions[\s\S]*-notcontains/u);
  requireNoMatch("发布脚本仍在完整性核验后从 Portable 目录批量删除文件", releaseScript,
    /Get-ChildItem[^\n]*\$portableRoot[\s\S]{0,240}Remove-Item/u);
  requireMatch("Portable 复制模块未验证 Windows App Runtime", portablePayloadScript,
    /Microsoft\.WindowsAppRuntime\.dll/u);
  requireMatch("Portable 复制模块未排除运行生成的数据目录", portablePayloadScript,
    /PortablePayloadExcludedTopLevelDirectories[\s\S]*"captures"[\s\S]*"logs"[\s\S]*"settings"/u);
  requireMatch("Portable 复制模块未声明可选工作负载拦截规则", portablePayloadScript,
    /PortablePayloadForbiddenOptionalPattern/u);
  for (const marker of ["DirectML", "onnxruntime", "AI\\..+", "Widgets", "Workloads"]) {
    if (!portablePayloadScript.includes(marker)) {
      failures.push(`Portable 可选工作负载拦截规则缺少 ${marker}`);
    }
  }
  requireMatch("发布脚本没有默认执行平台无关测试", releaseScript,
    /Invoke-Logged \$ctest/u);
  requireMatch("发布脚本不能生成唯一预览版本名", releaseScript,
    /ReleaseStage[\s\S]*?preview\.\$PreviewNumber[\s\S]*?Rillshot-\$artifactVersion/u);
  requireMatch("Stable 直发路径没有统一强制代码签名参数", releaseScript,
    /Stable direct releases require -SigningCertificateThumbprint/u);
  requireMatch("Store 路径没有限制为 MSIX", releaseScript,
    /MicrosoftStore distribution supports -Mode Msix only/u);
  requireMatch("Store 路径仍允许本地证书签名", releaseScript,
    /Do not locally certificate-sign a Microsoft Store submission/u);
  requireMatch("Store Stable 没有强制 Partner Center Publisher", releaseScript,
    /Microsoft Store submissions require -ExpectedMsixPublisher/u);
  requireMatch("MSIX Publisher 预检没有在实际构建前执行", releaseScript,
    /\$buildsMsix[\s\S]*?Assert-RillshotPackagePublisher[\s\S]*?Write-Host "Rillshot product version/u);
  requireMatch("Store Publisher 没有与清单做精确匹配", signingScript,
    /ExpectedMsixPublisher[\s\S]*?StringComparison\]::Ordinal[\s\S]*?reserved Partner Center publisher identity/u);
  requireMatch("Store 交付说明未明确由 Store 审核后签名", releaseScript,
    /Store signs it after certification/u);
  requireMatch("Stable 路径没有统一阻断未完成的二进制载荷许可复核", releaseScript,
    /Stable releases require a completed binary payload license review/u);
  requireMatch("Stable 构建未拒绝重复或非法的第三方复核状态行", releaseScript,
    /reviewLines[\s\S]*?StartsWith\("Binary payload review:"\)[\s\S]*?Count -ne 1[\s\S]*?-cne "Binary payload review: COMPLETE"/u);
  requireMatch("签名路径允许省略 RFC 3161 时间戳", releaseScript,
    /Every signed release requires a non-empty HTTPS RFC 3161 TimestampUrl/u);
  requireMatch("发布脚本未加载独立签名模块", releaseScript,
    /release\\Signing\.ps1/u);
  requireMatch("签名模块未验证证书私钥、有效期与 Code Signing EKU", signingScript,
    /HasPrivateKey[\s\S]*?NotBefore[\s\S]*?NotAfter[\s\S]*?1\.3\.6\.1\.5\.5\.7\.3\.3/u);
  requireMatch("签名模块未固定 SHA-256 文件摘要与 RFC 3161 时间戳摘要", signingScript,
    /"\/fd", "SHA256"[\s\S]*?"\/tr"[\s\S]*?"\/td", "SHA256"/u);
  requireMatch("签名模块未执行完整 Authenticode 验证", signingScript,
    /"verify", "\/pa", "\/all", "\/v"/u);
  requireMatch("签名模块未机械确认时间戳证书", signingScript,
    /TimeStamperCertificate/u);
  requireMatch("MSIX 未验证 Publisher 与签名证书 Subject 精确一致", signingScript,
    /Identity\.Publisher[\s\S]*?SigningContext\.Subject[\s\S]*?StringComparison\]::Ordinal/u);
  requireMatch("Portable 没有走统一签名与时间戳验证", releaseScript,
    /portableExecutable[\s\S]*?Invoke-RillshotSignAndVerify/u);
  requireMatch("MSIX 没有走统一签名与时间戳验证", releaseScript,
    /foreach \(\$package in \$packages\)[\s\S]*?Invoke-RillshotSignAndVerify/u);
  requireMatch("一键构建没有默认生成可辨认的 preview.1", releaseCommand,
    /-ReleaseStage Preview -PreviewNumber 1/u);
  requireMatch("Portable 缺少可报告的发布身份元数据", releaseBundleScript,
    /sourceRevision[\s\S]*?RELEASE-METADATA\.json/u);
  requireMatch("Portable 根目录没有唯一且醒目的启动入口", releaseScript,
    /Destination \(Join-Path \$portableRoot "Rillshot\.cmd"\)/u);
  requireMatch("Portable 运行时没有隔离到 app 子目录", releaseScript,
    /\$runtimeRoot = Join-Path \$portableRoot "app"/u);
  requireMatch("Portable 支持脚本没有隔离到 support 子目录", releaseScript,
    /\$supportRoot = Join-Path \$portableRoot "support"/u);
  requireMatch("发布脚本未在压缩前验证最终目录结构", releaseScript,
    /Assert-PortableReleaseLayout -PortableRoot \$portableRoot[\s\S]*Compress-Archive/u);
  requireMatch("Portable 布局检查未禁止根目录运行时文件", releaseBundleScript,
    /rootRuntimeFiles[\s\S]*Portable root contains runtime files/u);
  requireMatch("Portable 布局检查未禁止发布用户数据", releaseBundleScript,
    /@\("captures", "logs", "settings"\)[\s\S]*Portable release contains generated user data/u);
  requireMatch("稳定 Portable 没有强制最终条款和第三方通知", releaseBundleScript,
    /Stable Portable release requires a final LICENSE or EULA\.txt[\s\S]*?Stable Portable release requires THIRD_PARTY_NOTICES\.txt/u);
  requireMatch("稳定 Portable 没有强制版权和对应源码说明", releaseBundleScript,
    /COPYRIGHT\.md[\s\S]*?SOURCE_OFFER\.md[\s\S]*?Stable Portable release requires \$legalName/u);
  requireMatch("稳定 Portable 没有强制完成实际二进制载荷许可复核", releaseBundleScript,
    /reviewLines[\s\S]*?StartsWith\("Binary payload review:"\)[\s\S]*?Count -ne 1[\s\S]*?Binary payload review: COMPLETE[\s\S]*?completed binary payload license review/u);
  checkLineBudget(["tools/build-release.ps1"], 550);
  checkLineBudget(["tools/release/Signing.ps1"], 180);
  checkLineBudget(["tools/release/PortablePayload.ps1"], 220);
  checkLineBudget(["tools/release/BuildToolDiscovery.ps1"], 140);
  checkLineBudget(["tools/check_powershell_syntax.ps1"], 80);

  if (fs.existsSync(path.join(projectRoot, "prompt-engineering"))) {
    failures.push("AI 提示词工程必须位于 Git 源码包之外");
  }

  const artifactRoot = path.join(projectRoot, "artifacts");
  if (fs.existsSync(artifactRoot)) {
    failures.push("交付源码不应包含本机构建 artifacts 目录");
  }
  const forbiddenArtifacts = context.walk(projectRoot, (file) =>
    (/\.(?:o|obj|exe|pdb|ilk|log|jsonl)$/iu.test(file) ||
      /^cc.+\.s$/iu.test(path.basename(file))) &&
    !file.includes(`${path.sep}docs${path.sep}archive${path.sep}`));
  for (const file of forbiddenArtifacts) {
    failures.push(`交付源码含本机生成物：${path.relative(projectRoot, file)}`);
  }

  const activeHumanFiles = context.walk(projectRoot, (file) =>
    /\.(?:md|txt)$/iu.test(file) &&
    !file.includes(`${path.sep}docs${path.sep}archive${path.sep}`));
  for (const file of activeHumanFiles) {
    const content = fs.readFileSync(file, "utf8");
    if (/\brelease-candidate-r\d+\b|\br\d+\b/u.test(content)) {
      failures.push(`活动文档仍使用过时的 rN 修订编号：${path.relative(projectRoot, file)}`);
    }
  }
}
