# Releasing

Development happens on `main`. The `release` branch holds the code that
player builds are made from.

## 1. Prepare the release branch

```powershell
git checkout release
git merge main            # only the changes approved for this release
```

Update `VERSION`, `CHANGELOG.md` and the version shown in `release/README.txt`
and the launcher, then commit.

## 2. Build the release

Your own game files must be in `game/` (the build checks the supported
`default.xex`).

```powershell
.\scripts\build-release.ps1
```

It builds the SDK, the recompiled game and the launcher in `out/release` with
source paths mapped to relative ones, so no local folder or user name ends up
in the binaries. It then writes to `dist/`:

| File | Contents |
|---|---|
| `DOAX2-Recomp-v<version>-win64.zip` | Player package: launcher, `out/game` (game executable, runtime, renderer, Visual C++ runtime), README, licenses, empty `extracted game files here` folder |
| `DOAX2-Recomp-v<version>-source.zip` | Complete corresponding source including the patched SDK and its submodules (required for the LGPL components) |
| `SHA256SUMS.txt` | Checksums of both archives |

Both archives are scanned with `check-personal-data.ps1`; the script stops if
anything personal is found. No saves, logs, settings or game files are
packaged.

Use `-SkipSdk` / `-SkipCodegen` to reuse an earlier SDK build or generated
code.

## 3. Publish

```powershell
git tag v<version>
git push origin release --tags
```

Create a GitHub release for the tag and attach the two zips and
`SHA256SUMS.txt`. Never attach or link game files.
