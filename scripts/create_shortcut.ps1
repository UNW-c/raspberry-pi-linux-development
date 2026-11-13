<#
.SYNOPSIS
    Creates a desktop shortcut that opens the PRTS remote link console.

.DESCRIPTION
    The shortcut runs scripts\connect_prts.cmd, which prefers Windows Terminal
    and falls back to the classic console host.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\create_shortcut.ps1
#>
[CmdletBinding()]
param(
    [string]$ShortcutName = 'PRTS',
    [string]$Destination = [Environment]::GetFolderPath('Desktop')
)

$ErrorActionPreference = 'Stop'

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$launcher = Join-Path $scriptDirectory 'connect_prts.cmd'
if (-not (Test-Path -LiteralPath $launcher)) {
    Write-Error "launcher not found: $launcher"
    exit 1
}

$shortcutPath = Join-Path $Destination "$ShortcutName.lnk"
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $launcher
$shortcut.WorkingDirectory = $scriptDirectory
$shortcut.IconLocation = "$env:SystemRoot\System32\OpenSSH\ssh.exe,0"
$shortcut.Description = 'Open the PRTS remote link console (ssh to the Raspberry Pi)'
$shortcut.WindowStyle = 1
$shortcut.Save()

Write-Host "Created shortcut: $shortcutPath" -ForegroundColor Green
Write-Host 'Double click it to open the PRTS console.' -ForegroundColor Green
