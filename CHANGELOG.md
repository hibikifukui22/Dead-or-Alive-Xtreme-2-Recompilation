# Changelog

## 1.1.6

- **Renderer setting with a new Vulkan renderer (experimental)** (Graphics):
  Emulated, DX12 (Native) or Vulkan (Native). Vulkan (Native) has the same
  native-renderer speed-ups as DX12 and is the base for a future Android
  version; try it if DX12 runs poorly on your GPU or driver. It presents
  without tearing and without waiting for the display, like DX12. Fixed on
  the way: 3D scenes drawn black, wrong alpha in some screen copies, and a
  crash in a copy that wraps around the EDRAM end. Swimsuit menu at 4K:
  39 -> 60 FPS in tests. The old Native Renderer toggle becomes this
  setting (on = DX12 (Native), off = Emulated).
- **Faster texture reloads after screen copies:** a texture that a copy
  writes only partly now reloads just the rows that were written (both
  renderers). Vulkan swimsuit menu GPU time at 4K: 23 -> 12 ms in tests.
- **Much lower GPU load with Vulkan (Native):** screen copies (bloom,
  depth of field, picture-in-picture, the swimsuit menu, the shadow map)
  go straight from the render target into the texture that uses them, and
  are written to the emulated memory only if something else reads that
  memory, like on the Xbox 360 where these copies were almost free. Texture
  uploads after copies use a compute shader instead of a slow image copy.
  At 4K in tests: volleyball 10.3 -> 4.6 ms of GPU time per frame,
  swimsuit menu 6.8 -> 5.7 ms on top of the gains above. DX12 (Native) has
  the partial-reload speed-up only.
- **Character Shadows** (Graphics, on by default): turn off to skip
  drawing the characters' real-time shadows (saves GPU and CPU time on
  slow machines). Scenery shadows are part of the scenery's textures and
  cost nothing, so they stay.
- **Smooth 60 FPS on any display:** the game's clock follows the display
  when its refresh rate is a multiple of the game's (60, 120, 180, 240 Hz
  and up), so every frame lands on its own refresh: no dropped frames or
  judder in camera pans. Other refresh rates and G-SYNC / FreeSync use a
  precise timer instead, automatically. Never tears.
- **Fixed lines across the screen on the water** (Marine Race and other
  water scenes, Native Renderer): native clears used a wrong stencil value.
- **Fixed the corrupted lower half of the screen in the roulette payout**
  (Native Renderer): draws without depth overwrote colour with depth data.
- **Fixed a yellow tint in some water scenes** (Vulkan): a screen copy could
  go into a smaller leftover texture and lose most of its rows.
- **FPS overlay fix:** with the overlay on, the screen was redrawn at every
  monitor refresh plus once per game frame (about 300 times a second on a
  240 Hz display), taking GPU time from the game. It is now redrawn once per
  game frame.
- **Faster swimsuit menu and repeated screen copies (Native Renderer):**
  when the game copies the same render target to the same texture again,
  only the part that changed since is copied, and only those rows of the
  texture are reloaded. Swimsuit menu GPU time: about -30% in tests at 4K.
- **Lower CPU load in the renderer:** GPU register writes are processed in
  batches, unchanged game memory is no longer re-copied to the graphics card
  every frame, and a second CPU thread records and submits most of each
  frame's GPU work (swimsuit menu: 4.1 -> 2.1 ms per frame in tests).
- The log now lists the launch settings (graphics options) at startup, and
  with MSAA Mode Off it no longer fills with "Incorrect resolve sample
  selected" warnings.
- **Native Renderer (experimental, on by default):** DX12 (Native) in the
  Renderer setting.
  The renderer now follows the game's own render passes instead of only
  emulating the Xbox 360's EDRAM: the main 3D scene is drawn once instead
  of in two halves (about half the draw calls in 3D scenes), and clears are
  done directly by the graphics card, and full-screen passes no longer copy
  render target contents they overwrite. In tests at 4K, GPU time dropped
  by about 40% at the pool and 35% in the swimsuit menu. Lower CPU and GPU
  load; see
  docs/NATIVE-RENDERER.md. If anything looks wrong, turn it off and report
  whether that fixes it.
- **MSAA Mode: Off (use FXAA)** (Graphics): draws the game without
  multisample anti-aliasing, the fastest choice for lower-end graphics
  cards; turn on Post-Process Anti-Aliasing (FXAA) to smooth edges instead.
