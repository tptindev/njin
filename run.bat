@echo off
cd /d "%~dp0"

echo [1/2] Building sandbox...

cmake --build --preset debug --target njin_sandbox --parallel

if errorlevel 1 goto error

echo.
echo [2/2] Running sandbox...

if not exist "build\bin\njin_sandbox.exe" goto missing

"build\bin\njin_sandbox.exe"

exit /b %errorlevel%

:missing
echo sandbox.exe not found!
pause
exit /b 1

:error
echo Build failed!
pause
exit /b 1
