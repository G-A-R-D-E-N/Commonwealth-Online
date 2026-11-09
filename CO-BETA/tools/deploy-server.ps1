param(
    [Parameter(Mandatory = $true)]
    [string]$DataDir,

    [string]$Package
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'debug-log.ps1')

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$targetRoot = [System.IO.Path]::GetFullPath($DataDir).TrimEnd('\', '/')
$serverName = 'CommonwealthOnline.Server.exe'
$hostStateNames = @(
    'commonwealth-server.json',
    'workshop-zones.json',
    'bans.json',
    'durable-state.json',
    'durable-state.json.bak',
    'durable-state.json.save-recovery',
    'durable-state.json.save-recovery.bak',
    'world-policy.json',
    'blocked-words.txt',
    'raid_settings.json',
    '.admin-token',
    'admins.json',
    '.save-attestation-key',
    '.player-key-secret',
    'admin-audit.log',
    'mo2-hash-cache.txt'
)
$configPathKeys = @(
    'worldPolicyPath',
    'world_policy_path',
    'durableStatePath',
    'durable_state_path',
    'questStoryOrderPath',
    'quest_story_order_path',
    'requiredModpackPath',
    'required_modpack_path',
    'requiredMo2Instance',
    'required_mo2_instance',
    'modpackDownloadDir',
    'modpack_download_dir'
)
$script:expandedPackage = $null

function Get-HostServerFolder() {
    if (-not $env:USERPROFILE) {
        return $null
    }
    $store = Join-Path $env:USERPROFILE 'Documents\My Games\Fallout4\CommonwealthOnline\host-server-folder.json'
    if (-not (Test-Path -LiteralPath $store -PathType Leaf)) {
        return $null
    }
    try {
        $saved = (Get-Content -LiteralPath $store -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop).serverDirectory
    } catch {
        return $null
    }
    if (-not $saved) {
        return $null
    }
    return [System.IO.Path]::GetFullPath([string]$saved).TrimEnd('\', '/')
}

function New-HostStatePlan([string]$From, [string]$To) {
    $result = @{ Unreadable = $false; Conflict = $false; Reason = ''; Names = @(); Files = @() }
    if (-not $From -or -not (Test-Path -LiteralPath $From -PathType Container)) {
        return $result
    }
    $fromFull = [System.IO.Path]::GetFullPath($From).TrimEnd('\', '/')
    $toFull = [System.IO.Path]::GetFullPath($To).TrimEnd('\', '/')

    $names = New-Object System.Collections.Generic.List[string]
    foreach ($name in $hostStateNames) {
        $names.Add($name)
    }
    $config = Join-Path $fromFull 'commonwealth-server.json'
    if (Test-Path -LiteralPath $config -PathType Leaf) {
        try {
            $root = Get-Content -LiteralPath $config -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        } catch {
            $result['Unreadable'] = $true
            $result['Reason'] = 'the saved configuration could not be read'
            $result['Names'] = $names.ToArray()
            return $result
        }
        foreach ($key in $configPathKeys) {
            $value = $root.$key
            if ($value -is [string] -and $value -and -not [System.IO.Path]::IsPathRooted($value)) {
                $resolvedSource = [System.IO.Path]::GetFullPath((Join-Path $fromFull $value))
                $resolvedTarget = [System.IO.Path]::GetFullPath((Join-Path $toFull $value))
                if (-not $resolvedSource.StartsWith($fromFull + '\', [System.StringComparison]::OrdinalIgnoreCase) -or
                    -not $resolvedTarget.StartsWith($toFull + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
                    $result['Unreadable'] = $true
                    $result['Reason'] = 'a configured relative path is outside the server folder: ' + $value
                    return $result
                }
                $value = $resolvedSource.Substring($fromFull.Length + 1)
                $names.Add($value)
                if ($key -in @('durableStatePath', 'durable_state_path')) {
                    $names.Add($value + '.bak')
                    $names.Add($value + '.save-recovery')
                    $names.Add($value + '.save-recovery.bak')
                }
            }
        }
    }
    $result['Names'] = $names.ToArray()
    if ($fromFull -ieq $toFull) {
        return $result
    }

    $seen = @{}
    $items = New-Object System.Collections.Generic.List[object]
    foreach ($name in $names) {
        if (-not $name) {
            continue
        }
        $key = $name.ToLowerInvariant()
        if ($seen.ContainsKey($key)) {
            continue
        }
        $seen[$key] = $true
        $source = Join-Path $fromFull $name
        if (-not (Test-Path -LiteralPath $source)) {
            continue
        }
        $destination = Join-Path $toFull $name
        $isDirectory = Test-Path -LiteralPath $source -PathType Container
        if (Test-Path -LiteralPath $destination -PathType Leaf) {
            if ($isDirectory) {
                $result['Conflict'] = $true
                $result['Reason'] = 'the deploy target already has its own ' + $name
                return $result
            }
            try {
                $same = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
            } catch {
                $result['Conflict'] = $true
                $result['Reason'] = 'the host state in ' + $name + ' could not be read'
                return $result
            }
            if (-not $same) {
                $result['Conflict'] = $true
                $result['Reason'] = 'the deploy target already has its own ' + $name
                return $result
            }
            continue
        }
        if (Test-Path -LiteralPath $destination) {
            $result['Conflict'] = $true
            $result['Reason'] = 'the deploy target already has a folder at ' + $name
            return $result
        }
        $items.Add([pscustomobject]@{
            Source = $source
            Dest = $destination
            Relative = $name
            IsDirectory = $isDirectory
        })
    }
    $result['Files'] = $items.ToArray()
    return $result
}

function Set-HostState([object[]]$Plan, [string]$TargetRoot) {
    if (-not $Plan -or $Plan.Count -eq 0) {
        return @{ Ok = $true; Count = 0 }
    }
    $stage = Join-Path $TargetRoot ('.co-migrate-' + [System.Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    try {
        $staged = New-Object System.Collections.Generic.List[object]
        $index = 0
        foreach ($item in $Plan) {
            $stagedPath = Join-Path $stage ([string]$index)
            if ($item.IsDirectory) {
                Copy-Item -LiteralPath $item.Source -Destination $stagedPath -Recurse -ErrorAction Stop
            } else {
                Copy-Item -LiteralPath $item.Source -Destination $stagedPath -ErrorAction Stop
            }
            $staged.Add([pscustomobject]@{ Item = $item; Path = $stagedPath; Index = $index })
            $index++
        }

        $done = New-Object System.Collections.Generic.List[object]
        try {
            foreach ($entry in $staged) {
                $parent = Split-Path -Parent $entry.Item.Dest
                New-Item -ItemType Directory -Path $parent -Force | Out-Null
                $backup = $null
                if (Test-Path -LiteralPath $entry.Item.Dest) {
                    $backup = Join-Path $stage ('bak-' + $entry.Index)
                    Move-Item -LiteralPath $entry.Item.Dest -Destination $backup -ErrorAction Stop
                }
                try {
                    Move-Item -LiteralPath $entry.Path -Destination $entry.Item.Dest -ErrorAction Stop
                } catch {
                    if ($backup) {
                        Move-Item -LiteralPath $backup -Destination $entry.Item.Dest -Force -ErrorAction SilentlyContinue
                    }
                    throw
                }
                $done.Add([pscustomobject]@{ Dest = $entry.Item.Dest; Backup = $backup })
            }
        } catch {
            foreach ($entry in $done) {
                if ($entry.Backup) {
                    Move-Item -LiteralPath $entry.Backup -Destination $entry.Dest -Force -ErrorAction SilentlyContinue
                } else {
                    Remove-Item -LiteralPath $entry.Dest -Recurse -Force -ErrorAction SilentlyContinue
                }
            }
            throw
        }
        return @{ Ok = $true; Count = $staged.Count }
    } catch {
        return @{ Ok = $false; Reason = $_.Exception.Message }
    } finally {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Reset-HostServerFolder([string]$ServerFolder) {
    if (-not $env:USERPROFILE) {
        Write-Warning 'The user profile folder is unknown, so Server settings keeps the last folder it saved.'
        return
    }
    $store = Join-Path $env:USERPROFILE 'Documents\My Games\Fallout4\CommonwealthOnline\host-server-folder.json'
    try {
        New-Item -ItemType Directory -Path (Split-Path -Parent $store) -Force | Out-Null
        $json = @{ serverDirectory = $ServerFolder; movedByDeploy = $true } | ConvertTo-Json -Compress
        [System.IO.File]::WriteAllText($store, $json, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host "Server settings now use $ServerFolder"
    } catch {
        Write-Warning ('The package was deployed, but Server settings could not be pointed at it: ' + $_.Exception.Message)
    }
}

function Resolve-ServerPackage([string]$ExplicitPackage) {
    $candidates = @()
    if ($ExplicitPackage) {
        $candidates += $ExplicitPackage
    }
    foreach ($name in @('CO_NATIVE_SERVER_PACKAGE', 'CO_NATIVE_PACKAGE_DIR')) {
        $value = [System.Environment]::GetEnvironmentVariable($name)
        if ($value) {
            $candidates += $value
        }
    }
    $candidates += (Join-Path $repoRoot 'CO-SERVER\out\native-win-x64')
    $candidates += (Join-Path $repoRoot 'CO-SERVER\server-cpp\out\native-win-x64')
    foreach ($candidate in $candidates) {
        if (-not (Test-Path -LiteralPath $candidate)) {
            continue
        }
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
        if ([System.IO.Path]::GetExtension($candidate) -ieq '.zip') {
            $temp = Join-Path ([System.IO.Path]::GetTempPath()) ('co-server-package-' + [System.Guid]::NewGuid().ToString('N'))
            New-Item -ItemType Directory -Path $temp -Force | Out-Null
            Expand-Archive -LiteralPath $candidate -DestinationPath $temp -Force
            $script:expandedPackage = $temp
            return $temp
        }
    }
    return $null
}

$source = Resolve-ServerPackage $Package
if (-not $source) {
    Write-Warning ("No native server package was found, so Data\server was not deployed. " +
        'Build one with CO-SERVER/server-cpp/scripts/build-native-package-ci.sh win-x64 <dir>, or set ' +
        'CO_NATIVE_SERVER_PACKAGE. In-game hosting needs the package in Data\server.')
    Write-CODebug -Component 'Deploy' -Message 'server package not found; Data\server not deployed'
    exit 0
}

$serverExe = Join-Path $source $serverName
try {
    if (-not (Test-Path -LiteralPath $serverExe -PathType Leaf)) {
        throw "The server package has no $serverName`: $source"
    }

    $target = Join-Path $targetRoot 'server'
    New-Item -ItemType Directory -Path $target -Force | Out-Null

    $previousFolder = Get-HostServerFolder
    if (-not $previousFolder -or -not (Test-Path -LiteralPath $previousFolder -PathType Container)) {
        $previousFolder = $target
    }
    $plan = New-HostStatePlan $previousFolder $target

    if ($plan.Unreadable) {
        Write-Warning ('The native server package was not deployed and Server settings still points at ' + $previousFolder +
            ' because ' + $plan.Reason + '. Fix that file, or choose another folder in Server settings.')
        Write-CODebug -Component 'Deploy' -Message 'saved configuration is unreadable; deploy skipped'
    } else {
        $protected = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
        foreach ($name in $hostStateNames) {
            [void]$protected.Add($name)
        }
        foreach ($name in $plan.Names) {
            [void]$protected.Add($name)
        }

        Get-ChildItem -LiteralPath $source -Recurse -File -Force | ForEach-Object {
            $relative = $_.FullName.Substring($source.Length).TrimStart('\', '/')
            $destination = Join-Path $target $relative
            if (Test-Path -LiteralPath $destination) {
                foreach ($name in $protected) {
                    if ($relative -ieq $name -or
                        $relative.StartsWith($name + '\', [System.StringComparison]::OrdinalIgnoreCase) -or
                        $relative.StartsWith($name + '/', [System.StringComparison]::OrdinalIgnoreCase)) {
                        return
                    }
                }
            }
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
        }

        Write-CODebug -Component 'Deploy' -Message "server package=$source target=$target"
        Write-Host "Deployed the native server package to $target"

        if ($plan.Conflict) {
            Write-Warning ('The package was deployed, but the host state in ' + $previousFolder + ' was not moved to ' + $target +
                ' because ' + $plan.Reason + '. Server settings still points at the folder holding it.')
            Write-CODebug -Component 'Deploy' -Message 'host state not moved; kept the saved server folder'
        } else {
            $migrated = Set-HostState $plan.Files $targetRoot
            if (-not $migrated.Ok) {
                Write-Warning ('The package was deployed, but the host state in ' + $previousFolder + ' was not moved to ' + $target +
                    ' because ' + $migrated.Reason + '. Server settings still points at the folder holding it.')
                Write-CODebug -Component 'Deploy' -Message 'host state not moved; kept the saved server folder'
            } else {
                if ($migrated.Count -gt 0) {
                    Write-Host ('Copied the host state from ' + $previousFolder + ' to ' + $target + '.')
                }
                Reset-HostServerFolder $target
            }
        }
    }
} finally {
    if ($script:expandedPackage) {
        Remove-Item -LiteralPath $script:expandedPackage -Recurse -Force -ErrorAction SilentlyContinue
    }
}
