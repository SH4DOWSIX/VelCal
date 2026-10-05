param([Parameter(Mandatory = $true)][string] $Workspace)
$ErrorActionPreference = 'Stop'
$workspacePath = (Resolve-Path -LiteralPath $Workspace).Path
$build = Join-Path $workspacePath 'build/windows-installer'
$version = (Get-Content -LiteralPath (Join-Path $build 'velcal-version.txt') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid version' }
$stage = Join-Path $build ('installer-payload-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage, (Join-Path $stage 'LICENSES'), (Join-Path $workspacePath 'build/installers') | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'velcal_app_artefacts/Release/VelCal.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $build 'velcal_plugin_artefacts/Release/VST3/VelCal.vst3') -Destination $stage -Recurse -Force
Copy-Item -LiteralPath (Join-Path $workspacePath 'LICENSE') -Destination $stage
Copy-Item -LiteralPath (Join-Path $workspacePath '.deps/juce-src/LICENSE.md') -Destination (Join-Path $stage 'LICENSES/JUCE.md')
Copy-Item -LiteralPath (Join-Path $workspacePath '.deps/nlohmann_json-src/LICENSE.MIT') -Destination (Join-Path $stage 'LICENSES/nlohmann-json.txt')
Copy-Item -LiteralPath (Join-Path $workspacePath '.deps/juce-src/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt') -Destination (Join-Path $stage 'LICENSES/VST3-SDK.txt')
$nsisRoot = Join-Path $workspacePath '.deps/nsis-download/nsis-bundle/windows'
if (-not (Test-Path -LiteralPath (Join-Path $nsisRoot 'makensis.exe'))) {
    $archive = Join-Path $workspacePath '.deps/nsis-download/nsis-bundle-3.12.tar.gz'
    New-Item -ItemType Directory -Force -Path (Split-Path $archive) | Out-Null
    Invoke-WebRequest -Uri 'https://github.com/electron-userland/electron-builder-binaries/releases/download/nsis%402.0.0/nsis-bundle-3.12.tar.gz' -OutFile $archive
    if ((Get-FileHash -LiteralPath $archive).Hash -ne 'e2c84b314160604d5132cb15b65fee92981320572902b3a1aa17a9a0d632cd58') { throw 'NSIS checksum mismatch' }
    tar -xf $archive -C (Split-Path $archive)
    if ($LASTEXITCODE) { throw 'NSIS extraction failed' }
}
$env:NSISDIR = $nsisRoot
$output = Join-Path $workspacePath "build/installers/VelCal-$version-Windows-x64-Setup.exe"
& (Join-Path $nsisRoot 'makensis.exe') /V2 "/DVERSION=$version" "/DSTAGE=$stage" "/DOUTPUT=$output" (Join-Path $workspacePath 'installers/windows/VelCal.nsi')
if ($LASTEXITCODE) { throw 'Installer compilation failed' }
Write-Output "Windows installer: $output"
