<#
.SYNOPSIS
    Opens the detector page: the WebRTC picture plus the detection overlay.

.DESCRIPTION
    Serves stream/web on http://127.0.0.1:8890, where the page pulls the same
    WebRTC stream the browser shows (through a same-origin WHEP proxy) and draws
    boxes on top. Detection runs separately from the display, so the picture
    keeps the browser's smoothness.

    Needs start-stream.ps1 running: it opens the tunnel that carries the WHEP
    handshake (port 8889).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File stream\start-detect-web.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File stream\start-detect-web.ps1 -Port 8891 -NoBrowser
#>
[CmdletBinding()]
param(
    [int]$Port = 8891,
    [string]$Whep = '',
    [string]$Python = '',
    [switch]$NoBrowser
)

$ErrorActionPreference = 'Stop'

try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $OutputEncoding = [System.Text.Encoding]::UTF8
}
catch {
    # Best effort only.
}

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = $scriptDirectory
while ($repositoryRoot -and -not (Test-Path -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt'))) {
    $repositoryRoot = Split-Path -Parent $repositoryRoot
}

# Follow whichever tunnel start-stream.ps1 opened last (LAN and Tailscale use
# different local ports), unless one was passed explicitly.
if ([string]::IsNullOrWhiteSpace($Whep)) {
    $stateFile = Join-Path $repositoryRoot '.tmp-ops\stream-whep.txt'
    if (Test-Path -LiteralPath $stateFile) {
        $Whep = (Get-Content -LiteralPath $stateFile -Raw).Trim()
    }
    if ([string]::IsNullOrWhiteSpace($Whep)) {
        $Whep = 'http://127.0.0.1:8889/prts/whep'
    }
}

function Find-Python {
    param([string]$Explicit)

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) { $candidates += $Explicit }
    $candidates += (Join-Path $env:USERPROFILE 'myenv\Scripts\python.exe')
    $candidates += (Join-Path $repositoryRoot '.venv-detect\Scripts\python.exe')

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    foreach ($name in @('python', 'python3')) {
        $found = (Get-Command $name -ErrorAction SilentlyContinue).Source
        if ($found) { return $found }
    }
    return $null
}

$python = Find-Python -Explicit $Python
if ($null -eq $python) {
    Write-Host '  no Python found; it is only needed to serve the page' -ForegroundColor Red
    exit 2
}

$serveScript = Join-Path $scriptDirectory 'web\serve.py'

# An orphaned server from an earlier run can hold the port; each shell in this
# environment sees a different PID view, so it may not be killable. Walk up to
# the first free port instead of failing.
function Test-PortFree {
    param([int]$Candidate)

    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $task = $client.ConnectAsync('127.0.0.1', $Candidate)
        if ($task.Wait(300) -and $client.Connected) {
            return $false
        }
        return $true
    }
    catch {
        return $true
    }
    finally {
        $client.Close()
    }
}

$requested = $Port
for ($attempt = 0; $attempt -lt 10; $attempt++) {
    if (Test-PortFree -Candidate $Port) { break }
    $Port += 1
}
if ($Port -ne $requested) {
    Write-Host "  port $requested is taken, using $Port instead" -ForegroundColor Yellow
}

$url = "http://127.0.0.1:$Port/"

# The page can only play what the tunnel forwards, so say it up front instead of
# letting it fail silently in the browser.
$code = & curl.exe --noproxy '*' -s -o NUL -w '%{http_code}' --max-time 3 `
    'http://127.0.0.1:8889/prts/'
if ($LASTEXITCODE -ne 0 -or $code -ne '200') {
    Write-Host '  warning: the WebRTC endpoint on 127.0.0.1:8889 is not answering.' -ForegroundColor Yellow
    Write-Host '           run stream\start-stream.bat first (it opens that tunnel).' -ForegroundColor Yellow
    Write-Host ''
}

Write-Host ''
Write-Host "  page:  $url"
Write-Host "  whep:  /whep -> $Whep"
Write-Host '  Ctrl+C to stop' -ForegroundColor DarkGray
Write-Host ''

$server = Start-Process -FilePath $python `
    -ArgumentList @('-u', $serveScript, '--port', "$Port", '--whep', $Whep) `
    -WindowStyle Hidden -PassThru

try {
    Start-Sleep -Milliseconds 900
    if ($server.HasExited) {
        Write-Host "  the page server exited at once (port $Port taken?)" -ForegroundColor Red
        exit 3
    }
    if (-not $NoBrowser) {
        Start-Process $url
    }
    while ($true) {
        Start-Sleep -Seconds 1
        if ($server.HasExited) { break }
    }
}
finally {
    if (-not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
}
