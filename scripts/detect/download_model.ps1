<#
.SYNOPSIS
    Downloads the 奶龙 detection model (YOLOX, ONNX) into models/.

.DESCRIPTION
    The model belongs to nkxingxh/NailongDetection (AGPL-3.0) and comes from its
    v2.6 release. Only the tiny model is fetched: 20 MB, fast enough to run next
    to the camera view on this machine.

    GitHub is not always reachable from here, so three routes are tried in order:
    the local proxy (when one is listening), a direct connection, and the
    ghproxy.net mirror that also works without a proxy.

    models/ is not committed: every machine downloads its own copy.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\detect\download_model.ps1
#>
[CmdletBinding()]
param(
    [string]$Proxy = 'http://127.0.0.1:10808',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = $scriptDirectory
while ($repositoryRoot -and -not (Test-Path -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt'))) {
    $repositoryRoot = Split-Path -Parent $repositoryRoot
}

$targetDirectory = Join-Path $repositoryRoot 'models'
New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null

$base = 'https://github.com/nkxingxh/NailongDetection/releases/download/v2.6'
$files = @(
    @{ Name = 'nailong_v2.6_tiny.onnx'; Bytes = 20189246 }
    @{ Name = 'labels.txt'; Bytes = 61 }
)

function Test-ProxyListening {
    param([string]$ProxyUrl)

    if ([string]::IsNullOrWhiteSpace($ProxyUrl)) {
        return $false
    }
    try {
        $uri = [Uri]$ProxyUrl
        $client = New-Object System.Net.Sockets.TcpClient
        $task = $client.ConnectAsync($uri.Host, $uri.Port)
        $ok = $task.Wait(800) -and $client.Connected
        $client.Close()
        return $ok
    }
    catch {
        return $false
    }
}

$proxyUp = Test-ProxyListening -ProxyUrl $Proxy
if (-not $proxyUp) {
    Write-Host "  proxy $Proxy is not listening, going direct" -ForegroundColor DarkGray
}

function Save-RemoteFile {
    param([string]$Url, [string]$Path, [long]$Bytes)

    $routes = @()
    if ($proxyUp) {
        $routes += , @('--proxy', $Proxy, $Url)
    }
    $routes += , @($Url)
    $routes += , @("https://ghproxy.net/$Url")

    foreach ($route in $routes) {
        $source = $route[-1]
        $prefix = @($route[0..($route.Length - 2)])
        # GitHub over a slow link drops out mid-file, so every attempt resumes
        # where the previous one stopped instead of starting over.
        for ($attempt = 1; $attempt -le 4; $attempt++) {
            $have = if (Test-Path -LiteralPath $Path) { (Get-Item -LiteralPath $Path).Length } else { 0 }
            if ($have -ge $Bytes) {
                return $true
            }
            & curl.exe -sSL --fail --continue-at - --max-time 300 @prefix -o $Path $source
            $now = if (Test-Path -LiteralPath $Path) { (Get-Item -LiteralPath $Path).Length } else { 0 }
            if ($now -le $have) {
                break  # this route made no progress
            }
        }
        if ((Test-Path -LiteralPath $Path) -and (Get-Item -LiteralPath $Path).Length -ge $Bytes) {
            return $true
        }
    }
    return $false
}

foreach ($file in $files) {
    $path = Join-Path $targetDirectory $file.Name
    if (-not $Force -and (Test-Path -LiteralPath $path)) {
        $existing = (Get-Item -LiteralPath $path).Length
        if ($existing -eq $file.Bytes) {
            Write-Host "  $($file.Name): already there"
            continue
        }
    }

    Write-Host "  $($file.Name): downloading ..."
    if (-not (Save-RemoteFile -Url "$base/$($file.Name)" -Path $path -Bytes $file.Bytes)) {
        Write-Host "  $($file.Name): download failed on every route" -ForegroundColor Red
        exit 2
    }

    $size = (Get-Item -LiteralPath $path).Length
    if ($size -ne $file.Bytes) {
        Write-Host "  $($file.Name): got $size bytes, expected $($file.Bytes)" -ForegroundColor Red
        exit 3
    }
}

Write-Host ''
Write-Host "  model ready in $targetDirectory" -ForegroundColor Green
