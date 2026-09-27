@echo off
rem Builds a game and the inspector, starts the inspector, then the game.
rem   run_inspected.bat              the debug demo
rem   run_inspected.bat platformer   njin_platformer (a Debug build opens the debug port)
rem   Any sample works the same way: sandbox, pong, platformer, topdown, debug_demo, render_demo.
cd /d "%~dp0"

set GAME=%1
if "%GAME%"=="" set GAME=debug_demo

echo [1/2] Building njin_%GAME% and njin_inspector...
cmake --build --preset debug --target njin_%GAME% njin_inspector --parallel
if errorlevel 1 goto error

echo [2/2] Starting the inspector and njin_%GAME%...
rem Games with assets run from their own folder, build\bin\<game>\.
set DIR=build\bin
if exist "build\bin\%GAME%\njin_%GAME%.exe" set DIR=build\bin\%GAME%
start "" "build\bin\njin_inspector.exe"
"%DIR%\njin_%GAME%.exe"
exit /b %errorlevel%

:error
echo Build failed!
pause
exit /b 1
