@echo off
rem One click: start the camera stack on the node, open the SSH tunnel and the
rem browser for the PRTS camera page.
setlocal
chcp 65001 >nul 2>&1
set "SCRIPT=%~dp0camera_web.ps1"
if not exist "%SCRIPT%" (
    echo PRTS camera: cannot find "%SCRIPT%"
    pause
    exit /b 2
)

powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" %*
set "CODE=%errorlevel%"

if not "%CODE%"=="0" (
    echo.
    echo PRTS camera launcher exited with code %CODE%.
    pause
)
endlocal
exit /b %CODE%
