@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "BUILD_DIR=build-windows-x64"
set "OUTPUT_DIR=0-Finished"
set "LOG_FILE=build-windows-x64-warnings-errors.log"

if exist "%LOG_FILE%" del /q "%LOG_FILE%"

echo Configuring GBFR - TOOL v1.00 x64 Release build...
cmake -S . -B "%BUILD_DIR%" -A x64 -DGBFR_TOOL_BUILD_TESTS=ON >>"%LOG_FILE%" 2>&1
if errorlevel 1 goto :fail

echo Building GUI, CLI, and tests...
cmake --build "%BUILD_DIR%" --config Release --parallel >>"%LOG_FILE%" 2>&1
if errorlevel 1 goto :fail

echo Running tests...
ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure >>"%LOG_FILE%" 2>&1
if errorlevel 1 goto :fail

if exist "%OUTPUT_DIR%" rmdir /s /q "%OUTPUT_DIR%"
mkdir "%OUTPUT_DIR%"

copy /y "%BUILD_DIR%\Release\GBFR-TOOL.exe" "%OUTPUT_DIR%\" >nul
copy /y "%BUILD_DIR%\Release\GBFR-TOOL-CLI.exe" "%OUTPUT_DIR%\" >nul

if exist "Hashfolder\" (
    xcopy /e /i /y "Hashfolder" "%OUTPUT_DIR%\Hashfolder\" >nul
) else (
    for %%F in (
        "GameData-Section-Cross-Reference.csv"
        "GBFR-Character-Sections.txt"
        "GBFR-Hash-Database.txt"
        "GBFR-Section-Names.txt"
    ) do if exist "%%~F" copy /y "%%~F" "%OUTPUT_DIR%\" >nul
)

copy /y "LICENSE.txt" "%OUTPUT_DIR%\" >nul
if exist "Licenses\" xcopy /e /i /y "Licenses" "%OUTPUT_DIR%\Licenses\" >nul
copy /y "README.txt" "%OUTPUT_DIR%\" >nul

(
  echo Build completed successfully.
  echo GUI: %OUTPUT_DIR%\GBFR-TOOL.exe
  echo CLI: %OUTPUT_DIR%\GBFR-TOOL-CLI.exe
  echo Log: %LOG_FILE%
)
exit /b 0

:fail
echo.
echo Build failed. Review:
echo   %LOG_FILE%
type "%LOG_FILE%"
exit /b 1
