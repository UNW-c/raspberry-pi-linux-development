<#
.SYNOPSIS
    Command line entry point for the PRTS camera.

.DESCRIPTION
    One command for the whole camera workflow:

        prts-cam               open the web page (default)
        prts-cam web           same as above
        prts-cam view          open the desktop window (WinForms)
        prts-cam status        show what runs on the node and on the desktop
        prts-cam stop          stop prts-camera/prts-web and the local tunnel
        prts-cam help          this text

    Arguments after the command are passed through, e.g.
        prts-cam view -SaveDir .\frames -Count 3
        prts-cam web -NoBrowser -Seconds 10

.EXAMPLE
    .\bin\prts-cam status
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$Command = 'web',
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest = @()
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

$configPath = Join-Path $repositoryRoot 'config\connection.ini'
$localPort = 18080

function Show-Usage {
    Write-Host ''
    Write-Host '  prts-cam <command> [options]' -ForegroundColor Cyan
    Write-Host ''
    Write-Host '    web       start the node stack, open the tunnel and the browser (default)'
    Write-Host '    view      open the desktop window (scripts\camera\camera_view.ps1)'
    Write-Host '    status    processes on the node, endpoint and local tunnel'
    Write-Host '    stop      stop the node stack and close the local tunnel'
    Write-Host '    help      this text'
    Write-Host ''
    Write-Host '  examples:' -ForegroundColor DarkGray
    Write-Host '    prts-cam                        # browser view' -ForegroundColor DarkGray
    Write-Host '    prts-cam view -SaveDir .\frames -Count 3' -ForegroundColor DarkGray
    Write-Host '    prts-cam web -NoBrowser -Seconds 10' -ForegroundColor DarkGray
    Write-Host ''
}

function Get-NodeContext {
    $values = Read-PrstIniFile -Path $configPath
    $primary = Get-PrstIniValue $values 'ssh.host' 'PRTS.local'
    $fallback = Get-PrstIniValue $values 'ssh.fallback_host' 'PRTS'
    $user = Get-PrstIniValue $values 'ssh.user' 'seven'
    $password = Get-PrstIniValue $values 'ssh.password' '123456'
    $port = [int](Get-PrstIniValue $values 'ssh.port' '22')
    $timeout = [int](Get-PrstIniValue $values 'ssh.connect_timeout' '10')

    $endpoint = Resolve-PrstEndpoint -Candidates (Get-PrstHostCandidates -Primary $primary -Fallback $fallback) -Port $port
    if ($null -eq $endpoint) {
        return $null
    }
    return [pscustomobject]@{
        User = $user
        Password = $password
        Port = $port
        Address = $endpoint.Address
        Name = $endpoint.Name
        Options = @(
            '-o', 'StrictHostKeyChecking=accept-new',
            '-o', 'NumberOfPasswordPrompts=1',
            '-o', "ConnectTimeout=$timeout",
            '-p', "$port"
        )
    }
}

# Turns "-Name value" / "-Switch" tokens into a hashtable that can be splatted
# onto a sibling script's parameters. Splatting a plain string array would bind
# positionally, which maps flags onto the wrong parameter.
function ConvertTo-ParameterTable {
    param([string[]]$Arguments)

    $table = @{}
    for ($index = 0; $index -lt $Arguments.Count; $index++) {
        $argument = $Arguments[$index]
        if ($argument -match '^-{1,2}([A-Za-z][A-Za-z0-9]*)$') {
            $name = $Matches[1]
            $next = $null
            if ($index + 1 -lt $Arguments.Count) { $next = $Arguments[$index + 1] }

            if ($null -ne $next -and $next -notmatch '^-{1,2}[A-Za-z]') {
                $table[$name] = $next
                $index++
            }
            else {
                $table[$name] = $true
            }
            continue
        }
        $table['ConfigPath'] = $argument
    }
    return $table
}

function Invoke-RemoteScript {
    param($Context, [string]$RemoteCommand)

    $target = "$($Context.User)@$($Context.Address)"
    $state = New-PrstPasswordEnvironment -Password $Context.Password
    $previous = $ErrorActionPreference
    # ssh writes its progress to stderr; treat that as output, not as a failure.
    $ErrorActionPreference = 'Continue'
    try {
        & ssh @($Context.Options) $target $RemoteCommand 2>&1 |
            ForEach-Object { Write-Host "  $_" }
        return $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
        Remove-PrstPasswordEnvironment -State $state
    }
}

switch ($Command.ToLowerInvariant()) {
    'web' {
        $parameters = ConvertTo-ParameterTable -Arguments $Rest
        & (Join-Path $scriptDirectory 'camera_web.ps1') @parameters
        exit $LASTEXITCODE
    }
    'view' {
        $parameters = ConvertTo-ParameterTable -Arguments $Rest
        & (Join-Path $scriptDirectory 'camera_view.ps1') @parameters
        exit $LASTEXITCODE
    }
    'status' {
        $context = Get-NodeContext
        if ($null -eq $context) {
            Write-Host '  PRTS: node unreachable.' -ForegroundColor Red
            exit 2
        }
        Write-PrstLinkPanel -Node "$($context.User)@$($context.Name) ($($context.Address))" -State 'STATUS'
        $null = Invoke-RemoteScript -Context $context -RemoteCommand '~/.local/bin/prts-camera-web status'

        Write-Host '  --- desktop ---'
        $pageBytes = Test-PrstHttpPage -Port $localPort -Path '/status' -Pattern '"frames"'
        if ($pageBytes -ge 0) {
            Write-Host "  tunnel: http://127.0.0.1:$localPort/ is serving ($pageBytes bytes of status)" -ForegroundColor Green
        }
        else {
            Write-Host "  tunnel: nothing serving on 127.0.0.1:$localPort (run: prts-cam web)" -ForegroundColor DarkGray
        }
        exit 0
    }
    'stop' {
        $context = Get-NodeContext
        if ($null -eq $context) {
            Write-Host '  PRTS: node unreachable.' -ForegroundColor Red
            exit 2
        }
        $null = Invoke-RemoteScript -Context $context -RemoteCommand '~/.local/bin/prts-camera-web stop'

        $closed = Stop-PrstSshListenerOnPort -Port $localPort
        if ($closed -gt 0) {
            Write-Host "  tunnel: closed $closed local forward(s) on port $localPort" -ForegroundColor Green
        }
        elseif (Test-PrstRemotePort -HostName '127.0.0.1' -Port $localPort -TimeoutMs 400) {
            Write-Host "  tunnel: port $localPort is still forwarded by a process this session cannot stop." -ForegroundColor Yellow
            Write-Host "          close the ssh window that owns it, or use: prts-cam web -LocalPort 18081" -ForegroundColor Yellow
        }
        else {
            Write-Host "  tunnel: nothing listening on port $localPort" -ForegroundColor DarkGray
        }
        exit 0
    }
    'help' { Show-Usage; exit 0 }
    '-h' { Show-Usage; exit 0 }
    '--help' { Show-Usage; exit 0 }
    default {
        Write-Host "  prts-cam: unknown command '$Command'" -ForegroundColor Red
        Show-Usage
        exit 2
    }
}
