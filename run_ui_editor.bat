@echo off
cd /d "%~dp0"

echo [1/2] Building njin_ui_editor...
cmake --build --preset debug --target njin_ui_editor --parallel
if errorlevel 1 goto error

echo.
echo [2/2] Running njin_ui_editor...

if not exist "build\bin\njin_ui_editor.exe" goto missing

"build\bin\njin_ui_editor.exe" %*

exit /b %errorlevel%

:missing
echo build\bin\njin_ui_editor.exe not found!
pause
exit /b 1

:error
echo Build failed!
pause
exit /b 1
