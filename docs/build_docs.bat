@echo off
cd /d "%~dp0"

set "DOXYGEN=doxygen"
where doxygen >nul 2>nul
if errorlevel 1 set "DOXYGEN=%ProgramFiles%\doxygen\bin\doxygen.exe"

if not exist "%DOXYGEN%" if "%DOXYGEN%"=="%ProgramFiles%\doxygen\bin\doxygen.exe" goto nodoxygen

echo Generating njin docs (Vietnamese, then English)...

if exist doxygen_warnings.txt del doxygen_warnings.txt
if exist doxygen_warnings_en.txt del doxygen_warnings_en.txt

"%DOXYGEN%" Doxyfile
if errorlevel 1 goto error
if exist Doxyfile.en if exist pages_en (
    "%DOXYGEN%" Doxyfile.en
    if errorlevel 1 goto error
)

echo.
if exist doxygen_warnings.txt (
    echo Vietnamese docs generated with warnings, see docs\doxygen_warnings.txt
) else (
    echo Vietnamese docs generated, no warnings.
)
if exist doxygen_warnings_en.txt (
    echo English docs generated with warnings, see docs\doxygen_warnings_en.txt
) else (
    echo English docs generated, no warnings.
)
echo Open docs\html\index.html  or  docs\html\en\index.html
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
