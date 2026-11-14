<#
.SYNOPSIS
    Runs the 奶龙 detector on this machine.

.DESCRIPTION
    Uses a Python that already has OpenCV and numpy - an existing environment is
    reused as it is, nothing is installed twice. Only when no such Python is
    found does the script create .venv-detect and install the requirements.

    Inference happens here, not on the node: the Pi 3B would spend its whole CPU
    on the detector and the camera view would suffer.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\detect\run_detect.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\detect\run_detect.ps1 --http 18081

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\detect\run_detect.ps1 -Python C:\Users\me\myenv\Scripts\python.exe
#>
[CmdletBinding()]
param(
    [string]$Python = "",
    [string]$Proxy = '',
    [switch]$Setup,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Arguments = @()
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

function Test-DetectionPython {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path)) {
        return $false
    }
    # A python without the packages writes a traceback on stderr, which would
    # abort the script while ErrorActionPreference is Stop.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Path -c 'import cv2, numpy' *> $null
        return ($LASTEXITCODE -eq 0)
    }
    finally {
        $ErrorActionPreference = $previous
    }
}

$candidates = @()
if (-not [string]::IsNullOrWhiteSpace($Python)) { $candidates += $Python }
$candidates += (Join-Path $repositoryRoot '.venv-detect\Scripts\python.exe')
$candidates += (Join-Path $env:USERPROFILE 'myenv\Scripts\python.exe')

$python = $null
foreach ($candidate in $candidates) {
    if (Test-DetectionPython -Path $candidate) {
        $python = $candidate
        break
    }
}

if ($null -eq $python) {
    # Last resort: any interpreter on PATH that already has the two packages.
    foreach ($name in @('python', 'python3')) {
        $found = (Get-Command $name -ErrorAction SilentlyContinue).Source
        if ($found -and (Test-DetectionPython -Path $found)) {
            $python = $found
            break
        }
    }
}

if ($null -eq $python) {
    $venv = Join-Path $repositoryRoot '.venv-detect'
    $python = Join-Path $venv 'Scripts\python.exe'
    Write-Host '  no Python with OpenCV found, preparing .venv-detect ...' -ForegroundColor Yellow
    if (-not (Test-Path -LiteralPath $python)) {
        $launcher = $null
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            foreach ($candidate in @('3.13', '3.12', '3.11')) {
                & py "-$candidate" -c 'import sys' *> $null
                if ($LASTEXITCODE -eq 0) { $launcher = $candidate; break }
            }
        }
        finally {
            $ErrorActionPreference = $previous
        }
        if ($null -eq $launcher) {
            Write-Host '  install Python 3.11+ first' -ForegroundColor Red
            exit 2
        }
        & py "-$launcher" -m venv $venv
    }
    if ($Proxy) {
        $env:HTTPS_PROXY = $Proxy
        $env:HTTP_PROXY = $Proxy
    }
    try {
        & $python -m pip install --disable-pip-version-check -q -r (Join-Path $scriptDirectory 'requirements.txt')
        if ($LASTEXITCODE -ne 0) {
            Write-Host '  pip failed: no network and no local wheels' -ForegroundColor Red
            exit 3
        }
    }
    finally {
        Remove-Item Env:HTTPS_PROXY -ErrorAction SilentlyContinue
        Remove-Item Env:HTTP_PROXY -ErrorAction SilentlyContinue
    }
}

if ($Setup) {
    Write-Host "  python: $python"
    exit 0
}

$model = Join-Path $repositoryRoot 'models\nailong_v2.6_tiny.onnx'
if (-not (Test-Path -LiteralPath $model)) {
    & (Join-Path $scriptDirectory 'download_model.ps1') -Proxy $Proxy
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Push-Location $repositoryRoot
try {
    & $python -u (Join-Path $scriptDirectory 'detect_nailong.py') @Arguments
    $exitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}
exit $exitCode
