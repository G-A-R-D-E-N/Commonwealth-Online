param(
    [string]$Instance = "D:\CO Modlist",
    [string]$Downloads,
    [string]$Profile = "Default"
)

$ErrorActionPreference = "Stop"

if (-not $Downloads) { $Downloads = Join-Path $Instance "downloads" }
$ini = Join-Path $Instance "ModOrganizer.ini"
if (-not (Test-Path -LiteralPath $ini)) { throw "not a Mod Organizer 2 instance (no ModOrganizer.ini): $Instance" }
if (Get-Process ModOrganizer -ErrorAction SilentlyContinue) { throw "close Mod Organizer 2 first; it rewrites modlist.txt when it exits" }
$tar = Join-Path $env:SystemRoot "System32\tar.exe"

$runtimeByDll = @{
    "f4se_1_10_163.dll" = "OG"
    "f4se_1_10_984.dll" = "NG"
    "f4se_1_11_240.dll" = "AE"
}
$gameRuntimeByVersion = @{ "1.10.163" = "OG"; "1.10.984" = "NG"; "1.11.240" = "AE" }

$rawPath = (Get-Content -LiteralPath $ini | Where-Object { $_ -like "gamePath=*" } | Select-Object -First 1) -replace '^gamePath=', ''
if ($rawPath -match '^@ByteArray\((.*)\)$') { $rawPath = $Matches[1] }
$gamePath = $rawPath -replace '\\\\', '\'
if (-not $gamePath) { throw "no gamePath in $ini" }
$activeRuntime = $null
$gameExe = Join-Path $gamePath "Fallout4.exe"
if (Test-Path -LiteralPath $gameExe) {
    $v = (Get-Item -LiteralPath $gameExe).VersionInfo.FileVersion -replace '\.0$', ''
    $activeRuntime = $gameRuntimeByVersion[$v]
}
Write-Host "game: $gamePath (runtime: $(if ($activeRuntime) { $activeRuntime } else { 'unknown' }))"

$pluginDir = Join-Path $Instance "plugins\rootbuilder"
if (-not (Test-Path -LiteralPath $pluginDir)) {
    $zip = Get-ChildItem -LiteralPath $Downloads -Filter "rootbuilder*.zip" -File | Sort-Object Name | Select-Object -Last 1
    if (-not $zip) { throw "no rootbuilder*.zip in $Downloads" }
    $stage = Join-Path $env:TEMP "co-rootbuilder-$([guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Path $stage | Out-Null
    try {
        & $tar -xf $zip.FullName -C $stage
        if ($LASTEXITCODE -ne 0) { throw "could not extract $($zip.Name)" }
        if (-not (Test-Path -LiteralPath (Join-Path $stage "rootbuilder\__init__.py"))) { throw "$($zip.Name) has no rootbuilder\__init__.py" }
        Copy-Item -LiteralPath (Join-Path $stage "rootbuilder") -Destination $pluginDir -Recurse
        Write-Host "installed Root Builder from $($zip.Name)"
    } finally {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    }
} else {
    Write-Host "Root Builder already installed"
}

foreach ($rb in @(if (Test-Path -LiteralPath $Downloads) { Get-ChildItem -LiteralPath $Downloads -Filter "rootbuilder*.zip" -File })) {
    $metaPath = "$($rb.FullName).meta"
    if (Test-Path -LiteralPath $metaPath) { continue }
    $rbVersion = $rb.BaseName -replace '^rootbuilder\.', ''
    $rbMeta = @("[General]", "gameName=SkyrimSE", "modID=31720", "fileID=0", "url=https://github.com/Kezyma/ModOrganizer-Plugins/releases/download/rootbuilder/$($rb.Name)", "name=Root Builder", "modName=Root Builder", "version=$rbVersion", "newestVersion=$rbVersion", "fileCategory=1", "category=0", "repository=Nexus", "installed=true", "uninstalled=false", "paused=false", "removed=false")
    [IO.File]::WriteAllText($metaPath, (($rbMeta -join "`r`n") + "`r`n"))
    Write-Host "wrote $($rb.Name).meta"
}

