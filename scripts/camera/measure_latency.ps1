<#
.SYNOPSIS
    Measures how old the picture on the screen actually is.

.DESCRIPTION
    Reads the MJPEG stream like a browser does and compares every frame with the
    capture stamp the node attached to it (X-Stamp header). The result is the
    real end-to-end age of a frame: sensor exposure -> rescale -> MQTT -> web
    server -> SSH forward -> this machine.

    Run it against the forwarded port on the desktop (the same address the
    browser uses), not against the node itself, or the tunnel is left out.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\camera\measure_latency.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\camera\measure_latency.ps1 -Seconds 20
#>
[CmdletBinding()]
param(
    [string]$HostName = '127.0.0.1',
    [int]$Port = 18080,
    [int]$Seconds = 10,
    [int]$Warmup = 2
)

$ErrorActionPreference = 'Stop'

function Read-StreamLine {
    param([System.IO.Stream]$Stream)

    $builder = New-Object System.Text.StringBuilder
    while ($true) {
        $value = $Stream.ReadByte()
        if ($value -lt 0) {
            return $null
        }
        if ($value -eq 10) {
            $line = $builder.ToString()
            while ($line.EndsWith("`r")) {
                $line = $line.Substring(0, $line.Length - 1)
            }
            return $line
        }
        [void]$builder.Append([char]$value)
    }
}

function Skip-Bytes {
    param([System.IO.Stream]$Stream, [int]$Count)

    $buffer = New-Object byte[] 8192
    $left = $Count
    while ($left -gt 0) {
        $chunk = $Stream.Read($buffer, 0, [Math]::Min($buffer.Length, $left))
        if ($chunk -le 0) {
            throw 'stream closed while skipping frame data'
        }
        $left -= $chunk
    }
}

# The node has no RTC and its clock drifts away from this machine, so a raw
# subtraction would only show the clock difference. /status carries the node
# clock; the half round trip is assumed to be the same in both directions.
function Get-NodeClockOffset {
    param([string]$HostName, [int]$Port)

    $best = $null
    for ($attempt = 0; $attempt -lt 5; $attempt++) {
        $before = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        $body = & curl.exe --noproxy '*' -s --max-time 3 "http://${HostName}:$Port/status" 2>$null
        $after = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        if ($LASTEXITCODE -ne 0 -or $null -eq $body) { continue }
        if ($body -notmatch '"server_ms":(\d+)') { continue }
        $offset = [long]$Matches[1] - [long](($before + $after) / 2)
        $roundTrip = $after - $before
        if ($null -eq $best -or $roundTrip -lt $best.RoundTrip) {
            $best = [pscustomobject]@{ Offset = $offset; RoundTrip = $roundTrip }
        }
    }
    return $best
}

# The clock is sampled before the stream is opened: the node closes a request
# that has not sent anything within its read timeout.
$clock = Get-NodeClockOffset -HostName $HostName -Port $Port
$clockOffset = 0L
if ($null -eq $clock) {
    Write-Host '  no clock reference from /status, assuming both clocks agree' -ForegroundColor Yellow
}
else {
    $clockOffset = $clock.Offset
}

try {
    $client = New-Object System.Net.Sockets.TcpClient
    $client.NoDelay = $true
    $client.ReceiveBufferSize = 16384
    $client.Connect($HostName, $Port)
}
catch {
    Write-Host "  cannot reach http://${HostName}:$Port - is the SSH forward up?" -ForegroundColor Red
    exit 2
}

$ages = New-Object System.Collections.Generic.List[int]
$bytes = 0L
$frames = 0

try {
    $stream = $client.GetStream()
    $stream.ReadTimeout = 8000

    $request = "GET /stream HTTP/1.1`r`nHost: ${HostName}:$Port`r`nConnection: close`r`n`r`n"
    $requestBytes = [System.Text.Encoding]::ASCII.GetBytes($request)
    $stream.Write($requestBytes, 0, $requestBytes.Length)
    $stream.Flush()

    # Response head, then the multipart parts.
    do {
        $line = Read-StreamLine -Stream $stream
        if ($null -eq $line) { throw 'the server closed the stream immediately' }
    } while ($line.Length -gt 0)

    $started = Get-Date
    $warmDone = $started.AddSeconds($Warmup)

    while ($true) {
        $line = Read-StreamLine -Stream $stream
        if ($null -eq $line) { break }
        if ($line.Length -eq 0 -or -not $line.StartsWith('--')) { continue }

        $stamp = 0L
        $length = -1
        while ($true) {
            $header = Read-StreamLine -Stream $stream
            if ($null -eq $header) { throw 'stream ended inside a frame header' }
            if ($header.Length -eq 0) { break }
            if ($header -match '^(?i)content-length:\s*(\d+)') { $length = [int]$Matches[1] }
            elseif ($header -match '^(?i)x-stamp:\s*(\d+)') { $stamp = [long]$Matches[1] }
        }
        if ($length -lt 0) { throw 'frame has no content length' }

        $now = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        if ((Get-Date) -ge $warmDone -and $stamp -gt 0) {
            $age = $now + $clockOffset - $stamp
            if ($age -ge 0 -and $age -lt 60000) { $ages.Add([int]$age) }
        }
        Skip-Bytes -Stream $stream -Count $length
        if ((Get-Date) -ge $warmDone) {
            $bytes += $length
            $frames++
        }
        if ((Get-Date) -ge $started.AddSeconds($Warmup + $Seconds)) { break }
    }
}
catch {
    if ($ages.Count -eq 0) {
        Write-Host "  $($_.Exception.Message)" -ForegroundColor Red
        exit 3
    }
}
finally {
    $client.Close()
}

if ($ages.Count -eq 0) {
    Write-Host '  no stamped frames arrived' -ForegroundColor Red
    exit 4
}

$sorted = $ages | Sort-Object
$median = $sorted[[int][Math]::Floor($sorted.Count / 2)]
$p95 = $sorted[[Math]::Min($sorted.Count - 1, [int][Math]::Floor($sorted.Count * 0.95))]
$mean = [int](($ages | Measure-Object -Average).Average)
$elapsed = [Math]::Max(0.001, $Seconds)

Write-Host ''
Write-Host "  frame age, ${HostName}:$Port" -ForegroundColor Cyan
Write-Host ('  ' + ('-' * 46)) -ForegroundColor DarkGray
Write-Host ("  clock    {0,5} ms  (node minus this machine)" -f $clockOffset)
Write-Host ("  median   {0,5} ms" -f $median)
Write-Host ("  mean     {0,5} ms" -f $mean)
Write-Host ("  p95      {0,5} ms" -f $p95)
Write-Host ("  max      {0,5} ms" -f $sorted[$sorted.Count - 1])
Write-Host ("  min      {0,5} ms" -f $sorted[0])
Write-Host ('  ' + ('-' * 46)) -ForegroundColor DarkGray
Write-Host ("  frames   {0,5}" -f $frames)
Write-Host ("  fps      {0,5:n1}" -f ($frames / $elapsed))
Write-Host ("  rate     {0,5:n0} KiB/s" -f ($bytes / $elapsed / 1024))
Write-Host ''
exit 0
