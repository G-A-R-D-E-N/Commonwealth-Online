#Requires -Version 5.1
<#
.SYNOPSIS
Runs the packaged Windows native server through the same gameplay, identity,
persistence and admin smokes the Linux lane runs.

.DESCRIPTION
The Windows package is cross-built on Linux and statically audited, which
proves what it imports but not that it runs. This is the counterpart of
smoke-native-linux-package.sh and smoke-native-linux-release.sh: a missing DLL,
a CRT mismatch or a bridge entry point that faults on load fails here instead
of in a release.

The smoke clients are cross-built alongside the package because the machine
running this has no toolchain. Each loads the bridge from its own directory.

The server resolves a relative durable_state_path and its .admin-token against
the directory holding the config file, so every scenario keeps its config in
the scratch directory and looks for those artifacts there.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $PackageDir,
    [Parameter(Mandatory = $true)][string] $SmokeDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$packagePath = (Resolve-Path -LiteralPath $PackageDir).Path
$smokePath = (Resolve-Path -LiteralPath $SmokeDir).Path
$server = Join-Path $packagePath 'CommonwealthOnline.Server.exe'

$clients = @{
    Gameplay    = Join-Path $smokePath 'CommonwealthOnlineServerLiveGnsSmoke.exe'
    Identity    = Join-Path $smokePath 'CommonwealthOnlineServerLiveIdentityGnsSmoke.exe'
    Persistence = Join-Path $smokePath 'CommonwealthOnlineServerLivePersistenceGnsSmoke.exe'
    Admin       = Join-Path $smokePath 'CommonwealthOnlineServerLiveAdminGnsSmoke.exe'
    ProfileBan  = Join-Path $smokePath 'CommonwealthOnlineServerLiveProfileBanGnsSmoke.exe'
}

$required = @(
    $server,
    (Join-Path $packagePath 'commonwealth_online_gns_bridge.dll'),
    (Join-Path $packagePath 'CommonwealthOnline.Server.native'),
    (Join-Path $smokePath 'commonwealth_online_gns_bridge.dll')
) + $clients.Values
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "runtime smoke input is missing: $path"
    }
}

function Get-FreePort {
    param([ValidateSet('udp', 'tcp')][string] $Protocol)

    for ($attempt = 0; $attempt -lt 200; $attempt++) {
        $candidate = Get-Random -Minimum 20000 -Maximum 45000
        $inUse = if ($Protocol -eq 'udp') {
            @(Get-NetUDPEndpoint -LocalPort $candidate -ErrorAction SilentlyContinue).Count -gt 0
        } else {
            @(Get-NetTCPConnection -State Listen -LocalPort $candidate -ErrorAction SilentlyContinue).Count -gt 0
        }
        if (-not $inUse) { return $candidate }
    }
    throw "could not find a free $Protocol port after 200 attempts"
}

