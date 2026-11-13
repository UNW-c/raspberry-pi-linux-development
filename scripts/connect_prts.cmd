@echo off
rem PRTS remote link console - double click this file to open the Pi session.
rem It prefers Windows Terminal (sized for the boot sequence) and falls back
rem to the classic console host.
setlocal
chcp 65001 >nul 2>&1
set "SCRIPT=%~dp0connect_prts.ps1"
set "COLS=120"
set "ROWS=38"

where wt.exe >nul 2>&1
if %errorlevel%==0 (
    start "" wt.exe --size %COLS%,%ROWS% --title "PRTS" powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%"
    exit /b 0
)

powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%"
set "CODE=%errorlevel%"
if not "%CODE%"=="0" (
    echo.
    echo PRTS console exited with code %CODE%.
    pause
)
endlocal
exit /b %CODE%
