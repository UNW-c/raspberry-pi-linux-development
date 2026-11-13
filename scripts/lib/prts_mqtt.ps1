<#
.SYNOPSIS
    Minimal MQTT 3.1.1 helpers (QoS 0) shared by the Windows side scripts.

.DESCRIPTION
    Dot-source this file and use the *-PrstMqtt functions. Payloads are returned
    as raw byte arrays so binary frames (JPEG) survive the trip unchanged.

        . "$PSScriptRoot\lib\prts_mqtt.ps1"
        $mqtt = Connect-PrstMqtt -Address '192.168.210.239' -Port 1883
        Send-PrstMqttSubscribe -Connection $mqtt -Filter 'prts/#'
        $message = Read-PrstMqttMessage -Connection $mqtt
        Close-PrstMqttConnection -Connection $mqtt
#>

function Get-PrstMqttVarint {
    param([int]$Value)

    $bytes = New-Object System.Collections.Generic.List[byte]
    do {
        $digit = [byte]($Value % 128)
        $Value = [math]::Floor($Value / 128)
        if ($Value -gt 0) {
            $digit = [byte]($digit -bor 0x80)
        }
        $bytes.Add($digit)
    } while ($Value -gt 0)
    return $bytes.ToArray()
}

function Add-PrstMqttString {
    param([System.Collections.Generic.List[byte]]$Buffer, [string]$Text)

    $encoded = [System.Text.Encoding]::UTF8.GetBytes($Text)
    $Buffer.Add([byte](($encoded.Length -shr 8) -band 0xFF))
    $Buffer.Add([byte]($encoded.Length -band 0xFF))
    foreach ($byte in $encoded) { $Buffer.Add($byte) }
}

function New-PrstMqttFrame {
    param([byte]$Header, [System.Collections.Generic.List[byte]]$Body)

    $frame = New-Object System.Collections.Generic.List[byte]
    $frame.Add($Header)
    foreach ($byte in (Get-PrstMqttVarint -Value $Body.Count)) { $frame.Add($byte) }
    foreach ($byte in $Body) { $frame.Add($byte) }
    return $frame.ToArray()
}

function Read-PrstMqttByte {
    param([System.IO.Stream]$Stream)

    $buffer = New-Object byte[] 1
    $read = $Stream.Read($buffer, 0, 1)
    if ($read -le 0) { return $null }
    return $buffer[0]
}

function Read-PrstMqttPacket {
    param([System.IO.Stream]$Stream)

    $header = Read-PrstMqttByte -Stream $Stream
    if ($null -eq $header) { return $null }

    $remaining = 0
    $multiplier = 1
    for ($index = 0; $index -lt 4; $index++) {
        $digit = Read-PrstMqttByte -Stream $Stream
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

function Connect-PrstMqtt {
    param(
        [Parameter(Mandatory = $true)][string]$Address,
        [int]$Port = 1883,
        [string]$ClientId = ''
    )

    if ([string]::IsNullOrWhiteSpace($ClientId)) {
        $ClientId = 'prts-win-' + (Get-Random -Minimum 10000 -Maximum 99999)
    }

    $client = New-Object System.Net.Sockets.TcpClient
    $client.Connect($Address, $Port)
    $stream = $client.GetStream()
    $stream.ReadTimeout = 1000
    $stream.WriteTimeout = 2000

    $body = New-Object System.Collections.Generic.List[byte]
    Add-PrstMqttString -Buffer $body -Text 'MQTT'
    $body.Add(0x04)
    $body.Add(0x02)
    $body.Add([byte]0)
    $body.Add([byte]60)
    Add-PrstMqttString -Buffer $body -Text $ClientId
    $connect = New-PrstMqttFrame -Header 0x10 -Body $body
    $stream.Write($connect, 0, $connect.Length)

    $ack = Read-PrstMqttPacket -Stream $stream
    if ($null -eq $ack -or $ack.Header -ne 0x20) {
        $stream.Close()
        $client.Close()
        throw 'broker did not answer CONNECT'
    }
    if ($ack.Body[1] -ne 0) {
        $stream.Close()
        $client.Close()
        throw "broker refused the connection (code $($ack.Body[1]))"
    }

    return [pscustomobject]@{
        Client = $client
        Stream = $stream
        Address = $Address
        Port = $Port
        ClientId = $ClientId
    }
}

function Send-PrstMqttSubscribe {
    param(
        [Parameter(Mandatory = $true)]$Connection,
        [Parameter(Mandatory = $true)][string]$Filter,
        [int]$PacketId = 1
    )

    $body = New-Object System.Collections.Generic.List[byte]
    $body.Add([byte](($PacketId -shr 8) -band 0xFF))
    $body.Add([byte]($PacketId -band 0xFF))
    Add-PrstMqttString -Buffer $body -Text $Filter
    $body.Add(0x00)
    $frame = New-PrstMqttFrame -Header 0x82 -Body $body
    $Connection.Stream.Write($frame, 0, $frame.Length)

    $ack = Read-PrstMqttPacket -Stream $Connection.Stream
    if ($null -eq $ack -or ($ack.Header -shr 4) -ne 9) {
        throw 'broker did not acknowledge SUBSCRIBE'
    }
}

function Read-PrstMqttMessage {
    param([Parameter(Mandatory = $true)]$Connection)

    $packet = Read-PrstMqttPacket -Stream $Connection.Stream
    if ($null -eq $packet) { return $null }
    if (($packet.Header -shr 4) -ne 3) { return $null }

    $topicLength = ($packet.Body[0] -shl 8) -bor $packet.Body[1]
    $topic = [System.Text.Encoding]::UTF8.GetString($packet.Body, 2, $topicLength)
    $offset = 2 + $topicLength
    $qos = ($packet.Header -shr 1) -band 0x03
    if ($qos -gt 0) { $offset += 2 }

    $payload = New-Object byte[] ($packet.Body.Length - $offset)
    [Array]::Copy($packet.Body, $offset, $payload, 0, $payload.Length)

    return [pscustomobject]@{
        Topic = $topic
        Payload = $payload
        Qos = $qos
        Retained = (($packet.Header -band 0x01) -ne 0)
    }
}

function Close-PrstMqttConnection {
    param($Connection)

    if ($null -eq $Connection) { return }
    try {
        $Connection.Stream.Write([byte[]](0xE0, 0x00), 0, 2)
        $Connection.Stream.Close()
    }
    catch {
        # Closing a already broken stream is fine.
    }
    try { $Connection.Client.Close() } catch { }
}

function Resolve-PrstBrokerAddress {
    param(
        [string]$Address = '',
        [string]$ConfigPath = ''
    )

    if (-not [string]::IsNullOrWhiteSpace($Address)) {
        return $Address
    }

    $scriptDirectory = $PSScriptRoot
    . (Join-Path $scriptDirectory 'prts_remote.ps1')
    if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
        $ConfigPath = Join-Path (Split-Path -Parent $scriptDirectory) 'config\connection.ini'
    }
    $values = Read-PrstIniFile -Path $ConfigPath
    $primary = Get-PrstIniValue $values 'ssh.host' 'PRTS.local'
    $fallback = Get-PrstIniValue $values 'ssh.fallback_host' 'PRTS'

    foreach ($candidate in (Get-PrstHostCandidates -Primary $primary -Fallback $fallback)) {
        $resolved = Get-PrstIPv4Address -HostName $candidate
        if (-not [string]::IsNullOrWhiteSpace($resolved)) {
            return $resolved
        }
    }
    return ''
}
