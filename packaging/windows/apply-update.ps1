# Installs a downloaded PktSwerve update. PktSwerve starts this script (elevated, like
# the app itself) and quits; the script waits for it to exit, swaps the files and
# starts PktSwerve again. The old files are restored if copying fails, and
# PktSwerve is started again in every case.
param(
    [Parameter(Mandatory)][int]$AppPid,
    [Parameter(Mandatory)][string]$Zip,
    [Parameter(Mandatory)][string]$InstallDir,
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$LogFile
)

$ErrorActionPreference = 'Stop'

function Write-Log([string]$Message) {
    Add-Content -Path $LogFile -Value ('{0:s} {1}' -f (Get-Date), $Message)
}

# Copies the contents of a folder over another one, retrying while files are still locked.
function Copy-Tree([string]$From, [string]$To) {
    for ($try = 1; $try -le 30; $try++) {
        try {
            Copy-Item -Path (Join-Path $From '*') -Destination $To -Recurse -Force
            return
        } catch {
            if ($try -eq 30) { throw }
            Start-Sleep -Seconds 1
        }
    }
}

$work = Split-Path -Parent $Zip
$stage = Join-Path $work 'stage'
$backup = Join-Path $work 'backup'
$backedUp = $false
$copying = $false

try {
    Write-Log "Waiting for PktSwerve (pid $AppPid) to exit"
    $app = Get-Process -Id $AppPid -ErrorAction SilentlyContinue
    if ($app -and -not $app.WaitForExit(20000)) {
        # The app asked to quit before starting this script, so whatever is left is only teardown.
        Write-Log 'PktSwerve is still running after 20 s - ending it'
        Stop-Process -Id $AppPid -Force -ErrorAction SilentlyContinue
        if (-not $app.WaitForExit(10000)) { throw 'PktSwerve did not exit' }
    }

    # The WinDivert driver file stays locked while the driver is loaded.
    & sc.exe stop WinDivert 2>&1 | Out-Null

    Remove-Item -Recurse -Force $stage, $backup -ErrorAction SilentlyContinue
    Expand-Archive -Path $Zip -DestinationPath $stage -Force
    $newExe = Get-ChildItem -Path $stage -Recurse -Filter 'PktSwerve.exe' | Select-Object -First 1
    if (-not $newExe) { throw 'The package does not contain PktSwerve.exe' }
    $source = $newExe.Directory.FullName

    New-Item -ItemType Directory -Path $backup | Out-Null
    Copy-Tree $InstallDir $backup
    $backedUp = $true

    Write-Log "Copying the new files to $InstallDir"
    $copying = $true
    Copy-Tree $source $InstallDir
    $copying = $false
    Write-Log 'Update installed'
} catch {
    Write-Log "FAILED: $($_.Exception.Message)"
    if ($copying -and $backedUp) {
        try {
            Copy-Tree $backup $InstallDir
            Write-Log 'Previous version restored'
        } catch {
            Write-Log "Restore failed: $($_.Exception.Message)"
        }
    }
} finally {
    Remove-Item -Recurse -Force $stage, $backup -ErrorAction SilentlyContinue
    Start-Process -FilePath $Exe -WorkingDirectory $InstallDir
}
