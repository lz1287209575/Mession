@echo off
setlocal

cd /d "%~dp0.."

where py >nul 2>nul
if errorlevel 1 goto :python_missing

echo [Stop] Stopping servers...
py -3 Scripts\servers.py stop
exit /b %errorlevel%

:python_missing
echo [Stop] Python launcher 'py' was not found.
echo [Stop] Please install Python or run Scripts\servers.py manually with an available interpreter.
exit /b 1