function Get-AdminPort {
    param([int] $GamePort)

    do { $candidate = Get-FreePort -Protocol tcp } while ($candidate -eq $GamePort)
    return $candidate
}

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("co-native-win-smoke-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null

$script:ServerProcess = $null
$script:ServerLog = $null
$script:ServerErrorLog = $null

function Write-ServerLog {
    foreach ($output in @($script:ServerLog, $script:ServerErrorLog)) {
        if ($output -and (Test-Path -LiteralPath $output)) {
            $content = Get-Content -LiteralPath $output
            if ($content) {
                Write-Host "---- $([System.IO.Path]::GetFileName($output)) ----"
                $content | Write-Host
            }
        }
    }
}

function Stop-SmokeServer {
    if ($null -ne $script:ServerProcess) {
        if (-not $script:ServerProcess.HasExited) {
            Stop-Process -Id $script:ServerProcess.Id -Force -ErrorAction SilentlyContinue
            $script:ServerProcess.WaitForExit(10000) | Out-Null
        }
        $script:ServerProcess = $null
    }
}

function Start-SmokeServer {
    param(
        [Parameter(Mandatory = $true)][string] $ConfigPath,
        [Parameter(Mandatory = $true)][int] $GamePort,
        [Parameter(Mandatory = $true)][string] $LogName
    )

    Stop-SmokeServer
    $script:ServerLog = Join-Path $scratch "$LogName.log"
    $script:ServerErrorLog = Join-Path $scratch "$LogName.err.log"

    $script:ServerProcess = Start-Process -FilePath $server `
        -ArgumentList @('--config', $ConfigPath) `
        -WorkingDirectory $packagePath `
        -RedirectStandardOutput $script:ServerLog `
        -RedirectStandardError $script:ServerErrorLog `
        -NoNewWindow -PassThru

    for ($attempt = 0; $attempt -lt 200; $attempt++) {
        if ($script:ServerProcess.HasExited) {
            Write-ServerLog
            throw "packaged native server exited before becoming ready (exit code $($script:ServerProcess.ExitCode))"
        }
        if ((Test-Path -LiteralPath $script:ServerLog) -and
            (Select-String -LiteralPath $script:ServerLog -SimpleMatch "listening on 127.0.0.1:$GamePort (GNS)" -Quiet)) {
            return
        }
        Start-Sleep -Milliseconds 100
    }

    Write-ServerLog
    throw 'packaged native server did not become ready'
}

function Invoke-SmokeClient {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string[]] $ClientArguments
    )

    $name = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    Write-Host "==> $name $($ClientArguments -join ' ')"
    # Windows searches the executable's own directory first, so the bridge DLL
    # shipped beside the client resolves without touching PATH.
    & $Path @ClientArguments
    if ($LASTEXITCODE -ne 0) {
        Write-ServerLog
        throw "$name failed with exit code $LASTEXITCODE"
    }
}

function New-SmokeConfig {
    param(
        [Parameter(Mandatory = $true)][string] $Name,
        [Parameter(Mandatory = $true)][int] $GamePort,
        [Parameter(Mandatory = $true)][int] $AdminPort,
        [Parameter(Mandatory = $true)][string] $ServerName,
        [bool] $RequireV2Identity = $true,
        [bool] $EnablePersistence = $false
    )

    $config = [ordered]@{
        host                = '127.0.0.1'
        port                = $GamePort
        admin_port          = $AdminPort
        server_name         = $ServerName
        max_players         = 8
        require_v2_identity = $RequireV2Identity
        enable_persistence  = $EnablePersistence
        enable_chat         = $true
        enable_factions     = $true
        public_listing      = $false
        lan_discovery_enabled = $false
    }
    if ($EnablePersistence) {
        $config['durable_state_path'] = 'durable-state.json'
    }

    $path = Join-Path $scratch "$Name.json"
    ($config | ConvertTo-Json) | Set-Content -LiteralPath $path -Encoding UTF8
    return $path
}

try {
    # Gameplay relay on a clean package.
    $gameplayPort = Get-FreePort -Protocol udp
    $gameplayConfig = New-SmokeConfig -Name 'gameplay' -GamePort $gameplayPort `
        -AdminPort (Get-AdminPort -GamePort $gameplayPort) -ServerName 'Native Package Smoke'
    Start-SmokeServer -ConfigPath $gameplayConfig -GamePort $gameplayPort -LogName 'gameplay'
    Invoke-SmokeClient -Path $clients.Gameplay -ClientArguments @('127.0.0.1', "$gameplayPort")
    Stop-SmokeServer

    # Mandatory identity and resume behaviour.
    $identityPort = Get-FreePort -Protocol udp
    $identityConfig = New-SmokeConfig -Name 'identity' -GamePort $identityPort `
        -AdminPort (Get-AdminPort -GamePort $identityPort) -ServerName 'Native Release Identity' `
        -RequireV2Identity $true
    Start-SmokeServer -ConfigPath $identityConfig -GamePort $identityPort -LogName 'identity'
    Invoke-SmokeClient -Path $clients.Identity -ClientArguments @('127.0.0.1', "$identityPort")
    Stop-SmokeServer

    # Durable state and resume credentials across a real process restart.
    $persistencePort = Get-FreePort -Protocol udp
    $persistenceConfig = New-SmokeConfig -Name 'persistence' -GamePort $persistencePort `
        -AdminPort (Get-AdminPort -GamePort $persistencePort) -ServerName 'Native Release Persistence' `
        -RequireV2Identity $true -EnablePersistence $true
    $resumeToken = Join-Path $scratch 'persistence-resume-token.txt'
    $durableState = Join-Path $scratch 'durable-state.json'

    Start-SmokeServer -ConfigPath $persistenceConfig -GamePort $persistencePort -LogName 'persistence-seed'
    Invoke-SmokeClient -Path $clients.Persistence -ClientArguments @('seed', '127.0.0.1', "$persistencePort", $resumeToken, $durableState)
    Stop-SmokeServer

    if (-not (Test-Path -LiteralPath $durableState) -or (Get-Item -LiteralPath $durableState).Length -eq 0) {
        throw 'durable state was not written before restart'
    }

    Start-SmokeServer -ConfigPath $persistenceConfig -GamePort $persistencePort -LogName 'persistence-resume'
    Invoke-SmokeClient -Path $clients.Persistence -ClientArguments @('resume', '127.0.0.1', "$persistencePort", $resumeToken)
    Stop-SmokeServer

    # The authenticated admin contract the Host depends on.
    $adminGamePort = Get-FreePort -Protocol udp
    $adminPort = Get-AdminPort -GamePort $adminGamePort
    $adminConfig = New-SmokeConfig -Name 'admin' -GamePort $adminGamePort -AdminPort $adminPort `
        -ServerName 'Native Release Admin' -RequireV2Identity $true
    Start-SmokeServer -ConfigPath $adminConfig -GamePort $adminGamePort -LogName 'admin'

    $adminToken = Join-Path $scratch '.admin-token'
    $tokenReady = $false
    for ($attempt = 0; $attempt -lt 80; $attempt++) {
        if ((Test-Path -LiteralPath $adminToken) -and (Get-Item -LiteralPath $adminToken).Length -gt 0) {
            $tokenReady = $true
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $tokenReady) { throw 'packaged native server did not create an admin token' }

    Invoke-SmokeClient -Path $clients.Admin -ClientArguments @('127.0.0.1', "$adminGamePort", "$adminPort", $adminToken)
    Stop-SmokeServer

    Remove-Item -LiteralPath $adminToken -Force -ErrorAction SilentlyContinue
    Start-SmokeServer -ConfigPath $adminConfig -GamePort $adminGamePort -LogName 'profile-ban'
    $tokenReady = $false
    for ($attempt = 0; $attempt -lt 80; $attempt++) {
        if ((Test-Path -LiteralPath $adminToken) -and (Get-Item -LiteralPath $adminToken).Length -gt 0) {
            $tokenReady = $true
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $tokenReady) { throw 'packaged native server did not recreate an admin token' }
    Invoke-SmokeClient -Path $clients.ProfileBan -ClientArguments @('127.0.0.1', "$adminGamePort", "$adminPort", $adminToken)
    Stop-SmokeServer
}
finally {
    Stop-SmokeServer
    Write-ServerLog
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host 'native Windows package runtime smoke passed'
