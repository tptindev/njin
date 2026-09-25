@echo off
cd /d "%~dp0"

set "DOXYGEN=doxygen"
where doxygen >nul 2>nul
if errorlevel 1 set "DOXYGEN=%ProgramFiles%\doxygen\bin\doxygen.exe"

if not exist "%DOXYGEN%" if "%DOXYGEN%"=="%ProgramFiles%\doxygen\bin\doxygen.exe" goto nodoxygen

echo Generating njin docs...

if exist doxygen_warnings.txt del doxygen_warnings.txt

"%DOXYGEN%" Doxyfile
if errorlevel 1 goto error

echo.
if exist doxygen_warnings.txt (
    echo Docs generated with warnings, see docs\doxygen_warnings.txt
) else (
    echo Docs generated, no warnings.
)
echo Open docs\html\index.html
if not "%1"=="nopause" pause
exit /b 0

:nodoxygen
echo Doxygen not found. Install it with: winget install DimitriVanHeesch.Doxygen
if not "%1"=="nopause" pause
exit /b 1

:error
echo.
echo Doxygen failed!
if not "%1"=="nopause" pause
exit /b 1
