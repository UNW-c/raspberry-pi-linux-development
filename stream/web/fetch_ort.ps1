<#
.SYNOPSIS
    Fetches the ONNX Runtime Web runtime into stream/web/vendor/ort.

.DESCRIPTION
    The detector page runs YOLOX inside the browser, which needs Microsoft's
    onnxruntime-web WASM runtime (MIT). It is ~11 MB of binary, so it is not
    committed: every machine fetches its own copy.

    The model itself is not downloaded here - it is the same
    models/nailong_v2.6_tiny.onnx the desktop detector already uses.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File stream\web\fetch_ort.ps1
#>
[CmdletBinding()]
param(
    [string]$Version = '1.20.1',
    [string]$Registry = 'https://registry.npmmirror.com',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$target = Join-Path $scriptDirectory 'vendor\ort'
$wasm = Join-Path $target 'ort-wasm-simd-threaded.wasm'

if ((Test-Path -LiteralPath $wasm) -and -not $Force) {
    Write-Host "  already there: $target"
    exit 0
}

$work = Join-Path $env:TEMP ('prts-ort-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null

Write-Host "  fetching onnxruntime-web $Version from $Registry ..."
& npm pack "onnxruntime-web@$Version" --pack-destination $work --registry $Registry
if ($LASTEXITCODE -ne 0) {
    Write-Host '  npm pack failed (no network or registry unreachable)' -ForegroundColor Red
    exit 2
}

$tarball = Get-ChildItem -Path $work -Filter 'onnxruntime-web-*.tgz' | Select-Object -First 1
if ($null -eq $tarball) {
    Write-Host '  the package archive was not produced' -ForegroundColor Red
    exit 3
}

$wanted = @(
    'package/dist/ort.wasm.min.mjs',
    'package/dist/ort-wasm-simd-threaded.mjs',
    'package/dist/ort-wasm-simd-threaded.wasm'
)
& tar.exe -xzf $tarball.FullName -C $work @wanted
if ($LASTEXITCODE -ne 0) {
    Write-Host '  extracting failed' -ForegroundColor Red
    exit 4
}

New-Item -ItemType Directory -Force -Path $target | Out-Null
foreach ($file in $wanted) {
    Copy-Item -LiteralPath (Join-Path $work $file) -Destination $target -Force
}

Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue

Get-ChildItem -Path $target | ForEach-Object {
    Write-Host ("  {0,-32} {1,7:n1} MB" -f $_.Name, ($_.Length / 1MB))
}
Write-Host ''
Write-Host "  ready: $target" -ForegroundColor Green
