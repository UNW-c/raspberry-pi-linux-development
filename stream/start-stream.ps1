<#
.SYNOPSIS
    PRTS video stream: V4L2 + FFmpeg + RTSP on the node, watched in the browser.

.DESCRIPTION
    Replaces the MQTT image path for the live picture:
      1. asks the node to run mediamtx (RTSP server) and the ffmpeg publisher
      2. opens ssh -L 127.0.0.1:8888 (HLS) and 127.0.0.1:8554 (RTSP)
      3. opens the HLS player page in the default browser
      4. keeps the tunnel alive until Ctrl+C

    The node side listens on loopback only, so the same command works on the
    LAN and through a tunneled SSH port.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File stream\start-stream.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File stream\start-stream.ps1 -Link tailscale -NoBrowser -Seconds 20

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File stream\start-stream.ps1 -NodeOptions "--copy"
#>
[CmdletBinding()]
param(
    [string]$ConfigPath = '',
    [int]$WebrtcPort = 8889,
    [int]$HlsPort = 8888,
    [int]$RtspPort = 8554,
    [ValidateSet('auto', 'lan', 'tailscale')]
    [string]$Link = 'auto',
    [ValidateSet('webrtc', 'hls')]
    [string]$Viewer = 'webrtc',
    [string]$NodeOptions = '',
    [switch]$NoBrowser,
    [int]$Seconds = 0
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
. (Join-Path $repositoryRoot 'scripts\lib\prts_remote.ps1')

if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $repositoryRoot 'config\connection.ini'
}
$values = Read-PrstIniFile -Path $ConfigPath
$hostName = Get-PrstIniValue $values 'ssh.host' 'PRTS.local'
$fallbackHost = Get-PrstIniValue $values 'ssh.fallback_host' 'PRTS'
$user = Get-PrstIniValue $values 'ssh.user' 'seven'
$password = Get-PrstIniValue $values 'ssh.password' '123456'
$port = [int](Get-PrstIniValue $values 'ssh.port' '22')
$connectTimeout = [int](Get-PrstIniValue $values 'ssh.connect_timeout' '10')

$candidates = Get-PrstLinkCandidates -RepositoryRoot $repositoryRoot -Primary $hostName `
    -Fallback $fallbackHost -Mode $Link
$endpoint = Resolve-PrstEndpoint -Candidates $candidates -Port $port
if ($null -eq $endpoint) {
    Write-Host '  PRTS is unreachable, nothing started.' -ForegroundColor Red
    exit 2
}

# The node always listens on these; the parameters above only shift the local
# side, so a second tunnel (say, over Tailscale) does not fight for the same
# local ports.
$NodeWebrtcPort = 8889
$NodeHlsPort = 8888
$NodeRtspPort = 8554

$target = "$user@$($endpoint.Address)"
$sshOptions = @(
    '-o', 'StrictHostKeyChecking=accept-new',
    '-o', 'NumberOfPasswordPrompts=1',
    '-o', "ConnectTimeout=$connectTimeout",
    '-p', "$port"
)

# The playlist is the readiness signal: the port can be open while mediamtx is
# still waiting for the publisher. mediamtx answers the playlist URL with a
# redirect, so -L is required.
function Test-StreamPage {
    try {
        $body = & curl.exe --noproxy '*' -sL --max-time 4 `
            "http://127.0.0.1:$HlsPort/prts/index.m3u8"
        return ($LASTEXITCODE -eq 0 -and ($body -join "`n") -match '#EXTM3U')
    }
    catch {
        return $false
    }
}

