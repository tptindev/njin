@echo off
cd /d "%~dp0"

echo [1/3] Configuring Njin...

cmake -S . -B build ^
    -G Ninja ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

if errorlevel 1 goto error

echo [2/3] Updating Clangd database...

copy /Y "build\compile_commands.json" "compile_commands.json" >nul

if errorlevel 1 goto error

echo [3/3] Building Njin...

cmake --build build --parallel

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
