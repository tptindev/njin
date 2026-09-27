@echo off
cd /d "%~dp0"

echo [1/3] Configuring Njin...

rem The "debug" preset in CMakePresets.json: Ninja, Debug, build/. VS Code
rem (CMake Tools) uses the same preset, so both build the same way.
cmake --preset debug

if errorlevel 1 goto error

echo [2/3] Updating Clangd database...

copy /Y "build\compile_commands.json" "compile_commands.json" >nul

if errorlevel 1 goto error

echo [3/3] Building Njin...

cmake --build --preset debug --parallel

if errorlevel 1 goto error

echo.
echo Build successful!
pause
exit /b 0

:error
echo.
echo Build failed!
pause
exit /b 1
