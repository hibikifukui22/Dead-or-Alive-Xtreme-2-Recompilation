# DOAX2 Recomp

A native Windows PC port of **Dead or Alive Xtreme 2** (Xbox 360), built with
the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) static
recompiler. The game's PowerPC code is translated ahead of time into C++ and
runs natively, at 30 FPS or at a native 60 FPS with the original game speed.

> **This repository and its releases do not contain the game.** No game code,
> data, audio or video is included. You need your own copy of Dead or Alive
> Xtreme 2 for the Xbox 360. Please do not share or link to game files in
> issues or pull requests. The launcher uses a fan-made project logo; its
> background shows a Dead or Alive character, who belongs to Koei Tecmo and
> is not covered by this project's license.
>
> Not affiliated with or endorsed by Koei Tecmo, Team NINJA or Microsoft.
> All trademarks belong to their respective owners.

Current version: **1.0.0** (see [CHANGELOG.md](CHANGELOG.md)).

## Supported game version

Only the retail disc version is supported:

| | |
|---|---|
| Title ID | `544307D2` |
| Media ID | `21B9628E` |
| `default.xex` version | `3` (base version `3`), XEX2 |
| `default.xex` SHA-256 | `c4a14853e1f7af951be34f380604113703638780b36d5e74e674fe7431992870` |

Title updates are not supported.

## Playing (release build)

1. Download the latest `DOAX2-Recomp-v<version>-win64.zip` from the
   [Releases](../../releases) page and extract it anywhere.
2. Extract your Xbox 360 disc (for example with `extract-xiso`) into the
   folder `extracted game files here`, so that `default.xex` sits directly
   inside it.
3. Run `DOAX2 Recomp Launcher.exe`, choose 30 or 60 FPS and press **PLAY**.
   **Settings** and **Graphics Settings** hold the other options, each with
   an explanation.

Requirements: Windows 10/11 x64 and a Direct3D 12 GPU. Saves are written to
the `userdata` folder next to the launcher.

## Building from source

See [docs/BUILDING.md](docs/BUILDING.md). In short:

```powershell
git clone --recurse-submodules <this repository>
cd DOAX2-Recomp
.\scripts\setup-sdk.ps1        # SDK submodule + this project's SDK patch + SDK build
# put your extracted game files in .\game (default.xex directly inside)
.\scripts\generate-code.ps1    # recompile default.xex into generated/ (never committed)
.\scripts\configure-game.ps1
cmake --build out\game
```

## Repository layout

| Path | Contents |
|---|---|
| `src/` | Port code: `main.cpp`, the 60 FPS / timing hooks (`doax2_60fps.cpp`), the release launcher (`launcher_public.cpp`) and the developer launcher (`launcher.cpp`) |
| `resources/launcher/` | Launcher art (background, fan-made logo, icon) embedded into the launcher |
| `config/`, `doax2_manifest.toml` | ReXGlue code generation manifest (function boundaries, fibers) |
| `patches/rexglue-sdk/` | This project's changes to ReXGlue SDK v0.10.0 (renderer fixes, title music player) |
| `sdk/rexglue-sdk/` | ReXGlue SDK v0.10.0 (git submodule) |
| `scripts/` | Setup, code generation, build, release packaging and analysis helpers |
| `tests/`, `tools/` | Host-side tests and small developer utilities |
| `docs/` | Build and release guides; `docs/notes/` holds engineering notes from development |
| `game/` | Your extracted game files go here (ignored by git) |

## Branches

- **`main`**: development. Fork this to make changes.
- **`release`**: the code that release builds are made from. Approved changes
  are merged from `main`, then a release is built with
  `scripts/build-release.ps1` and published on the Releases page (see
  [docs/RELEASING.md](docs/RELEASING.md)).

## License

The port's own code is BSD 3-Clause (see [LICENSE](LICENSE)); it covers only
material owned by the project's contributors and grants no rights to the
game. Third-party components keep their own licenses, listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
