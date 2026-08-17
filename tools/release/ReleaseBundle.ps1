function Add-PortableReleaseFiles {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$ProjectRoot,
        [Parameter(Mandatory = $true)][string]$ScriptRoot,
        [Parameter(Mandatory = $true)][string]$PortableRoot,
        [Parameter(Mandatory = $true)][string]$ProductVersion,
        [Parameter(Mandatory = $true)][string]$ArtifactVersion,
        [Parameter(Mandatory = $true)][ValidateSet("Stable", "Preview")]
        [string]$ReleaseStage,
        [Parameter(Mandatory = $true)][string]$SourceRevision,
        [Parameter(Mandatory = $true)][string]$Platform,
        [Parameter(Mandatory = $true)][string]$Configuration,
        [Parameter(Mandatory = $true)][System.Text.Encoding]$Utf8NoBom
    )

    $releaseReadme = if ($ReleaseStage -eq "Preview") {
        "README-FIRST-PREVIEW-ZH.txt"
    } else {
        "README-FIRST-STABLE-ZH.txt"
    }
    $releaseFiles = @(
        @{ Source = Join-Path $ProjectRoot "SOURCE_REVISION.txt"; Destination = "SOURCE_REVISION.txt" }
        @{ Source = Join-Path $ScriptRoot "release\$releaseReadme"; Destination = "README-FIRST-ZH.txt" }
        @{ Source = Join-Path $ScriptRoot "release\FEEDBACK-TEMPLATE-ZH.md"; Destination = "FEEDBACK-TEMPLATE-ZH.md" }
    )
    foreach ($entry in $releaseFiles) {
        if (-not (Test-Path -LiteralPath $entry.Source -PathType Leaf)) {
            throw "Portable release file is missing: $($entry.Source)"
        }
        Copy-Item -LiteralPath $entry.Source `
            -Destination (Join-Path $PortableRoot $entry.Destination) -Force
    }

    foreach ($optionalName in @("LICENSE", "EULA.txt", "COPYRIGHT.md", "SOURCE_OFFER.md", "THIRD_PARTY_NOTICES.txt")) {
        $candidate = Join-Path $ProjectRoot $optionalName
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            Copy-Item -LiteralPath $candidate `
                -Destination (Join-Path $PortableRoot $optionalName) -Force
        }
    }
    if ($ReleaseStage -eq "Stable") {
        $hasTerms = (Test-Path -LiteralPath (Join-Path $PortableRoot "LICENSE") -PathType Leaf) -or
            (Test-Path -LiteralPath (Join-Path $PortableRoot "EULA.txt") -PathType Leaf)
        if (-not $hasTerms) {
            throw "Stable Portable release requires a final LICENSE or EULA.txt."
        }
        if (-not (Test-Path -LiteralPath (
                Join-Path $PortableRoot "THIRD_PARTY_NOTICES.txt") -PathType Leaf)) {
            throw "Stable Portable release requires THIRD_PARTY_NOTICES.txt."
        }
        foreach ($legalName in @("COPYRIGHT.md", "SOURCE_OFFER.md")) {
            if (-not (Test-Path -LiteralPath (
                    Join-Path $PortableRoot $legalName) -PathType Leaf)) {
                throw "Stable Portable release requires $legalName."
            }
        }
        $thirdPartyNotice = Get-Content -LiteralPath (
            Join-Path $PortableRoot "THIRD_PARTY_NOTICES.txt") -Raw
        $reviewLines = @($thirdPartyNotice -split "\r?\n" |
            Where-Object { $_.StartsWith("Binary payload review:") })
        if ($reviewLines.Count -ne 1 -or
            $reviewLines[0] -cne "Binary payload review: COMPLETE") {
            throw "Stable Portable release requires a completed binary payload license review."
        }
    }

    $metadata = [ordered]@{
        schemaVersion = 1
        product = "Rillshot"
        productVersion = $ProductVersion
        artifactVersion = $ArtifactVersion
        releaseStage = $ReleaseStage.ToLowerInvariant()
        sourceRevision = $SourceRevision
        platform = $Platform
        configuration = $Configuration
        builtAtUtc = [DateTime]::UtcNow.ToString("o")
    }
    $metadataPath = Join-Path $PortableRoot "RELEASE-METADATA.json"
    [IO.File]::WriteAllText(
        $metadataPath,
        ($metadata | ConvertTo-Json -Depth 3) + [Environment]::NewLine,
        $Utf8NoBom)

    foreach ($requiredName in @(
        "SOURCE_REVISION.txt",
        "README-FIRST-ZH.txt",
        "FEEDBACK-TEMPLATE-ZH.md",
        "RELEASE-METADATA.json")) {
        $requiredPath = Join-Path $PortableRoot $requiredName
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Staged Portable release bundle is missing $requiredName"
        }
    }
    Write-Host "Portable release identity and tester guidance added: $ArtifactVersion"
}

function Assert-PortableReleaseLayout {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$PortableRoot
    )

    foreach ($relativePath in @(
        "Rillshot.cmd",
        "README-FIRST-ZH.txt",
        "RELEASE-METADATA.json",
        "app\Rillshot.WinUI.exe",
        "support\start-rillshot.ps1")) {
        if (-not (Test-Path -LiteralPath (
                Join-Path $PortableRoot $relativePath) -PathType Leaf)) {
            throw "Portable release layout is missing $relativePath"
        }
    }

    $rootRuntimeFiles = @(
        Get-ChildItem -LiteralPath $PortableRoot -File -Force |
            Where-Object {
                $_.Name -eq "Rillshot.WinUI.exe" -or
                $_.Extension.ToLowerInvariant() -in @(
                    ".dll", ".winmd", ".xbf", ".pri")
            }
    )
    if ($rootRuntimeFiles.Count -ne 0) {
        throw "Portable root contains runtime files that must stay below app: $(@($rootRuntimeFiles.Name) -join ', ')"
    }

    foreach ($directoryName in @("captures", "logs", "settings")) {
        $generatedDirectory = Join-Path (
            Join-Path $PortableRoot "app") $directoryName
        if (Test-Path -LiteralPath $generatedDirectory) {
            throw "Portable release contains generated user data: app\$directoryName"
        }
    }
    Write-Host "Portable release layout check passed: clear root launcher, isolated app runtime, and no generated user data."
}
