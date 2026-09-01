function Invoke-RillshotGitProbe {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$GitPath,
        [Parameter(Mandatory = $true)][string]$ProjectRoot,
        [Parameter(Mandatory = $true)][string[]]$GitArguments
    )

    $output = @()
    $exitCode = 1
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        # Git uses non-zero exits and stderr for expected probes such as an
        # unborn HEAD. Windows PowerShell otherwise promotes that stderr to a
        # NativeCommandError when the caller uses ErrorActionPreference=Stop.
        $ErrorActionPreference = "SilentlyContinue"
        $output = @(& $GitPath -C $ProjectRoot @GitArguments 2>$null)
        $exitCode = $LASTEXITCODE
    } catch {
        $output = @()
        $exitCode = 1
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    return [PSCustomObject]@{
        Succeeded = $exitCode -eq 0
        Output = @($output)
    }
}

function Resolve-RillshotSourceRevision {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$ProjectRoot,
        [Parameter(Mandatory = $true)][ValidateSet("Stable", "Preview")]
        [string]$ReleaseStage,
        [Parameter(Mandatory = $true)][string]$PackagedSourceIdentity
    )

    $gitMetadataPath = Join-Path $ProjectRoot ".git"
    if (-not (Test-Path -LiteralPath $gitMetadataPath)) {
        if ($ReleaseStage -eq "Stable") {
            throw "Stable releases require a Git checkout at the source root. Extracted source archives can build Preview releases directly."
        }
        Write-Host "Git metadata is not present; Preview metadata uses packaged source identity: $PackagedSourceIdentity"
        return $PackagedSourceIdentity
    }

    $gitCommand = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($null -eq $gitCommand) {
        $gitCommand = Get-Command git -ErrorAction SilentlyContinue
    }
    if ($null -eq $gitCommand) {
        if ($ReleaseStage -eq "Stable") {
            throw "Stable releases require Git so the exact source commit and worktree state can be verified."
        }
        Write-Host "Git is not available; Preview metadata uses packaged source identity: $PackagedSourceIdentity"
        return $PackagedSourceIdentity
    }

    $gitPath = $gitCommand.Source
    $topLevelProbe = Invoke-RillshotGitProbe `
        -GitPath $gitPath -ProjectRoot $ProjectRoot `
        -GitArguments @("rev-parse", "--show-toplevel")
    $topLevel = if ($topLevelProbe.Output.Count -gt 0) {
        $topLevelProbe.Output[0].Trim()
    } else {
        ""
    }
    $rootMatches = (
        $topLevelProbe.Succeeded -and
        -not [string]::IsNullOrWhiteSpace($topLevel) -and
        [IO.Path]::GetFullPath($topLevel) -eq [IO.Path]::GetFullPath($ProjectRoot)
    )
    if (-not $rootMatches) {
        if ($ReleaseStage -eq "Stable") {
            throw "Stable releases require the source root to be the Git worktree root."
        }
        Write-Host "The source root is not a usable Git worktree; Preview metadata uses packaged source identity: $PackagedSourceIdentity"
        return $PackagedSourceIdentity
    }

    $revisionProbe = Invoke-RillshotGitProbe `
        -GitPath $gitPath -ProjectRoot $ProjectRoot `
        -GitArguments @("rev-parse", "--verify", "--quiet", "HEAD^{commit}")
    $revision = if ($revisionProbe.Output.Count -gt 0) {
        $revisionProbe.Output[0].Trim()
    } else {
        ""
    }
    if (-not $revisionProbe.Succeeded -or
        [string]::IsNullOrWhiteSpace($revision)) {
        if ($ReleaseStage -eq "Stable") {
            throw "Stable releases require a Git repository with at least one commit."
        }
        Write-Host "Git HEAD has no commit; Preview metadata uses packaged source identity: $PackagedSourceIdentity"
        return $PackagedSourceIdentity
    }

    $statusProbe = Invoke-RillshotGitProbe `
        -GitPath $gitPath -ProjectRoot $ProjectRoot `
        -GitArguments @("status", "--porcelain", "--untracked-files=normal")
    if (-not $statusProbe.Succeeded) {
        if ($ReleaseStage -eq "Stable") {
            throw "Stable releases require a verifiable clean Git worktree."
        }
        Write-Warning "Git worktree status could not be verified; Preview metadata marks the commit status as unknown."
        return "$revision-status-unknown"
    }
    if ($statusProbe.Output.Count -ne 0) {
        if ($ReleaseStage -eq "Stable") {
            throw "Stable releases require a clean Git worktree so sourceRevision identifies the exact source."
        }
        return "$revision-dirty"
    }

    return $revision
}
