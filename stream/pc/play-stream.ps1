<#
.SYNOPSIS
    Plays the node's RTSP stream with ffplay, tuned for the lowest latency.

.DESCRIPTION
    The browser viewers go through HLS (seconds) or WebRTC (a few hundred ms).
    ffplay pulls RTSP directly and can start rendering a frame as soon as it is
    decoded, so this is the quickest path available on this machine.

    ffplay is looked up on PATH first, then in the local tools folder that
    install-ffmpeg.ps1 uses.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File stream\pc\play-stream.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File stream\pc\play-stream.ps1 -RtspUrl rtsp://127.0.0.1:8554/prts
#>
[CmdletBinding()]
param(
    [string]$RtspUrl = 'rtsp://127.0.0.1:8554/prts',
    [int]$Width = 960,
    [switch]$Wide,
    [switch]$NoTuning
)

$ErrorActionPreference = 'Stop'

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = $scriptDirectory
while ($repositoryRoot -and -not (Test-Path -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt'))) {
    $repositoryRoot = Split-Path -Parent $repositoryRoot
}

function Find-Ffplay {
    $onPath = (Get-Command ffplay -ErrorAction SilentlyContinue).Source
    if ($onPath) { return $onPath }

    $tools = Join-Path $env:LOCALAPPDATA 'prts\ffmpeg'
    $candidates = @(Get-ChildItem -Path $tools -Filter ffplay.exe -Recurse -ErrorAction SilentlyContinue)
    if ($candidates.Count -gt 0) { return $candidates[0].FullName }

    $portable = Join-Path $repositoryRoot '.tmp-ops\ffmpeg\ffplay.exe'
    if (Test-Path -LiteralPath $portable) { return $portable }
    return $null
}

$ffplay = Find-Ffplay
if ($null -eq $ffplay) {
    Write-Host '  ffplay not found - run: powershell -File stream\pc\install-ffmpeg.ps1' -ForegroundColor Red
    exit 2
}

# These flags are what actually removes the delay: no input buffering, no
# probing pause, and late frames are dropped instead of queued.
$latencyArgs = @(
    '-fflags', 'nobuffer',
    '-flags', 'low_delay',
    '-probesize', '32',
    '-analyzeduration', '0',
    '-framedrop',
    '-sync', 'ext'
)
if ($NoTuning) { $latencyArgs = @() }

$arguments = @('-hide_banner', '-loglevel', 'warning', '-rtsp_transport', 'tcp') +
    $latencyArgs + @('-window_title', 'PRTS / STREAM', '-i', $RtspUrl)
if (-not $Wide -and $Width -gt 0) {
    $height = [int]($Width * 480 / 640)
    $arguments = @('-x', "$Width", '-y', "$height") + $arguments
}

Write-Host "  ffplay: $ffplay"
Write-Host "  source: $RtspUrl"
Write-Host '  press q or Esc to close'
& $ffplay @arguments
exit $LASTEXITCODE
