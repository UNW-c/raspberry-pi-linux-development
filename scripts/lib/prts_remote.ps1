<#
.SYNOPSIS
    Shared helpers for the PRTS Windows side scripts.

.DESCRIPTION
    Dot-source this file:

        . "$PSScriptRoot\lib\prts_remote.ps1"

    It provides INI parsing, endpoint probing, the PRTS link panel and the
    SSH_ASKPASS plumbing used to forward the operator password without any
    interactive prompt.
#>

function Read-PrstIniFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    $result = @{}
    if (-not (Test-Path -LiteralPath $Path)) {
        return $result
    }

    $section = ''
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#') -or $trimmed.StartsWith(';')) {
            continue
        }
        if ($trimmed.StartsWith('[') -and $trimmed.EndsWith(']')) {
            $section = $trimmed.Substring(1, $trimmed.Length - 2).Trim()
            continue
        }
        $separator = $trimmed.IndexOf('=')
        if ($separator -lt 0) {
            continue
        }
        $key = $trimmed.Substring(0, $separator).Trim()
        $value = $trimmed.Substring($separator + 1).Trim()
        if ($section.Length -gt 0) {
            $result["$section.$key"] = $value
        }
        else {
            $result[$key] = $value
        }
    }
    return $result
}

function Get-PrstIniValue {
    param(
        [Parameter(Mandatory = $true)]$Values,
        [Parameter(Mandatory = $true)][string]$Key,
        [string]$Default = ''
    )

    if ($Values.ContainsKey($Key) -and -not [string]::IsNullOrWhiteSpace($Values[$Key])) {
        return $Values[$Key]
    }
    return $Default
}

function Test-PrstRemotePort {
    param(
        [Parameter(Mandatory = $true)][string]$HostName,
        [Parameter(Mandatory = $true)][int]$Port,
        [int]$TimeoutMs = 1500
    )

    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $task = $client.ConnectAsync($HostName, $Port)
        if (-not $task.Wait($TimeoutMs)) {
            return $false
        }
        return $client.Connected
    }
    catch {
        return $false
    }
    finally {
        $client.Close()
    }
}

# Resolves a host name to an IPv4 address. .NET's DNS client does not perform
# mDNS, so Resolve-DnsName (which does) is used as the second attempt.
function Get-PrstIPv4Address {
    param([Parameter(Mandatory = $true)][string]$HostName)

    try {
        $addresses = [System.Net.Dns]::GetHostAddresses($HostName)
        foreach ($address in $addresses) {
            if ($address.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetwork) {
                return $address.IPAddressToString
            }
        }
    }
    catch {
        # Fall through to the mDNS capable resolver.
    }

    try {
        $records = Resolve-DnsName -Name $HostName -Type A -ErrorAction Stop |
            Where-Object { $_.IPAddress }
        foreach ($record in $records) {
            return $record.IPAddress
        }
    }
    catch {
        return $null
    }
    return $null
}

# Returns the first candidate that accepts a TCP connection on the SSH port.
# The result carries both the human readable name and the numeric address that
# ssh should use.
function Resolve-PrstEndpoint {
    param(
        [Parameter(Mandatory = $true)][string[]]$Candidates,
        [Parameter(Mandatory = $true)][int]$Port,
        [int]$TimeoutMs = 1500
    )

    foreach ($candidate in $Candidates) {
        if (Test-PrstRemotePort -HostName $candidate -Port $Port -TimeoutMs $TimeoutMs) {
            $address = Get-PrstIPv4Address -HostName $candidate
            if ([string]::IsNullOrWhiteSpace($address)) {
                $address = $candidate
            }
            return [pscustomobject]@{
                Name = $candidate
                Address = $address
            }
        }
    }
    return $null
}

function Write-PrstLinkPanel {
    param(
        [Parameter(Mandatory = $true)][string]$Node,
        [Parameter(Mandatory = $true)][string]$State,
        [string]$Detail = '',
        [string]$Title = 'PRTS  >  REMOTE LINK CONSOLE',
        [ConsoleColor]$TitleColor = [ConsoleColor]::Cyan
    )

    $inner = 56
    $horizontal = [string]::new([char]0x2500, $inner)
    $vertical = [char]0x2502

    $renderRow = {
        param([string]$text, [ConsoleColor]$color)
        $padded = (' ' + $text).PadRight($inner)
        Write-Host ('  ' + $vertical + $padded + $vertical) -ForegroundColor $color
    }

    Write-Host ''
    Write-Host ('  ' + [char]0x250C + $horizontal + [char]0x2510) -ForegroundColor DarkCyan
    & $renderRow $Title $TitleColor
    Write-Host ('  ' + [char]0x251C + $horizontal + [char]0x2524) -ForegroundColor DarkCyan
    & $renderRow ('NODE   ' + $Node) ([ConsoleColor]::Gray)
    & $renderRow ('STATE  ' + $State) ([ConsoleColor]::Gray)
    if (-not [string]::IsNullOrWhiteSpace($Detail)) {
        & $renderRow ('NOTE   ' + $Detail) ([ConsoleColor]::DarkGray)
    }
    Write-Host ('  ' + [char]0x2514 + $horizontal + [char]0x2518) -ForegroundColor DarkCyan
    Write-Host ''
}

# Creates a temporary askpass helper so ssh can read the password without a
# console prompt. Returns a state object that must be passed to
# Remove-PrstPasswordEnvironment when done.
function New-PrstPasswordEnvironment {
    param([Parameter(Mandatory = $true)][string]$Password)

    $askpassFile = Join-Path $env:TEMP ('prts-askpass-' + [guid]::NewGuid().ToString('N') + '.cmd')
    Set-Content -LiteralPath $askpassFile -Value "@echo $Password" -Encoding ASCII

    $state = [pscustomobject]@{
        AskpassFile = $askpassFile
        PreviousAskpass = $env:SSH_ASKPASS
        PreviousRequire = $env:SSH_ASKPASS_REQUIRE
        PreviousDisplay = $env:DISPLAY
    }

    $env:SSH_ASKPASS = $askpassFile
    $env:SSH_ASKPASS_REQUIRE = 'force'
    $env:DISPLAY = 'localhost:0'
    return $state
}

function Remove-PrstPasswordEnvironment {
    param($State)

    if ($null -eq $State) {
        return
    }
    Remove-Item -LiteralPath $State.AskpassFile -Force -ErrorAction SilentlyContinue
    $env:SSH_ASKPASS = $State.PreviousAskpass
    $env:SSH_ASKPASS_REQUIRE = $State.PreviousRequire
    $env:DISPLAY = $State.PreviousDisplay
}
