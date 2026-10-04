param(
    [Parameter(Mandatory = $true)]
    [string] $Workspace
)

$ErrorActionPreference = 'Stop'
try {
    $workspacePath = (Resolve-Path -LiteralPath $Workspace).Path
    $executablePath = Join-Path $workspacePath 'build\windows-portable\velcal_app_artefacts\Release\VelCal.exe'
    if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
        throw "Release executable not found: $executablePath"
    }

    # Unique output folders avoid overwriting a portable copy used for playing.
    $packageName = 'VelCal-Windows-x64-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
    if ($env:VELCAL_RELEASE_PACKAGE -eq '1') {
        $version = (Get-Content -LiteralPath (Join-Path $workspacePath 'build\windows-portable\velcal-version.txt') -Raw).Trim()
        if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid release version.' }
        $packageName = "VelCal-$version-Windows-x64-portable"
    }
    $outputRoot = Join-Path $workspacePath 'build\portable'
    $packagePath = Join-Path $outputRoot $packageName
    New-Item -ItemType Directory -Path $packagePath | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $packagePath 'profiles') | Out-Null
    Copy-Item -LiteralPath $executablePath -Destination $packagePath
    Copy-Item -LiteralPath (Join-Path $workspacePath 'LICENSE') -Destination $packagePath
    $licences = Join-Path $packagePath 'LICENSES'
    New-Item -ItemType Directory -Path $licences | Out-Null
    Copy-Item -LiteralPath (Join-Path $workspacePath '.deps\juce-src\LICENSE.md') -Destination (Join-Path $licences 'JUCE.md')
    Copy-Item -LiteralPath (Join-Path $workspacePath '.deps\nlohmann_json-src\LICENSE.MIT') -Destination (Join-Path $licences 'nlohmann-json.txt')

    $archivePath = Join-Path $outputRoot ($packageName + '.zip')
    Compress-Archive -LiteralPath $packagePath -DestinationPath $archivePath
    Write-Host "Portable folder: $packagePath"
    Write-Host "ZIP archive:     $archivePath"
} catch {
    Write-Error $_
    exit 1
}
