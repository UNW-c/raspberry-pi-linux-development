@echo off
rem PRTS remote link console - double click this file inside the workspace to
rem open the Raspberry Pi session.
rem
rem Launch strategy: go straight to powershell.exe with the script path as an
rem argument. Two intermediate launchers were removed on purpose:
rem   - wt.exe   : the Windows Terminal app alias on this machine is broken
rem                (0 byte stub), so forwarding the command line through it
rem                fails with 0x80070002 and pops a shell error dialog
rem   - cmd start: quoting the non-ASCII path through `start` is fragile
rem connect_prts.ps1 resizes the console window itself, so the boot sequence
rem still gets a full size terminal.
setlocal
set "SCRIPT=%~dp0connect_prts.ps1"
if not exist "%SCRIPT%" (
    echo PRTS launcher: cannot find "%SCRIPT%"
    pause
    exit /b 2
)

chcp 65001 >nul 2>&1
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" %*
set "CODE=%errorlevel%"

if not "%CODE%"=="0" (
    echo.
    echo PRTS console exited with code %CODE%.
    pause
)
endlocal
exit /b %CODE%
