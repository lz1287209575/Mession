@echo off
setlocal

cd /d "%~dp0.."

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

echo [Build] Configuring project for Visual Studio 2022 x64...
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto :error

echo [Build] Building configuration: %CONFIG%
cmake --build Build --config %CONFIG%
if errorlevel 1 goto :error

echo [Build] Done.
exit /b 0

:error
echo [Build] Failed with exit code %errorlevel%.
exit /b %errorlevel%
