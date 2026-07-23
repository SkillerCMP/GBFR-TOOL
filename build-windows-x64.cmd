@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "BUILD_DIR=build-windows-x64"
set "OUTPUT_DIR=0-Finish"
set "LOG_FILE=build-windows-x64-warnings-errors.log"
set "OUTPUT_LOG=%OUTPUT_DIR%\%LOG_FILE%"

if exist "%LOG_FILE%" del /q "%LOG_FILE%"

rem Always configure from a clean build tree so stale post-build commands do not survive.
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%BUILD_DIR%" goto :cleanup_fail

echo Configuring GBFR - TOOL v1.10 x64 Release build...
cmake -S . -B "%BUILD_DIR%" -A x64 -DGBFR_TOOL_BUILD_TESTS=ON >>"%LOG_FILE%" 2>&1
if errorlevel 1 goto :fail

echo Building GUI, CLI, and tests...
rem Build serially so Visual Studio does not launch overlapping CMake reconfigure checks.
cmake --build "%BUILD_DIR%" --config Release --parallel 1 >>"%LOG_FILE%" 2>&1
if errorlevel 1 goto :fail

echo Running tests...
ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure >>"%LOG_FILE%" 2>&1
if errorlevel 1 goto :fail

rem Package only after the full build and test pass has completed.
if exist "%OUTPUT_DIR%" rmdir /s /q "%OUTPUT_DIR%"
if exist "%OUTPUT_DIR%" goto :package_fail
mkdir "%OUTPUT_DIR%"
if errorlevel 1 goto :package_fail

copy /y "%BUILD_DIR%\Release\GBFR-TOOL.exe" "%OUTPUT_DIR%\" >nul
if errorlevel 1 goto :package_fail
copy /y "%BUILD_DIR%\Release\GBFR-TOOL-CLI.exe" "%OUTPUT_DIR%\" >nul
if errorlevel 1 goto :package_fail

if exist "Hashfolder\" (
    xcopy /e /i /y "Hashfolder" "%OUTPUT_DIR%\Hashfolder\" >nul
    if errorlevel 1 goto :package_fail
) else (
    for %%F in (
        "GameData-Section-Cross-Reference.csv"
        "GBFR-Character-Sections.txt"
        "GBFR-Hash-Database.txt"
        "GBFR-Section-Names.txt"
    ) do if exist "%%~F" (
        copy /y "%%~F" "%OUTPUT_DIR%\" >nul
        if errorlevel 1 goto :package_fail
    )
)

if exist "LICENSE.txt" (
    copy /y "LICENSE.txt" "%OUTPUT_DIR%\" >nul
    if errorlevel 1 goto :package_fail
)
if exist "Licenses\" (
    xcopy /e /i /y "Licenses" "%OUTPUT_DIR%\Licenses\" >nul
    if errorlevel 1 goto :package_fail
)
if exist "README.md" (
    copy /y "README.md" "%OUTPUT_DIR%\" >nul
    if errorlevel 1 goto :package_fail
)

rem Keep the warnings/errors log with the finished release files and remove the root copy.
move /y "%LOG_FILE%" "%OUTPUT_LOG%" >nul
if errorlevel 1 goto :package_fail

rem Delete the generated CMake/MSBuild tree only after every release file is in 0-Finish.
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%BUILD_DIR%" goto :cleanup_fail

(
  echo Build completed successfully.
  echo GUI: %OUTPUT_DIR%\GBFR-TOOL.exe
  echo CLI: %OUTPUT_DIR%\GBFR-TOOL-CLI.exe
  echo Log: %OUTPUT_LOG%
  echo Temporary build folder removed: %BUILD_DIR%
)
exit /b 0

:package_fail
echo.>>"%LOG_FILE%"
echo ERROR: Release packaging failed before all files were safely moved to %OUTPUT_DIR%.>>"%LOG_FILE%"
goto :fail

:cleanup_fail
echo.
echo ERROR: Could not remove the temporary build folder:
echo   %BUILD_DIR%
echo Close any program using files in that folder and run the build again.
if exist "%OUTPUT_LOG%" (
    echo.>>"%OUTPUT_LOG%"
    echo ERROR: Could not remove temporary build folder %BUILD_DIR%.>>"%OUTPUT_LOG%"
)
exit /b 1

:fail
echo.
echo Build failed. Review:
if exist "%LOG_FILE%" (
    echo   %LOG_FILE%
    type "%LOG_FILE%"
) else if exist "%OUTPUT_LOG%" (
    echo   %OUTPUT_LOG%
    type "%OUTPUT_LOG%"
) else (
    echo   No build log was found.
)
exit /b 1
