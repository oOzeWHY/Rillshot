$script:PortablePayloadRequiredFiles = @(
    "Rillshot.WinUI.exe",
    "Rillshot.WinUI.pri",
    "Microsoft.WindowsAppRuntime.dll",
    "Microsoft.WindowsAppRuntime.pri",
    "Microsoft.WindowsAppRuntime.Bootstrap.dll",
    "Microsoft.UI.Xaml.dll",
    "Microsoft.UI.Xaml.Controls.dll",
    "Microsoft.UI.Xaml.Controls.pri"
)
# Keep exclusions in the source manifest. Windows PowerShell 5.1 ignores
# Get-ChildItem -Include/-Exclude when -LiteralPath is used; piping that result
# to Remove-Item can therefore delete the complete staged payload.
$script:PortablePayloadExcludedExtensions = @(
    ".pdb",
    ".ilk",
    ".lib",
    ".exp"
)
$script:PortablePayloadExcludedTopLevelDirectories = @(
    "captures",
    "logs",
    "settings"
)
$script:PortablePayloadForbiddenOptionalPattern = (
    "^(DirectML\.dll|NPUDetect\.dll|onnxruntime\.dll|PerceptiveStreaming\.dll|" +
    "Microsoft\.Windows\.(AI\..+|Internal\.(AI|ImageCreation|SemanticSearch|Vision).*|" +
    "(ImageCreation|SemanticSearch|Vision|Widgets|Workloads).*)|workloads\..+\.json)$"
)

