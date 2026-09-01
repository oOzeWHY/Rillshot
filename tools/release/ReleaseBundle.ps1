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
        @{ Source = Join-Path $ScriptRoot "release\$releaseReadme"; Destination = "README-FIRST-ZH.txt" }
        @{ Source = Join-Path $ScriptRoot "release\FEEDBACK-TEMPLATE-ZH.md"; Destination = "FEEDBACK-TEMPLATE-ZH.md" }
        @{ Source = Join-Path $ProjectRoot "LICENSE"; Destination = "LICENSE" }
        @{ Source = Join-Path $ProjectRoot "COPYRIGHT.md"; Destination = "COPYRIGHT.md" }
        @{ Source = Join-Path $ProjectRoot "SOURCE_OFFER.md"; Destination = "SOURCE_OFFER.md" }
        @{ Source = Join-Path $ProjectRoot "THIRD_PARTY_NOTICES.txt"; Destination = "THIRD_PARTY_NOTICES.txt" }
    )
    foreach ($entry in $releaseFiles) {
        Copy-Item -LiteralPath $entry.Source `
            -Destination (Join-Path $PortableRoot $entry.Destination) -Force
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
    [IO.File]::WriteAllText(
        (Join-Path $PortableRoot "RELEASE-METADATA.json"),
        ($metadata | ConvertTo-Json -Depth 3) + [Environment]::NewLine,
        $Utf8NoBom)
}
