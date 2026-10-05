@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1
if not exist ".tmp" mkdir ".tmp"
set "TEMP=%CD%\.tmp"
set "TMP=%CD%\.tmp"
cmake -S . -B build\windows-installer -A x64 -DVELCAL_INSTALLED=ON -DVELCAL_BUILD_APP=ON -DVELCAL_BUILD_VST3=ON -DVELCAL_BUILD_TESTS=ON
if errorlevel 1 goto failed
cmake --build build\windows-installer --config Release --target velcal_app velcal_plugin_VST3 velcal_core_tests velcal_app_tests velcal_plugin_tests --parallel 2
if errorlevel 1 goto failed
ctest --test-dir build\windows-installer -C Release --output-on-failure
if errorlevel 1 goto failed
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package_windows_installer.ps1 -Workspace "%CD%"
if errorlevel 1 goto failed
echo Installer Release build completed successfully.
popd
if /i not "%~1"=="--no-pause" pause
exit /b 0
:failed
echo ERROR: Installer build failed.
popd
if /i not "%~1"=="--no-pause" pause
exit /b 1
