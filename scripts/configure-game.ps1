$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/dev-shell.ps1"
$projectRoot = Split-Path $PSScriptRoot -Parent
$sdkInstall = Join-Path $projectRoot 'sdk/rexglue-sdk/out/install/win-amd64'
Push-Location $projectRoot
try {
    cmake --preset win-amd64-release -B out/game "-DCMAKE_PREFIX_PATH=$sdkInstall"
    if ($LASTEXITCODE) { throw 'DOAX2 configure failed.' }
} finally { Pop-Location }