- **Display Refresh Compatibility** (Settings, off by default): presents
  frames on a separate GPU queue (the 1.1.4a test build). Turn it on if the
  frame rate drops a lot while the game window is focused.

## 1.1.4a (experimental pre-release)

- Test build for players whose frame rate drops while the game window is
  focused (for example with G-SYNC, frame limiters, forced V-Sync or
  overlays): the renderer paints and presents on its own GPU queue, so a
  present that waits on the display can no longer stall the game.
  Runtime setting `d3d12_separate_present_queue` (on in this build).

## 1.1.4

- Pool Hopping at 60 FPS runs at the original speed: the intro and outro
  length, your partner's hops (she no longer finishes at double speed) and
  the hop timing (quick taps give short hops again) all match 30 FPS.
- Known issue: the camera pan at the start of the Pool Hopping intro can
  hitch a few times at 60 FPS; a fix is in progress.

## 1.1.3

- **Input page is experimental.**
- Keyboard controls work: the keyboard acts as a controller (together with
  a connected pad), so Enter presses Start on the title screen. Earlier
  versions did not read the keyboard in the game.
- New **Input** page in the launcher, with the controller diagram:
  - Keyboard tab: two keys for every controller input, keyboard on/off,
    Reset to Defaults.
  - Controller tab: remap player 1's buttons, triggers and stick
    directions; picking a button that is in use swaps the two. Detects
    Xbox, PlayStation, Switch and other controllers and shows their own
    button names. Reset to Defaults.
- New **Update** page in the launcher: checks GitHub for the latest
  release, then downloads it, verifies its SHA-256 checksum, extracts it and
  installs it, with a progress bar for every step. Saves, settings and game
  files are kept, and the previous version is restored if anything fails.
  It only goes online while the page is open (see docs/NETWORK.md).
- Runtime: new `pad_remap` setting for player 1's controller (keyboard
  input is not remapped).

## 1.1.2

- All network access is blocked. The game no longer opens a network port
  (UDP 1000, the original game's System Link port), so Windows Firewall no
  longer asks about doax2.exe. Winsock is never started, and every socket,
  send, receive and lookup call fails as if no network cable is connected.
  Developers can turn networking back on with `--guest_network=true`; see
  docs/NETWORK.md.
- No local communication either: debug output is only sent while a debugger
  is attached, and the diagnostic tools load debug symbols from the game
  folder only, never from a symbol server.

## 1.1.1

- **Winnings Multiplier is experimental.**
- Casino payouts now show the multiplied amount while you play:
  - blackjack win popup and table chips, including double down, split and
    insurance payouts;
  - roulette spin winnings and chips (your money is right as soon as you
    leave the table);
  - poker win counter and balance.
- Minigame payout screens show every prize and bonus multiplied, and the
  total matches the money you receive.
- Launcher: the "DOAX2 is running" message clears when the game closes.

## 1.1

- New launcher option in Settings: **Winnings Multiplier** (Normal, 10x,
  15x, 20x, 100x). Multiplies casino payouts (blackjack, poker, roulette,
  slots) and minigame prize money; bets, losses and shop prices are not
  changed.

## 1.0.0

First public release.

- Native Windows x64 port of Dead or Alive Xtreme 2 through ReXGlue static
  recompilation; Direct3D 12 renderer.
- Native 60 FPS mode with the original game speed: base game timing halved,
  minigame, animation and camera timing fixes (volleyball, Beach Flags, pose
  camera, Butt Battle, Tug of War, Niki Marina, Pool Slide), smooth 60 FPS
  Pool Slide motion.
- Menus at the original speed in 60 FPS mode: window pop in/out, held
  direction scrolling, option/image bounce.
- Licensed soundtrack playback with the in-game music volume; song title and
  artist shown once per song change; optional Continuous Music Playback
  (the song plays on across scenes and minigames; movies and the title
  screen still stop the music).
- Boot movies can be skipped with A / Start / Enter / Space; the title voice
  plays once when they are skipped.
- Launcher: 30 FPS or 60 FPS on the main page; Settings and Graphics
  Settings pages with a ? explanation popup for every option.
- Renderer fixes and performance work (A8 shadow resolve, EDRAM wrap, exact
  screen-space draw extents, batched uploads).
