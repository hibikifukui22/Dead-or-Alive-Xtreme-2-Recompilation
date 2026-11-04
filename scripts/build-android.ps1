# Builds the Android test APK: native libraries (NDK), then Java, then a signed APK.
#   -NativeOnly  stop after the native libraries
#   -SkipNative  reuse the native libraries and rebuild only the Java side and APK
param([switch]$NativeOnly, [switch]$SkipNative)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$androidHome = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { 'S:\Android\Sdk' }
$ndk = Join-Path $androidHome 'ndk\27.2.12479018'
$buildTools = Join-Path $androidHome 'build-tools\35.0.0'
$platformJar = Join-Path $androidHome 'platforms\android-35\android.jar'
if (-not $env:JAVA_HOME) { $env:JAVA_HOME = 'S:\Android\jdk-17.0.21+9' }
$vsCMake = 'E:\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake'
$cmake = Join-Path $vsCMake 'CMake\bin\cmake.exe'
$ninja = Join-Path $vsCMake 'Ninja\ninja.exe'
# Generates the recompiled code: comes from the Windows build (build-release.ps1).
$hostTool = (Join-Path $root 'out\release\sdk-install\bin\rexglue.exe') -replace '\\', '/'
$out = Join-Path $root 'out\android'
$gameBuild = Join-Path $out 'game'
$bin = Join-Path $out 'game-bin'
$apkWork = Join-Path $out 'apk'
$version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
$apk = Join-Path $root "dist\DOAX2-Recomp-v$version-android-test.apk"

function Run {
    param([string]$Exe, [string[]]$Arguments)
    # Tools write warnings to stderr; only the exit code decides.
    $ErrorActionPreference = 'Continue'
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}

if (-not $SkipNative) {
    Run $cmake @('-S', $root, '-B', $gameBuild, '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DCMAKE_TOOLCHAIN_FILE=$ndk\build\cmake\android.toolchain.cmake",
        '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-29', '-DANDROID_STL=c++_shared',
        '-DCMAKE_BUILD_TYPE=Release', "-DREXSDK_DIR=$root\sdk\rexglue-sdk",
        "-DREXGLUE_OUTPUT_DIR=$bin", "-DREXGLUE_HOST_TOOL=$hostTool",
        '-DREXGLUE_BUILD_TESTS=OFF', '-DREXGLUE_ENABLE_FIDELITYFX=OFF')
    Run $cmake @('--build', $gameBuild, '--target', 'doax2', '--', '-k', '0')
}
if ($NativeOnly) { return }

# Java: the app and SDL's activity classes.
if (Test-Path $apkWork) { Remove-Item -Recurse -Force $apkWork }
$classes = Join-Path $apkWork 'classes'
$dex = Join-Path $apkWork 'dex'
$assets = Join-Path $apkWork 'assets'
$libs = Join-Path $apkWork 'lib\arm64-v8a'
New-Item -ItemType Directory -Force $classes, $dex, $assets, $libs | Out-Null
$sdlJava = Join-Path $root 'sdk\rexglue-sdk\thirdparty\sdl3\android-project\app\src\main\java'
$sources = @(Get-ChildItem -Recurse -Filter *.java $sdlJava, (Join-Path $root 'android\src') | ForEach-Object { '"' + ($_.FullName -replace '\\', '/') + '"' })
$sourceList = Join-Path $apkWork 'sources.txt'
[IO.File]::WriteAllLines($sourceList, $sources)
Run (Join-Path $env:JAVA_HOME 'bin\javac.exe') @('-nowarn', '-encoding', 'UTF-8', '-source', '8', '-target', '8',
    '-classpath', $platformJar, '-d', $classes, "@$sourceList")
$classFiles = @(Get-ChildItem -Recurse -Filter *.class $classes | ForEach-Object { $_.FullName })
$classList = Join-Path $apkWork 'classes.txt'
[IO.File]::WriteAllLines($classList, $classFiles)
Run (Join-Path $buildTools 'd8.bat') @('--release', '--min-api', '29', '--lib', $platformJar, '--output', $dex, "@$classList")

# Resources and the launcher art.
Copy-Item (Join-Path $root 'resources\launcher\background.jpg'), (Join-Path $root 'resources\launcher\logo.png') $assets
$compiled = Join-Path $apkWork 'res.zip'
Run (Join-Path $buildTools 'aapt2.exe') @('compile', '--dir', (Join-Path $root 'android\res'), '-o', $compiled)
$unsigned = Join-Path $apkWork 'unsigned.apk'
Run (Join-Path $buildTools 'aapt2.exe') @('link', '-I', $platformJar, '--manifest', (Join-Path $root 'android\AndroidManifest.xml'),
    '-A', $assets, '-o', $unsigned, $compiled)

# Native libraries, stripped.
$strip = Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe'
$stl = Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\aarch64-linux-android\libc++_shared.so'
Copy-Item $stl $libs
foreach ($name in 'libmain.so', 'librexruntime.so', 'librexgpu-xenos.so') {
    $lib = Get-ChildItem -Recurse -Filter $name $bin, $gameBuild | Select-Object -First 1
    if (-not $lib) { throw "$name was not built" }
    Run $strip @('--strip-unneeded', '-o', (Join-Path $libs $name), $lib.FullName)
}

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open($unsigned, [IO.Compression.ZipArchiveMode]::Update)
try {
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $dex 'classes.dex'), 'classes.dex')
    foreach ($lib in Get-ChildItem $libs) {
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $lib.FullName, "lib/arm64-v8a/$($lib.Name)")
    }
} finally {
    $zip.Dispose()
}

# Align and sign with a local test key.
$keystore = Join-Path $out 'test.keystore'
if (-not (Test-Path $keystore)) {
    Run (Join-Path $env:JAVA_HOME 'bin\keytool.exe') @('-genkeypair', '-keystore', $keystore, '-storepass', 'android',
        '-keypass', 'android', '-alias', 'test', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000',
        '-dname', 'CN=DOAX2 Recomp Test')
}
$aligned = Join-Path $apkWork 'aligned.apk'
Run (Join-Path $buildTools 'zipalign.exe') @('-f', '4', $unsigned, $aligned)
New-Item -ItemType Directory -Force (Split-Path $apk) | Out-Null
Run (Join-Path $buildTools 'apksigner.bat') @('sign', '--v4-signing-enabled', 'false', '--ks', $keystore, '--ks-pass', 'pass:android', '--out', $apk, $aligned)
Run (Join-Path $buildTools 'apksigner.bat') @('verify', $apk)
Copy-Item (Join-Path $root 'android\README-ANDROID-TEST.txt') (Split-Path $apk)
Write-Host "Built $apk ($([math]::Round((Get-Item $apk).Length / 1MB)) MB)"
