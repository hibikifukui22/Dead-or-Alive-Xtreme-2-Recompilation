# DOAX2 title music playback

DOAX2 supplies its licensed soundtrack to the Xbox Music Player (XMP) title-playlist service as
MP3 files under `game/dat/sound/songs`. ReXGlue previously accepted those XMP calls but did not
decode or submit any music audio.

The v1.3.82 implementation keeps XMP playlist ownership in `XmpApp` and adds one process-wide
host player. Song paths are resolved through the title VFS and mapped read-only. A worker thread
decodes the selected MP3 with the FFmpeg libraries already bundled with ReXGlue, converts it to
48 kHz stereo, and the SDL audio callback mixes it into the same host stream as the game's sound
effects and voices. Pause, continue, stop, next, and previous are wired to that player. The MP3
path applies a 0.25 base calibration before the runtime 0..1 volume so the mastered songs retain
headroom against DOAX2's XACT effects and voices. Volume changes are logged at info level so the
live title-side control source can be verified rather than inferred.

Log 133 proved that the main-menu **Music Volume** setting does not send XMP's volume message.
The live `67 -> 23 -> 0 -> 80` trace then proved that the title maintains the setting as matching
normalized floats at `0x8397D290` and `0x8397D294`, plus an integer-percent copy at
`0x8398A5D8`. v1.3.82 reads the authoritative first float only when all three representations
agree and forwards it to the host playlist mixer. This independently controls Music without
consulting or changing the S.E. and Voice settings. The bridge never writes guest memory and
does not alter simulation timing.

The bridge is enabled by every launcher mode and is part of the DOAX2 build manifests and package
scripts, so rebuilding or packaging later versions retains it. It does not depend on any
experimental 60 FPS mode.

Host validation decodes and mixes every MP3 in the DOAX2 soundtrack. A live run is confirmed by
these log messages:

```text
XMP title playlist playback started: ...
XMP title music decoded: ...
```

If music remains inaudible while those messages appear, the remaining issue is host output routing
or volume. If the first message is absent, DOAX2 did not start its title playlist. If decoding fails,
the log reports the exact track and decoder error.
