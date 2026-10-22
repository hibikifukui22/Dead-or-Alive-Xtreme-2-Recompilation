param([int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/dev-shell.ps1"
$projectRoot = Split-Path $PSScriptRoot -Parent
$sdkSource = Join-Path $projectRoot 'sdk/rexglue-sdk'
# Git on Windows can check out cabextract's symlinks as plain text files.
# Use the canonical libmspack source and headers instead.
$thirdpartyCmake = Join-Path $sdkSource 'thirdparty/CMakeLists.txt'
$thirdpartyText = [IO.File]::ReadAllText($thirdpartyCmake)
$patchedText = $thirdpartyText.Replace('/libmspack/cabextract/mspack', '/libmspack/libmspack/mspack')
if ($patchedText -ne $thirdpartyText) {
    [IO.File]::WriteAllText($thirdpartyCmake, $patchedText)
}
Push-Location $sdkSource
try {
    cmake --preset win-amd64 -DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON
    if ($LASTEXITCODE) { throw 'SDK configure failed.' }
    cmake --build out/build/win-amd64 --config Release --target install --parallel $Jobs
    if ($LASTEXITCODE) { throw 'SDK build failed.' }
} finally { Pop-Location }
