# Removes PktSwerve completely: the running app, the "launch at startup" task, the WinDivert
# driver service, the program folder, settings and logs.
#
# Run it from the extracted folder (right-click > Run with PowerShell). It asks for administrator
# rights itself. PktSwerve's tray menu runs the same script through "Uninstall".
param(
    [string]$InstallDir = $PSScriptRoot,
    [switch]$Quiet
)

$identity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-InstallDir', "`"$InstallDir`"")
    if ($Quiet) { $argList += '-Quiet' }
    Start-Process -FilePath powershell.exe -Verb RunAs -ArgumentList $argList
    exit
}

$ErrorActionPreference = 'SilentlyContinue'

# End the app first; it keeps the driver and its own files locked.
Stop-ScheduledTask -TaskName 'PktSwerve'
Unregister-ScheduledTask -TaskName 'PktSwerve' -Confirm:$false
Get-Process -Name 'PktSwerve' | Stop-Process -Force
Get-Process -Name 'PktSwerve' | Wait-Process -Timeout 15

# The driver service is registered by WinDivert itself and is not removed with the files.
& sc.exe stop WinDivert | Out-Null
for ($i = 0; $i -lt 10; $i++) {
    if ((Get-Service -Name 'WinDivert') -eq $null -or (Get-Service -Name 'WinDivert').Status -eq 'Stopped') { break }
    Start-Sleep -Seconds 1
}
& sc.exe delete WinDivert | Out-Null

# Only delete a folder that really is a PktSwerve install.
if (Test-Path (Join-Path $InstallDir 'PktSwerve.exe')) {
    for ($i = 0; $i -lt 10 -and (Test-Path $InstallDir); $i++) {
        Remove-Item -Recurse -Force $InstallDir
        if (Test-Path $InstallDir) { Start-Sleep -Seconds 1 }
    }
}

Remove-Item -Recurse -Force (Join-Path $env:LOCALAPPDATA 'PktSwerve')
Remove-Item -Recurse -Force 'HKCU:\Software\PktSwerve'

if (-not $Quiet) {
    Write-Host 'PktSwerve has been removed.'
    Start-Sleep -Seconds 3
}
