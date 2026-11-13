<#
.SYNOPSIS
    Shows the camera frames published by the PRTS node, or saves them to disk.

.DESCRIPTION
    Subscribes to prts/camera/frame (binary JPEG) and renders each message in a
    small window. With -SaveDir the frames are written to disk instead, which is
    also how the pipeline is tested without a display.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\camera\camera_view.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\camera\camera_view.ps1 -SaveDir .\frames -Count 3
#>
[CmdletBinding()]
param(
    [string]$Broker = '',
    [int]$Port = 1883,
    [string]$Topic = 'prts/camera/frame',
    [string]$SaveDir = '',
    [int]$Count = 0,
    [int]$TimeoutSeconds = 15,
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
    Write-Host "  PRTS: cannot reach $address`:$Port - $($_.Exception.Message)" -ForegroundColor Red
    exit 2
}

$saved = 0

if (-not [string]::IsNullOrWhiteSpace($SaveDir)) {
    # Headless mode: write every frame as its own JPEG and report the sizes.
    New-Item -ItemType Directory -Force -Path $SaveDir | Out-Null
    Write-Host "  PRTS camera  $address`:$Port  topic=$Topic  -> $SaveDir" -ForegroundColor Cyan
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($Count -le 0 -or $saved -lt $Count) {
        if ((Get-Date) -gt $deadline) {
            Write-Host "  no frame arrived within $TimeoutSeconds s (saved $saved)." -ForegroundColor Yellow
            Close-PrstMqttConnection -Connection $connection
            exit 3
        }
        $message = $null
        try {
            $message = Read-PrstMqttMessage -Connection $connection
        }
        catch {
            continue
        }
        if ($null -eq $message) { continue }

        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        $saved++
        $path = Join-Path $SaveDir ('frame_{0:D4}.jpg' -f $saved)
        [System.IO.File]::WriteAllBytes($path, $message.Payload)
        Write-Host ("  frame {0,4}  {1,7} B  -> {2}" -f $saved, $message.Payload.Length, $path)
    }
    Close-PrstMqttConnection -Connection $connection
    Write-Host "  saved $saved frame(s)." -ForegroundColor Green
    exit 0
}

# Live window.
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$form = New-Object System.Windows.Forms.Form
$form.Text = "PRTS camera - $address`:$Port"
$form.Width = 700
$form.Height = 560
$form.BackColor = [System.Drawing.Color]::Black
$form.StartPosition = 'CenterScreen'
$form.KeyPreview = $true

$picture = New-Object System.Windows.Forms.PictureBox
$picture.Dock = 'Fill'
$picture.SizeMode = 'Zoom'
$picture.BackColor = [System.Drawing.Color]::Black
$form.Controls.Add($picture)

$status = New-Object System.Windows.Forms.Label
$status.Dock = 'Bottom'
$status.Height = 24
$status.ForeColor = [System.Drawing.Color]::LightGray
$status.BackColor = [System.Drawing.Color]::Black
$status.Text = 'waiting for frames ...'
$form.Controls.Add($status)

$script:frames = 0
$script:lastSecond = Get-Date
$script:framesThisSecond = 0
$script:fps = 0
$script:stream = $null
$script:image = $null

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 25
$timer.Add_Tick({
    param($sender, $eventArgs)

    try {
        $script:stream.ReadTimeout = 20
        $message = Read-PrstMqttMessage -Connection $connection
    }
    catch {
        $message = $null
    }
    if ($null -eq $message) { return }

    try {
        $memory = New-Object System.IO.MemoryStream (, $message.Payload)
        $image = [System.Drawing.Image]::FromStream($memory)
        $previous = $picture.Image
        $picture.Image = $image
        if ($null -ne $previous) { $previous.Dispose() }
        $script:image = $image
        $script:frames++
        $script:framesThisSecond++

        $now = Get-Date
        if (($now - $script:lastSecond).TotalSeconds -ge 1) {
            $script:fps = $script:framesThisSecond
            $script:framesThisSecond = 0
            $script:lastSecond = $now
        }
        $status.Text = ("frame {0}   {1} B   {2}x{3}   {4} fps" -f
            $script:frames, $message.Payload.Length, $image.Width, $image.Height, $script:fps)
    }
    catch {
        $status.Text = 'decode failed: ' + $_.Exception.Message
    }
}
)

$form.Add_FormClosed({
    $timer.Stop()
    Close-PrstMqttConnection -Connection $connection
}
)

$script:stream = $connection.Stream
$timer.Start()
[void]$form.ShowDialog()
