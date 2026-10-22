# Third-party notices

DOAX2 Recomp is built on the ReXGlue SDK and the libraries it bundles. Each
component keeps its own license. The full license texts are in the SDK checkout
(`sdk/rexglue-sdk/LICENSE` and the license file in each
`sdk/rexglue-sdk/thirdparty/<component>/` folder) and in the source archive
attached to every release.

## Not included

Dead or Alive Xtreme 2 and all of its code, data, audio, video and trademarks
belong to their respective owners (Koei Tecmo / Team NINJA). No game code,
data, audio or video is in this repository or in its releases; you must
provide your own copy of the game. The launcher uses a fan-made project logo;
its background (`resources/launcher/background.jpg`) shows a Dead or Alive
character, who belongs to Koei Tecmo and is not licensed under this project's
license. This project is not affiliated with or
endorsed by Koei Tecmo or Microsoft.

## ReXGlue SDK

- **ReXGlue SDK** v0.10.0 (`sdk/rexglue-sdk`, git submodule of
  https://github.com/rexglue/rexglue-sdk), BSD 3-Clause, Copyright (c) 2026
  Tom Clay. Portions derived from the **Xenia** project, BSD 3-Clause,
  Copyright (c) 2022 Ben Vanik and Xenia project contributors.
  This project's changes to the SDK are in
  `patches/rexglue-sdk/0001-doax2-runtime-changes.patch` (same license).

## Components in the released runtime (rexruntime.dll, rexgpu-xenos.dll, game executable)

| Component | License |
|---|---|
| FFmpeg (libavcodec / libavutil, audio decoding; built without `--enable-gpl`) | LGPL 2.1 or later |
| libmspack | LGPL 2.1 |
| SDL 3 | zlib |
| {fmt} | MIT |
| spdlog | MIT |
| toml++ | MIT |
| SIMDe | MIT |
| xxHash | BSD 2-Clause |
| aes_128 | MIT |
| tiny-AES-c | Unlicense (public domain) |
| crypto (TinySHA1, DES, RC4, Rijndael, SHA-256) | see `thirdparty/crypto` (DES: see `thirdparty/crypto/des/LICENSE`) |
| Dear ImGui | MIT |
| stb_image | MIT or public domain |
| DXBC checksum | see `thirdparty/dxbc` |
| DirectX Shader Compiler headers | University of Illinois / LLVM |
| disruptorplus | MIT |
| Vulkan Headers | Apache 2.0 / MIT |
| Vulkan Memory Allocator | MIT |
| Vulkan Loader | Apache 2.0 |
| SPIR-V Headers | MIT (Khronos) |
| SPIR-V Tools | Apache 2.0 |
| glslang | BSD 3-Clause and others (see its LICENSE.txt) |
| Tracy (profiling builds only) | BSD 3-Clause |
| RenderDoc in-app API header | MIT |

## Build-time tools only (not in the player release)

| Component | License |
|---|---|
| PowerPC disassembler (`thirdparty/disasm`, from GNU binutils), used by the `rexglue` code generator | GPL 2.0 or later |
| CLI11 | BSD 3-Clause |
| inja | MIT |
| Catch2 | Boost Software License 1.0 |
| utfcpp | Boost Software License 1.0 |
| MoltenVK (macOS only) | Apache 2.0 |

## Microsoft Visual C++ runtime

The player release includes the Microsoft Visual C++ 2015-2022 runtime DLLs
(`msvcp140.dll`, `msvcp140_atomic_wait.dll`, `vcruntime140.dll`,
`vcruntime140_1.dll`), redistributed under the Visual Studio redistributable
code terms.

## LGPL source availability

FFmpeg and libmspack are linked into `rexruntime.dll`. The complete
corresponding source, including the SDK submodule, its third-party submodules
and this project's patch, is attached to each GitHub release as
`DOAX2-Recomp-v<version>-source.zip`, so the runtime can be rebuilt and
relinked with modified versions of these libraries.

This table was compiled from the license files in the SDK checkout; the
license files themselves are authoritative.
