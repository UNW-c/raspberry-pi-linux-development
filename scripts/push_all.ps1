<#
.SYNOPSIS
    Commits the working tree and pushes it to both remotes.

.DESCRIPTION
    gitee is reached over SSH, github over HTTPS through the local proxy
    configured in config/connection.ini ([git] github_proxy). The proxy is only
    exported for the github push, so the rest of the session is unaffected.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\push_all.ps1 -Message "feat: ..."

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\push_all.ps1 -NoCommit
#>
[CmdletBinding()]
param(
    [string]$Message = "",
    [switch]$NoCommit,
    [string]$Proxy = "",
    [string]$ConfigPath = ""
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
. (Join-Path $scriptDirectory 'lib\prts_remote.ps1')

$repositoryRoot = Split-Path -Parent $scriptDirectory
if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $repositoryRoot 'config\connection.ini'
}
$values = Read-PrstIniFile -Path $ConfigPath
$proxyUrl = if ([string]::IsNullOrWhiteSpace($Proxy)) {
    Get-PrstIniValue $values 'git.github_proxy' 'http://127.0.0.1:10808'
}
else {
    $Proxy
}

Push-Location $repositoryRoot
# Git writes progress to stderr; treat those messages as output, not failures.
$ErrorActionPreference = 'Continue'
try {
    if (-not $NoCommit) {
        if ([string]::IsNullOrWhiteSpace($Message)) {
            Write-Error 'a commit message is required unless -NoCommit is used'
            exit 2
        }
        & git add -A
        & git commit -m $Message
        if ($LASTEXITCODE -ne 0) {
            Write-Host '  nothing to commit, pushing the existing HEAD' -ForegroundColor Yellow
        }
    }

    $head = (& git rev-parse --short HEAD).Trim()
    Write-PrstLinkPanel -Node "HEAD $head" -State 'PUSH' `
        -Detail "gitee ssh, github https via $proxyUrl"

    & git push gitee main 2>&1 | ForEach-Object { Write-Host "  gitee  $_" }
    $giteeCode = $LASTEXITCODE

    $previousProxy = $env:HTTPS_PROXY
    $env:HTTPS_PROXY = $proxyUrl
    try {
        & git -c http.sslBackend=openssl push github main 2>&1 | ForEach-Object { Write-Host "  github $_" }
        $githubCode = $LASTEXITCODE
    }
    finally {
        $env:HTTPS_PROXY = $previousProxy
    }

    Write-Host ''
    if ($giteeCode -eq 0) {
        Write-Host '  gitee  : pushed' -ForegroundColor Green
    }
    else {
        Write-Host "  gitee  : failed (exit $giteeCode)" -ForegroundColor Red
    }
    if ($githubCode -eq 0) {
        Write-Host '  github : pushed' -ForegroundColor Green
    }
    else {
        Write-Host "  github : failed (exit $githubCode) - is the proxy listening on $proxyUrl ?" -ForegroundColor Red
    }

    if ($giteeCode -ne 0 -or $githubCode -ne 0) {
        exit 1
    }
}
finally {
    Pop-Location
}

exit 0
