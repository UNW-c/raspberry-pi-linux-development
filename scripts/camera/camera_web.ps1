<#
.SYNOPSIS
    Opens the camera web viewer: starts the node-side stack, opens the SSH
    tunnel and launches the browser.

.DESCRIPTION
    One click path for the camera page:
      1. asks the node to run prts-camera + prts-web (idempotent)
      2. opens ssh -L 127.0.0.1:18080:127.0.0.1:8080 in the background
      3. opens http://127.0.0.1:18080/ in the default browser
      4. keeps the tunnel alive until you press Ctrl+C

    Because the tunnel rides on the existing SSH connection, the same command
    works on the LAN and through a NAT/tunnel exposed SSH port.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\camera\camera_web.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\camera\camera_web.ps1 -NoBrowser -Seconds 15
#>
[CmdletBinding()]
param(
    [string]$ConfigPath = '',
    [int]$LocalPort = 18080,
    [int]$RemotePort = 8080,
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
    -Fallback $fallbackHost
$endpoint = Resolve-PrstEndpoint -Candidates $candidates -Port $port
if ($null -eq $endpoint) {
    Write-Host '  PRTS is unreachable, nothing started.' -ForegroundColor Red
    exit 2
}

$target = "$user@$($endpoint.Address)"
$sshOptions = @(
    '-o', 'StrictHostKeyChecking=accept-new',
    '-o', 'NumberOfPasswordPrompts=1',
    '-o', "ConnectTimeout=$connectTimeout",
    '-p', "$port"
)

# A previous run can leave an ssh -N forward behind. It is harmless while the
# node serves the page, but it points nowhere once prts-web stops, which shows
# up in the browser as ERR_CONNECTION_RESET. Only ssh processes listening on our
# local port are closed, and only when the page is not answering.
function Test-CameraPage {
    return (Test-PrstHttpPage -Port $LocalPort -Path '/status' -Pattern '"frames"') -ge 0
}

function Stop-StaleCameraTunnels {
    if (-not (Test-PrstRemotePort -HostName '127.0.0.1' -Port $LocalPort -TimeoutMs 400)) {
        return 0
    }
    return (Stop-PrstSshListenerOnPort -Port $LocalPort)
}

# The port may be open but dead. Ask the endpoint for /status instead. curl is
# used on purpose: Invoke-WebRequest would honour the system proxy for localhost.
function Test-CameraPage {
    try {
        $content = & curl.exe --noproxy '*' -s --max-time 3 "http://127.0.0.1:$LocalPort/status"
        return ($LASTEXITCODE -eq 0 -and $null -ne $content -and ($content -join '') -match '"frames"')
    }
    catch {
        return $false
    }
}

Write-PrstLinkPanel -Node "$target`:$port" -State 'CAMERA' `
    -Detail "local $LocalPort -> node $RemotePort"

$passwordState = New-PrstPasswordEnvironment -Password $password
$tunnel = $null

try {
    Write-Host '  starting the node side stack ...'
    & ssh @sshOptions $target '~/.local/bin/prts-camera-web start' 2>&1 |
        ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }

    if (Test-CameraPage) {
        Write-Host "  an existing tunnel on $LocalPort already serves the page, reusing it." -ForegroundColor DarkGray
    }
    else {
        $removed = Stop-StaleCameraTunnels
        if ($removed -gt 0) {
            Write-Host "  closed $removed stale tunnel(s) on port $LocalPort." -ForegroundColor DarkGray
            Start-Sleep -Milliseconds 500
        }

        Write-Host "  opening ssh tunnel 127.0.0.1:$LocalPort -> 127.0.0.1:$RemotePort ..."
        $arguments = @('-N') + $sshOptions + @(
            '-o', 'ExitOnForwardFailure=yes',
            '-L', "127.0.0.1:$LocalPort`:127.0.0.1:$RemotePort",
            $target
        )
        $tunnelLog = Join-Path $env:TEMP ("prts-tunnel-" + [guid]::NewGuid().ToString('N') + ".log")
        $tunnel = Start-Process -FilePath 'ssh' -ArgumentList $arguments -WindowStyle Hidden `
            -PassThru -RedirectStandardError $tunnelLog

        $deadline = (Get-Date).AddSeconds(12)
        $ready = $false
        while ((Get-Date) -lt $deadline) {
            if (Test-CameraPage) {
                $ready = $true
                break
            }
            if ($tunnel.HasExited) { break }
            Start-Sleep -Milliseconds 300
        }
        if (-not $ready) {
            Write-Host "  tunnel did not come up on port $LocalPort." -ForegroundColor Red
            if (Test-Path -LiteralPath $tunnelLog) {
                Get-Content -LiteralPath $tunnelLog -ErrorAction SilentlyContinue |
                    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
                    ForEach-Object { Write-Host "  ssh: $_" -ForegroundColor DarkYellow }
                Remove-Item -LiteralPath $tunnelLog -Force -ErrorAction SilentlyContinue
            }
            Write-Host '  close whatever holds that port, or pass -LocalPort 18081.' -ForegroundColor Yellow
            exit 3
        }
        Remove-Item -LiteralPath $tunnelLog -Force -ErrorAction SilentlyContinue
    }

    $url = "http://127.0.0.1:$LocalPort/"
    Write-Host ''
    Write-Host "  camera web: $url" -ForegroundColor Green
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

exit 0
