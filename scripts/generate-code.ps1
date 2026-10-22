$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$rexglue = Join-Path $projectRoot 'sdk/rexglue-sdk/out/install/win-amd64/bin/rexglue.exe'
Push-Location $projectRoot
try {
    & $rexglue codegen doax2_manifest.toml --ignore-stamp
    if ($LASTEXITCODE) { throw 'DOAX2 code generation failed.' }
    $unresolved = Get-ChildItem generated/default -Filter '*.cpp' |
        Select-String -Pattern 'FATAL: unresolved|REX_FATAL\("Unresolved'
    if ($unresolved) { throw 'Generated source still contains unresolved calls.' }
} finally { Pop-Location }
