@echo off
cd /d "%~dp0"
cmake --preset debug
if errorlevel 1 exit /b 1
cmake --build --preset debug --target njin_model_editor --parallel
if errorlevel 1 exit /b 1
"build\bin\njin_model_editor.exe" %*
exit /b %errorlevel%
