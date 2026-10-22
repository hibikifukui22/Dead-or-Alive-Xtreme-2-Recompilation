# Fetches the ReXGlue SDK submodule (v0.10.0), applies this project's SDK
# patch and builds/installs the SDK into sdk/rexglue-sdk/out/install/win-amd64.
# Safe to run again: an already applied patch is detected and skipped.
param([int]$Jobs = 8, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$sdk = Join-Path $projectRoot 'sdk/rexglue-sdk'
$patch = Join-Path $projectRoot 'patches/rexglue-sdk/0001-doax2-runtime-changes.patch'
Push-Location $projectRoot
try {
    # The SDK has paths longer than the Windows default limit.
    git config core.longpaths true
    git submodule update --init --recursive
    if ($LASTEXITCODE) { throw 'Submodule update failed.' }
    git -C $sdk config core.longpaths true
    git -C $sdk apply --reverse --check $patch 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host 'SDK patch already applied.'
    } else {
        git -C $sdk apply --check $patch
        if ($LASTEXITCODE) { throw 'The SDK patch does not apply; is sdk/rexglue-sdk at the pinned commit?' }
        git -C $sdk apply $patch
        if ($LASTEXITCODE) { throw 'Applying the SDK patch failed.' }
        Write-Host 'SDK patch applied.'
    }
} finally { Pop-Location }
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-sdk.ps1') -Jobs $Jobs
}
