param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$OutDir,
    [string]$TargetDir,
    [string]$ServerZip
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutDir) { $OutDir = Join-Path $scriptDir "..\dist" }
if (-not $TargetDir) { $TargetDir = Join-Path $env:TEMP "co-launcher-target-bundle" }

if ($Version -notmatch '^[0-9]+(\.[0-9]+)*$') {
    throw "Version must be dotted digits, got '$Version'"
}

$launcherRoot = [IO.Path]::GetFullPath((Join-Path $scriptDir ".."))
$manifest = Join-Path $launcherRoot "Cargo.toml"
$TargetDir = [IO.Path]::GetFullPath($TargetDir)

if ($ServerZip) {
    if (-not (Test-Path -LiteralPath $ServerZip -PathType Leaf)) {
        throw "Server zip not found: $ServerZip"
    }
    $bundleDir = Join-Path $launcherRoot "launcher-gui\bundle"
    New-Item -ItemType Directory -Path $bundleDir -Force | Out-Null
    Copy-Item -LiteralPath $ServerZip -Destination (Join-Path $bundleDir "CommonwealthOnline-server.zip") -Force
}

$cargo = Get-Command cargo -ErrorAction SilentlyContinue
if (-not $cargo) {
    throw "cargo was not found on PATH; install the Rust toolchain first"
}
& $cargo.Source build --release -p launcher-gui --manifest-path $manifest --target-dir $TargetDir
if ($LASTEXITCODE -ne 0) {
    throw "cargo build --release -p launcher-gui failed with exit code $LASTEXITCODE"
}
$exe = Join-Path $TargetDir "release\launcher-gui.exe"
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "launcher-gui.exe was not produced at $exe"
}

$OutDir = [IO.Path]::GetFullPath($OutDir)
$bundleName = "CommonwealthOnline-Launcher-$Version"
$bundle = Join-Path $OutDir $bundleName
$zip = Join-Path $OutDir "$bundleName.zip"
if (Test-Path -LiteralPath $bundle) {
    Remove-Item -LiteralPath $bundle -Recurse -Force
}
if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}

New-Item -ItemType Directory -Path $bundle -Force | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $bundle "CommonwealthOnlineLauncher.exe")

$readme = @(
    "Commonwealth Online launcher $Version",
    "",
    "Download the Commonwealth Online modlist with Wabbajack. It sets up a Mod Organizer 2",
    "instance that already holds the game packages, PrismaUI, F4SE and the other mods.",
    "Copy CommonwealthOnlineLauncher.exe into that instance folder (next to ModOrganizer.exe)",
    "and run it. The launcher finds your Fallout 4 install and the instance, detects the",
    "runtime and switches on the matching F4SE for you.",
    "",
    "PLAY starts the game through Mod Organizer 2. The MODS menu has OPEN MOD ORGANIZER 2.",
    "Modding besides our selection is not yet supported. The MODS menu lists the mods and",
    "plugins in the Mod Organizer 2 instance for information only.",
    "",
    "The SERVER menu installs the Commonwealth Online server as the Commonwealth Online Server",
    "mod inside your Mod Organizer 2 instance (nothing goes into the Fallout 4 folder), turns the",
    "mod on in the active profile, so Start Hosting in the game finds it. It also checks your",
    "network for Steam P2P or a dedicated server and lets you run admin commands, kick and ban",
    "players."
)
Set-Content -LiteralPath (Join-Path $bundle "README.txt") -Value $readme -Encoding ASCII

& (Join-Path $env:SystemRoot "System32\tar.exe") -a -cf $zip -C $OutDir $bundleName
if ($LASTEXITCODE -ne 0) {
    throw "tar failed to create $zip with exit code $LASTEXITCODE"
}

Write-Output "launcher bundle folder: $bundle"
Write-Output "launcher bundle zip: $zip"
