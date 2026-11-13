@echo off
rem PRTS camera command: prts-cam [web|view|status|stop|help]
rem Install with scripts\install_commands.ps1 to use it from any directory.
setlocal
chcp 65001 >nul 2>&1
set "ENTRY=%~dp0..\scripts\prts_cam.ps1"
if not exist "%ENTRY%" (
    echo prts-cam: cannot find "%ENTRY%"
    exit /b 2
)

powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%ENTRY%" %*
set "CODE=%errorlevel%"
endlocal & exit /b %CODE%
