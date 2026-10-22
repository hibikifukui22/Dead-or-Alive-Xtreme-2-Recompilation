# Builds a player release from the current checkout (use the `release`
# branch) and packages it into dist/:
#   DOAX2-Recomp-v<version>-win64.zip    player package (no game files)
#   DOAX2-Recomp-v<version>-source.zip   complete corresponding source,
#                                        including the patched SDK and its
#                                        submodules (LGPL compliance)
#   SHA256SUMS.txt
# Builds in out/release with source paths mapped to relative paths, so no
# local folder or user name ends up in the binaries, then scans both archives
# with check-personal-data.ps1. Requires your own game files in game/.
param([int]$Jobs = 8, [switch]$SkipSdk, [switch]$SkipCodegen)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Split-Path $PSScriptRoot -Parent)).Path
$version = (Get-Content (Join-Path $projectRoot 'VERSION') -TotalCount 1).Trim()
$name = "DOAX2-Recomp-v$version"
$sdkSource = Join-Path $projectRoot 'sdk/rexglue-sdk'
$releaseOut = Join-Path $projectRoot 'out/release'
$sdkBuild = Join-Path $releaseOut 'sdk-build'
$sdkInstall = Join-Path $releaseOut 'sdk-install'
$gameBuild = Join-Path $releaseOut 'game'
$dist = Join-Path $projectRoot 'dist'

# Supported retail default.xex (see README).
$xex = Join-Path $projectRoot 'game/default.xex'
$xexSha256 = 'C4A14853E1F7AF951BE34F380604113703638780B36D5E74E674FE7431992870'
if (-not (Test-Path -LiteralPath $xex)) { throw 'game/default.xex not found: extract your own game disc into game/.' }
if ((Get-FileHash -LiteralPath $xex -Algorithm SHA256).Hash -ne $xexSha256) { throw 'game/default.xex is not the supported retail version.' }

. "$PSScriptRoot/dev-shell.ps1"

