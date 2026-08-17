function Get-RillshotSigningContext {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Thumbprint,
        [Parameter(Mandatory = $true)][string]$TimestampUrl
    )

    $normalized = ($Thumbprint -replace "\s", "").ToUpperInvariant()
    if ($normalized -notmatch "^[0-9A-F]{40}$") {
        throw "SigningCertificateThumbprint must be a 40-character SHA-1 certificate thumbprint."
    }

    $timestampUri = $null
    if (-not [Uri]::TryCreate(
            $TimestampUrl, [UriKind]::Absolute, [ref]$timestampUri) -or
        $timestampUri.Scheme -ne [Uri]::UriSchemeHttps) {
        throw "TimestampUrl must be an absolute HTTPS RFC 3161 timestamp endpoint."
    }

    $candidates = @(
        foreach ($entry in @(
            @{ Root = "Cert:\CurrentUser\My"; MachineStore = $false },
            @{ Root = "Cert:\LocalMachine\My"; MachineStore = $true }
        )) {
            $certificate = Get-Item -LiteralPath (
                Join-Path $entry.Root $normalized) -ErrorAction SilentlyContinue
            if ($null -ne $certificate) {
                [PSCustomObject]@{
                    Certificate = $certificate
                    MachineStore = $entry.MachineStore
                }
            }
        }
    )
    if ($candidates.Count -eq 0) {
        throw "The signing certificate was not found in CurrentUser\My or LocalMachine\My: $normalized"
    }
    if ($candidates.Count -gt 1) {
        throw "The signing certificate exists in both user and machine stores. Remove the duplicate or use an isolated release account: $normalized"
    }

    $candidate = $candidates[0]
    $certificate = $candidate.Certificate
    if (-not $certificate.HasPrivateKey) {
        throw "The signing certificate does not expose an associated private key: $normalized"
    }
    $now = Get-Date
    if ($now -lt $certificate.NotBefore -or $now -ge $certificate.NotAfter) {
        throw "The signing certificate is not currently valid: $($certificate.NotBefore.ToString('o')) .. $($certificate.NotAfter.ToString('o'))"
    }
    $codeSigningOid = "1.3.6.1.5.5.7.3.3"
    $hasCodeSigningEku = @(
        $certificate.EnhancedKeyUsageList |
            Where-Object { $_.ObjectId.Value -eq $codeSigningOid }
    ).Count -gt 0
    if (-not $hasCodeSigningEku) {
        throw "The selected certificate does not contain the Code Signing EKU ($codeSigningOid)."
    }

    return [PSCustomObject]@{
        Thumbprint = $normalized
        Subject = $certificate.Subject
        MachineStore = [bool]$candidate.MachineStore
        TimestampUrl = $timestampUri.AbsoluteUri
    }
}

function Assert-RillshotPackagePublisher {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)]$SigningContext
    )

    [xml]$manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8
    $publisher = [string]$manifest.Package.Identity.Publisher
    if ([string]::IsNullOrWhiteSpace($publisher)) {
        throw "Package.appxmanifest does not declare Identity Publisher."
    }
    if (-not [string]::Equals(
            $publisher,
            [string]$SigningContext.Subject,
            [StringComparison]::Ordinal)) {
        throw "MSIX Identity Publisher must exactly match the signing certificate subject. Manifest='$publisher'; Certificate='$($SigningContext.Subject)'."
    }
}

function Assert-RillshotExpectedPackagePublisher {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$ExpectedPublisher
    )

    [xml]$manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8
    $publisher = [string]$manifest.Package.Identity.Publisher
    if ([string]::IsNullOrWhiteSpace($ExpectedPublisher) -or
        $ExpectedPublisher -eq "CN=Rillshot Development") {
        throw "ExpectedMsixPublisher must be the exact Partner Center publisher identity, not the development placeholder."
    }
    if (-not [string]::Equals(
            $publisher, $ExpectedPublisher, [StringComparison]::Ordinal)) {
        throw "MSIX Identity Publisher must exactly match the reserved Partner Center publisher identity. Manifest='$publisher'; Expected='$ExpectedPublisher'."
    }
}

function Invoke-RillshotSignAndVerify {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$SignTool,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [Parameter(Mandatory = $true)]$SigningContext
    )

    $signArguments = @("sign", "/v")
    if ($SigningContext.MachineStore) {
        $signArguments += "/sm"
    }
    $signArguments += @(
        "/sha1", $SigningContext.Thumbprint,
        "/fd", "SHA256",
        "/tr", $SigningContext.TimestampUrl,
        "/td", "SHA256",
        $FilePath
    )
    Invoke-Logged $SignTool $signArguments $LogPath
    Invoke-Logged $SignTool @("verify", "/pa", "/all", "/v", $FilePath) $LogPath

    $signature = Get-AuthenticodeSignature -LiteralPath $FilePath
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "Authenticode verification did not return Valid for ${FilePath}: $($signature.Status) $($signature.StatusMessage)"
    }
    if ($null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $SigningContext.Thumbprint) {
        throw "The verified signer certificate does not match the requested thumbprint for $FilePath"
    }
    if ($null -eq $signature.TimeStamperCertificate) {
        throw "The signature is valid but has no verified RFC 3161 timestamp: $FilePath"
    }
    Write-Host "Authenticode signature and RFC 3161 timestamp verified: $FilePath"
}
