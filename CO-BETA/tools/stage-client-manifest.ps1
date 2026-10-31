param(
    [Parameter(Mandatory = $true)][string] $PluginDir,
    [Parameter(Mandatory = $true)][string] $DataDir,
    [Parameter(Mandatory = $true)][string] $ClientManifest,
    [Parameter(Mandatory = $true)][string] $FingerprintFile,
    [Parameter(Mandatory = $true)][string] $VerifyFile,
    [Parameter(Mandatory = $true)][string] $RepoDir
)

$ErrorActionPreference = "Stop"

function Invoke-Xmake([string[]] $Arguments)
{
    $output = @(& xmake @Arguments)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0)
    {
        throw "xmake failed with exit code ${exitCode}: $($Arguments -join ' ')"
    }
    return $output
}

function Read-Fingerprint([object[]] $Output)
{
    $matches = @(
        $Output |
            ForEach-Object { ([string] $_).Trim() } |
            Where-Object { $_ -match '^[0-9a-f]{64}$' }
    )
    if ($matches.Count -eq 0)
    {
        throw "content-manifest tool did not return a lowercase SHA-256 fingerprint"
    }
    return $matches[-1]
}

function Get-Sha256Hex([byte[]] $Bytes)
{
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try
    {
        return -join ($sha.ComputeHash($Bytes) | ForEach-Object { $_.ToString("x2") })
    }
    finally
    {
        $sha.Dispose()
    }
}

function Write-UiProvenance([string] $RepoDir, [string] $DataDir)
{
    $pluginsDir = Join-Path $DataDir "F4SE\Plugins"
    $uiDll = Join-Path $pluginsDir "CommonwealthOnlineUI.dll"
    if (-not (Test-Path -LiteralPath $uiDll))
    {
        return
    }

    $commit = (& git -C $RepoDir rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0)
    {
        throw "git rev-parse HEAD failed for $RepoDir"
    }
    $dirty = @(& git -C $RepoDir status --porcelain -- CommonwealthOnlineUI)
    if ($LASTEXITCODE -ne 0)
    {
        throw "git status failed for the CommonwealthOnlineUI source tree"
    }

    $viewsDir = Join-Path $DataDir "PrismaUI_F4\views"
    $viewLines = [string[]] @(
        Get-ChildItem -LiteralPath $viewsDir -Recurse -File -Force |
            ForEach-Object {
                $relative = $_.FullName.Substring($viewsDir.Length + 1).Replace("\", "/")
                "$relative`t$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())`n"
            }
    )
    [Array]::Sort($viewLines, [StringComparer]::Ordinal)

    $provenance = [ordered]@{
        sourceCommit = $commit
        uiSourceDirty = ($dirty.Count -gt 0)
        uiDllSha256 = (Get-FileHash -LiteralPath $uiDll -Algorithm SHA256).Hash.ToLowerInvariant()
        uiViewsFileCount = $viewLines.Count
        uiViewsSha256 = Get-Sha256Hex ([System.Text.Encoding]::UTF8.GetBytes(($viewLines -join "")))
    }
    [System.IO.File]::WriteAllText(
        (Join-Path $pluginsDir "CommonwealthOnlineUI.provenance.json"),
        ($provenance | ConvertTo-Json) + [Environment]::NewLine)
}

Push-Location $PluginDir
try
{
    Invoke-Xmake @("build", "-y", "CommonwealthOnlineCoreContentManifestTool") |
        ForEach-Object { Write-Host $_ }

    Write-UiProvenance $RepoDir $DataDir

    $manifestOutput = Invoke-Xmake @(
        "run", "CommonwealthOnlineCoreContentManifestTool",
        "--root", $DataDir,
        "--exclude-name", "CommonwealthOnline.dll",
        "--exclude-name", "CommonwealthOnline-NG.dll",
        "--exclude-name", "CommonwealthOnline-OG.dll",
        "--exclude-name", "commonwealth_online_gns_bridge.dll",
        "--exclude-name", "CommonwealthOnlineUI.provenance.json",
        "--emit-client-json", $ClientManifest
    )
    $fingerprint = Read-Fingerprint $manifestOutput

    if (-not (Test-Path -LiteralPath $ClientManifest))
    {
        throw "CommonwealthOnlineClient.json was not generated"
    }

    $fingerprintParent = Split-Path -Parent $FingerprintFile
    if ($fingerprintParent)
    {
        New-Item -ItemType Directory -Force -Path $fingerprintParent | Out-Null
    }
    [System.IO.File]::WriteAllText($FingerprintFile, $fingerprint + [Environment]::NewLine)

    $verifyOutput = Invoke-Xmake @(
        "run", "CommonwealthOnlineCoreContentManifestTool",
        "--root", $DataDir,
        "--exclude-name", "CommonwealthOnline.dll",
        "--exclude-name", "CommonwealthOnline-NG.dll",
        "--exclude-name", "CommonwealthOnline-OG.dll",
        "--exclude-name", "commonwealth_online_gns_bridge.dll",
        "--exclude-name", "CommonwealthOnlineUI.provenance.json"
    )
    $verifiedFingerprint = Read-Fingerprint $verifyOutput
    [System.IO.File]::WriteAllText($VerifyFile, $verifiedFingerprint + [Environment]::NewLine)

    if ($fingerprint -ne $verifiedFingerprint)
    {
        throw "staged content fingerprint changed after CommonwealthOnlineClient.json was emitted"
    }

    Remove-Item -LiteralPath $VerifyFile -Force -ErrorAction SilentlyContinue
    Write-Host "  F4SE\Plugins\CommonwealthOnlineClient.json"
}
finally
{
    Pop-Location
}
