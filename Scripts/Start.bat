@echo off
setlocal

cd /d "%~dp0.."

where py >nul 2>nul
if errorlevel 1 goto :python_missing

set "MODE=%~1"

if /I "%MODE%"=="foreground" (
    echo [Start] Starting servers in foreground mode...
    py -3 Scripts\servers.py start --foreground
    exit /b %errorlevel%
)

if /I "%MODE%"=="background" (
    echo [Start] Starting servers in background mode...
    py -3 Scripts\servers.py start
    exit /b %errorlevel%
)

echo [Start] Starting servers in split-window mode...
py -3 Scripts\servers.py start --split-windows
exit /b %errorlevel%

:python_missing
echo [Start] Python launcher 'py' was not found.
echo [Start] Please install Python or run Scripts\servers.py manually with an available interpreter.
exit /b 1
