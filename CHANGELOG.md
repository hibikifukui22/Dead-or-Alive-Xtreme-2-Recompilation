# Changelog

## 1.1.5

- **Native Renderer (experimental, on by default):** new Graphics setting.
  The renderer now follows the game's own render passes instead of only
  emulating the Xbox 360's EDRAM: the main 3D scene is drawn once instead
  of in two halves (about half the draw calls in 3D scenes), and clears are
  done directly by the graphics card. Lower CPU and GPU load; see
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
