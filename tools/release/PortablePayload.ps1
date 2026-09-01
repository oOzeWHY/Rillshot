$script:PortablePayloadRequiredFiles = @(
    "Rillshot.WinUI.exe",
    "Rillshot.WinUI.pri",
    "Microsoft.WindowsAppRuntime.dll",
    "Microsoft.UI.Xaml.dll"
)
$script:PortablePayloadExcludedExtensions = @(".pdb", ".ilk", ".lib", ".exp")
$script:PortablePayloadExcludedDirectories = @("captures", "logs", "settings")

function Assert-PortableRuntimePayload(
    [string]$PayloadRoot,
    [string]$StageLabel
) {
    if (-not (Test-Path -LiteralPath $PayloadRoot -PathType Container)) {
        throw "$StageLabel directory was not produced: $PayloadRoot"
    }
    foreach ($fileName in $script:PortablePayloadRequiredFiles) {
        if (-not (Test-Path -LiteralPath (
                Join-Path $PayloadRoot $fileName) -PathType Leaf)) {
            throw "$StageLabel is missing $fileName"
        }
    }
}

function Copy-PortablePayload(
    [string]$SourceRoot,
    [string]$DestinationRoot
) {
    if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
        throw "Portable source directory was not produced: $SourceRoot"
    }

    $sourcePath = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd([char[]]"\/")
    $sourcePrefix = $sourcePath + [IO.Path]::DirectorySeparatorChar
    foreach ($sourceFile in Get-ChildItem -LiteralPath $sourcePath -Recurse -File -Force) {
        $relativePath = $sourceFile.FullName.Substring($sourcePrefix.Length)
        $topLevelDirectory = ($relativePath -split '[\\/]')[0].ToLowerInvariant()
        if ($script:PortablePayloadExcludedExtensions -contains `
                $sourceFile.Extension.ToLowerInvariant() -or
            $script:PortablePayloadExcludedDirectories -contains $topLevelDirectory) {
            continue
        }

        $destinationFile = Join-Path $DestinationRoot $relativePath
        $destinationDirectory = Split-Path -Parent $destinationFile
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        Copy-Item -LiteralPath $sourceFile.FullName -Destination $destinationFile -Force
    }
    Assert-PortableRuntimePayload $DestinationRoot "Staged Portable app runtime"
}
