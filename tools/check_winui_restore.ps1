[CmdletBinding()]
param(
    [string]$AssetsPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($AssetsPath)) {
    $AssetsPath = Join-Path -Path $PSScriptRoot -ChildPath "..\apps\rillshot_winui\obj\project.assets.json"
}

$resolvedPath = [System.IO.Path]::GetFullPath($AssetsPath)
if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
    throw "未找到 NuGet 资产文件：$resolvedPath。请先执行干净还原。"
}

$assets = Get-Content -LiteralPath $resolvedPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($null -eq $assets.project -or $null -eq $assets.project.frameworks) {
    throw "NuGet 资产文件缺少 project.frameworks：$resolvedPath"
}
if ($null -eq $assets.targets) {
    throw "NuGet 资产文件缺少 targets：$resolvedPath"
}

$libraryNames = @($assets.libraries.PSObject.Properties.Name)
function Test-PackagePresent([string]$PackageName) {
    $prefix = $PackageName + "/"
    return @($libraryNames | Where-Object {
        $_.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
    }).Count -ne 0
}
function Test-PackageVersionPresent(
    [string]$PackageName,
    [string]$PackageVersion
) {
    return $libraryNames -icontains "$PackageName/$PackageVersion"
}

$frameworkNames = @($assets.project.frameworks.PSObject.Properties.Name)
$targetNames = @($assets.targets.PSObject.Properties.Name)

Write-Host "NuGet 还原框架：$($frameworkNames -join ', ')"
Write-Host "NuGet 资产目标：$($targetNames -join ', ')"

$nativeFrameworkNames = @($frameworkNames | Where-Object { $_ -match '^native(?:,|/|$)' })
$nativeTargetNames = @($targetNames | Where-Object { $_ -match '^native(?:,|/|$)' })

if ($nativeFrameworkNames.Count -eq 0) {
    throw "project.frameworks 缺少 native；当前还原未生成原生 C++ 资产图。"
}
if ($nativeTargetNames.Count -eq 0) {
    throw "targets 缺少 native；当前还原未生成原生 C++ 资产目标。"
}

$requiredPackages = [ordered]@{
    "Microsoft.WindowsAppSDK.WinUI" = "2.3.0"
    "Microsoft.WindowsAppSDK.Foundation" = "2.3.5"
    "Microsoft.WindowsAppSDK.InteractiveExperiences" = "2.1.3"
    "Microsoft.WindowsAppSDK.Runtime" = "2.3.1"
    "Microsoft.Windows.CppWinRT" = "2.0.250303.1"
}
foreach ($requiredPackage in $requiredPackages.GetEnumerator()) {
    if (-not (Test-PackageVersionPresent $requiredPackage.Key $requiredPackage.Value)) {
        throw "NuGet 资产图缺少固定的稳定组件：$($requiredPackage.Key)/$($requiredPackage.Value)"
    }
}
$prereleaseLibraries = @($libraryNames | Where-Object {
    $parts = $_ -split '/', 2
    $parts.Count -eq 2 -and $parts[1].Contains("-")
})
if ($prereleaseLibraries.Count -ne 0) {
    throw "NuGet 资产图混入预发布包：$($prereleaseLibraries -join ', ')"
}
$duplicateComponentFamilies = @(
    $libraryNames |
        Where-Object { $_.StartsWith(
            "Microsoft.WindowsAppSDK.", [StringComparison]::OrdinalIgnoreCase) } |
        Group-Object { ($_ -split '/', 2)[0].ToLowerInvariant() } |
        Where-Object { $_.Count -gt 1 }
)
if ($duplicateComponentFamilies.Count -ne 0) {
    $duplicateNames = @($duplicateComponentFamilies.Name | Sort-Object)
    throw "NuGet 资产图包含同一 Windows App SDK 组件的多个版本：$($duplicateNames -join ', ')"
}
foreach ($forbiddenPackage in @(
    "Microsoft.WindowsAppSDK",
    "Microsoft.WindowsAppSDK.AI",
    "Microsoft.WindowsAppSDK.ML",
    "Microsoft.WindowsAppSDK.Widgets")) {
    if (Test-PackagePresent $forbiddenPackage) {
        throw "NuGet 资产图包含未使用的聚合/可选组件：$forbiddenPackage。Portable 只应引用 WinUI 与 Runtime 组件包。"
    }
}

Write-Host "WinUI NuGet 还原契约检查通过：稳定组件版本唯一，无预发布包或 AI/ML/Widgets 聚合依赖。"
