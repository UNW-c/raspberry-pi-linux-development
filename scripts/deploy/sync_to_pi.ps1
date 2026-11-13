<#
.SYNOPSIS
    Synchronises the project to the Raspberry Pi, builds and installs it there.

.DESCRIPTION
    Packs the working tree (git metadata and build output excluded), copies the
    archive over SSH, extracts it into the configured remote path and runs
    scripts/deploy/deploy.sh.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\deploy\sync_to_pi.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\deploy\sync_to_pi.ps1 -SkipBuild
#>
[CmdletBinding()]
param(
    [string]$ConfigPath = "",
    [string]$RemotePath = "",
    [switch]$SkipBuild,
    [switch]$SkipTests
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
$hostName = Get-PrstIniValue $values 'ssh.host' 'PRTS'
$fallbackHost = Get-PrstIniValue $values 'ssh.fallback_host' 'PRTS.local'
$user = Get-PrstIniValue $values 'ssh.user' 'seven'
$password = Get-PrstIniValue $values 'ssh.password' '123456'
$port = [int](Get-PrstIniValue $values 'ssh.port' '22')
$connectTimeout = [int](Get-PrstIniValue $values 'ssh.connect_timeout' '10')
if ([string]::IsNullOrWhiteSpace($RemotePath)) {
    $RemotePath = Get-PrstIniValue $values 'project.remote_path' "/home/$user/raspberry-pi-linux-development"
}

$candidates = Get-PrstHostCandidates -Primary $hostName -Fallback $fallbackHost
$endpoint = Resolve-PrstEndpoint -Candidates $candidates -Port $port
if ($null -eq $endpoint) {
    Write-Host '  PRTS unreachable, nothing synchronised.' -ForegroundColor Red
    Write-Host "  Tried: $($candidates -join ', ')" -ForegroundColor Yellow
    exit 2
}

Write-PrstLinkPanel -Node "$user@$($endpoint.Name) ($($endpoint.Address))" -State 'SYNC' `
    -Detail "target $RemotePath"

$passwordState = New-PrstPasswordEnvironment -Password $password
$archiveName = 'prts-source.tar.gz'
$archivePath = Join-Path $env:TEMP $archiveName

$sshCommon = @(
    '-o', 'StrictHostKeyChecking=accept-new',
    '-o', 'NumberOfPasswordPrompts=1',
    '-o', "ConnectTimeout=$connectTimeout"
)
# scp uses -P for the port, ssh uses -p.
$scpOptions = $sshCommon + @('-P', "$port")
$sshOptions = $sshCommon + @('-p', "$port")
$remoteTarget = "$user@$($endpoint.Address)"

try {
    if (Test-Path -LiteralPath $archivePath) {
        Remove-Item -LiteralPath $archivePath -Force
    }

    Write-Host '  packing sources ...'
    & tar.exe -czf $archivePath -C $repositoryRoot `
        --exclude=.git --exclude=build --exclude=.tmp-ops --exclude=.vscode .
    if ($LASTEXITCODE -ne 0) {
        throw "tar failed with exit code $LASTEXITCODE"
    }

    Write-Host '  uploading archive ...'
    & scp @scpOptions $archivePath "${remoteTarget}:/tmp/$archiveName"
    if ($LASTEXITCODE -ne 0) {
        throw "scp failed with exit code $LASTEXITCODE"
    }

    $deployArguments = @()
    if ($SkipTests) {
        $deployArguments += '--skip-tests'
    }

    if ($SkipBuild) {
        $remoteCommand = "mkdir -p '$RemotePath' && rm -rf '$RemotePath/scripts' && tar -xzf /tmp/$archiveName -C '$RemotePath' && echo EXTRACTED"
    }
    else {
        $remoteCommand = "mkdir -p '$RemotePath' && rm -rf '$RemotePath/scripts' && tar -xzf /tmp/$archiveName -C '$RemotePath' && cd '$RemotePath' && bash scripts/deploy/deploy.sh $($deployArguments -join ' ')"
    }

    Write-Host '  building on the Pi ...'
    & ssh @sshOptions $remoteTarget $remoteCommand
    $exitCode = $LASTEXITCODE
}
finally {
    Remove-PrstPasswordEnvironment -State $passwordState
    Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
}

if ($exitCode -ne 0) {
    Write-Host "  remote step failed with exit code $exitCode" -ForegroundColor Red
    exit $exitCode
}

Write-Host ''
Write-Host '  PRTS deploy complete. Open a new ssh session to see the sequence.' -ForegroundColor Green
exit 0
