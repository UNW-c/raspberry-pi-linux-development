<#
.SYNOPSIS
    Subscribes to the PRTS MQTT broker and prints every message on the console.

.DESCRIPTION
    A self contained MQTT 3.1.1 subscriber (QoS 0) written against a plain TCP
    socket, so nothing has to be installed on Windows. By default it resolves the
    node from config/connection.ini and listens on prts/#.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\mqtt_listen.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\mqtt_listen.ps1 -Topic "prts/telemetry" -Count 5
#>
[CmdletBinding()]
param(
    [string]$Broker = '',
    [int]$Port = 1883,
    [string]$Topic = 'prts/#',
    [int]$Count = 0,
    [string]$ConfigPath = ''
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

if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path (Split-Path -Parent $scriptDirectory) 'config\connection.ini'
}
$values = Read-PrstIniFile -Path $ConfigPath
$hostName = Get-PrstIniValue $values 'ssh.host' 'PRTS.local'
$fallbackHost = Get-PrstIniValue $values 'ssh.fallback_host' 'PRTS'
$brokerPort = if ($Port -gt 0) { $Port } else { [int](Get-PrstIniValue $values 'mqtt.port' '1883') }

function Get-MqttBytes {
    param([int]$Number)

    $bytes = New-Object System.Collections.Generic.List[byte]
    do {
        $digit = [byte]($Number % 128)
        $Number = [math]::Floor($Number / 128)
        if ($Number -gt 0) {
            $digit = [byte]($digit -bor 0x80)
        }
        $bytes.Add($digit)
    } while ($Number -gt 0)
    return $bytes.ToArray()
}

function Add-MqttString {
    param([System.Collections.Generic.List[byte]]$Buffer, [string]$Text)

    $encoded = [System.Text.Encoding]::UTF8.GetBytes($Text)
    $Buffer.Add([byte](($encoded.Length -shr 8) -band 0xFF))
    $Buffer.Add([byte]($encoded.Length -band 0xFF))
    foreach ($byte in $encoded) { $Buffer.Add($byte) }
}

function New-MqttFrame {
    param([byte]$Header, [System.Collections.Generic.List[byte]]$Body)

    $frame = New-Object System.Collections.Generic.List[byte]
    $frame.Add($Header)
    foreach ($byte in (Get-MqttBytes -Number $Body.Count)) { $frame.Add($byte) }
    foreach ($byte in $Body) { $frame.Add($byte) }
    return $frame.ToArray()
}

function New-MqttConnect {
    param([string]$ClientId, [int]$KeepAliveSeconds = 60)

    $body = New-Object System.Collections.Generic.List[byte]
    Add-MqttString -Buffer $body -Text 'MQTT'
    $body.Add(0x04)          # protocol level 3.1.1
    $body.Add(0x02)          # clean session
    $body.Add([byte](($KeepAliveSeconds -shr 8) -band 0xFF))
    $body.Add([byte]($KeepAliveSeconds -band 0xFF))
    Add-MqttString -Buffer $body -Text $ClientId
    return New-MqttFrame -Header 0x10 -Body $body
}

function New-MqttSubscribe {
    param([string]$Filter, [int]$PacketId = 1)

    $body = New-Object System.Collections.Generic.List[byte]
    $body.Add([byte](($PacketId -shr 8) -band 0xFF))
    $body.Add([byte]($PacketId -band 0xFF))
    Add-MqttString -Buffer $body -Text $Filter
    $body.Add(0x00)          # requested QoS 0
    return New-MqttFrame -Header 0x82 -Body $body
}

function Read-MqttByte {
    param([System.IO.Stream]$Stream)

    $buffer = New-Object byte[] 1
    $read = $Stream.Read($buffer, 0, 1)
    if ($read -le 0) { return $null }
    return $buffer[0]
}

function Read-MqttPacket {
    param([System.IO.Stream]$Stream)

    $header = Read-MqttByte -Stream $Stream
    if ($null -eq $header) { return $null }

    $remaining = 0
    $multiplier = 1
    for ($index = 0; $index -lt 4; $index++) {
        $digit = Read-MqttByte -Stream $Stream
        if ($null -eq $digit) { return $null }
        $remaining += ($digit -band 0x7F) * $multiplier
        if (($digit -band 0x80) -eq 0) { break }
        $multiplier *= 128
    }

    $body = New-Object byte[] $remaining
    $filled = 0
    while ($filled -lt $remaining) {
        $read = $Stream.Read($body, $filled, $remaining - $filled)
        if ($read -le 0) { return $null }
        $filled += $read
    }

    return [pscustomobject]@{ Header = $header; Body = $body }
}

