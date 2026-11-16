@echo off
rem Double-click entry for the RTSP video stream. Everything lives in
rem start-stream.ps1 so the command line stays identical.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-stream.ps1" %*
if errorlevel 1 pause
