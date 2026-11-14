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

# Returns the first candidate that answers on the SSH port. DNS/mDNS lookup is
# separate from the TCP probe: a cold mDNS resolution can take well over a
# second, so the socket timeout must not be used to bound it.
function Resolve-PrstEndpoint {
    param(
        [Parameter(Mandatory = $true)][string[]]$Candidates,
        [Parameter(Mandatory = $true)][int]$Port,
        [int]$TimeoutMs = 3000
    )

    foreach ($candidate in $Candidates) {
        $address = Get-PrstIPv4Address -HostName $candidate
        if (-not [string]::IsNullOrWhiteSpace($address)) {
            if (Test-PrstRemotePort -HostName $address -Port $Port -TimeoutMs $TimeoutMs) {
                return [pscustomobject]@{
                    Name = $candidate
                    Address = $address
                }
            }
            continue
        }
        # Unresolvable name: give the OS one direct attempt before giving up.
        if (Test-PrstRemotePort -HostName $candidate -Port $Port -TimeoutMs $TimeoutMs) {
            return [pscustomobject]@{
                Name = $candidate
                Address = $candidate
            }
        }
    }
    return $null
}

# Builds the candidate list: configured name, fallback name, and the mDNS
# variant for single label names ("PRTS" -> "PRTS.local").
function Get-PrstHostCandidates {
    param(
        [string]$Primary,
        [string]$Fallback,
        [string[]]$Tailscale = @()
    )

    $candidates = @()
    foreach ($name in @($Primary, $Fallback)) {
        if ([string]::IsNullOrWhiteSpace($name)) {
            continue
        }
        $candidates += $name
        if ($name -notlike '*.*') {
            $candidates += "$name.local"
        }
    }
    # Tailscale addresses come last: on the same network the mDNS name answers
    # faster, off it the tunnel address is the only way in.
    foreach ($address in $Tailscale) {
        if (-not [string]::IsNullOrWhiteSpace($address)) {
            $candidates += $address
        }
    }
    return $candidates | Select-Object -Unique
}

# Reads the device addresses that are kept out of the repository (Tailscale IPs,
# future network names). The file is optional: without it everything falls back
# to the names in config/connection.ini.
function Read-PrstPrivateDevices {
    param([string]$RepositoryRoot)

    if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
        return @{}
    }
    $path = Join-Path $RepositoryRoot 'config\devices.private.ini'
    if (-not (Test-Path -LiteralPath $path)) {
        return @{}
    }
    return Read-PrstIniFile -Path $path
}

# Candidate list with the private addresses folded in, in probe order.
function Get-PrstLinkCandidates {
    param(
        [string]$RepositoryRoot,
        [string]$Primary,
        [string]$Fallback
    )

    $devices = Read-PrstPrivateDevices -RepositoryRoot $RepositoryRoot
    $tailscale = @()
    foreach ($key in @('tailscale.prts', 'tailscale.pi', 'tailscale.host')) {
        if ($devices.ContainsKey($key) -and -not [string]::IsNullOrWhiteSpace($devices[$key])) {
            $tailscale += $devices[$key]
            break
        }
    }
    return Get-PrstHostCandidates -Primary $Primary -Fallback $Fallback -Tailscale $tailscale
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
        if ($text.Length -gt $inner - 1) {
            $text = $text.Substring(0, $inner - 1)
        }
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

# Probes a local HTTP endpoint without going through the system proxy.
# Returns the number of bytes in the body, or -1 when the endpoint is not
# serving (connection refused, reset, timeout, wrong payload).
function Test-PrstHttpPage {
    param(
        [int]$Port,
        [string]$Path = '/status',
        [string]$Pattern = '',
        [int]$TimeoutSeconds = 3
    )

    $body = & curl.exe --noproxy '*' -s --max-time $TimeoutSeconds "http://127.0.0.1:$Port$Path" 2>$null
    if ($LASTEXITCODE -ne 0 -or $null -eq $body) {
        return -1
    }
    $text = ($body -join "`n")
    if ($text.Length -eq 0) {
        return -1
    }
    if (-not [string]::IsNullOrWhiteSpace($Pattern) -and $text -notmatch $Pattern) {
        return -1
    }
    return $text.Length
}

# Stops ssh processes that hold a local listening port. Used to clean up an
# orphaned "-L" forward left behind by an interrupted session.
function Stop-PrstSshListenerOnPort {
    param([int]$Port)

    $removed = 0
    $netstat = & netstat.exe -ano 2>$null
    foreach ($line in $netstat) {
        $text = $line.ToString().Trim()
        if ($text -notmatch ":$Port\s") { continue }
        if ($text -notmatch 'LISTENING') { continue }

        $fields = $text -split '\s+'
        $pidText = $fields[$fields.Length - 1]
        if ($pidText -notmatch '^\d+$') { continue }

        $process = Get-Process -Id ([int]$pidText) -ErrorAction SilentlyContinue
        if ($null -ne $process -and $process.ProcessName -eq 'ssh') {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $removed++
        }
    }
    return $removed
}

# Resizes the console window so the boot sequence has room for the full layout.
# Only the classic console host can be resized; Windows Terminal and non
# interactive hosts simply ignore the request.
function Set-PrstConsoleSize {
    param(
        [int]$Columns = 120,
        [int]$Rows = 38
    )

    if (-not [string]::IsNullOrWhiteSpace($env:WT_SESSION)) {
        return
    }

    try {
        $raw = $Host.UI.RawUI
        if ($null -eq $raw) {
            return
        }
        $max = $raw.MaxWindowSize
        $targetColumns = [Math]::Min($Columns, $max.Width)
        $targetRows = [Math]::Min($Rows, $max.Height)

        $buffer = $raw.BufferSize
        $bufferWidth = [Math]::Max($buffer.Width, $targetColumns)
        $bufferHeight = [Math]::Max($buffer.Height, $targetRows)
        if ($bufferWidth -ne $buffer.Width -or $bufferHeight -ne $buffer.Height) {
            $raw.BufferSize = New-Object System.Management.Automation.Host.Size($bufferWidth, $bufferHeight)
        }
        $raw.WindowSize = New-Object System.Management.Automation.Host.Size($targetColumns, $targetRows)
    }
    catch {
        # Non interactive host or a terminal that owns its own geometry.
    }
}
