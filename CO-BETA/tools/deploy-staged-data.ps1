param(
    [Parameter(Mandatory = $true)]
    [string]$StagedData,

    [Parameter(Mandatory = $true)]
    [string]$DataDir
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'debug-log.ps1')

$sourceRoot = [System.IO.Path]::GetFullPath($StagedData).TrimEnd('\', '/')
$targetRoot = [System.IO.Path]::GetFullPath($DataDir).TrimEnd('\', '/')
Write-CODebug -Component 'Deploy' -Message "source=$sourceRoot target=$targetRoot"
if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    throw "Staged Data directory not found: $sourceRoot"
}
if (-not (Test-Path -LiteralPath $targetRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null
}

$stateDir = Join-Path $targetRoot 'CommonwealthOnline'
$manifestPath = Join-Path $stateDir 'installed-files.txt'
New-Item -ItemType Directory -Path $stateDir -Force | Out-Null

function Get-RelativeFileList([string]$Root) {
    $prefix = $Root.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    Get-ChildItem -LiteralPath $Root -File -Recurse | ForEach-Object {
        $_.FullName.Substring($prefix.Length).Replace('/', '\')
    } | Sort-Object -Unique
}

function Resolve-OwnedPath([string]$RelativePath) {
    if ([System.IO.Path]::IsPathRooted($RelativePath)) {
        return $null
    }
    $normalized = $RelativePath.Replace('/', '\')
    if ($normalized.Split('\') -contains '..') {
        return $null
    }
    $candidate = [System.IO.Path]::GetFullPath((Join-Path $targetRoot $normalized))
    $targetPrefix = $targetRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($targetPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $null
    }
    return $candidate
}

$newFiles = @(Get-RelativeFileList $sourceRoot)
$newSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $newFiles) {
    [void]$newSet.Add($entry)
}

if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    foreach ($entry in Get-Content -LiteralPath $manifestPath) {
        $relative = $entry.Trim()
        if ($relative.Length -eq 0 -or $newSet.Contains($relative)) {
            continue
        }
        $ownedPath = Resolve-OwnedPath $relative
        if ($null -ne $ownedPath -and (Test-Path -LiteralPath $ownedPath -PathType Leaf)) {
            Write-CODebug -Component 'Deploy' -Message "remove obsolete $relative"
            Remove-Item -LiteralPath $ownedPath -Force
        }
    }
}

$legacyFiles = @(
    'Scripts\CoSync.pex',
    'Scripts\CoSyncPlayer.pex',
    'Scripts\CoSyncQuest.pex'
)
foreach ($relative in $legacyFiles) {
    if ($newSet.Contains($relative)) {
        continue
    }
    $legacyPath = Resolve-OwnedPath $relative
    if ($null -ne $legacyPath -and (Test-Path -LiteralPath $legacyPath -PathType Leaf)) {
        Write-CODebug -Component 'Deploy' -Message "remove legacy $relative"
        Remove-Item -LiteralPath $legacyPath -Force
    }
}

foreach ($relative in $newFiles) {
    $sourcePath = Join-Path $sourceRoot $relative
    $destinationPath = Resolve-OwnedPath $relative
    if ($null -eq $destinationPath) {
        throw "Invalid staged relative path: $relative"
    }
    $destinationDirectory = Split-Path -Parent $destinationPath
    if (-not (Test-Path -LiteralPath $destinationDirectory -PathType Container)) {
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    }
    Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
}

$manifestTemp = $manifestPath + '.tmp'
$newFiles | Set-Content -LiteralPath $manifestTemp -Encoding UTF8
Move-Item -LiteralPath $manifestTemp -Destination $manifestPath -Force

Write-CODebug -Component 'Deploy' -Message "manifest=$manifestPath files=$($newFiles.Count)"
Write-Host "Deployed $($newFiles.Count) staged Commonwealth Online file(s)."
