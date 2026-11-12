#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$serverDir = Join-Path $root 'CO-SERVER/server-cpp/build/windows/x64/debug'
$server = Join-Path $serverDir 'CommonwealthOnlineServerCpp.exe'
$serverBridge = Join-Path $root 'CO-SERVER/build/gns-server-bridge-windows/Release/commonwealth_online_gns_bridge.dll'
$client = Join-Path $PSScriptRoot 'build/windows/x64/debug/CommonwealthOnlineClientProtocolAcceptance.exe'
$clientBridge = Join-Path $root 'CO-BETA/plugin/build/gns-bridge/bin/commonwealth_online_gns_bridge.dll'
foreach ($path in @($server, $serverBridge, $client, $clientBridge)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "missing acceptance input: $path" }
}
Copy-Item -LiteralPath $serverBridge -Destination $serverDir -Force

function Get-FreePort([string] $Protocol) {
    for ($attempt = 0; $attempt -lt 200; $attempt++) {
        $port = Get-Random -Minimum 20000 -Maximum 45000
        $inUse = if ($Protocol -eq 'udp') {
            @(Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue).Count -gt 0
        } else {
            @(Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue).Count -gt 0
        }
        if (-not $inUse) { return $port }
    }
    throw "could not find a free $Protocol port"
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('co-client-acceptance-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$process = $null
try {
    $compatibility = Get-Content -Raw (Join-Path $root 'co-core/include/F4TClientCompatibility.h')
    $clientVersion = [regex]::Match($compatibility, 'kClientVersion = "([^"]+)"').Groups[1].Value
    $runtimeVersion = [regex]::Match($compatibility, 'kAeRuntimeVersion = "([^"]+)"').Groups[1].Value
    if (-not $clientVersion -or -not $runtimeVersion) { throw 'could not read compatibility versions' }
    $requirements = @{ required_client_version = $clientVersion; required_fallout_runtime_version = $runtimeVersion; required_content_manifest_fingerprint = ('a' * 64); required_capabilities = @('acceptance-required') }
    $capacity = @{ max_players = 1; resume_token_lifetime_seconds = 1 }
    $cases = @(
        @{ target = 'current'; scenario = 'default'; config = $null },
        @{ target = 'ng'; scenario = 'default'; config = $null },
        @{ target = 'og'; scenario = 'default'; config = $null },
        @{ target = 'current'; scenario = 'requirements'; config = $requirements },
        @{ target = 'current'; scenario = 'capacity'; config = $capacity }
    )
    foreach ($case in $cases) {
        $port = Get-FreePort 'udp'
        do { $adminPort = Get-FreePort 'tcp' } while ($adminPort -eq $port)
        $args = @('--bind', '127.0.0.1', '--port', "$port", '--admin-port', "$adminPort", '--no-lan-discovery', '--no-public-listing')
        if ($null -ne $case.config) {
            $configPath = Join-Path $scratch "$($case.scenario).json"
            [IO.File]::WriteAllText($configPath, ($case.config | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
            $args += @('--config', ('"' + $configPath + '"'))
        }
        $log = Join-Path $scratch "$($case.target)-$($case.scenario).log"
        $errorLog = "$log.err"
        $process = Start-Process -FilePath $server -ArgumentList $args -WorkingDirectory $serverDir -RedirectStandardOutput $log -RedirectStandardError $errorLog -WindowStyle Hidden -PassThru
        $ready = $false
        for ($attempt = 0; $attempt -lt 600; $attempt++) {
            if ($process.HasExited) { throw "server exited before acceptance: $($process.ExitCode)" }
            if ((Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -SimpleMatch "listening on 127.0.0.1:$port (GNS)" -Quiet)) { $ready = $true; break }
            Start-Sleep -Milliseconds 50
        }
        if (-not $ready) { throw 'server did not become ready' }
        & $client $clientBridge '127.0.0.1' "$port" $case.target $case.scenario
        if ($LASTEXITCODE -ne 0) { throw "client acceptance failed: $LASTEXITCODE" }
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
        $process = $null
    }
} finally {
    if ($null -ne $process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() }
    Get-ChildItem -LiteralPath $scratch -Filter '*.log*' | ForEach-Object { Write-Host "--- $($_.Name) ---"; Get-Content -LiteralPath $_.FullName | Write-Host }
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
