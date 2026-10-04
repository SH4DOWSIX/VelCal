@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1

if not exist ".tmp" mkdir ".tmp"
if not exist ".tmp" goto failed
set "TEMP=%CD%\.tmp"
set "TMP=%CD%\.tmp"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake is required and must be on PATH.
    goto failed
)
where ctest >nul 2>nul
if errorlevel 1 (
    echo ERROR: CTest is required and must be on PATH.
    goto failed
)
where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: Git is required to fetch the pinned JUCE source.
    goto failed
)

echo Configuring Windows x64 portable Release...
cmake -S . -B build\windows-portable -A x64 -DVELCAL_PORTABLE=ON -DVELCAL_BUILD_APP=ON -DVELCAL_BUILD_TESTS=ON
if errorlevel 1 goto failed

echo Building Release...
cmake --build build\windows-portable --config Release --target velcal_app velcal_core_tests --parallel 2
if errorlevel 1 goto failed

echo Running Release tests...
ctest --test-dir build\windows-portable -C Release --output-on-failure
if errorlevel 1 goto failed

echo Packaging portable folder and ZIP...
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package_windows_portable.ps1 -Workspace "%CD%"
if errorlevel 1 goto failed

echo.
echo Portable Release build completed successfully.
popd
if /i not "%~1"=="--no-pause" pause
exit /b 0

:failed
echo.
echo ERROR: Portable Release build failed. See the message above.
echo Requires CMake, Git, and Visual Studio C++ build tools with a Windows SDK.
popd
if /i not "%~1"=="--no-pause" pause
exit /b 1
