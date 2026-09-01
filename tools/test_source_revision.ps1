[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptRoot "release\SourceRevision.ps1")

function Assert-Equal([string]$Expected, [string]$Actual, [string]$Scenario) {
    if ($Expected -cne $Actual) {
        throw "$Scenario failed. Expected '$Expected', got '$Actual'."
    }
}

function Assert-Throws([scriptblock]$Action, [string]$Scenario) {
    $threw = $false
    try {
        & $Action
    } catch {
        $threw = $true
    }
    if (-not $threw) {
        throw "$Scenario failed. The operation should have been blocked."
    }
}

function Invoke-TestGit([string]$GitPath, [string]$Root, [string[]]$Arguments) {
    & $GitPath -C $Root @Arguments | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Test Git command failed: git -C $Root $($Arguments -join ' ')"
    }
}

$identity = "1.1.9-source-ready-reviewed-r2"
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) (
    "rillshot-source-revision-" + [Guid]::NewGuid().ToString("N"))
$archiveRoot = Join-Path $temporaryRoot "archive"
$repositoryRoot = Join-Path $temporaryRoot "repository"

try {
    New-Item -ItemType Directory -Path $archiveRoot -Force | Out-Null
    $archiveRevision = Resolve-RillshotSourceRevision `
        -ProjectRoot $archiveRoot -ReleaseStage Preview `
        -PackagedSourceIdentity $identity
    Assert-Equal $identity $archiveRevision "Extracted archive Preview"
    Assert-Throws {
        Resolve-RillshotSourceRevision `
            -ProjectRoot $archiveRoot -ReleaseStage Stable `
            -PackagedSourceIdentity $identity
    } "Extracted archive Stable"

    $gitCommand = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($null -eq $gitCommand) {
        $gitCommand = Get-Command git -ErrorAction SilentlyContinue
    }
    if ($null -eq $gitCommand) {
        throw "Git is required to test empty, clean, and dirty repository source identities."
    }
    $gitPath = $gitCommand.Source

    New-Item -ItemType Directory -Path $repositoryRoot -Force | Out-Null
    Invoke-TestGit $gitPath $repositoryRoot @("init", "--quiet")
    $emptyRevision = Resolve-RillshotSourceRevision `
        -ProjectRoot $repositoryRoot -ReleaseStage Preview `
        -PackagedSourceIdentity $identity
    Assert-Equal $identity $emptyRevision "Empty repository Preview"
    Assert-Throws {
        Resolve-RillshotSourceRevision `
            -ProjectRoot $repositoryRoot -ReleaseStage Stable `
            -PackagedSourceIdentity $identity
    } "Empty repository Stable"

    Invoke-TestGit $gitPath $repositoryRoot @("config", "user.name", "Rillshot Test")
    Invoke-TestGit $gitPath $repositoryRoot @("config", "user.email", "rillshot-test@example.invalid")
    [IO.File]::WriteAllText(
        (Join-Path $repositoryRoot "tracked.txt"),
        "source revision test`n",
        (New-Object System.Text.UTF8Encoding($false)))
    Invoke-TestGit $gitPath $repositoryRoot @("add", "tracked.txt")
    Invoke-TestGit $gitPath $repositoryRoot @("commit", "--quiet", "-m", "source revision test")
    $expectedRevision = (& $gitPath -C $repositoryRoot rev-parse --verify "HEAD^{commit}").Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Could not read the test repository HEAD."
    }
    $cleanRevision = Resolve-RillshotSourceRevision `
        -ProjectRoot $repositoryRoot -ReleaseStage Stable `
        -PackagedSourceIdentity $identity
    Assert-Equal $expectedRevision $cleanRevision "Clean repository Stable"

    [IO.File]::WriteAllText(
        (Join-Path $repositoryRoot "untracked.txt"),
        "dirty`n",
        (New-Object System.Text.UTF8Encoding($false)))
    $dirtyRevision = Resolve-RillshotSourceRevision `
        -ProjectRoot $repositoryRoot -ReleaseStage Preview `
        -PackagedSourceIdentity $identity
    Assert-Equal "$expectedRevision-dirty" $dirtyRevision "Dirty repository Preview"
    Assert-Throws {
        Resolve-RillshotSourceRevision `
            -ProjectRoot $repositoryRoot -ReleaseStage Stable `
            -PackagedSourceIdentity $identity
    } "Dirty repository Stable"
} finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

Write-Host "Source revision checks passed: archive, empty, clean, and dirty repository states."
