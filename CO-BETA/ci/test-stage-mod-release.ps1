$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$repoRoot = (Resolve-Path (Join-Path $root "..")).Path
$stageScript = Join-Path $root "stage-mod.bat"
$pluginBuild = Join-Path $root "plugin\build\windows\x64\release"
$bridgeBuild = Join-Path $root "plugin\build\gns-bridge\bin"
$uiPackage = Join-Path $repoRoot "CommonwealthOnlineUI\package"
$runnerTemp = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [System.IO.Path]::GetTempPath() }
$fakeBin = Join-Path $runnerTemp "co-stage-contract-$PID"
$oldPath = $env:PATH
$fingerprint = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
$addictolDir = Join-Path $root "ThirdParty\Addictol"
$crashLoggerDir = Join-Path $root "ThirdParty\AddictolCrashLogger"
$createdFiles = [System.Collections.Generic.List[string]]::new()

function Write-DummyFile([string]$Path, [string]$Content = "contract")
{
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    Set-Content -LiteralPath $Path -Value $Content -NoNewline
}

function Write-MissingDummyFile([string]$Path)
{
    if (Test-Path -LiteralPath $Path)
    {
        return
    }
    Write-DummyFile $Path
    $createdFiles.Add($Path)
}

function Assert-Crlf([string]$Path)
{
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    for ($index = 0; $index -lt $bytes.Length; $index++)
    {
        if ($bytes[$index] -eq 10 -and ($index -eq 0 -or $bytes[$index - 1] -ne 13))
        {
            throw "$Path contains LF-only line endings; cmd.exe batch labels require CRLF in this repository"
        }
    }
}

function Invoke-Stage([string[]]$Arguments)
{
    & cmd.exe /d /c ('"' + $stageScript + '" ' + ($Arguments -join ' ')) |
        ForEach-Object { Write-Host $_ }
    $exitCode = $LASTEXITCODE
    return $exitCode
}

