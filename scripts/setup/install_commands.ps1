<#
.SYNOPSIS
    Adds the repository's bin directory to the user PATH so "prts-cam" can be
    typed from any directory.

.DESCRIPTION
    Only the current user's PATH is touched (HKCU\Environment). Open a new
    terminal afterwards; existing windows keep their old PATH.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\setup\install_commands.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\setup\install_commands.ps1 -Remove
#>
[CmdletBinding()]
param(
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = $scriptDirectory
while ($repositoryRoot -and -not (Test-Path -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt'))) {
    $repositoryRoot = Split-Path -Parent $repositoryRoot
}
$binDirectory = Join-Path $repositoryRoot 'bin'

if (-not (Test-Path -LiteralPath $binDirectory)) {
    Write-Error "bin directory not found: $binDirectory"
    exit 1
}

$current = [Environment]::GetEnvironmentVariable('Path', 'User')
if ($null -eq $current) { $current = '' }

$entries = $current -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
$exists = $entries | Where-Object { $_.TrimEnd('\') -ieq $binDirectory.TrimEnd('\') }

if ($Remove) {
    if (-not $exists) {
        Write-Host "  $binDirectory is not on the user PATH." -ForegroundColor DarkGray
        exit 0
    }
    $updated = ($entries | Where-Object { $_.TrimEnd('\') -ine $binDirectory.TrimEnd('\') }) -join ';'
    [Environment]::SetEnvironmentVariable('Path', $updated, 'User')
    Write-Host "  removed from user PATH: $binDirectory" -ForegroundColor Green
    Write-Host '  open a new terminal to take effect.' -ForegroundColor DarkGray
    exit 0
}

if ($exists) {
    Write-Host "  already on the user PATH: $binDirectory" -ForegroundColor DarkGray
}
else {
    $updated = (($entries + $binDirectory) -join ';')
    [Environment]::SetEnvironmentVariable('Path', $updated, 'User')
    Write-Host "  added to user PATH: $binDirectory" -ForegroundColor Green
}

Write-Host ''
Write-Host '  open a new terminal, then:' -ForegroundColor DarkGray
Write-Host '    prts-cam            # camera web page' -ForegroundColor DarkGray
Write-Host '    prts-cam status' -ForegroundColor DarkGray
Write-Host '    prts-cam stop' -ForegroundColor DarkGray
Write-Host ''
Write-Host '  current window (no new terminal):' -ForegroundColor DarkGray
Write-Host "    .\bin\prts-cam status" -ForegroundColor DarkGray
Write-Host ''
