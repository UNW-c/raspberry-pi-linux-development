@echo off
rem Double-click entry for the detection page (WebRTC picture + overlay).
rem Start stream\start-stream.bat first: the tunnel carries the WHEP handshake.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-detect-web.ps1" %*
if errorlevel 1 pause
