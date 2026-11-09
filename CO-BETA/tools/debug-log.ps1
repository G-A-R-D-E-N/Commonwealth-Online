function Test-CODebugEnabled {
    $value = $env:COMMONWEALTH_ONLINE_DEBUG
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $true
    }
    switch ($value.Trim().ToLowerInvariant()) {
        '0' { return $false }
        'false' { return $false }
        'no' { return $false }
        'off' { return $false }
        'release' { return $false }
        'disabled' { return $false }
        default { return $true }
    }
}

function Write-CODebug {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Component,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if (Test-CODebugEnabled) {
        Write-Host "[CO DEBUG][$Component] $Message"
    }
}
