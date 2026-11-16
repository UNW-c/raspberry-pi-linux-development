<#
.SYNOPSIS
    Asks which link to use, then starts the stream on it.

.DESCRIPTION
    Two ways to reach the node, listed with their addresses:

      1. LAN        the address mDNS resolves to (fastest when both are on the
                    same network)
      2. Tailscale  the 100.x address from config/devices.private.ini (works
                    from another network)

    Each link keeps its own local ports, so both tunnels can be up at the same
    time without fighting over 8888/8889:

                    WebRTC   HLS    RTSP
        LAN         8889     8888   8554
        Tailscale   9889     9888   9554

    Any arguments are passed straight to start-stream.ps1, so
    `start-stream.bat -Link lan` skips the menu.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File stream\choose-link.ps1
#>
[CmdletBinding()]
param(
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
. (Join-Path $repositoryRoot 'scripts\lib\prts_remote.ps1')

$values = Read-PrstIniFile -Path (Join-Path $repositoryRoot 'config\connection.ini')
$hostName = Get-PrstIniValue $values 'ssh.host' 'PRTS'
$fallbackHost = Get-PrstIniValue $values 'ssh.fallback_host' 'PRTS.local'
$port = [int](Get-PrstIniValue $values 'ssh.port' '22')

$link = Read-PrstLinkChoice -RepositoryRoot $repositoryRoot -Primary $hostName `
    -Fallback $fallbackHost -Port $port
if ($null -eq $link) {
    exit 0
}

$ports = switch ($link) {
    'tailscale' { @{ Webrtc = 9889; Hls = 9888; Rtsp = 9554 } }
    default { @{ Webrtc = 8889; Hls = 8888; Rtsp = 8554 } }
}

$startScript = Join-Path $scriptDirectory 'start-stream.ps1'

# Turn "-Name value" / "-Switch" tokens into a splattable table; splatting the
# raw array would bind the first token positionally (to -Viewer, here).
$extra = @{}
for ($index = 0; $index -lt $Arguments.Count; $index++) {
    $argument = $Arguments[$index]
    if ($argument -match '^-{1,2}([A-Za-z][A-Za-z0-9]*)$') {
        $name = $Matches[1]
        $next = $null
        if ($index + 1 -lt $Arguments.Count) { $next = $Arguments[$index + 1] }
        if ($null -ne $next -and $next -notmatch '^-{1,2}[A-Za-z]') {
            $extra[$name] = $next
            $index++
        }
        else {
            $extra[$name] = $true
        }
    }
}

& $startScript -Link $link -WebrtcPort $ports.Webrtc -HlsPort $ports.Hls -RtspPort $ports.Rtsp @extra
exit $LASTEXITCODE
