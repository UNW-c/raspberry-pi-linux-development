<#
.SYNOPSIS
    Subscribes to the PRTS MQTT broker and prints every message on the console.

.DESCRIPTION
    Thin wrapper around the shared MQTT helpers in lib/prts_mqtt.ps1, so nothing
    has to be installed on Windows. Defaults to the node resolved from
    config/connection.ini and to the prts/# topic tree.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\mqtt\mqtt_listen.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\mqtt\mqtt_listen.ps1 -Topic "prts/telemetry" -Count 5
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
$repositoryRoot = $scriptDirectory
while ($repositoryRoot -and -not (Test-Path -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt'))) {
    $repositoryRoot = Split-Path -Parent $repositoryRoot
}
. (Join-Path $repositoryRoot 'scripts\lib\prts_mqtt.ps1')

$address = Resolve-PrstBrokerAddress -Address $Broker -ConfigPath $ConfigPath
if ([string]::IsNullOrWhiteSpace($address)) {
    Write-Host '  PRTS: could not resolve the broker address.' -ForegroundColor Red
    exit 2
}

try {
    $connection = Connect-PrstMqtt -Address $address -Port $Port
    Send-PrstMqttSubscribe -Connection $connection -Filter $Topic
}
catch {
    Write-Host "  PRTS MQTT: cannot reach $address`:$Port - $($_.Exception.Message)" -ForegroundColor Red
    exit 2
}

Write-Host ''
Write-Host "  PRTS MQTT  $address`:$Port  topic=$Topic  (Ctrl+C to stop)" -ForegroundColor Cyan
Write-Host ''

$received = 0
$lastPing = Get-Date
while ($true) {
    if ($Count -gt 0 -and $received -ge $Count) { break }

    $message = $null
    try {
        $message = Read-PrstMqttMessage -Connection $connection
    }
    catch {
        $message = $null
    }

    if ($null -eq $message) {
        if (((Get-Date) - $lastPing).TotalSeconds -ge 20) {
            try { $connection.Stream.Write([byte[]](0xC0, 0x00), 0, 2) } catch { }
            $lastPing = Get-Date
        }
        continue
    }

    $text = [System.Text.Encoding]::UTF8.GetString($message.Payload)
    $stamp = (Get-Date).ToString('HH:mm:ss')
    Write-Host "  [$stamp] " -NoNewline -ForegroundColor DarkGray
    Write-Host "$($message.Topic) " -NoNewline -ForegroundColor Gray
    Write-Host $text -ForegroundColor White
    $received++
}

Close-PrstMqttConnection -Connection $connection
Write-Host ''
Write-Host "  received $received message(s)." -ForegroundColor Green
exit 0
