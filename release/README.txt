DOAX2 Recomp v1.1.4 - native PC port of Dead or Alive Xtreme 2 (Xbox 360)

This package does NOT include the game. You need your own copy of
Dead or Alive Xtreme 2 for the Xbox 360 (retail disc, title ID 544307D2).

1. Extract your Xbox 360 disc (for example with extract-xiso) into the folder
   "extracted game files here", so that default.xex sits directly inside it.
2. Run "DOAX2 Recomp Launcher.exe", choose 30 FPS or 60 FPS and press PLAY.
   - SETTINGS: Continuous Music Playback, Winnings Multiplier
     (experimental), FPS overlay, diagnostic logging and the logs folder.
   - GRAPHICS SETTINGS: resolution, fullscreen, resolution scale,
     anti-aliasing, texture filtering and Guest V-Sync (needed for 60 FPS).
   - INPUT (experimental): keyboard keys and player 1's controller buttons.
   - UPDATE: checks GitHub for a newer version and installs it (keeps your
     saves, settings and game files).
   Press ? next to an option for an explanation; changes are saved
   automatically in config\launcher.ini.
3. Saves are stored in the "userdata" folder next to the launcher.

Requirements: Windows 10/11 x64, a DirectX 12 graphics card (RTX 2060 Super /
RX 6600 XT class or faster for 1080p, see the project page). The Visual C++ runtime
DLLs are included next to the game; if it still fails to start, install the
latest Microsoft Visual C++ 2015-2022 Redistributable (x64).

Controls: Xbox, PlayStation, Switch and most other controllers work; the
bottom face button (Xbox A, PlayStation Cross, Switch B) confirms. The
keyboard works as a controller too: Enter = Start, Space = A,
Backspace = B, W A S D = left stick, arrow keys = D-pad. Change keys and
controller buttons on the launcher's INPUT page.

Network: the game does not use the internet or your network. Every
network call is blocked, so Windows Firewall should not ask about doax2.exe.
Only the launcher's UPDATE page goes online (to github.com), and only when
you open it.
(Older versions could show that prompt; it came from the original game's
System Link code and was safe to block.)

Reporting problems: include the newest file(s) from out\game\logs
(doax2_<number>.log). Never share game files.

Not affiliated with or endorsed by Koei Tecmo, Team NINJA or Microsoft.
The launcher logo is fan-made; the character shown in its background belongs
to Koei Tecmo.
License: LICENSE (port code, BSD 3-Clause) and THIRD-PARTY-NOTICES.md.
Source code: see the project page on GitHub.
