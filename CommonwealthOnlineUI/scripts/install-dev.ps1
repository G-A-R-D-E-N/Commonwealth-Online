param(
    [Parameter(Mandatory = $true)]
    [string]$Fallout4Path
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$stagedData = Join-Path $root "build\Fallout 4\Data"
$packageDir = Join-Path $root "package"

$source = $null
if (Test-Path (Join-Path $stagedData "F4SE\Plugins\CommonwealthOnlineUI.dll")) {
    $source = $stagedData
} elseif (Test-Path (Join-Path $packageDir "F4SE\Plugins\CommonwealthOnlineUI.dll")) {
    $source = $packageDir
} else {
    throw "No staged UI package found. Run build.bat or stage-mod.bat first."
}

$dataTarget = Join-Path $Fallout4Path "Data"
if (-not (Test-Path $dataTarget) -and (Test-Path (Join-Path $Fallout4Path "F4SE"))) {
    # Caller passed Data folder directly
    $dataTarget = $Fallout4Path
}

if (-not (Test-Path $dataTarget)) {
    throw "Fallout 4 Data folder not found at: $dataTarget"
}

New-Item -ItemType Directory -Force -Path $dataTarget | Out-Null
Copy-Item -Force -Recurse (Join-Path $source "*") $dataTarget

Write-Host "Installed CommonwealthOnlineUI package to $dataTarget"
Write-Host "  source: $source"