function Read-MqttString {
    param([byte[]]$Body, [int]$Offset)

    $length = ($Body[$Offset] -shl 8) -bor $Body[$Offset + 1]
    $text = [System.Text.Encoding]::UTF8.GetString($Body, $Offset + 2, $length)
    return [pscustomobject]@{ Text = $text; Next = $Offset + 2 + $length }
}

# ---------------------------------------------------------------------------
# Resolve the broker and connect
# ---------------------------------------------------------------------------
$candidates = Get-PrstHostCandidates -Primary $hostName -Fallback $fallbackHost
$address = $null
if (-not [string]::IsNullOrWhiteSpace($Broker)) {
    $address = $Broker
}
else {
    foreach ($candidate in $candidates) {
        $resolved = Get-PrstIPv4Address -HostName $candidate
        if (-not [string]::IsNullOrWhiteSpace($resolved)) {
            $address = $resolved
            break
        }
    }
}
if ([string]::IsNullOrWhiteSpace($address)) {
    Write-Host '  PRTS: could not resolve the broker address.' -ForegroundColor Red
    exit 2
}

$client = New-Object System.Net.Sockets.TcpClient
try {
    $client.Connect($address, $brokerPort)
}
catch {
    Write-Host "  PRTS: cannot reach $address`:$brokerPort - $($_.Exception.Message)" -ForegroundColor Red
    exit 2
}

$stream = $client.GetStream()
$stream.ReadTimeout = 1000
$stream.WriteTimeout = 2000

$clientId = 'prts-win-' + (Get-Random -Minimum 10000 -Maximum 99999)
$connectFrame = New-MqttConnect -ClientId $clientId
$stream.Write($connectFrame, 0, $connectFrame.Length)

$connAck = Read-MqttPacket -Stream $stream
if ($null -eq $connAck -or $connAck.Header -ne 0x20) {
    Write-Host '  PRTS: broker did not answer CONNECT.' -ForegroundColor Red
    $client.Close()
    exit 3
}
if ($connAck.Body[1] -ne 0) {
    Write-Host "  PRTS: broker refused the connection (code $($connAck.Body[1]))." -ForegroundColor Red
    $client.Close()
    exit 3
}

$subscribeFrame = New-MqttSubscribe -Filter $Topic
$stream.Write($subscribeFrame, 0, $subscribeFrame.Length)
$null = Read-MqttPacket -Stream $stream

Write-Host ''
Write-Host "  PRTS MQTT  $address`:$brokerPort  topic=$Topic  (Ctrl+C to stop)" -ForegroundColor Cyan
Write-Host ''

$received = 0
$lastPing = Get-Date
while ($true) {
    if ($Count -gt 0 -and $received -ge $Count) { break }

    $packet = $null
    try {
        $packet = Read-MqttPacket -Stream $stream
    }
    catch {
        # Read timeout: keep waiting, the loop checks the deadline below.
        $packet = $null
    }

    if ($null -eq $packet) {
        if (((Get-Date) - $lastPing).TotalSeconds -ge 20) {
            $ping = [byte[]](0xC0, 0x00)
            $stream.Write($ping, 0, 2)
            $lastPing = Get-Date
        }
        continue
    }

    $type = $packet.Header -shr 4
    if ($type -eq 3) {
        $topicInfo = Read-MqttString -Body $packet.Body -Offset 0
        $qos = ($packet.Header -shr 1) -band 0x03
        $payloadOffset = $topicInfo.Next
        if ($qos -gt 0) { $payloadOffset += 2 }
        $payload = [System.Text.Encoding]::UTF8.GetString(
            $packet.Body, $payloadOffset, $packet.Body.Length - $payloadOffset)
        $stamp = (Get-Date).ToString('HH:mm:ss')
        Write-Host "  [$stamp] " -NoNewline -ForegroundColor DarkGray
        Write-Host "$($topicInfo.Text) " -NoNewline -ForegroundColor Gray
        Write-Host $payload -ForegroundColor White
        $received++
    }
}

$stream.Write([byte[]](0xE0, 0x00), 0, 2)
$stream.Close()
$client.Close()
Write-Host ''
Write-Host "  received $received message(s)." -ForegroundColor Green
exit 0
