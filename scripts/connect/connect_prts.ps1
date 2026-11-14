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
    powershell -ExecutionPolicy Bypass -File scripts\connect\connect_prts.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\connect\connect_prts.ps1 -Command "uname -a"
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
$repositoryRoot = $scriptDirectory
while ($repositoryRoot -and -not (Test-Path -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt'))) {
    $repositoryRoot = Split-Path -Parent $repositoryRoot
}
. (Join-Path $repositoryRoot 'scripts\lib\prts_remote.ps1')

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
$windowColumns = [int](Get-PrstIniValue $values 'window.columns' '120')
$windowRows = [int](Get-PrstIniValue $values 'window.rows' '38')

if (-not $NoBanner) {
    Set-PrstConsoleSize -Columns $windowColumns -Rows $windowRows
}

$candidates = Get-PrstLinkCandidates -RepositoryRoot $repositoryRoot -Primary $hostName `
    -Fallback $fallbackHost

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
    $detail = if ($endpoint.Name -ne $endpoint.Address) {
        "$($endpoint.Name) -> $($endpoint.Address)"
    }
    else {
        'password via SSH_ASKPASS'
    }
    Write-PrstLinkPanel -Node "$user@$($endpoint.Address):$port" -State 'HANDSHAKE' -Detail $detail
    Write-Host '  camera web: http://127.0.0.1:18080/  (run prts-web on the node)' -ForegroundColor DarkGray
    Write-Host ''
}

$passwordState = New-PrstPasswordEnvironment -Password $password
$stderrFile = Join-Path $env:TEMP ('prts-ssh-' + [guid]::NewGuid().ToString('N') + '.log')

$sshArguments = @(
    '-o', 'StrictHostKeyChecking=accept-new',
    '-o', 'NumberOfPasswordPrompts=1',
    '-o', "ConnectTimeout=$connectTimeout",
    '-o', 'ServerAliveInterval=30',
    '-o', 'ExitOnForwardFailure=no',
    # Camera web viewer: the node serves it on loopback, the tunnel exposes it
    # locally. 18080 is used because 8080 is often taken on the desktop.
    '-L', '127.0.0.1:18080:127.0.0.1:8080',
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
