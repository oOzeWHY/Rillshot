[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath,
    [ValidateRange(2, 60)]
    [int]$TimeoutSeconds = 10,
    [switch]$KeepOpen
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$resolvedExecutable = (Resolve-Path -LiteralPath $ExecutablePath).Path
$startupLog = Join-Path (Split-Path -Parent $resolvedExecutable) "logs\startup.log"

function ConvertTo-HexExitCode([int]$ExitCode) {
    $bytes = [BitConverter]::GetBytes($ExitCode)
    $unsigned = [BitConverter]::ToUInt32($bytes, 0)
    return "0x{0:X8}" -f $unsigned
}

function Read-StartupEvidence([int]$TargetProcessId) {
    if (-not (Test-Path -LiteralPath $startupLog -PathType Leaf)) {
        return "No startup log was created. The failure happened before the application object was constructed, or the portable directory was not writable."
    }

    try {
        $marker = "[pid=$TargetProcessId]"
        $matchingLines = @(
            Get-Content -LiteralPath $startupLog -Encoding UTF8 |
                Where-Object { $_.Contains($marker) }
        )
        if ($matchingLines.Count -eq 0) {
            return "No startup log entries were written for pid $TargetProcessId. The failure happened before the application object was constructed."
        }
        return $matchingLines -join [Environment]::NewLine
    } catch {
        return "The startup log could not be read: $($_.Exception.Message)"
    }
}

$process = Start-Process `
    -FilePath $resolvedExecutable `
    -WorkingDirectory (Split-Path -Parent $resolvedExecutable) `
    -PassThru
$startupPassed = $false

try {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $windowFound = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($process.HasExited) {
            $exitCodeHex = ConvertTo-HexExitCode $process.ExitCode
            $startupEvidence = Read-StartupEvidence $process.Id
            throw "Rillshot exited during startup with code $($process.ExitCode) ($exitCodeHex).`r`nStartup evidence:`r`n$startupEvidence"
        }
        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            $windowFound = $true
            break
        }
        Start-Sleep -Milliseconds 200
    }

    if (-not $windowFound) {
        $startupEvidence = Read-StartupEvidence $process.Id
        throw "Rillshot stayed alive but did not create a main window within $TimeoutSeconds seconds.`r`nStartup evidence:`r`n$startupEvidence"
    }

    $expectedLogEntry = "[pid=$($process.Id)] Main window activated."
    $logDeadline = [DateTime]::UtcNow.AddSeconds(2)
    $logConfirmed = $false
    while ([DateTime]::UtcNow -lt $logDeadline) {
        try {
            if (Test-Path -LiteralPath $startupLog -PathType Leaf) {
                $logText = Get-Content -LiteralPath $startupLog -Raw -Encoding UTF8
                if ($logText.Contains($expectedLogEntry)) {
                    $logConfirmed = $true
                    break
                }
            }
        } catch {
            # The application may be appending to the file. Retry briefly.
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $logConfirmed) {
        $startupEvidence = Read-StartupEvidence $process.Id
        throw "The main window appeared, but the current process did not write its startup completion marker to $startupLog`r`nStartup evidence:`r`n$startupEvidence"
    }

    $stabilityDeadline = [DateTime]::UtcNow.AddSeconds(2)
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        if ($process.HasExited) {
            $exitCodeHex = ConvertTo-HexExitCode $process.ExitCode
            $startupEvidence = Read-StartupEvidence $process.Id
            throw "Rillshot exited after activating its main window with code $($process.ExitCode) ($exitCodeHex).`r`nStartup evidence:`r`n$startupEvidence"
        }
    } while ([DateTime]::UtcNow -lt $stabilityDeadline)

    $startupPassed = $true
    if ($KeepOpen) {
        Write-Host "Rillshot started successfully and will remain open. Process id: $($process.Id)"
    } else {
        Write-Host "Startup smoke test passed: the process created a main window, confirmed startup in its log, and remained alive for 2 seconds."
        Write-Host "The verified smoke-test process will now close intentionally."
    }
}
finally {
    if ((-not $KeepOpen -or -not $startupPassed) -and -not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(5000)) {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit()
        }
    }
}
