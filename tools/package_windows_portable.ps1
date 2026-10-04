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
    $outputRoot = Join-Path $workspacePath 'build\portable'
    $packagePath = Join-Path $outputRoot $packageName
    New-Item -ItemType Directory -Path $packagePath | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $packagePath 'profiles') | Out-Null
    Copy-Item -LiteralPath $executablePath -Destination $packagePath
    Copy-Item -LiteralPath (Join-Path $workspacePath 'LICENSE') -Destination $packagePath

    $archivePath = Join-Path $outputRoot ($packageName + '.zip')
    Compress-Archive -LiteralPath $packagePath -DestinationPath $archivePath
    Write-Host "Portable folder: $packagePath"
    Write-Host "ZIP archive:     $archivePath"
} catch {
    Write-Error $_
    exit 1
}
