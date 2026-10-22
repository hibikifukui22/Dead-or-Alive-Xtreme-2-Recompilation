# DOAX2 60 FPS architecture notes

Superseded audit: see `60fps-title-boundary-audit.md`. The original probe
established numeric similarity, not a safe replay boundary or a complete
semantic classification of constants. Whole-stream replay failed in log 057.
The current launcher disables capture/replay and offers a CPU callback probe.

## Confirmed frame loop

The main loop in `sub_8258DD38` executes these stages in order:

1. `sub_8258E000` samples/publishes frame timing.
2. `sub_8258E500` updates game systems and constructs/submits render work.
3. `sub_8258E0D8` performs guest presentation and closes the frame.

`doax2_053.log` proved that shortening the guest display semaphore makes this
entire sequence run at approximately 60 Hz. Relax animation and minigame state
then advance twice as fast. DOAX2 therefore uses fixed-step advancement inside
the main update and does not expose a safe global delta-time correction.

The semaphore written by `sub_82705B38` is not a literal requested-vblank
count. The observed value `4294967270` is `-26` when interpreted as signed, so
the old unsigned `> 1` clamp was invalid independently of the speed coupling.
That hook has been removed.

## Existing render-side representation

DOAX2 already separates the representation consumed by rendering from the
simulation objects at the Xenos command boundary. Every draw consumes an
immutable packed vertex-constant buffer produced by ReXGlue from the title's
shader constant registers. Consecutive frame captures identify:

- camera/projection data around c0, c17 and c37;
- object and GPU-skinning/bone transforms beginning around c84;
- a stable shader-specific register bitmap describing the packed layout;
- one constant-buffer binding associated with every recorded draw.

This is downstream of gameplay, input, physics and animation advancement. It
is therefore the preferred interpolation boundary. Interpolating these values
cannot advance guest simulation state.

`sub_8258E500` also has a recognizable late render-management phase. It builds
global scene/view state, visits 33 render-list objects, then calls
`sub_824894C0`. That final function enters a critical section and clears 25
render work-queue records (20 bytes each), consistent with end-of-frame render
queue retirement rather than simulation. This is a candidate boundary for a
future title-side render-evaluation call, but it must not be called a second
time until its indirect callbacks are classified as render-only.

## Current prototype

The D3D12 backend retains two completed native-frame records containing:

- the pre-presentation deferred command stream;
- per-draw vertex shader identity;
- the exact shader register bitmap;
- packed vertex constants for that draw;
- the source constant-buffer GPU address.

Every 120 guest swaps, the probe compares adjacent frames and reports:

- draw positions with identical shader and constant layout;
- stable draws;
- draws changing c0-c83 (camera/instance class);
- draws changing c84+ (pose/skinning class);
- changed float4 register counts in both classes;
- unique constant buffers that have an unambiguous previous/current pairing.

This establishes whether the existing representation is stable enough to
drive a midpoint renderer. It deliberately presents no generated frame yet.

## Safety gate for midpoint rendering

Do not replay a whole captured D3D12 stream into live targets. Besides draw
commands, it contains resource transitions, render-target writes, resolves,
queries, copies and possibly memory-export side effects. Repeating it without
isolation can corrupt EDRAM, shared memory or the following native frame.

Proceed in this order:

1. Validate high draw/layout match coverage in a relax cutscene and minigame.
2. Restrict interpolation to finite values in confirmed transform registers;
   copy flags, material values and unmatched constants from the newer state.
3. Prefer a render-only title-side evaluation if the late `sub_8258E500`
   callbacks can be proven side-effect-free.
4. Otherwise replay only the matched draw representation into isolated color,
   depth and resolve targets, excluding queries, memory exports and guest
   readbacks.
5. Queue native and midpoint images to the presenter at 16.67 ms spacing while
   the guest simulation remains at 33.33 ms.