function Get-PayloadRelativePath(
    [string]$RootPrefix,
    [string]$FilePath,
    [string]$Label
) {
    if (-not $FilePath.StartsWith(
            $RootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escaped its payload root: $FilePath"
    }
    return $FilePath.Substring($RootPrefix.Length)
}

function Assert-PortableRuntimePayload(
    [string]$PayloadRoot,
    [string]$StageLabel
) {
    if (-not (Test-Path -LiteralPath $PayloadRoot -PathType Container)) {
        throw "$StageLabel directory was not produced: $PayloadRoot"
    }

    foreach ($fileName in $script:PortablePayloadRequiredFiles) {
        $candidate = Join-Path $PayloadRoot $fileName
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "$StageLabel is missing $fileName at $candidate"
        }
    }

    $priFiles = @(
        Get-ChildItem -LiteralPath $PayloadRoot -File -Filter *.pri `
            -Force -ErrorAction SilentlyContinue
    )
    if ($priFiles.Count -lt 3) {
        throw "$StageLabel contains only $($priFiles.Count) PRI file(s); the self-contained XAML payload is incomplete: $PayloadRoot"
    }
    Write-Host "$StageLabel check passed: required runtime DLLs and PRI files are present."
}

function Copy-PortablePayloadVerified(
    [string]$SourceRoot,
    [string]$DestinationRoot
) {
    if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
        throw "Portable source directory was not produced: $SourceRoot"
    }
    if (-not (Test-Path -LiteralPath $DestinationRoot -PathType Container)) {
        throw "Portable staging directory does not exist: $DestinationRoot"
    }

    $sourcePath = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd([char[]]"\/")
    $destinationPath = (Resolve-Path -LiteralPath $DestinationRoot).Path.TrimEnd([char[]]"\/")
    $sourcePrefix = $sourcePath + [IO.Path]::DirectorySeparatorChar
    $destinationPrefix = $destinationPath + [IO.Path]::DirectorySeparatorChar

    $existingDestinationItems = @(
        Get-ChildItem -LiteralPath $destinationPath -Force -ErrorAction Stop
    )
    if ($existingDestinationItems.Count -ne 0) {
        throw "Portable staging directory must be empty before copying: $destinationPath"
    }

    $allSourceFiles = @(
        Get-ChildItem -LiteralPath $sourcePath -Recurse -File -Force `
            -ErrorAction Stop | Sort-Object FullName
    )
    $sourceFiles = @(
        $allSourceFiles | Where-Object {
            $relativePath = Get-PayloadRelativePath `
                $sourcePrefix $_.FullName "Portable source file"
            $topLevelName = ($relativePath -split '[\\/]')[0].ToLowerInvariant()
            $script:PortablePayloadExcludedExtensions -notcontains `
                $_.Extension.ToLowerInvariant() -and `
                $script:PortablePayloadExcludedTopLevelDirectories -notcontains `
                $topLevelName
        }
    )
    if ($sourceFiles.Count -eq 0) {
        throw "Portable MSBuild output contains no files: $sourcePath"
    }
    $unexpectedOptionalFiles = @(
        $sourceFiles | Where-Object {
            $_.Name -match $script:PortablePayloadForbiddenOptionalPattern
        }
    )
    if ($unexpectedOptionalFiles.Count -ne 0) {
        $unexpectedNames = @($unexpectedOptionalFiles.Name | Sort-Object -Unique)
        throw "Portable payload contains unused Windows App SDK optional workloads: $($unexpectedNames -join ', '). Use the WinUI + Runtime component packages instead of the aggregate Microsoft.WindowsAppSDK package."
    }

    foreach ($sourceFile in $sourceFiles) {
        $relativePath = Get-PayloadRelativePath `
            $sourcePrefix $sourceFile.FullName "Portable source file"
        $destinationFile = Join-Path $destinationPath $relativePath
        $destinationDirectory = Split-Path -Parent $destinationFile
        if (-not (Test-Path -LiteralPath $destinationDirectory -PathType Container)) {
            New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        }
        [IO.File]::Copy($sourceFile.FullName, $destinationFile, $true)
    }

    $destinationFiles = @(
        Get-ChildItem -LiteralPath $destinationPath -Recurse -File -Force `
            -ErrorAction Stop | Sort-Object FullName
    )
    $destinationByRelativePath = @{}
    foreach ($destinationFile in $destinationFiles) {
        $relativePath = Get-PayloadRelativePath `
            $destinationPrefix $destinationFile.FullName "Portable staged file"
        $key = $relativePath.ToLowerInvariant()
        if ($destinationByRelativePath.ContainsKey($key)) {
            throw "Portable staging produced a case-insensitive path collision: $relativePath"
        }
        $destinationByRelativePath.Add($key, $destinationFile)
    }

    if ($sourceFiles.Count -ne $destinationFiles.Count) {
        throw "Portable staging file count differs from MSBuild output: source=$($sourceFiles.Count), staged=$($destinationFiles.Count)"
    }
    $unexpectedDevelopmentFiles = @(
        $destinationFiles | Where-Object {
            $script:PortablePayloadExcludedExtensions -contains `
                $_.Extension.ToLowerInvariant()
        }
    )
    if ($unexpectedDevelopmentFiles.Count -ne 0) {
        throw "Portable staging contains excluded development files."
    }
    foreach ($directoryName in $script:PortablePayloadExcludedTopLevelDirectories) {
        if (Test-Path -LiteralPath (Join-Path $destinationPath $directoryName)) {
            throw "Portable staging contains generated user-data directory: $directoryName"
        }
    }
    foreach ($sourceFile in $sourceFiles) {
        $relativePath = Get-PayloadRelativePath `
            $sourcePrefix $sourceFile.FullName "Portable source file"
        $key = $relativePath.ToLowerInvariant()
        if (-not $destinationByRelativePath.ContainsKey($key)) {
            throw "Portable staging omitted a source file: $relativePath"
        }
        $destinationFile = $destinationByRelativePath[$key]
        if ($sourceFile.Length -ne $destinationFile.Length) {
            throw "Portable staging changed the size of $relativePath"
        }
    }

    $excludedCount = $allSourceFiles.Count - $sourceFiles.Count
    Write-Host "Portable payload staged: paths, count, and sizes verified for $($sourceFiles.Count) files; $excludedCount development files excluded before copying."
}
