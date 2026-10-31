
param(
    [string]$GameData = "D:\SteamLibrary\steamapps\common\Fallout 4\Data"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent
$ba2 = Join-Path $GameData "Fallout4 - Interface.ba2"
$extractor = Join-Path $repoRoot "tools\extract_ba2.py"
$vanillaOut = Join-Path $repoRoot "Interface\MainMenu.swf.vanilla.bak"
$repoSwf = Join-Path $repoRoot "Interface\MainMenu.swf"
$deploySwf = Join-Path $GameData "Interface\MainMenu.swf"

if (-not (Test-Path $ba2)) {
    Write-Error "Interface BA2 not found: $ba2"
}

python $extractor $ba2 "Interface\MainMenu.swf" $vanillaOut
Copy-Item -Force $vanillaOut $repoSwf
New-Item -ItemType Directory -Force -Path (Split-Path $deploySwf -Parent) | Out-Null
Copy-Item -Force $vanillaOut $deploySwf

Write-Host "Restored vanilla MainMenu.swf to:"
Write-Host "  $repoSwf"
Write-Host "  $deploySwf"
Write-Host ""
Write-Host "Settings -> Gameplay/Display/Audio should work again."
Write-Host "Do not deploy a patched MainMenu.swf. Multiplayer is injected at runtime by CommonwealthOnline.dll."
Write-Host "See docs/MainMenu_Injection.md for setup and testing."
