[CmdletBinding()]
param(
    [string]$SourceRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = Join-Path $PSScriptRoot ".."
}
$resolvedRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$scriptFiles = @(
    Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Filter *.ps1 |
        Where-Object {
            $relativePath = $_.FullName.Substring($resolvedRoot.Length).TrimStart([char[]]"\/")
            $topLevelName = ($relativePath -split '[\\/]')[0]
            $topLevelName -notin @(".git", ".vs", "artifacts", "build", "out") -and
                $topLevelName -notlike "build-*" -and
                $relativePath -notmatch '^apps[\\/]rillshot_winui[\\/](bin|obj|Generated Files)[\\/]'
        } |
        Sort-Object FullName
)
if ($scriptFiles.Count -eq 0) {
    throw "No PowerShell scripts were found below $resolvedRoot"
}

$failures = New-Object System.Collections.Generic.List[string]
foreach ($scriptFile in $scriptFiles) {
    $tokens = $null
    $parseErrors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        $scriptFile.FullName,
        [ref]$tokens,
        [ref]$parseErrors)
    foreach ($parseError in @($parseErrors)) {
        $relativePath = $scriptFile.FullName.Substring($resolvedRoot.Length).TrimStart([char[]]"\/")
        $failures.Add(
            "${relativePath}:$($parseError.Extent.StartLineNumber):$($parseError.Extent.StartColumnNumber): $($parseError.Message)")
    }
}

if ($failures.Count -ne 0) {
    throw "PowerShell syntax check failed:`r`n$($failures -join [Environment]::NewLine)"
}
Write-Host "PowerShell syntax check passed: $($scriptFiles.Count) scripts."
