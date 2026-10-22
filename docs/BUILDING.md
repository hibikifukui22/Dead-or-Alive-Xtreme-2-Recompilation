# Building DOAX2 Recomp

## Requirements

- Windows 10/11 x64
- Visual Studio 2022 with the **Desktop development with C++** workload and the
  **C++ Clang tools for Windows** component (clang, CMake and Ninja come with
  it; the scripts use the Visual Studio copies)
- Git and Python 3 (for the analysis helpers)
- Your own Dead or Alive Xtreme 2 Xbox 360 disc, extracted (see the supported
  `default.xex` in the main README)

## 1. Clone and set up the SDK

```powershell
git clone --recurse-submodules <repository url> DOAX2-Recomp
cd DOAX2-Recomp
.\scripts\setup-sdk.ps1
```

`setup-sdk.ps1` checks out the ReXGlue SDK submodule at v0.10.0, applies
`patches/rexglue-sdk/0001-doax2-runtime-changes.patch` and builds and installs
the SDK into `sdk/rexglue-sdk/out/install/win-amd64` (this takes a while the
first time).

## 2. Add your game files

Extract your disc into `game/` so that `game/default.xex` exists. The folder is
ignored by git.

## 3. Generate the recompiled code

```powershell
.\scripts\generate-code.ps1
```

This runs the ReXGlue code generator on `game/default.xex` with
`doax2_manifest.toml` and writes `generated/`. Generated code is derived from
the game and is never committed.

## 4. Build

```powershell
.\scripts\configure-game.ps1
. .\scripts\dev-shell.ps1
cmake --build out\game
```

Outputs in `out\game`: `doax2.exe` (with `rexruntime.dll` and
`rexgpu-xenos.dll` next to it) and `tester\DOAX2 60FPS Test Launcher.exe`; the
developer launcher `DOAX2 Launcher.exe` is written to the repository root.

## Changing the SDK

Edit the files in `sdk/rexglue-sdk`, rebuild with `.\scripts\build-sdk.ps1`,
and regenerate the patch before committing:

```powershell
cd sdk\rexglue-sdk
git add -A
git diff --cached --binary HEAD > ..\..\patches\rexglue-sdk\0001-doax2-runtime-changes.patch
git reset
```

## Before you push

```powershell
.\scripts\check-personal-data.ps1 -Path .
```

It fails on your Windows user name, profile path, git name/e-mail, absolute
user-profile paths, saves, logs and game files.