# WebRTC is the low latency viewer: signaling goes through the tunnel, the media
# itself travels directly over the LAN (UDP 8189).
function Test-WebrtcPage {
    try {
        $code = & curl.exe --noproxy '*' -s -o NUL -w '%{http_code}' --max-time 4 `
            "http://127.0.0.1:$WebrtcPort/prts/"
        return ($LASTEXITCODE -eq 0 -and $code -eq '200')
    }
    catch {
        return $false
    }
}

Write-PrstLinkPanel -Node "$user@$($endpoint.Name) ($($endpoint.Address))" -State 'STREAM' `
    -Detail "hls $HlsPort, rtsp $RtspPort"

# The detector reads the stream directly from the node instead of through the
# tunnel: same path as WebRTC, so both pictures stay at the same distance from
# the camera. Handing the address over through a file keeps the two scripts
# independent.
$stateDirectory = Join-Path $repositoryRoot '.tmp-ops'
New-Item -ItemType Directory -Force -Path $stateDirectory | Out-Null
Set-Content -LiteralPath (Join-Path $stateDirectory 'stream-rtsp.txt') `
    -Value "rtsp://$($endpoint.Address):$NodeRtspPort/prts" -Encoding ASCII
# The detector page reads this, so it always follows the tunnel that was started
# last without needing an argument.
Set-Content -LiteralPath (Join-Path $stateDirectory 'stream-whep.txt') `
    -Value "http://127.0.0.1:$WebrtcPort/prts/whep" -Encoding ASCII

$passwordState = New-PrstPasswordEnvironment -Password $password
$tunnel = $null

try {
    Write-Host '  starting the node side stream ...'
    $remoteCommand = "~/.local/bin/prts-stream start $NodeOptions"
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & ssh @sshOptions $target $remoteCommand 2>&1 |
            ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
    }
    finally {
        $ErrorActionPreference = $previous
    }

    $viewerReady = if ($Viewer -eq 'webrtc') { Test-WebrtcPage } else { Test-StreamPage }
    if ($viewerReady) {
        Write-Host "  a tunnel already serves the stream, reusing it." -ForegroundColor DarkGray
    }
    else {
        if (Test-PrstRemotePort -HostName '127.0.0.1' -Port $HlsPort -TimeoutMs 400) {
            $removed = Stop-PrstSshListenerOnPort -Port $HlsPort
            if ($removed -gt 0) {
                Write-Host "  closed $removed stale tunnel(s) on port $HlsPort." -ForegroundColor DarkGray
                Start-Sleep -Milliseconds 500
            }
        }

        Write-Host "  opening ssh tunnel webrtc $WebrtcPort->$NodeWebrtcPort, hls $HlsPort->$NodeHlsPort, rtsp $RtspPort->$NodeRtspPort ..."
        $arguments = @('-N') + $sshOptions + @(
            '-o', 'ExitOnForwardFailure=yes',
            '-o', 'ServerAliveInterval=20',
            '-L', "127.0.0.1:$WebrtcPort`:127.0.0.1:$NodeWebrtcPort",
            '-L', "127.0.0.1:$HlsPort`:127.0.0.1:$NodeHlsPort",
            '-L', "127.0.0.1:$RtspPort`:127.0.0.1:$NodeRtspPort",
            $target
        )
        $tunnelLog = Join-Path $env:TEMP ("prts-stream-" + [guid]::NewGuid().ToString('N') + ".log")
        $tunnel = Start-Process -FilePath 'ssh' -ArgumentList $arguments -WindowStyle Hidden `
            -PassThru -RedirectStandardError $tunnelLog

        $deadline = (Get-Date).AddSeconds(20)
        $ready = $false
        while ((Get-Date) -lt $deadline) {
            if ($Viewer -eq 'webrtc') {
                $ready = Test-WebrtcPage
            }
            else {
                $ready = Test-StreamPage
            }
            if ($ready) { break }
            if ($tunnel.HasExited) { break }
            Start-Sleep -Milliseconds 400
        }
        if (-not $ready) {
            Write-Host "  the stream page did not answer on port $HlsPort." -ForegroundColor Red
            if (Test-Path -LiteralPath $tunnelLog) {
                Get-Content -LiteralPath $tunnelLog -ErrorAction SilentlyContinue |
                    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
                    ForEach-Object { Write-Host "  ssh: $_" -ForegroundColor DarkYellow }
                Remove-Item -LiteralPath $tunnelLog -Force -ErrorAction SilentlyContinue
            }
            Write-Host '  check on the node: ~/.local/bin/prts-stream status' -ForegroundColor Yellow
            exit 3
        }
        Remove-Item -LiteralPath $tunnelLog -Force -ErrorAction SilentlyContinue
    }

    if ($Viewer -eq 'webrtc') {
        $url = "http://127.0.0.1:$WebrtcPort/prts"
        $note = 'WebRTC, about 200-500 ms behind the camera'
        $other = "http://127.0.0.1:$HlsPort/prts (HLS, seconds behind)"
    }
    else {
        $url = "http://127.0.0.1:$HlsPort/prts"
        $note = 'HLS, seconds behind the camera'
        $other = "http://127.0.0.1:$WebrtcPort/prts (WebRTC, much lower latency)"
    }
    Write-Host ''
    Write-Host "  browser: $url   ($note)" -ForegroundColor Green
    Write-Host "  also:    $other" -ForegroundColor DarkGray
    Write-Host "  rtsp:    rtsp://127.0.0.1:$RtspPort/prts (needs a player)" -ForegroundColor DarkGray
    if (-not $NoBrowser) {
        Start-Process $url
    }

    if ($Seconds -gt 0) {
        Write-Host "  (test mode: keeping the tunnel for $Seconds s)" -ForegroundColor DarkGray
        Start-Sleep -Seconds $Seconds
    }
    else {
        Write-Host '  Ctrl+C to close the tunnel.' -ForegroundColor DarkGray
        while ($true) { Start-Sleep -Seconds 1 }
    }
}
finally {
    if ($null -ne $tunnel -and -not $tunnel.HasExited) {
        Stop-Process -Id $tunnel.Id -Force -ErrorAction SilentlyContinue
    }
    Remove-PrstPasswordEnvironment -State $passwordState
}