# Map the checkout path to relative paths in __FILE__ strings and debug
# info, and keep only the PDB file name in the executables.
$rootFwd = $projectRoot.Replace('\', '/')
# (Quoted, so a checkout path with spaces stays one argument.)
$prefixMap = "`"-ffile-prefix-map=$rootFwd/=`" `"-ffile-prefix-map=$projectRoot\=`""
$cFlags = "-march=x86-64-v2 $prefixMap"
$linkFlags = '-Wl,/PDBALTPATH:%_PDB%'

if (-not $SkipSdk) {
    Write-Host '== ReXGlue SDK (release build)'
    & (Join-Path $PSScriptRoot 'setup-sdk.ps1') -SkipBuild
    Push-Location $sdkSource
    try {
        cmake --preset win-amd64 -B $sdkBuild -DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON `
            "-DCMAKE_INSTALL_PREFIX=$sdkInstall" "-DCMAKE_C_FLAGS=$cFlags" "-DCMAKE_CXX_FLAGS=$cFlags" `
            "-DCMAKE_EXE_LINKER_FLAGS=$linkFlags" "-DCMAKE_SHARED_LINKER_FLAGS=$linkFlags"
        if ($LASTEXITCODE) { throw 'SDK configure failed.' }
        cmake --build $sdkBuild --config Release --target install --parallel $Jobs
        if ($LASTEXITCODE) { throw 'SDK build failed.' }
    } finally { Pop-Location }
}

Push-Location $projectRoot
try {
    if (-not $SkipCodegen) {
        Write-Host '== Code generation from game/default.xex'
        & (Join-Path $sdkInstall 'bin/rexglue.exe') codegen doax2_manifest.toml --ignore-stamp
        if ($LASTEXITCODE) { throw 'Code generation failed.' }
    }
    Write-Host '== Game and launcher'
    cmake --preset win-amd64-release -B $gameBuild "-DCMAKE_PREFIX_PATH=$sdkInstall" `
        "-DCMAKE_C_FLAGS=$prefixMap" "-DCMAKE_CXX_FLAGS=$prefixMap" "-DCMAKE_EXE_LINKER_FLAGS=$linkFlags"
    if ($LASTEXITCODE) { throw 'Game configure failed.' }
    cmake --build $gameBuild --target doax2 doax2_public_launcher --parallel $Jobs
    if ($LASTEXITCODE) { throw 'Game build failed.' }
} finally { Pop-Location }

Write-Host '== Packaging'
$package = Join-Path $dist $name
if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Recurse -Force }
$gameDir = Join-Path $package 'out/game'
New-Item -ItemType Directory -Force $gameDir, (Join-Path $package 'extracted game files here') | Out-Null
# The launcher starts out/game/doax2.exe (30 and 60 FPS) and reads the game
# from "extracted game files here".
Copy-Item (Join-Path $gameBuild 'doax2.exe') $gameDir
Copy-Item (Join-Path $gameBuild 'rexruntime.dll'), (Join-Path $gameBuild 'rexgpu-xenos.dll') $gameDir
Copy-Item (Join-Path $gameBuild 'public/DOAX2 Recomp Launcher.exe') $package
# Visual C++ runtime next to the game (redistributable).
$crt = Get-ChildItem (Join-Path $vsPath 'VC/Redist/MSVC') -Directory |
    Sort-Object Name -Descending | ForEach-Object { Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC14*.CRT' -ErrorAction SilentlyContinue } |
    Select-Object -First 1
if (-not $crt) { throw 'Visual C++ redistributable DLLs not found in the Visual Studio installation.' }
foreach ($dll in 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    Copy-Item (Join-Path $crt.FullName $dll) $gameDir
}
Copy-Item (Join-Path $projectRoot 'release/README.txt') $package
Copy-Item (Join-Path $projectRoot 'LICENSE'), (Join-Path $projectRoot 'THIRD-PARTY-NOTICES.md') $package
Set-Content -LiteralPath (Join-Path $package 'extracted game files here/PUT YOUR GAME FILES HERE.txt') -Encoding ascii -Value `
    'Extract your own Dead or Alive Xtreme 2 (Xbox 360) disc into this folder so that default.xex sits directly in it.'

$playerZip = Join-Path $dist "$name-win64.zip"
$sourceZip = Join-Path $dist "$name-source.zip"
Remove-Item -LiteralPath $playerZip, $sourceZip -ErrorAction SilentlyContinue
Compress-Archive -Path $package -DestinationPath $playerZip -CompressionLevel Optimal

# Complete corresponding source: every tracked file of this repository and of
# the (patched) SDK submodule tree.
Push-Location $projectRoot
try {
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $files = git -c core.quotepath=off ls-files --recurse-submodules
    if ($LASTEXITCODE) { throw 'git ls-files failed.' }
    $archive = [IO.Compression.ZipFile]::Open($sourceZip, 'Create')
    try {
        foreach ($file in $files) {
            $full = Join-Path $projectRoot $file
            if (Test-Path -LiteralPath $full -PathType Leaf) {
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $full, "$name-source/$file", 'Optimal')
            }
        }
        # Untracked files the SDK patch adds.
        $added = git -C $sdkSource ls-files --others --exclude-standard
        foreach ($file in $added) {
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Join-Path $sdkSource $file), "$name-source/sdk/rexglue-sdk/$file", 'Optimal')
        }
    } finally { $archive.Dispose() }
} finally { Pop-Location }

Write-Host '== Personal data check'
& (Join-Path $PSScriptRoot 'check-personal-data.ps1') -Path $playerZip, $sourceZip

$sums = foreach ($zip in $playerZip, $sourceZip) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant(), (Split-Path $zip -Leaf)
}
Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Value $sums -Encoding ascii
Write-Host "Release $version ready in $dist" -ForegroundColor Green
$sums
