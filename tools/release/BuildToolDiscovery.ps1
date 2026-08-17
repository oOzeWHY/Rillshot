function Find-VisualStudioInstallation {
    $programFilesX86 = [Environment]::GetFolderPath(
        [Environment+SpecialFolder]::ProgramFilesX86)
    $vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "vswhere.exe was not found. Install Visual Studio 2026 with MSBuild and C++ desktop tools."
    }

    $installation = & $vswhere -latest -products * `
        -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installation)) {
        throw "A compatible Visual Studio installation was not found."
    }
    return $installation.Trim()
}

function Find-MSBuild([string]$VisualStudioRoot) {
    $candidate = Join-Path $VisualStudioRoot "MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        return $candidate
    }
    $command = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }
    throw "MSBuild.exe was not found in the selected Visual Studio installation or PATH."
}

function Find-CMake([string]$VisualStudioRoot) {
    $candidate = Join-Path $VisualStudioRoot (
        "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        return $candidate
    }
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }
    throw "cmake.exe was not found. Install the Visual Studio C++ CMake tools."
}

function Assert-CMakeGeneratorAvailable(
    [string]$CMakePath,
    [string]$Generator
) {
    $capabilityOutput = & $CMakePath -E capabilities 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "cmake -E capabilities failed with exit code $exitCode."
    }
    try {
        $capabilities = ($capabilityOutput -join [Environment]::NewLine) |
            ConvertFrom-Json
    } catch {
        throw "CMake returned an unreadable capabilities document: $($_.Exception.Message)"
    }
    $generatorNames = @($capabilities.generators | ForEach-Object { [string]$_.name })
    if ($generatorNames -notcontains $Generator) {
        $cmakeVersion = [string]$capabilities.version.string
        throw "CMake $cmakeVersion does not provide generator '$Generator'. Use the Visual Studio 2026 bundled CMake (4.2 or newer) or pass a generator reported by 'cmake --help'."
    }
    Write-Host "CMake: $CMakePath ($([string]$capabilities.version.string))"
    Write-Host "CMake generator available: $Generator"
}

function Find-SignTool {
    $kitsRoot = Join-Path ([Environment]::GetFolderPath(
        [Environment+SpecialFolder]::ProgramFilesX86)) "Windows Kits\10\bin"
    $candidates = @(
        Get-ChildItem -LiteralPath $kitsRoot -Filter signtool.exe `
            -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.DirectoryName -match "\\x64$" }
    )
    $candidate = $candidates |
        Sort-Object `
            @{ Expression = {
                $sdkDirectory = Split-Path -Parent $_.DirectoryName
                $parsedVersion = [version]"0.0"
                [void][version]::TryParse(
                    (Split-Path -Leaf $sdkDirectory),
                    [ref]$parsedVersion)
                $parsedVersion
            }; Descending = $true }, `
            @{ Expression = { $_.FullName }; Descending = $true } |
        Select-Object -First 1
    if ($null -eq $candidate) {
        throw "SignTool.exe was not found. Install the Windows SDK signing tools."
    }
    Write-Host "SignTool: $($candidate.FullName) ($($candidate.VersionInfo.FileVersion))"
    return $candidate.FullName
}
