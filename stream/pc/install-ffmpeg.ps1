<#
.SYNOPSIS
    Installs FFmpeg (with ffplay) for this user, without touching PATH unless asked.

.DESCRIPTION
    Downloads the official Windows build from gyan.dev, unpacks it to
    %LOCALAPPDATA%\prts\ffmpeg and leaves it there. play-stream.ps1 finds it on
    its own, so nothing has to be added to PATH unless -AddToPath is used.

    ffplay is the lowest latency viewer for this project: the browser paths add
    either HLS segments (seconds) or a WebRTC jitter buffer (hundreds of ms).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File stream\pc\install-ffmpeg.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File stream\pc\install-ffmpeg.ps1 -AddToPath
#>
[CmdletBinding()]
param(
    [string]$Version = '9.0.2',
    [string]$Proxy = 'http://127.0.0.1:10808',
    [switch]$AddToPath,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $OutputEncoding = [System.Text.Encoding]::UTF8
}
catch {
    # Best effort only.
}

$target = Join-Path $env:LOCALAPPDATA 'prts\ffmpeg'
$ffplay = Get-ChildItem -Path $target -Filter ffplay.exe -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1

if ($null -ne $ffplay -and -not $Force) {
    Write-Host "  already installed: $($ffplay.FullName)"
    exit 0
}

$url = "https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-$Version-essentials_build.zip"
$archive = Join-Path $env:TEMP "ffmpeg-$Version-essentials.zip"

function Test-ProxyListening {
    param([string]$ProxyUrl)

    if ([string]::IsNullOrWhiteSpace($ProxyUrl)) { return $false }
    try {
        $uri = [Uri]$ProxyUrl
        $client = New-Object System.Net.Sockets.TcpClient
        $ok = $client.ConnectAsync($uri.Host, $uri.Port).Wait(800) -and $client.Connected
        $client.Close()
        return $ok
    }
    catch {
        return $false
    }
}

$proxyArgs = @()
if (Test-ProxyListening -ProxyUrl $Proxy) {
    $proxyArgs = @('-x', $Proxy)
}

Write-Host "  downloading FFmpeg $Version (about 110 MB, official build)"
for ($attempt = 1; $attempt -le 6; $attempt++) {
    $have = if (Test-Path -LiteralPath $archive) { (Get-Item -LiteralPath $archive).Length } else { 0 }
    Write-Host ("    attempt {0}, {1:n0} MB so far" -f $attempt, ($have / 1MB))
    & curl.exe -sL --fail --continue-at - --max-time 600 @proxyArgs -o $archive $url
    if ($LASTEXITCODE -eq 0) { break }
}
if (-not (Test-Path -LiteralPath $archive)) {
    Write-Host '  download failed: check the network (or start the proxy)' -ForegroundColor Red
    exit 2
}

$size = (Get-Item -LiteralPath $archive).Length
if ($size -lt 50MB) {
    Write-Host "  the archive is only $([math]::Round($size/1MB,1)) MB, download looks incomplete" -ForegroundColor Red
    exit 3
}

Write-Host '  unpacking ...'
$staging = Join-Path $env:TEMP ('prts-ffmpeg-' + [guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath $archive -DestinationPath $staging -Force

$root = Get-ChildItem -Path $staging -Directory | Select-Object -First 1
if ($null -eq $root) {
    Write-Host '  the archive had no folder inside' -ForegroundColor Red
    exit 4
}

if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
Move-Item -LiteralPath $root.FullName -Destination $target
Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue

$ffplay = Get-ChildItem -Path $target -Filter ffplay.exe -Recurse | Select-Object -First 1
if ($null -eq $ffplay) {
    Write-Host '  ffplay.exe missing after unpacking' -ForegroundColor Red
    exit 5
}

if ($AddToPath) {
    $binDirectory = $ffplay.DirectoryName
    $current = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (($current -split ';') -notcontains $binDirectory) {
        [Environment]::SetEnvironmentVariable('Path', ($current.TrimEnd(';') + ';' + $binDirectory), 'User')
        Write-Host "  added to the user PATH: $binDirectory (open a new terminal)"
    }
}

Write-Host ''
Write-Host "  installed: $($ffplay.FullName)" -ForegroundColor Green
Write-Host "  play:      powershell -File stream\pc\play-stream.ps1"
