<#
.SYNOPSIS
    PRTS remote link console - opens an SSH session to the Raspberry Pi.

.DESCRIPTION
    Reads config/connection.ini, picks the first reachable host name, prints a
    PRTS style link panel and then hands the terminal over to ssh. The password
    is delivered through SSH_ASKPASS, so the operator never types it.

    The script makes a single attempt. When the link fails it prints a
    diagnosis and exits; it never retries in a loop.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\connect_prts.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\connect_prts.ps1 -Command "uname -a"
#>
[CmdletBinding()]
param(
    [string]$ConfigPath = "",
    [string]$Command = "",
    [switch]$NoBanner
)

$ErrorActionPreference = 'Stop'

try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $OutputEncoding = [System.Text.Encoding]::UTF8
}
catch {
    # Encoding hints are best effort only.
}

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDirectory 'lib\prts_remote.ps1')

$repositoryRoot = Split-Path -Parent $scriptDirectory
if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $repositoryRoot 'config\connection.ini'
}

$values = Read-PrstIniFile -Path $ConfigPath
$hostName = Get-PrstIniValue $values 'ssh.host' 'PRTS'
$fallbackHost = Get-PrstIniValue $values 'ssh.fallback_host' 'PRTS.local'
$user = Get-PrstIniValue $values 'ssh.user' 'seven'
$password = Get-PrstIniValue $values 'ssh.password' '123456'
$port = [int](Get-PrstIniValue $values 'ssh.port' '22')
$connectTimeout = [int](Get-PrstIniValue $values 'ssh.connect_timeout' '10')

$candidates = @($hostName, $fallbackHost, "$hostName.local") |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
    Select-Object -Unique

$endpoint = Resolve-PrstEndpoint -Candidates $candidates -Port $port

if ($null -eq $endpoint) {
    if (-not $NoBanner) {
        Write-PrstLinkPanel -Node "$user@$hostName`:$port" -State 'LINK FAILED' `
            -Detail 'no candidate answered on the ssh port' -TitleColor Red
    }
    Write-Host '  PRTS link could not be established.' -ForegroundColor Red
    Write-Host ''
    Write-Host '  Candidates tried:' -ForegroundColor Yellow
    foreach ($candidate in $candidates) {
        Write-Host "    - $candidate`:$port"
    }
    Write-Host ''
    Write-Host '  Check, in this order:' -ForegroundColor Yellow
    Write-Host '    1. the Pi is powered on and the activity LED is blinking'
    Write-Host '    2. this PC and the Pi are on the same network segment'
    Write-Host '    3. ping PRTS.local    (mDNS resolution)'
    Write-Host '    4. if mDNS is unavailable, set the fixed IP in config\connection.ini'
    Write-Host ''
    exit 2
}

if (-not $NoBanner) {
    Write-PrstLinkPanel -Node "$user@$($endpoint.Name):$($endpoint.Address):$port" -State 'HANDSHAKE' `
        -Detail 'password forwarded through SSH_ASKPASS'
}

$passwordState = New-PrstPasswordEnvironment -Password $password
$stderrFile = Join-Path $env:TEMP ('prts-ssh-' + [guid]::NewGuid().ToString('N') + '.log')

$sshArguments = @(
    '-o', 'StrictHostKeyChecking=accept-new',
    '-o', 'NumberOfPasswordPrompts=1',
    '-o', "ConnectTimeout=$connectTimeout",
    '-o', 'ServerAliveInterval=30',
    '-p', "$port"
)

if ([string]::IsNullOrWhiteSpace($Command)) {
    $sshArguments += @('-t', "$user@$($endpoint.Address)")
}
else {
    $sshArguments += @("$user@$($endpoint.Address)", $Command)
}

try {
    & ssh @sshArguments 2> $stderrFile
    $exitCode = $LASTEXITCODE
}
finally {
    Remove-PrstPasswordEnvironment -State $passwordState
}

if ($exitCode -eq 255) {
    Write-Host ''
    Write-Host '  PRTS link dropped during handshake or authentication.' -ForegroundColor Red
    if (Test-Path -LiteralPath $stderrFile) {
        $details = Get-Content -LiteralPath $stderrFile -Raw
        if (-not [string]::IsNullOrWhiteSpace($details)) {
            Write-Host ('  ssh said: ' + $details.Trim()) -ForegroundColor DarkYellow
        }
        Remove-Item -LiteralPath $stderrFile -Force -ErrorAction SilentlyContinue
    }
    Write-Host '  Verify the operator password in config\connection.ini and that sshd is running.' -ForegroundColor Yellow
    exit 255
}

Remove-Item -LiteralPath $stderrFile -Force -ErrorAction SilentlyContinue
exit $exitCode
