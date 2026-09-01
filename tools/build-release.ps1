[CmdletBinding()]
param(
    [ValidateSet("Portable", "Msix", "All")]
    [string]$Mode = "Portable",
    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [ValidateSet("Stable", "Preview")]
    [string]$ReleaseStage = "Stable",
    [ValidateRange(1, 9999)]
    [int]$PreviewNumber = 1,
    [string]$IntermediateRoot = "",
    [string]$CMakeGenerator = "Visual Studio 18 2026",
    [switch]$Clean,
    [switch]$SkipCoreTests,
    [switch]$SmokeTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$script:Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[Console]::InputEncoding = $script:Utf8NoBom
[Console]::OutputEncoding = $script:Utf8NoBom
$OutputEncoding = $script:Utf8NoBom

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $scriptRoot "..")).Path
$winuiRoot = Join-Path $projectRoot "apps\rillshot_winui"
$solutionPath = Join-Path $winuiRoot "Rillshot.WinUI.sln"
$artifactRoot = Join-Path $projectRoot "artifacts"
$cacheRoot = Join-Path $artifactRoot "cache"
$logRoot = Join-Path $artifactRoot "logs"
$releaseRoot = Join-Path $artifactRoot "release"
$version = "1.2.0"
$artifactVersion = $version
if ($ReleaseStage -eq "Preview") {
    $artifactVersion = "$version-preview.$PreviewNumber"
}
. (Join-Path $scriptRoot "release\BuildToolDiscovery.ps1")
. (Join-Path $scriptRoot "release\PortablePayload.ps1")
. (Join-Path $scriptRoot "release\ReleaseBundle.ps1")

function Invoke-Logged(
    [string]$FilePath,
    [string[]]$Arguments,
    [string]$LogPath
) {
    Write-Host "Running: $([IO.Path]::GetFileName($FilePath)) $($Arguments -join ' ')"
    & $FilePath @Arguments 2>&1 | ForEach-Object {
        $line = $_.ToString()
        Write-Host $line
        [IO.File]::AppendAllText(
            $LogPath, $line + [Environment]::NewLine, $script:Utf8NoBom)
    }
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$([IO.Path]::GetFileName($FilePath)) failed with exit code $exitCode. See $LogPath"
    }
}

function Assert-AsciiPath([string]$PathValue, [string]$Label) {
    if ($PathValue -match "[^\x00-\x7F]") {
        throw "$Label must use a short ASCII-only path because CppWinRT mdmerge cannot reliably process non-ASCII response-file paths. Move the source tree to a path such as D:\src\Rillshot and retry: $PathValue"
    }
}

