DOAX2 Recomp v1.0.0 - native PC port of Dead or Alive Xtreme 2 (Xbox 360)

This package does NOT include the game. You need your own copy of
Dead or Alive Xtreme 2 for the Xbox 360 (retail disc, title ID 544307D2).

1. Extract your Xbox 360 disc (for example with extract-xiso) into the folder
   "extracted game files here", so that default.xex sits directly inside it.
2. Run "DOAX2 Recomp Launcher.exe", pick your resolution and options, and
   press Launch DOAX2.
   - "Guest V-sync" must stay enabled for 60 FPS.
   - "Keep song playing across scenes and minigames" (off by default): the
     current song plays to its end instead of restarting on every scene
     change; movies and the title screen still stop the music.
   - "Diagnostic logging" writes detailed logs to out\game\logs but lowers
     FPS; leave it off unless you are reporting a problem.
3. Saves are stored in the "userdata" folder next to the launcher.

Requirements: Windows 10/11 x64, a Direct3D 12 GPU. The Visual C++ runtime
DLLs are included next to the game; if it still fails to start, install the
latest Microsoft Visual C++ 2015-2022 Redistributable (x64).

Reporting problems: include the newest file(s) from out\game\logs
(doax2_<number>.log). Never share game files.

Not affiliated with or endorsed by Koei Tecmo, Team NINJA or Microsoft.
License: LICENSE (port code, BSD 3-Clause) and THIRD-PARTY-NOTICES.md.
Source code: see the project page on GitHub.
