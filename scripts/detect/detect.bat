@echo off
rem Double-click entry for the desktop detector. Everything else lives in
rem run_detect.ps1 so the command line stays identical.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_detect.ps1" %*
if errorlevel 1 pause
