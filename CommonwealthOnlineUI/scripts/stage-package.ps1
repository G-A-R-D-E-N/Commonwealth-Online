param()

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDataDir = Join-Path $root "build\Fallout 4\Data"
$packageDir = Join-Path $root "package"

# Prefer xmake output so staging can safely wipe package/ without deleting the source DLL.
$uiDllCandidates = @(
    (Join-Path $root "build\windows\x64\releasedbg\CommonwealthOnlineUI.dll"),
    (Join-Path $packageDir "F4SE\Plugins\CommonwealthOnlineUI.dll")
)
$uiDll = $uiDllCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $uiDll) {
    throw "CommonwealthOnlineUI.dll not found. Run build.bat / xmake build first."
}

# Copy to a temp file if the only source lives under package/ (about to be wiped).
$uiDllSource = $uiDll
$packageResolved = Resolve-Path $packageDir -ErrorAction SilentlyContinue
if ($packageResolved -and $uiDllSource.StartsWith($packageResolved.Path, [System.StringComparison]::OrdinalIgnoreCase)) {
    $tempDll = Join-Path $env:TEMP "CommonwealthOnlineUI-stage.dll"
    Copy-Item -Force $uiDllSource $tempDll
    $uiDll = $tempDll
}


function Stage-ToDir([string]$dataDir) {
    $pluginsDir = Join-Path $dataDir "F4SE\Plugins"
    $prismaRoot = Join-Path $dataDir "PrismaUI_F4"
    $viewsDir = Join-Path $prismaRoot "views"
    $miscDir = Join-Path $prismaRoot "misc"
    $shellDir = Join-Path $prismaRoot "shell"

    if (Test-Path $dataDir) {
        Remove-Item -Recurse -Force $dataDir
    }

    New-Item -ItemType Directory -Force -Path $pluginsDir, $viewsDir, $miscDir | Out-Null

    Copy-Item -Force $uiDll (Join-Path $pluginsDir "CommonwealthOnlineUI.dll")

    $uiPdb = [System.IO.Path]::ChangeExtension($uiDll, ".pdb")
    if (Test-Path $uiPdb) {
        Copy-Item -Force $uiPdb $pluginsDir
    }


    # Ensure views/misc exist after framework asset copy (may have wiped empty dirs conceptually)
    New-Item -ItemType Directory -Force -Path $viewsDir, $miscDir | Out-Null

    $viewsSource = Join-Path $root "ui\views"
    if (Test-Path $viewsSource) {
        Copy-Item -Force -Recurse (Join-Path $viewsSource "*") $viewsDir
    }

    $mapMarkersSource = Join-Path $root "ui\MapMarkers"
    $mapMarkersDest = Join-Path $viewsDir "CommonwealthOnline\MapMarkers"
    if (Test-Path $mapMarkersSource) {
        New-Item -ItemType Directory -Force -Path $mapMarkersDest | Out-Null
        Copy-Item -Force -Recurse (Join-Path $mapMarkersSource "*") $mapMarkersDest
    }

    $sharedIcons = Join-Path $viewsDir "CommonwealthOnline\shared\icons"
    New-Item -ItemType Directory -Force -Path $sharedIcons | Out-Null
    $iconsSource = Join-Path $root "ui\Icons"
    if (Test-Path $iconsSource) {
        Copy-Item -Force -Recurse (Join-Path $iconsSource "*") $sharedIcons
    }

    $cursorSource = Join-Path $root "ui\views\CommonwealthOnline\browser\assets\F4MouseCursor.png"
    $cursorDest = Join-Path $miscDir "cursor.png"
    if (Test-Path $cursorSource) {
        Copy-Item -Force $cursorSource $cursorDest
    } elseif (-not (Test-Path $cursorDest)) {
        Write-Warning "F4MouseCursor.png not found - PrismaUI will use no custom cursor."
    }

    # Overlay shell tweaks (e.g. hide Prisma boot toast).
    $shellOverrides = Join-Path $root "ui\shell-overrides"
    if (Test-Path $shellOverrides) {
        New-Item -ItemType Directory -Force -Path $shellDir | Out-Null
        Copy-Item -Force -Recurse (Join-Path $shellOverrides "*") $shellDir
    }
}

Write-Host "Staging CommonwealthOnlineUI to:"
Write-Host "  $buildDataDir"
Write-Host "  $packageDir"

Stage-ToDir $buildDataDir
Stage-ToDir $packageDir

Write-Host "Package staged."
Write-Host "  F4SE/Plugins/CommonwealthOnlineUI.dll"
Write-Host "  PrismaUI_F4/views/"
Write-Host "  PrismaUI_F4 2.1.0 runtime must be installed separately."