try
{
    Assert-Crlf $stageScript

    Write-DummyFile (Join-Path $pluginBuild "CommonwealthOnline.dll")
    Write-DummyFile (Join-Path $bridgeBuild "commonwealth_online_gns_bridge.dll")
    Write-DummyFile (Join-Path $uiPackage "PrismaUI_F4\views\contract\index.html")

    Write-MissingDummyFile (Join-Path $addictolDir ".Build\F4SE\Plugins\Addictol.dll")
    Write-MissingDummyFile (Join-Path $crashLoggerDir "build\windows\x64\release\AddictolCrashLogger.dll")
    Write-MissingDummyFile (Join-Path $addictolDir "data\F4SE\Plugins\Addictol.toml")
    Write-MissingDummyFile (Join-Path $addictolDir "data\F4SE\Plugins\Addictol_FacegenExceptions.ini")
    Write-MissingDummyFile (Join-Path $addictolDir "data\F4SE\Plugins\Addictol_SNCT.ini")
    Write-MissingDummyFile (Join-Path $crashLoggerDir "data\F4SE\Plugins\AddictolCrashLogger.toml")
    Write-MissingDummyFile (Join-Path $addictolDir "data\Scripts\Addictol.pex")
    Write-MissingDummyFile (Join-Path $addictolDir "data\Scripts\XCELL.pex")
    Write-MissingDummyFile (Join-Path $addictolDir "LICENSE")
    Write-MissingDummyFile (Join-Path $addictolDir "EXCEPTIONS")
    Write-MissingDummyFile (Join-Path $crashLoggerDir "LICENSE")
    Write-MissingDummyFile (Join-Path $crashLoggerDir "EXCEPTIONS")

    New-Item -ItemType Directory -Force -Path $fakeBin | Out-Null
    $fakeXmake = @'
@echo off
setlocal EnableExtensions
if /I "%~1"=="build" exit /b 0
if /I not "%~1"=="run" exit /b 1
set "EMIT="
:scan
if "%~1"=="" goto done
if /I "%~1"=="--emit-client-json" set "EMIT=%~2"
shift
goto scan
:done
if defined EMIT (
    for %%D in ("%EMIT%") do if not exist "%%~dpD" mkdir "%%~dpD"
    >"%EMIT%" echo {"clientVersion":"0.1.0","contentManifestFingerprint":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}
)
echo aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
exit /b 0
'@
    $fakeXmake = $fakeXmake -replace "`r?`n", "`r`n"
    [System.IO.File]::WriteAllText(
        (Join-Path $fakeBin "xmake.cmd"),
        $fakeXmake,
        [System.Text.ASCIIEncoding]::new())
    $env:PATH = "$fakeBin;$oldPath"

    $strictExit = Invoke-Stage @("--release")
    if ($strictExit -ne 0)
    {
        throw "strict release staging should succeed for a complete payload, exit=$strictExit"
    }

    $staged = Join-Path $root "build\Fallout 4\Data"
    $manifest = Join-Path $staged "F4SE\Plugins\CommonwealthOnlineClient.json"
    $fingerprintFile = Join-Path $root "build\content-manifest.txt"
    if (-not (Test-Path -LiteralPath $manifest))
    {
        throw "strict release staging did not emit CommonwealthOnlineClient.json"
    }
    if (-not (Test-Path -LiteralPath $fingerprintFile))
    {
        throw "strict release staging did not emit content-manifest.txt"
    }
    if ((Get-Content -LiteralPath $fingerprintFile -Raw).Trim() -ne $fingerprint)
    {
        throw "strict release staging recorded an unexpected fingerprint"
    }

    foreach ($file in @("F4SE\Plugins\Addictol.dll", "F4SE\Plugins\AddictolCrashLogger.dll", "F4SE\Plugins\Addictol.toml", "F4SE\Plugins\AddictolCrashLogger.toml"))
    {
        if (-not (Test-Path -LiteralPath (Join-Path $staged $file)))
        {
            throw "strict release staging did not stage $file"
        }
    }

    $provenanceFile = Join-Path $staged "F4SE\Plugins\CommonwealthOnlineUI.provenance.json"
    if (-not (Test-Path -LiteralPath $provenanceFile))
    {
        throw "strict release staging did not emit CommonwealthOnlineUI.provenance.json"
    }
    $provenance = Get-Content -LiteralPath $provenanceFile -Raw | ConvertFrom-Json
    $expectedCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
    if ($provenance.sourceCommit -ne $expectedCommit)
    {
        throw "UI provenance sourceCommit does not match the staged repository HEAD"
    }
    if ($provenance.uiSourceDirty -isnot [bool])
    {
        throw "UI provenance uiSourceDirty must be a boolean"
    }
    $expectedDllHash = (Get-FileHash -LiteralPath (Join-Path $staged "F4SE\Plugins\CommonwealthOnline.dll") -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($provenance.uiDllSha256 -ne $expectedDllHash)
    {
        throw "UI provenance uiDllSha256 does not match the staged CommonwealthOnline.dll"
    }
    $viewHash = (Get-FileHash -LiteralPath (Join-Path $staged "PrismaUI_F4\views\contract\index.html") -Algorithm SHA256).Hash.ToLowerInvariant()
    $viewsDigest = [System.Security.Cryptography.SHA256]::Create().ComputeHash(
        [System.Text.Encoding]::UTF8.GetBytes("contract/index.html`t$viewHash`n"))
    $expectedViewsHash = -join ($viewsDigest | ForEach-Object { $_.ToString("x2") })
    if ($provenance.uiViewsFileCount -ne 1 -or $provenance.uiViewsSha256 -ne $expectedViewsHash)
    {
        throw "UI provenance views digest does not match the staged PrismaUI_F4\views tree"
    }

    function Assert-RuntimePackage([string]$RuntimeDir, [string]$ExpectedDll, [string[]]$ForbiddenDlls)
    {
        $plugins = Join-Path $RuntimeDir "Fallout 4\Data\F4SE\Plugins"
        $expectedPath = Join-Path $plugins $ExpectedDll
        if (-not (Test-Path -LiteralPath $expectedPath))
        {
            throw "Runtime package $RuntimeDir is missing expected DLL: $ExpectedDll"
        }
        foreach ($forbidden in $ForbiddenDlls)
        {
            $forbiddenPath = Join-Path $plugins $forbidden
            if (Test-Path -LiteralPath $forbiddenPath)
            {
                throw "Runtime package $RuntimeDir contains conflicting DLL: $forbidden"
            }
        }
        $bridgePath = Join-Path $plugins "commonwealth_online_gns_bridge.dll"
        if (-not (Test-Path -LiteralPath $bridgePath))
        {
            throw "Runtime package $RuntimeDir is missing bridge DLL"
        }
        $manifestPath = Join-Path $plugins "CommonwealthOnlineClient.json"
        if (-not (Test-Path -LiteralPath $manifestPath))
        {
            throw "Runtime package $RuntimeDir is missing CommonwealthOnlineClient.json"
        }
    }

    foreach ($runtime in @("AE", "NG", "OG"))
    {
        Assert-RuntimePackage (Join-Path $root "build\$runtime") "CommonwealthOnline.dll" @()
    }

    $addictolRuntimeFiles = @(
        "Fallout 4\Data\F4SE\Plugins\Addictol.dll",
        "Fallout 4\Data\F4SE\Plugins\AddictolCrashLogger.dll",
        "Fallout 4\Data\F4SE\Plugins\Addictol.toml",
        "Fallout 4\Data\F4SE\Plugins\AddictolCrashLogger.toml",
        "Fallout 4\Data\Scripts\Addictol.pex",
        "Fallout 4\Data\CommonwealthOnline\Licenses\Addictol-LICENSE.txt",
        "Fallout 4\Data\CommonwealthOnline\Licenses\Addictol-EXCEPTIONS.txt",
        "Fallout 4\Data\CommonwealthOnline\Licenses\AddictolCrashLogger-LICENSE.txt",
        "Fallout 4\Data\CommonwealthOnline\Licenses\AddictolCrashLogger-EXCEPTIONS.txt",
        "Fallout 4\Data\CommonwealthOnline\Licenses\Addictol.md"
    )
    foreach ($runtime in @("AE", "NG", "OG"))
    {
        foreach ($file in $addictolRuntimeFiles)
        {
            if (-not (Test-Path -LiteralPath (Join-Path $root "build\$runtime\$file")))
            {
                throw "Runtime package $runtime is missing $file"
            }
        }
    }

    Remove-Item -LiteralPath $uiPackage -Recurse -Force
    $developerExit = Invoke-Stage @()
    if ($developerExit -ne 0)
    {
        throw "developer staging should remain available without the optional UI package, exit=$developerExit"
    }

    Write-Host "stage-mod.bat strict release and developer staging contracts passed"
}
finally
{
    $env:PATH = $oldPath
    foreach ($file in $createdFiles)
    {
        Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath (Join-Path $root "build") -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $root "plugin\build") -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $uiPackage -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $fakeBin -Recurse -Force -ErrorAction SilentlyContinue
}
