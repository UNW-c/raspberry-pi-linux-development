@echo off
rem Double-click entry for the RTSP video stream.
rem   no arguments -> asks which link to use (LAN or Tailscale)
rem   arguments     -> passed straight to start-stream.ps1, e.g. -Link lan
if "%~1"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0choose-link.ps1"
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-stream.ps1" %*
)
if errorlevel 1 pause
