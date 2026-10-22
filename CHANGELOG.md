# Changelog

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
  artist shown once per song change; optional "Keep song playing across
  scenes and minigames" (movies and the title screen still stop the music).
- Boot movies can be skipped with A / Start / Enter / Space; the title voice
  plays once when they are skipped.
- Renderer fixes and performance work (A8 shadow resolve, EDRAM wrap, exact
  screen-space draw extents, batched uploads).
