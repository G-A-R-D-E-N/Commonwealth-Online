param(
    [string]$OutputName = "CommonwealthOnlineUI"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $root "package"
$outFile = Join-Path $root "$OutputName.zip"

if (Test-Path $outFile) {
    Remove-Item $outFile -Force
}

Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $outFile
Write-Host "Created $outFile"