function Assert-PathWithinRoot(
    [string]$PathValue,
    [string]$RootPath,
    [string]$Label
) {
    $fullPath = [IO.Path]::GetFullPath($PathValue)
    $fullRoot = [IO.Path]::GetFullPath($RootPath).TrimEnd([char[]]"\/") + "\"
    if (-not $fullPath.StartsWith(
            $fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must remain below $RootPath so the build does not create project files outside the source tree: $fullPath"
    }
}

function Assert-PathLengthBudget(
    [string]$PathValue,
    [string]$Label,
    [int]$MaximumLength = 259
) {
    if ($PathValue.Length -gt $MaximumLength) {
        throw "$Label would be $($PathValue.Length) characters, exceeding the safe Windows path budget of $MaximumLength. Move the source tree to a shorter ASCII-only path, such as D:\src\Rillshot, and retry: $PathValue"
    }
}

function Remove-DirectoryIfPresent([string]$PathValue) {
    if (Test-Path -LiteralPath $PathValue) {
        Remove-Item -LiteralPath $PathValue -Recurse -Force
    }
}

function New-ArtifactDriveMapping([string]$TargetPath) {
    $subst = Join-Path $env:SystemRoot "System32\subst.exe"
    if (-not (Test-Path -LiteralPath $subst -PathType Leaf)) {
        throw "subst.exe was not found. Move the source tree to a shorter ASCII-only path and retry."
    }

    $usedDriveNames = @(
        Get-PSDrive -PSProvider FileSystem |
            ForEach-Object { $_.Name.ToUpperInvariant() }
    )
    foreach ($characterCode in 90..68) {
        $driveName = ([char]$characterCode).ToString()
        if ($usedDriveNames -contains $driveName) {
            continue
        }

        $drive = "$driveName`:"
        $output = & $subst $drive $TargetPath 2>&1
        $exitCode = $LASTEXITCODE
        if ($exitCode -eq 0 -and (Test-Path -LiteralPath "$drive\" -PathType Container)) {
            return [PSCustomObject]@{
                Drive = $drive
                Root = "$drive\"
                Subst = $subst
            }
        }
        if ($output) {
            Write-Warning ($output -join [Environment]::NewLine)
        }
    }

    throw "No free drive letter could be mapped to $TargetPath. Disconnect an unused drive or move the source tree to a shorter ASCII-only path, then retry."
}

function Remove-ArtifactDriveMapping($Mapping) {
    if ($null -eq $Mapping) {
        return
    }
    & $Mapping.Subst $Mapping.Drive /d 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Could not remove temporary build drive $($Mapping.Drive). Run 'subst $($Mapping.Drive) /d' after the build."
    }
}

function Get-SourceRevision([string]$Root, [string]$Stage) {
    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($null -eq $git) {
        $git = Get-Command git -ErrorAction SilentlyContinue
    }
    if ($null -eq $git -or -not (Test-Path -LiteralPath (Join-Path $Root ".git"))) {
        if ($Stage -eq "Stable") {
            throw "Stable releases must be built from a Git checkout."
        }
        return "source-archive"
    }

    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "SilentlyContinue"
        $revisionOutput = @(& $git.Source -C $Root rev-parse --verify HEAD 2>$null)
        $revisionExitCode = $LASTEXITCODE
        $status = @(& $git.Source -C $Root status --porcelain 2>$null)
        $statusExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    $revision = if ($revisionOutput.Count -gt 0) {
        $revisionOutput[0].Trim()
    } else {
        ""
    }
    if ($revisionExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($revision)) {
        if ($Stage -eq "Stable") {
            throw "Stable releases require at least one Git commit."
        }
        return "source-archive"
    }
    if ($statusExitCode -ne 0) {
        throw "Git could not read the worktree status."
    }
    if ($status.Count -ne 0) {
        if ($Stage -eq "Stable") {
            throw "Commit or discard local changes before building a Stable release."
        }
        return "$revision-dirty"
    }
    return $revision
}

if ([string]::IsNullOrWhiteSpace($IntermediateRoot)) {
    $IntermediateRoot = Join-Path $artifactRoot "build\winui-intermediate"
}
$IntermediateRoot = [IO.Path]::GetFullPath($IntermediateRoot)
Assert-AsciiPath $projectRoot "ProjectRoot"
Assert-AsciiPath $IntermediateRoot "IntermediateRoot"
Assert-PathWithinRoot $IntermediateRoot $artifactRoot "IntermediateRoot"
$sourceRevision = Get-SourceRevision -Root $projectRoot -Stage $ReleaseStage
Write-Host "Rillshot product version: $version"
Write-Host "Release artifact version: $artifactVersion"
Write-Host "Source revision: $sourceRevision"

# Each invocation owns its release output; do not mix assets from older builds.
Remove-DirectoryIfPresent $releaseRoot
if ($Clean) {
    Remove-DirectoryIfPresent $cacheRoot
}
New-Item -ItemType Directory -Path `
    $artifactRoot, $cacheRoot, $logRoot, $releaseRoot, $IntermediateRoot -Force | Out-Null

$artifactDrive = $null
try {
    # Keep NuGet extraction paths below legacy MAX_PATH limits.
    $artifactDrive = New-ArtifactDriveMapping $cacheRoot
    $shortCacheRoot = $artifactDrive.Root.TrimEnd("\")

    $toolTempRoot = Join-Path $shortCacheRoot "t"
    $env:NUGET_PACKAGES = Join-Path $shortCacheRoot "p"
    $env:NUGET_HTTP_CACHE_PATH = Join-Path $shortCacheRoot "h"
    $env:NUGET_SCRATCH = Join-Path $shortCacheRoot "s"
    $env:NUGET_PLUGINS_CACHE_PATH = Join-Path $shortCacheRoot "g"
    $env:TEMP = $toolTempRoot
    $env:TMP = $toolTempRoot
    $env:NUGET_CLI_LANGUAGE = "en-us"
    $env:DOTNET_CLI_UI_LANGUAGE = "en-US"
    $env:VSLANG = "1033"
    New-Item -ItemType Directory -Path `
        $env:NUGET_PACKAGES, `
        $env:NUGET_HTTP_CACHE_PATH, `
        $env:NUGET_SCRATCH, `
        $env:NUGET_PLUGINS_CACHE_PATH, `
        $toolTempRoot -Force | Out-Null

    $longestKnownPackageTarget = Join-Path $env:NUGET_PACKAGES (
        "microsoft.windowsappsdk.interactiveexperiences\2.1.3\runtimes-framework\win-arm64\native\Microsoft.UI.Composition.OSSupport.dll")
    Assert-PathLengthBudget $longestKnownPackageTarget "NuGet global-packages target"
    Write-Host "Temporary short NuGet cache root: $shortCacheRoot -> $cacheRoot"
    Write-Host "NuGet global-packages: $env:NUGET_PACKAGES -> $(Join-Path $cacheRoot 'p')"

    $visualStudioRoot = Find-VisualStudioInstallation
    $msbuild = Find-MSBuild $visualStudioRoot
    $msbuildVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($msbuild).FileVersion
    Write-Host "Visual Studio: $visualStudioRoot"
    Write-Host "MSBuild: $msbuild ($msbuildVersion)"

    $cmake = Find-CMake $visualStudioRoot
    Assert-CMakeGeneratorAvailable $cmake $CMakeGenerator
    $ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"
    if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) {
        throw "ctest.exe was not found next to cmake.exe."
    }

    $cmakeBuildRoot = Join-Path $artifactRoot "build\cmake-$Platform"
    if ($Clean) {
        Remove-DirectoryIfPresent $cmakeBuildRoot
    }
    $coreLog = Join-Path $logRoot "core-$Platform-$Configuration.log"
    if (Test-Path -LiteralPath $coreLog) {
        Remove-Item -LiteralPath $coreLog -Force
    }
    $buildTests = if ($SkipCoreTests) { "OFF" } else { "ON" }

    Invoke-Logged $cmake @(
        "-S", $projectRoot,
        "-B", $cmakeBuildRoot,
        "-G", $CMakeGenerator,
        "-A", $Platform,
        "-DRILLSHOT_BUILD_TESTS=$buildTests",
        "-DRILLSHOT_BUILD_WINDOWS_APPS=ON",
        "-DRILLSHOT_WARNINGS_AS_ERRORS=ON"
    ) $coreLog
    if ($SkipCoreTests) {
        Invoke-Logged $cmake @(
            "--build", $cmakeBuildRoot,
            "--config", $Configuration,
            "--target", "rillshot_cli",
            "--parallel"
        ) $coreLog
    } else {
        Invoke-Logged $cmake @(
            "--build", $cmakeBuildRoot,
            "--config", $Configuration,
            "--parallel"
        ) $coreLog
        if ($Platform -eq "x64") {
            Invoke-Logged $ctest @(
                "--test-dir", $cmakeBuildRoot,
                "-C", $Configuration,
                "--output-on-failure"
            ) $coreLog
        } else {
            Write-Warning "ARM64 tests were built but not executed on this host."
        }
    }
    $cliExecutable = Join-Path $cmakeBuildRoot "$Configuration\rillshot-cli.exe"
    if (-not (Test-Path -LiteralPath $cliExecutable -PathType Leaf)) {
        throw "The CLI executable was not produced at $cliExecutable"
    }

    function Build-WinUI([ValidateSet("Portable", "Msix")][string]$Distribution) {
        $winuiLog = Join-Path $logRoot "winui-$Distribution-$Platform-$Configuration.log"
        if (Test-Path -LiteralPath $winuiLog) {
            Remove-Item -LiteralPath $winuiLog -Force
        }

        if ($Clean) {
            Remove-DirectoryIfPresent (Join-Path $winuiRoot "obj")
            Remove-DirectoryIfPresent (Join-Path $winuiRoot "bin")
            Remove-DirectoryIfPresent (Join-Path $winuiRoot "Generated Files")
            Remove-DirectoryIfPresent (
                Join-Path $IntermediateRoot "Rillshot.WinUI\$Distribution\$Platform\$Configuration")
            Remove-DirectoryIfPresent (
                Join-Path $IntermediateRoot "Rillshot.Launcher\$Distribution\$Platform\$Configuration")
        }

        $commonProperties = @(
            "/p:Configuration=$Configuration",
            "/p:Platform=$Platform",
            "/p:RillshotDistribution=$Distribution",
            "/p:RillshotBuildRoot=$IntermediateRoot",
            "/p:PreferredUILang=en-US"
        )
        Invoke-Logged $msbuild (@(
            $solutionPath,
            "/t:Restore",
            "/nologo",
            "/warnaserror:NU1603;NU1605;NU1608"
        ) + $commonProperties) $winuiLog

        $buildArguments = @(
            $solutionPath,
            "/m",
            "/nologo"
        ) + $commonProperties
        if ($Distribution -eq "Msix") {
            $packageRoot = Join-Path $releaseRoot "msix-$Platform"
            Remove-DirectoryIfPresent $packageRoot
            New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
            $packageRootWithSlash = $packageRoot.TrimEnd("\") + "\"
            $buildArguments += @(
                "/p:GenerateAppxPackageOnBuild=true",
                "/p:AppxPackageSigningEnabled=false",
                "/p:AppxBundle=Never",
                "/p:AppxPackageDir=$packageRootWithSlash"
            )
        }
        Invoke-Logged $msbuild $buildArguments $winuiLog

        if ($Distribution -eq "Portable") {
            $binRoot = Join-Path $IntermediateRoot "Rillshot.WinUI\Portable\$Platform\$Configuration\bin"
            $launcherBinRoot = Join-Path $IntermediateRoot "Rillshot.Launcher\Portable\$Platform\$Configuration\bin"
            if (-not (Test-Path -LiteralPath (Join-Path $binRoot "Rillshot.WinUI.exe") -PathType Leaf)) {
                throw "The portable executable was not produced below $binRoot"
            }
            $launcherExecutable = Join-Path $launcherBinRoot "Rillshot.exe"
            if (-not (Test-Path -LiteralPath $launcherExecutable -PathType Leaf)) {
                throw "The portable launcher was not produced below $launcherBinRoot"
            }
            $portableName = "Rillshot-$artifactVersion-win-$Platform-portable"
            $portableRoot = Join-Path $releaseRoot $portableName
            Remove-DirectoryIfPresent $portableRoot
            New-Item -ItemType Directory -Path $portableRoot -Force | Out-Null
            $runtimeRoot = Join-Path $portableRoot "app"
            New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
            Assert-PortableRuntimePayload $binRoot "MSBuild Portable output"
            Copy-PortablePayload $binRoot $runtimeRoot
            Copy-Item -LiteralPath $launcherExecutable `
                -Destination (Join-Path $portableRoot "Rillshot.exe") -Force
            Copy-Item -LiteralPath $cliExecutable `
                -Destination (Join-Path $portableRoot "rillshot-cli.exe") -Force
            if ($Platform -eq "x64") {
                $cliVersionOutput = @(& (Join-Path $portableRoot "rillshot-cli.exe") --version 2>&1)
                $cliExitCode = $LASTEXITCODE
                $cliVersionText = ($cliVersionOutput -join "`n").Trim()
                if ($cliExitCode -ne 0 -or $cliVersionText -ne "Rillshot $version") {
                    throw "The staged CLI version check failed."
                }
            }
            Add-PortableReleaseFiles `
                -ProjectRoot $projectRoot -ScriptRoot $scriptRoot `
                -PortableRoot $portableRoot -ProductVersion $version `
                -ArtifactVersion $artifactVersion -ReleaseStage $ReleaseStage `
                -SourceRevision $sourceRevision -Platform $Platform `
                -Configuration $Configuration -Utf8NoBom $script:Utf8NoBom
            Assert-PortableRuntimePayload $runtimeRoot "Staged Portable app runtime"
            if (-not (Test-Path -LiteralPath (
                    Join-Path $portableRoot "rillshot-cli.exe") -PathType Leaf)) {
                throw "The staged Portable package is missing rillshot-cli.exe"
            }

            $archivePath = Join-Path $releaseRoot "$portableName.zip"
            if (Test-Path -LiteralPath $archivePath) {
                Remove-Item -LiteralPath $archivePath -Force
            }

            if ($SmokeTest) {
                & (Join-Path $scriptRoot "test-winui-startup.ps1") -ExecutablePath (
                    Join-Path $runtimeRoot "Rillshot.WinUI.exe")
                foreach ($generatedDirectory in @("captures", "logs", "settings")) {
                    Remove-DirectoryIfPresent (Join-Path $runtimeRoot $generatedDirectory)
                }
            }
            Assert-PortableRuntimePayload $runtimeRoot "Clean staged Portable app runtime"
            Compress-Archive -Path $portableRoot -DestinationPath $archivePath -CompressionLevel Optimal
            Write-Host "Portable output: $portableRoot"
            Write-Host "Portable archive: $archivePath"
        } else {
            $packages = @(
                Get-ChildItem -LiteralPath (Join-Path $releaseRoot "msix-$Platform") `
                    -Recurse -File -Force |
                    Where-Object {
                        $_.Extension.ToLowerInvariant() -in @(".msix", ".appx")
                    }
            )
            if ($packages.Count -eq 0) {
                throw "MSIX packaging completed without producing an .msix or .appx file."
            }
            $packages | ForEach-Object { Write-Host "MSIX output: $($_.FullName)" }
        }
    }

    switch ($Mode) {
        "Portable" { Build-WinUI "Portable" }
        "Msix" { Build-WinUI "Msix" }
        "All" {
            Build-WinUI "Portable"
            Build-WinUI "Msix"
        }
    }

    Write-Host "Build pipeline completed successfully."
} finally {
    Remove-ArtifactDriveMapping $artifactDrive
}