$modsDir = Join-Path $Instance "mods"
$built = @()
foreach ($archive in Get-ChildItem -LiteralPath $Downloads -File | Where-Object { $_.Name -like "*Script Extender*" -and $_.Extension -in ".7z", ".zip" }) {
    $stage = Join-Path $env:TEMP "co-f4se-$([guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Path $stage | Out-Null
    try {
        & $tar -xf $archive.FullName -C $stage
        if ($LASTEXITCODE -ne 0) { Write-Warning "skipped $($archive.Name): could not extract"; continue }
        $top = Get-ChildItem -LiteralPath $stage -Directory | Where-Object { $_.Name -like "f4se_*" } | Select-Object -First 1
        if (-not $top) { Write-Warning "skipped $($archive.Name): no f4se_* folder"; continue }
        $dll = Get-ChildItem -LiteralPath $top.FullName -Filter "f4se_*.dll" -File | Where-Object { $_.Name -ne "f4se_steam_loader.dll" } | Select-Object -First 1
        $runtime = if ($dll) { $runtimeByDll[$dll.Name] } else { $null }
        if (-not $runtime) { Write-Warning "skipped $($archive.Name): $(if ($dll) { $dll.Name } else { 'no runtime dll' }) is not OG 1.10.163, NG 1.10.984 or AE 1.11.240"; continue }
        $ver = ($top.Name -replace '^f4se_', '') -split '_' | ForEach-Object { [int]$_ }
        $version = $ver -join '.'
        $name = "F4SE $runtime $version"
        $same = $built | Where-Object { $_.Runtime -eq $runtime }
        if ($same -and [version]$same.Version -ge [version]$version) { Write-Warning "skipped $($archive.Name): $($same.Name) is already built for $runtime"; continue }
        if ($same) { Remove-Item -LiteralPath (Join-Path $modsDir $same.Name) -Recurse -Force; $built = @($built | Where-Object { $_.Runtime -ne $runtime }) }
        $mod = Join-Path $modsDir $name
        if (Test-Path -LiteralPath $mod) { Remove-Item -LiteralPath $mod -Recurse -Force }
        $root = Join-Path $mod "Root"
        New-Item -ItemType Directory -Path $root -Force | Out-Null
        foreach ($f in "f4se_loader.exe", "f4se_steam_loader.dll", $dll.Name) {
            $src = Join-Path $top.FullName $f
            if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src -Destination $root }
        }
        $data = Join-Path $top.FullName "Data"
        if (Test-Path -LiteralPath $data) {
            Get-ChildItem -LiteralPath $data | Copy-Item -Destination $mod -Recurse
        }
        $meta = @("[General]", "gameName=Fallout4", "modid=42147", "version=$version", "newestVersion=", "category=0", "nexusFileStatus=1", "installationFile=$($archive.FullName -replace '\\', '/')", "repository=Nexus")
        [IO.File]::WriteAllText((Join-Path $mod "meta.ini"), (($meta -join "`r`n") + "`r`n"))
        Write-Host "built mod '$name' from $($archive.Name)"
        $built += [pscustomobject]@{ Name = $name; Runtime = $runtime; Version = $version }
    } finally {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$list = Join-Path $Instance "profiles\$Profile\modlist.txt"
if (-not (Test-Path -LiteralPath $list)) { throw "no profile modlist: $list" }
if (-not (Test-Path -LiteralPath "$list.bak")) { Copy-Item -LiteralPath $list -Destination "$list.bak" }
$lines = [System.Collections.Generic.List[string]]([IO.File]::ReadAllLines($list))
foreach ($b in $built) {
    $lines.RemoveAll([Predicate[string]] { param($l) $l.Substring([Math]::Min(1, $l.Length)) -eq $b.Name }) | Out-Null
    $flag = if ($b.Runtime -eq $activeRuntime) { "+" } else { "-" }
    $at = [Math]::Min(1, $lines.Count)
    $lines.Insert($at, "$flag$($b.Name)")
    Write-Host "modlist: $flag$($b.Name)"
}
[IO.File]::WriteAllText($list, (($lines -join "`r`n") + "`r`n"))
Write-Host "updated $list (first-run backup: modlist.txt.bak)"
if (-not ($built | Where-Object { $_.Runtime -eq "NG" })) {
    Write-Host "NG 1.10.984 needs F4SE 0.7.2 (f4se_1_10_984.dll) in $Downloads; re-run this script after downloading it"
}
