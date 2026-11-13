@echo off
rem Double click: connect to the node, start the camera stack and open the page
rem in the browser. Close the window (or press Ctrl+C) to drop the tunnel.
setlocal
chcp 65001 >nul 2>&1
call "%~dp0bin\prts-cam.cmd" web %*
endlocal
