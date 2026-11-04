> [!NOTE]
> If you notice **anything that looks or feels off** (stutters, speed, sound, controls, anything at all), please [open an issue](https://github.com/hibikifukui22/Dead-or-Alive-Xtreme-2-Recompilation/issues) with what happened, where in the game, which Renderer you use, whether you play at 30 or 60 FPS, and the newest log from `out\game\logs`.

> [!WARNING]
> The **Vulkan (Native)** renderer is new and still experimental, but we recommend trying it first for the best performance. If it doesn't work out, fall back to DX12 (Native) and tell us whether that fixes it. The **Input** page and the **Winnings Multiplier** are also still experimental.
>
> **Known issue:** on Intel Iris Xe integrated graphics, Vulkan (Native) crashes at the title screen. Use DX12 (Native) there, which runs much better than Emulated on those chips.

**This download does not include the game.** You need your own copy of Dead or Alive Xtreme 2 for the Xbox 360 (retail disc, title ID 544307D2).

## New in v1.1.6

This is the biggest update so far. The way the game is drawn has been rebuilt.

**This update should help lower-end systems run the game**, such as PCs with GTX 900 and 1000 series graphics cards. To target higher FPS, set **Renderer** to **Vulkan (Native)** on the Graphics page.

**Intel integrated graphics now works.** Use **DX12 (Native)** on Intel integrated graphics (see the known issue above).

**We need testers for Vulkan!** Please try Vulkan (Native) and [tell us how it runs](https://github.com/hibikifukui22/Dead-or-Alive-Xtreme-2-Recompilation/issues), including your graphics card, your settings and the FPS you get.

### Milestone: DX12 (Native) renderer
Until now the renderer rebuilt everything the way the Xbox 360's graphics chip works, including its small block of fast video memory (EDRAM). DX12 (Native) understands what the game is actually drawing and does it the way a PC graphics card prefers:

- **The 3D scene is drawn once.** The Xbox 360 had to draw large scenes in two halves to fit EDRAM. The renderer now recognises the game's render passes and draws each scene at full size in one go, with about half the draw calls in 3D scenes.
- **Clears are done directly by the graphics card**, instead of by imitating the game's clear drawing.
- **Full-screen passes no longer copy data they are about to overwrite.**
- **Repeated screen copies only copy what changed.** Textures made from them reload only the rows that changed.
- **Much lower CPU load.** GPU commands are processed in batches, unchanged game memory is no longer re-sent to the graphics card every frame, and a second CPU thread records and submits most of each frame.

In tests at 4K on an RTX 3090, GPU time dropped from 14.0 to 8.4 ms at the pool and from 19.0 to 12.3 ms in the swimsuit menu. Renderer CPU time in the swimsuit menu halved, from 4.1 to 2.1 ms per frame.

### Milestone: Vulkan (Native) renderer
A completely new way to run the game, with all of the above plus deeper optimisations, and the foundation for the Android version, which should be released in the next day or two:

- **Screen copies go straight into the texture that uses them.** On the Xbox 360 these copies (bloom, depth of field, picture-in-picture, the swimsuit menu, the shadow map) were almost free. On PC each one was a chain of 3 to 4 GPU passes; now it is usually one, with results that match the old method bit for bit in our checks.
- **Emulated memory is only written when something reads it.** Copies skip that memory, and it is rebuilt from the texture only if something else needs it.
- **Faster texture uploads** using compute shaders instead of a slow image copy.

In tests at 4K on an RTX 3090: volleyball GPU time per frame roughly halved, and the swimsuit menu went from 39 to a locked 60 FPS.

### New Renderer setting
Choose between **Emulated**, **DX12 (Native)** and **Vulkan (Native)** on the Graphics page. If you used the old Native Renderer toggle, your choice carries over. Try Vulkan (Native) for the lowest GPU load, or if DX12 runs poorly on your graphics card or driver. Emulated stays available as the fallback.

### Smooth frame pacing on any display
Fixes camera pans (cutscenes, the area selector, the hotel background) that could look like 30 FPS even though the counter showed 60.

- The game's clock now **follows your display** when its refresh rate is a multiple of the game's frame rate (60, 120, 180, 240 Hz and up), so every frame lands on its own refresh. In tests on a 60 Hz display, dropped frames went from as many as 67 in 5 seconds to none.
- Other refresh rates, and G-SYNC / FreeSync, automatically use a precise timer instead.
- Frames never tear, and showing them doesn't hold the game back.

### New options
- **Character Shadows** (Graphics, on by default): turn off to skip the characters' real-time shadows on slower PCs. Scenery shadows are part of the scenery's textures, cost nothing, and stay.
- **MSAA Mode: Off (use FXAA)** (Graphics): draws without multisample anti-aliasing, the fastest choice for lower-end graphics cards. Turn on Post-Process Anti-Aliasing (FXAA) to smooth edges instead.
- **Display Refresh Compatibility** (Settings, off by default): presents frames on a separate GPU queue. Turn it on if the frame rate drops a lot while the game window is focused (from the 1.1.4a test build).

### Fixes
- **FPS overlay:** with the overlay on, the screen was redrawn at every display refresh plus once per game frame, about 300 times a second on a 240 Hz display, taking GPU time from the game. It is now redrawn once per game frame.
- The log now lists your graphics settings at startup, which makes reports easier to diagnose. With MSAA Mode Off it no longer fills with warnings.

## Install
Extract the zip into a new folder, or use the launcher's **UPDATE** page (it keeps your saves, settings and game files). Then see `README.txt`.
