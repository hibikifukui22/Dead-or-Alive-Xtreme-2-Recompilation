# Native 60 Hz timing experiment (2026-09-29)

This is an isolated investigation of **60 genuine guest loops and renders per
second**, not the paused 30 Hz simulation / reconstructed-render path in
`60fps-paused-checkpoint.md`. The stable installed `out/game/doax2.exe` and
`out/game/rexgpu-xenos.dll` remain unchanged; the opt-in experimental filename
is consistently `out/game/doax2.native60-poc.exe` for the NVIDIA profile.
Launch only through `scripts/run-native60-poc.ps1`.

## Proven so far

- Guest refresh 120 with the 60 FPS engine mode produces approximately 60
  guest engine frames per second in gameplay (`doax2_088.log`, `090.log`).
  The user observed approximately double-speed relax/minigame behavior.
- Guest refresh 60 with vblank multiplier 2 did not raise gameplay above 30 Hz
  (`doax2_086.log`); the guest swap/fence wait remained about 31 ms.
- On one sampled procedural manager, virtual +28 evaluator `825AF158`
  produced 119 changed output matrices in 120 sampled 60 Hz frames, while
  manager+28 rose by 119 (`doax2_090.log`). A 30 Hz reference rose by about
  119 over 3.94 seconds (`doax2_089.log`). This is evidence that this pose
  input advances twice as fast, **not** proof of 60 distinct final images.
- The sampled manager's vtable is `823BFBF0`; virtual +12 contains
  `825B0028`, which statically adds the shared scalar to manager+28 when
  active. An opt-in hook halves only this function's phase step. However the
  hook emitted no first-call log in `doax2_090.log` despite the changing
  phase. Therefore that correction is **not proven active** and did not fix
  the observed double-speed run.
- Static inspection found another same-scalar +28 writer, `825AE0E8`, and a
  known virtual +12 dispatcher at `825ACE88`. The current read-only hooks
  trace both. In the noninteractive title segment `doax2_092.log`, neither
  hook logged calls, while sampled manager `82E49890` rose by 119 phase units
  per ~2 seconds for multiple windows. The subsequent relax trace
  (`doax2_093.log`) likewise showed no calls to either writer while the
  sampled phase advanced at ~60 units/second. `825ACE88` was reached, but its
  first object's virtual +12 target was `825AC548`, not `825B0028`. It is
  not proven to own the sampled evaluator manager. The experimental launch
  script now leaves the unproven phase correction disabled by default.
- Read-only boundary bisection with the automatic title/relax preview traced
  the live +1 write through `8258E500` → `825C19C8` → `825C27C8`
  (`doax2_095.log` through `098.log`). `825C27C8` dispatches scene-specific
  virtual +16 functions. For the recurring manager at `82E49890`, the live
  writer is `82606B98` (phase `82E498AC`); for the later preview manager at
  `82E3AC68`, the writer is `825E6900` (phase `82E3AC84`). `82612778`
  writes two other related fields. The generated PPC code directly shows
  these functions adding the float at `82E2DD44` to their own phase fields.
- An opt-in correction now intercepts the three live writer functions and
  halves only increments that equal the observed shared unit; resets and
  unrelated changes are rejected. `doax2_099.log` measured `82E49890`
  advancing 59.5 phase units across 120 sampled frames in 1.978 seconds,
  with 119 changed evaluator matrices. `doax2_100.log` repeated this result
  and measured `82E3AC68` advancing 59.5 units across 120 frames in 1.998
  seconds, again with 119 changed matrices. The uncorrected corresponding
  managers advanced 119 units in approximately 2 seconds. This proves one
  procedural pose-clock correction, not correct character motion or gameplay.
- The correction happens at hook exit, after each original guest function
  returns. Other work inside those functions may already have consumed the
  uncorrected value; this needs behavioral and possibly finer-grained review.
  No final-image capture or visual correctness comparison was performed in
  these runs. The stable 30 FPS executable and renderer DLL hashes remain
  unchanged after each build.
- Five local CTests pass, including the isolated phase arithmetic test. No
  automated test establishes correct gameplay timing or distinct final
  rendered images. The stable 30 FPS and renderer DLL SHA-256 values are
  `0FA4DC74922759BECC59C2EB1D7088877EBDEB1BCE5651282EB7125ED767702A`
  and `D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.

## Current blocker and next action

The user reported that the procedural phase correction alone still looked
approximately double-speed in relax (`doax2_101.log`). A separate read-only
time-source probe in the idle title preview showed `825A4550` (keyframe
updater) and `8265E7B8` (gameplay countdown) each running once per 60 Hz guest
frame (`doax2_102.log`). Guest function `825A4550` reads integer step at
manager+16 and adds it to each 20-byte keyframe record's integer subframe at
record+16 before pose callbacks. At 60 Hz, this subframe changed on 720/720
sampled calls (`doax2_103.log`); at the 30 Hz reference, it changed on
480/480 calls (`doax2_104.log`). This identifies another independent 2x
timing dependency.

An opt-in `doax2_native60_keyframe_half` mode now uses a per-manager 1,0
integer-step cadence around the original `825A4550` call. It is enabled by
`scripts/run-native60-poc.ps1 -CorrectKeyframeStep:$true`, separately from
`-CorrectProceduralPhase:$true`. The original function still evaluates every
60 Hz frame; its original manager step is restored immediately after the call.
The automatic preview run `doax2_105.log` showed 720 calls, 360 held integer
steps, 360 changed integer subframes, and 720 changed floating record values
without an observed crash. This proves the cadence mechanism in one preview
manager, **not** correct visible whole-character speed. The scoped manager
field is shared guest memory; concurrent readers remain a risk not yet ruled
out. The default remains disabled. Five CTests pass; the installed stable
exe and renderer hashes remain unchanged.

Next: have the user compare representative relax motion with both corrections
enabled against the stable 30 Hz build. If still fast, identify which visible
subsystem advances twice as quickly; do not further halve already-corrected
phases blindly. Gameplay countdowns (`8265E7B8`) and minigame timing remain
uncorrected. Distinct final images and correct whole-game speed remain
unproven. The existing `825B0028` correction remains opt-in but was never
exercised in the tested scene; it must not be counted as a verified fix.

## Native 60 Hz scene/camera investigation (2026-09-29)

The user tested procedural-phase plus integer-keyframe corrections in relax
(`doax2_106.log`) and reported that character motion, camera motion, and the
overall action duration still appeared approximately 2x fast. This rules out
those two clocks as a sufficient fix, not native 60 Hz rendering itself.

Read-only traces `doax2_109.log` through `doax2_111.log` identified the real
camera-publish route. The two 64-byte camera sources at `839BC1D0` and
`839BC210` change within the guest fiber scheduler `8258CC00`, in slot 10
callback `824180E8`, not in the earlier hypothesized `825C19C8` scene
manager. `824180E8` invokes `824183F0`, selected 4144-byte object updates
through `8241A630`, and `824187E0`; `8241AF08` constructs the selected
object's matrices at offsets +512/+576, and `824187E0` publishes them to
the global camera source. The camera source changed on approximately every
60 Hz guest update, with original-size per-update position steps. The 30 Hz
reference `doax2_114.log` had approximately 30 such steps per second.

`8241D038` increments selected object float timeline at +3540 by 1 per
active call (`doax2_113.log`) and an isolated, opt-in half-step hook was
verified in `doax2_115.log`/`116.log`. It did not affect the camera motion
in the initial preview segment, where that timeline was still zero; it is
not a complete camera or gameplay correction. It remains off by default.

`82417ED8` increments 14 integer counters in each of four 60-byte records
from the same fiber. The new `doax2_native60_scene_counters_half` option
executes the original guest routine and holds only a verified +1 result on
alternate calls. It is guarded to the four scene-owned records and remains
off by default. Its unit cadence test and the other four CTests pass. In
`doax2_117.log`, 23,040 hook calls included 11,520 held calls but only 14
actual active counter increments were held; the camera sample at change 120
was identical to the earlier uncorrected sample (`0.9699707` to
`1.0502748`). Thus these counters were effectively inactive in the preview
and this experiment is **not** a useful visual speed correction. Do not ask
for a manual test of this counter mode on the strength of that log.

Read-only camera-input bisection in `doax2_118.log` localized the recurring
camera state change further: `8241C360` changes the selected object's
64-byte region at +192 on every approximately 60 Hz update, while the
earlier scene children usually do not. `8241AF08` consumes +192/+208 and
other fields, writes +512/+576, then the fiber publishes the camera. The
generated `8241C360` first calls `8241D600` and applies matrix/vector math
to the object. This is the next real timing dependency to classify. No
correction to `8241C360` has yet been implemented. Its math and upstream
state (including +448 and +2736/+2752) need 30/60 Hz comparison before
choosing a timestep/coefficient change.
`doax2_119.log` adds a child hook around `8241D600`: it did not change
the watched +192 state in that preview, whereas its parent `8241C360`
changed it approximately every 60 Hz call. This confines the recurring
write to `8241C360` after its `8241D600` call. The generated function
contains extensive transform/vector and collision-camera logic; halving an
output position delta without understanding these operations would be a
speculative and possibly incorrect physics/camera change.
`doax2_120.log` additionally watched the +448 camera basis and +2736
target region: neither changed across `8241C360` in the repeating preview
segment, while +192 changed every 60 Hz call. A later per-word trace
`doax2_122.log` showed the first changed word in +192 to be reset from
zero to one on *every* call, so the observed hash change is consistent with
temporary matrix recomputation and does **not** prove a persistent
recurrence or identify a timing coefficient. The attempted scalar samples
in `doax2_121.log` chose two always-zero words and are not useful for speed
analysis. The `doax2_122.log` 16-word diff confirms a repeating mask
`0x08C8`: words 3, 6, 7, and 11 of +192..+255 are rewritten, with word 3
going from zero to one every call. This is a per-frame matrix construction,
not demonstrated camera-time accumulation. Do not patch `8241C360` yet.
The remaining live camera input
likely lies in other object regions or in `8241AF08`'s mutable auxiliary
region (+704); trace that region and its producing function before editing.
Compare actual camera trajectories at 30 and 60 Hz under matching scene
conditions. Static inspection shows `8241AF08` consumes +192/+208,
+704/+720 and scalar fields, writes +512/+576, then invokes `8241B080`
and `82419F68` for further matrix work. This is a render-state producer,
not yet a verified timing source. All five CTests passed after the final
read-only probe build. Stable exe/DLL hashes were verified unchanged,
and no experimental game process was left running.

The matching 30-Hz reference `doax2_123.log` is decisive for the visible
camera symptom. In the automatic preview, camera write #120 has the same
position transition (`0.9699707` to `1.0502748`) as write #120 in the
60-Hz trace, but arrives after about four seconds at 30 Hz versus about
two seconds at 60 Hz. The title therefore follows essentially the same
per-call camera sequence twice as quickly, not a 60-Hz trajectory with
half-sized steps. This is consistent with the user's visual report. It
does not locate the clock inside the camera path; +192 is a rebuilt matrix,
and +704 appears to be render-side matrix output. The next implementation
must identify an actual advancing guest-side trajectory parameter before
altering camera behavior. A 30-Hz hold on alternate 60-Hz calls would
repeat poses and fail the native-60 objective.

## 2026-09-29: title-preview motion-track clock and fractional curve sample

Read-only child traces (`doax2_124.log` through `128.log`) localized the
moving preview's source-transform write to `82421680`. Its two calls to
`82421CF8` operate on scene-object tracks at +832 and +1024. The first
mode-3 `82424558` call initializes an object+1940 gate to 900; subsequent
calls decrement it by one per guest update. The isolated
`doax2_native60_mode3_gate_half` switch halves only verified -1 steps, and
`doax2_127.log` shows the intended gate duration, but it does not slow
motion. The camera still completed its trajectory in about half the normal
wall-clock time. Thus the gate and motion clocks are distinct.

The new read-only track probe in `doax2_129.log` (60 Hz) and
`doax2_130.log` (30 Hz) confirms that `82421CF8` advances both tracks'
float phase at track+128 by **+1 per call** and remaining-time at +136 by
**-1 per call**. Both advance every 60 Hz guest update when uncapped. At
60 Hz, track phase 120 arrives after roughly two seconds; at 30 Hz it
arrives after roughly four. This is a verified fixed-step motion clock, not
the previously suspected `8241C360` matrix rebuild.

An off-by-default `doax2_native60_preview_motion_half` switch now
post-corrects only exact +1/-1 phase/remaining steps for the selected
mode-3 preview object. The original guest update still executes. In
`doax2_131.log`, track call 480 had phase about 240 instead of 480, while
the gate also ran at half pace. However, only 242 of 480 camera source
updates changed: the original evaluator truncates float phase to an
integer before sampling its authored track. Halving the phase without
changing the evaluator duplicates intermediate visual states and is not a
native 60 FPS solution.

The original scalar curve evaluator `82417810`, called from `82421CF8`,
accepts the truncated integer track index. The same opt-in switch now
evaluates that original scalar curve at adjacent integer indices and
computes the fractional curve parameter for a half-index phase. This is
**fractional evaluation of the game's authored motion track**, not
interpolation between two completed 30 Hz frames, and does not invoke a
second simulation or replay GPU commands. `doax2_132.log` shows output
parameters such as index 240.5 yielding 0.8016667 between original-curve
values 0.8000000 and 0.8033333. More importantly, the selected camera
source changed on 839 of 840 active 60 Hz mode-3 calls, compared with
about 422 of 840 before fractional curve evaluation. The one missing
change was a segment reset. At corrected 60 Hz call 240, the mode gate was
780.5, close to 781 at native 30 Hz call 120. This is evidence of a
distinct, correctly paced *camera input* in the preview, not yet proof of
distinct complete presented frames or visually correct gameplay.

All five CTests passed after the final fractional evaluator build. A final
source-order hardening runs the extra pure scalar evaluation first, then
the authoritative original invocation last; the resulting `doax2_133.log`
still shows a changed camera source on 1,438 of 1,440 active calls, with
the two exceptions at segment resets. The final stable exe and renderer
hashes are `0FA4DC74922759BECC59C2EB1D7088877EBDEB1BCE5651282EB7125ED767702A`
and `D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.
The experimental process was closed. No manual visual assessment of this
opt-in mode has occurred. The title-preview path alone cannot establish minigame timing,
character animation, physics, UI, audio sync, scene transitions, or
sustained rendering performance. The stable 30 FPS build and renderer DLL
remain separate and were restored after each build. Next: compare 30/60
wall-clock camera trajectories and request a focused human visual preview
test. If that succeeds, trace the
same clock/evaluator pattern in actual relax and minigame systems without
assuming this one hook generalizes.

The existing stable 30 FPS executable and renderer DLL were restored after
each experimental build and retained their known SHA-256 hashes. Tests and
traces above do **not** establish correct native 60 Hz gameplay or 60
distinct final rendered images per second. Minigame logic, action duration,
and additional animation/camera clocks remain unresolved. All experimental
corrections require explicit launcher switches; default stable behavior and
the paused reconstruction branch are unchanged.

Build recovery: CMake builds overwrite the stable output executable and may
replace the installed renderer DLL. Immediately copy the newly built exe to
the persistent `doax2.native60-poc.exe` path and restore `doax2.exe` from
`doax2.stable-before-native60.exe` and `rexgpu-xenos.dll` from
`rexgpu-xenos.pre-owner-lineage.dll`. Verify hashes before running the stable
launcher. Do not overwrite the paused reconstruction checkpoint.

## Handoff addendum — latest manual test, 2026-09-29

The user reported: "the camera speed is slightly slower at the title, but all
animations are still double speed." The manual run was `doax2_134.log`, launched
with mode-3 gate and preview-motion switches true but `TimeSourceProbe` false.
Packaging review found a configuration defect: `Native60SelectedCameraObject`
returns zero unless `doax2_time_source_probe` is true. Both correction hooks
currently depend on that selected object, so the manual launch did NOT exercise
these corrections. Do not interpret the subjective camera difference as their
validation. Logs 129–133 used the probe and remain the relevant telemetry.

The next agent should decouple correction eligibility from the logging switch,
or explicitly use `-TimeSourceProbe` for the next controlled test. Add a startup
or runtime marker proving the corrections executed. Preserve the stable build.
Full character animation remains unresolved, including in earlier correctly
enabled procedural/keyframe experiments. A camera-only success does not fix it.

No animation change followed the user's report. Read-only inspection reiterated
that 825A4550's floating record phase advances independently of its integer
subframe, and that 8248A640 has a whole-step deformation accumulator at slot
state+372 feeding 82489D68. Neither is proven to be the whole-character skeletal
clock. Do not label the procedural-instance matrices as character bones.
The game was closed before packaging. Development is paused for transfer.

## 2026-09-30: game-wide base speed (supersedes per-function halving)

Static xrefs show the shared unit `82E2DD44` has 24 readers and no direct
writer: `825C19C8` copies it each frame from `833B84CC`, which ~75 functions
read (camera tracks, mode-3 gate, scene timeline, `8248A640`, ...).
`8250A568` publishes `833B84CC = base(83426298) * modifier(834262A4)` only
when either changes. The base is written solely by init `8250A118` (1.0);
the modifier has 12 writers (the title's pause/slow-motion). The new
`--doax2_native60_tick_scale` (script `-TickScale 0.5`) multiplies the base
once after `8250A118`, so the game republishes a 0.5 step itself and pause
still yields 0. Log 137: base 0.5, step 0.5, unit 0.5. User test: character
animations now look correct speed; camera often lost the character; jitter;
speed felt inconsistent (title/menus run ~105-120 Hz at refresh 120).

Logs 140 (30 Hz ref) / 141 (scaled): camera tracks and the mode-3 gate size
themselves from the step (track limit 600 and gate 1800 at step 0.5, versus
300/900) and then also advance by the step, so the scaled camera ran at half
speed (~20 s per shot versus 10 s). `Native60UnscaledStep` now presents the
unscaled step only during `82421CF8` and `82424558` (nesting-guarded). Log
142: one 600-entry table sample per 60 Hz update, ~10 s per shot, gate
1800 -> 0 in ~30 s, positions on the reference trajectory. Needs a visual
check. Fractional track sampling (phase now integral) is a no-op here.
The mode-3/preview halving switches must stay off with tick scale.

Open: other step-sized-and-step-advanced systems may double-compensate the
same way; per-call counters that ignore the step (countdowns, springs) may
still run 2x; menus run above 60 Hz at guest refresh 120.

## 2026-09-30: minigame investigation (logs 143-151)

User result with base speed 0.5 + unscaled camera tracks: island/relax
animation and cameras correct at 60 Hz. Remaining: volleyball sets often
fail, volleyball intro and final win pose too short, Beach Flags broken
(short start, jittery double-image running, CPU runner 2x, flag glitches,
short win pose), menus 2x (low priority).

Tools added: `DOAX2_FUNC_CENSUS` CMake option (separate `out/census-build`)
counts every guest function per 30 updates via `REX_FUNC_PROLOGUE` (hook
added by hand to generated `doax2_pch.h`; lost on codegen) into
`out/game/logs/doax2_census.csv`; `-CounterScan` reports static words
83400000-839FFFFF changing exactly +-1 per update, plus heap object deltas
for Beach Flags `826AC790`/`826ACB20` and pose records `834271A0+k*288`.

Volleyball: module 8267xxxx-8269xxxx; ball 8394E878. Flight is analytic in
ball time +7364 (advanced by step in 82681628; correct). Launch helpers
82684B08(T)/82684C30/82684D80/826836F8; action dispatch on ball +7376
(2 receive, 3 set, 4 attack, 5 spike, 8 block touch). 30 Hz reference
(log 147): sets always attacked (action 4). 60 Hz (146/149/151): opposing
players commit action 8 from 82690160, a discrete body-sphere collision
test. Holding the 83947DB0 frames-in-state counters on alternate updates
(log 151) changed nothing and was removed. Current test: run 82690160 only
at whole ball times (original 30 Hz sample instants).

Intro 5.5 s at 60 Hz vs 12.4 s reference; final win pose 3.2-4.6 s vs
7.4 s; both end after a similar number of updates. They do not use
82421CF8 camera tracks; sequencer 82430628 compares pose record +80/+112.
Beach Flags: modules 826AB800-826AFAE0 and 8261F4xx-82625xxx barely read
the step; state is on the heap.

## 2026-09-30: logs 152-156 and the pending test build

Fixed (user-confirmed): Beach Flags intro and win pose via halving the
race camera clocks 8398A4A0+16/+20 in 826AC790 (+1.0 -> +0.5 per update).

`Native60HeldCounters` undoes exact +-1 changes on alternate updates for
fixed per-update duration counters. Applied: 82688388/826886D0 (839528E4
phase countdown), 825A0FD8 (screen manager 834BD010/14), 82687C20
(839525B4 frames in phase), 82687B38 (839525C0 match clock). None of these
alone fixed the volleyball intro (9.8 s original, 6.7 s at 60 Hz in 156).

Found via census (intro-only functions): 82688388 ends the intro when
82689E58 returns true; 82689D88 ends each girl's step when 839528C8 >= 75
(150 for type 7), incremented once per call in 82689E58. Final/results
phase 82689370 waits for pose motions (82689CA8 flags) and a 60-frame
countdown at 8341F984. Pending build holds 839528C8 and 8341F984/839525BC.

Sets (reference log 155 vs 154/156): teams are {0,2} and {1,3}. Originally
the setter's teammate starts spike motion 504 so its hit window (motion
frame 23) opens at ball time 38-39. At 60 Hz the same teammate is still in
motion 504 at ball time 43-44 with no window, and the ball meets her body
(82690160 -> action 8). Motion frames themselves advance correctly, so the
spike starts late. Pending build logs every 8268DAE8 motion start (caller,
ball time, planned T) to locate the trigger. The 30 Hz-instant block-test
gate in 82690160 is still present but is not the cause.

Test launcher: `Launch Native60 Test.cmd` (project root).

## 2026-09-30: logs 157-159

User: volleyball intro and final "extraordinarily better". Intro 9.2 s (orig
9.8), final 5.7 s (orig 5.9). Log 159: each intro step now ends at its
authored 75/150 count in 148/298 updates; results steps advance on pose
motion completion (82689CA8). Remaining difference is outside these steps.

Sets: CPU spiker's spike (504) is queued by 82678FF0 while approach motion
471 is at motion frame 7 (13 updates in) at 60 Hz; the original queues ~14
frames in. Holding the 83947DB0 counters (log 151) did not change it, so the
trigger is another per-update value. Current build defers any queue out of
471 until motion frame >= the update count at the first attempt, logging
"spike queue deferred/released" (depends on the AI re-issuing the queue).

Beach Flags: runner records at *(game+32), 192 bytes: +0/+8 position,
+32/+40 displacement, +156 speed-like. Needs a 30 Hz race with the region
probe (`Launch 30fps Reference.cmd`) to identify fields that advance per
update.

## 2026-09-30: logs 160-161

Spike-queue deferral (log 160) broke sets: the AI queues the spike once
and never re-issues it, and the deferral froze the human's motions. Removed;
replaced by a one-shot dump of the spiker's state/AI/AI-counter records at
the queue moment ("spike trigger" lines) to locate the per-update trigger.

Beach Flags (161 = 30 Hz reference vs 159): player runner speed correct
(1.76 u/s both); CPU runner 2.41 u/s at 60 Hz vs 1.74. Runner power gauge
+156 (826AFAE0): tiered per-call decay (<0.5: 0.015, <2: 0.1, <3: 0.15,
else 0.21); humans (+116 == 0) add +160 per press, CPUs add *(+176)+32 per
call. Fix: CPU net change halved; humans pre-credited half the decay.

## 2026-09-30: logs 163-166

Beach Flags CPU runner speed now matches the original (user-confirmed)
after the gauge fix plus holding the CPU decision countdown runner +164
(826AE108). Running animation still stutters.

Sets (164 = 30 Hz reference with motion-start probe): approach 471 starts
at ball time 2 (1.5 at 60 Hz) and spike 504 is queued exactly 14 updates
later in both runs (ball time 16 vs 8.5), while the approach motion frame is
7.0 at 60 Hz. No field in the spiker's state/AI records equals 14, and no
13-15 immediate exists in the queue callers. Hypothesis under test: motion
events fire from 825A4550's per-call integer keyframe subframe (manager
+16 step added per call). Test via `Launch Native60 Keyframe Test.cmd`
(`-CorrectKeyframeStep $true`, 1/0 cadence). Spike dump now logs caller.

Intro lead-in: before the first girl's step clock starts, 2.73 s originally
vs 1.71 s at 60 Hz; the steps themselves are exact. Phase-machine and
screen-manager state transitions are now logged to locate it.

## 2026-09-30: logs 167-168

Keyframe 1/0 cadence (-CorrectKeyframeStep) had no effect on sets and is
not the cause (island unaffected). Spike queue caller is 82679CB4 in
82679A30, the per-player AI action scheduler: AI record 839525C8 + p*92 has
u16 countdowns +10 (start planned action 83947E78 + p*56 + 24 at zero),
+12 and +14 (heading blend), each decremented once per call. Fix in build:
hold each decrement on alternate calls per player, never holding 1 -> 0.

Lead-in before minigame intros: fades now match (30 counted frames), but
the camera cut from lead-in object 6 to intro object 0 happens 49 updates
after fade-out originally (1.64 s) vs 44 updates at 60 Hz (0.72 s), i.e. a
per-update count. Build logs which probe-wrapped function changes the scene
camera index (839BC0F0 + 968) and a per-update Beach Flags runner trace.
Beach Flags: 60 fps average during races with ~48 ms render/present spikes.

## 2026-09-30: log 170 and fiber waits

User: gameplay better, sets mostly work, some spikes miss, some players feel
sluggish. Log 170: spike 504 now starts at ball time 14.5-15 (orig 15-16),
window at 37-37.5 (orig 38-39), commits action 4. Remaining misses: setters
enter set motion 205/206 from 191 after ~7 updates in both runs (per-update
wait in 8268C198 -> 8268C23C), so sets start 3-4 ball units early.

Counter scan upgraded: "short counter" lines report 8/16/32-bit and float
fields whose changes are >= 90% exact one-step ticks (>= 12 ticks), which
the old 80%-of-segment int/float test missed (e.g. hi16 countdowns).

Beach Flags stutter: runner gameplay position (+32) is smooth every update;
race render/present averages ~12 ms (max 16.8); pose evaluator 82511570
interpolates keyframes from the float frame (+76) when the fraction > 0.01.
So neither movement, render timing nor pose sampling explains it yet.
Post-race render spikes to 64-83 ms are a separate GPU-load issue.

Lead-in cut happens inside fiber scheduler 8258CC00. Its 24 slots at
834A3258 (28 bytes, +4 wait count decremented per call) implement scripted
"wait N frames". Build now holds decrements of waits > 1 on alternate calls
(waits of 1 untouched). Title preview unaffected (no waits held, log 171).

## 2026-09-30: logs 172-173

User: volleyball sets now work; the final attack after a set often misses.
Log 172/173: the attack swing (546/548/558/560) is queued by the CPU AI
tick 8267D318 at 8267D66C once the jump-frames byte 83950595 (block
8395057C + 25), incremented once per call in 8268CAB0 while airborne,
reaches the character delay looked up by 8267EC18 (stored at +56). At 60 Hz
the swing came ~7-10 ball units before arrival (orig ~2.5). Fix: hold that
increment on alternate increments (8268CAB0 hook, keeps the queue probe).
Spike slow-motion (step 0.25 = base 0.5 * modifier 0.5) is the title's own.

Beach Flags hitch: camera (839BC1D0) moves smoothly; runner +32 is a
lateral offset whose per-update deltas come in equal pairs. 826ACB20 indexes
a 32-byte keyframe table with int(clock +16) - 1 (clock <= 120) and builds a
4x4 matrix via 826ABE00/826AB800; with the halved clock the index changes
every other update. Fix: blend the matrices of keyframes n and n+1 by the
clock fraction (authored-table evaluation, as for camera tracks). The race
trace now also logs motion-record root positions and frame.

## 2026-09-30: log 175

Swing hold works (542 -> 546 now 5-7 units, orig 6-8.5). Remaining spike
misses follow high sets: the approach (541) starts at ~(T - 19)/2 at 60 Hz
versus T - 21 originally, i.e. a per-update wait. 82679D28 and 826797F0
also decrement the AI record countdowns +10/+12 (only 82679A30 was held);
all three now share one per-player hold (Native60AiCountdowns*).

Beach Flags: the keyframe blend in 826ACB20 only runs for the first
seconds (clock <= 120), and motion-record roots stay 0 for runners with the
frame advancing 0.5/update. Every simulated movement source traced is
smooth, so the hitch/double image may be render-side. `Launch Native60
Capture Test.cmd` adds -CaptureOnF10 (two consecutive final frames per F10).
User also reports palm-tree leaves flickering (alpha foliage), long-standing.

## 2026-09-30: log 177 (volleyball confirmed fixed)

User: volleyball perfect except spikes sometimes too fast. Cause: spike
slow-motion controller 83952998 (8268B3D8): +8 modifier published to
834262A4 then counter +2 ++ and +8 += ramp (+12) per call until +4. Fix:
half ramp per call and counter held on alternate calls.

Beach Flags F10 pair (frames 13971/13972, R/B channels swapped by the
capture) shows no in-frame ghosting. Frame pacing histogram (new, at
8258E0D8) on the 60 Hz title: ~70% of intervals 15.5-18 ms, ~17% 18-25 ms
(max ~21 ms) with guest refresh 120 and host d3d12_vsync off, i.e. regular
missed display refreshes that read as hitching on fast motion. Test option
`-HostVsync` / `Launch Native60 VSync Test.cmd` sets --d3d12_vsync=true.

## 2026-09-30: logs 180-181, launcher cleanup, jet-ski start

GUI launcher (src/launcher.cpp) rewritten: modes 30 FPS stable
(out/game/doax2.exe), 60 FPS experimental and 30 FPS reference (both
doax2.native60-poc.exe, tick scale 0.5 / 1.0, refresh 120 / 60);
experimental-only checkboxes for diagnostic logging (counter scan and
probes), F10 capture and FPS overlay. Retired modes 60-73 and transform
trace removed; old .cmd test launchers deleted. VSync (host d3d12_vsync)
test ran at ~10 FPS and was dropped.

Frame pacing: ~20% of 60 Hz frames arrive 18-25 ms apart in every scene;
timeBeginPeriod(1) (added in 60 Hz mode) did not change it, and volleyball
looks smooth to the user, so the Beach Flags hitch is treated as
minigame-specific. Trace now includes player-state position 833B8520
+12/+20 during races.

Jet-ski race (mode 9, "everything double speed"): per-racer counter blocks
at 834CDB00 + k*0x344 and 834F7E00 + k*0xA0, plus 839865CC / 839BC010.
Region statistics over those blocks are logged with diagnostics on for a
60 Hz vs 30 Hz reference comparison.

## 2026-09-30: census log 189 (Beach Flags + jet ski)

Beach Flags: no race function runs every other update (census windows);
race = 826AE108 once/update + 826AF238/826AFAE0 once per runner. Writers of
runner +32 are idle in the race, and the traced player-state record is not
the runner. Hitch still unexplained; runner world position not yet located.

Jet ski ("Niki Marina", camera mode 9): module 8262D768-8265DE20, ~30
functions once per racer per update. 82634EB0 sums racer forces and calls
the integrator 82652098 with f1 = hard-coded 1/120 s from 823EC120 (not the
game step). Readers: 82634EB0, 82635BD8, 82643028, jet camera 8242D1B8 (all
race-only), plus 82583B90/825851B0 (idle in tested modes). Fix: with the
base speed at 0.5, write 1/240 to 823EC120 each update if it still holds
1/120. The page is read-only in the host mapping (direct store faulted,
log 190), so the write is wrapped in VirtualProtect. Verified in log 191.
User: arrow buoys and palm leaves flicker also in the stable 30 FPS build
(pre-existing renderer issue, not frame-rate related).

## 2026-09-30: log 192 (jet ski confirmed; intro still short)

User: jet-ski racing correct after the physics-step fix; only the girls'
pre-race intro is too short. Reference 186 vs 192: lead-in 6.3 s vs 4.0 s,
intro 7.1 s vs 3.7 s, the intro lasting 211 updates in both. The intro
clock is 839865CC, incremented once per call by race manager 826496A0 and
compared with fixed counts; now held on alternate calls. The lead-in camera
cut and fades follow a script with ~per-update timing; the fiber-wait hold
never fired (scripts use wait 1 and count themselves). Scheduler 8258CC00
confirmed: 24 slots at 834A3258, +4 wait decremented while > 0, resume at 0.
New diagnostics: fiber slot start/stop and per-300-update listing; Beach
Flags pointer-follow region stats for pointers stored in runner records
(VirtualQuery-guarded). A stuck experimental process during teardown
coincided with a host crash; builds and stable files were intact after.

## 2026-09-30: log 194 and script-fiber pacing

User: jet-ski intro still too fast, READY/GO double speed; Beach Flags run
still hitches; jet-ski FPS drops with many models/water particles (perf).
Holding the jet clock 839865CC worked (value halved) but the intro still
lasted 211 updates: the sequence is a script fiber. Fiber log: slot 4
(30165000) starts at the post-lead-in fade, slot 5 (30166000) 13 ms before
the intro camera and lives until the race ends. System slots 0/10/14/15
always run; scripts use slots 1/2/4/5/7/9/12. Scripts yield by storing
wait 1 into their slot and switching via 82783210 (114 call sites), i.e.
they count frames themselves.

New `--doax2_native60_script_fibers_30hz` (default true): on alternate
updates non-system slots that would resume (wait <= 1) get wait 2, so
scripts run at 30 Hz. Pacing all scripts broke the pre-title FMV sequence
(it never started / could not be skipped); pacing is now limited to updates
where a minigame manager ran (826496A0, 826ACB20, 82687C20) within the last
2 updates. Title verified normal again (preview at 26.4 s, nothing paced).
All per-update corrections skip themselves inside a paced script fiber
(Native60InScriptFiber), and "fiber context" lines log where each runs;
82634EB0 warns if the jet physics ever runs inside a paced fiber.

Reverted (log 198): whole-fiber pacing made volleyball gameplay slow; the
volleyball manager/phase machine/AI run inside fiber slots 4/5/6 (fiber
context lines), so minigame "scripts" are the gameplay. The cvar now
defaults to false. Next path: a yield-site census on 82783210 (yields =
switches from a slot back to the scheduler fiber 834A3254; the return
address identifies the wait loop) to pace only intro/READY-GO wait loops.

## 2026-09-30: log 200 ("animations look 30 fps")

After the pacing revert the user reported animations stepping at 30 fps at
correct speed. Log 200: engine 60.0 Hz in gameplay, step 0.5, no pacing
lines, and the yield census shows every fiber slot (system and gameplay
4/5/6/7/9) resuming 300 times per 300 updates, so no simulation path runs
at 30 Hz. Frame-interval histogram is the same as logs 192-198 (~20%
12-15.5 ms, ~55% 15.5-18 ms, ~20% 18-25 ms): unchanged from the good builds.
The uneven delivery (host present not synced to the display, guest vblanks
from GraphicsSystem's Sleep(1) catch-up loop in rexgpu-xenos.dll) produces
repeated/skipped displayed frames, a candidate for both the "30 fps" look
and the Beach Flags double image. Host vsync (log 181) serialized swap
fence waits (~50 ms frames). The vblank emitter lives in the renderer DLL;
`--gpu_plugin=NAME` loads `rexgpu-NAME.dll`, so an experimental
`rexgpu-xenos60.dll` could fix it without touching the stable DLL, but the
SDK tree's Release/install outputs do not match the known-good hash
(D5D5FD6E...), so such a build needs its own validation.

## 2026-09-30: log 201 (pool hopping OK; jet READY/GO; priorities)

User: animations fine after all (no regression); Pool Hopping already correct
without fixes. Open: jet READY/GO too early, jet FPS drops with many racers
on screen, Beach Flags run hitch/double image.

Jet timeline (log 201): lead-in wait 82642968 (slot 4, loops on 824FF258,
126 updates) -> race loop 826423BC + manager 826496A0 from frame 7330 ->
race camera (mode 9) at 7542. The intro camera shot (82421CF8, object 0)
has a 240-entry table but an authored 220-frame script wait (+136 starts at
219 after the first decrement, versus limit-1 for step-sized shots such as
600/599). The wait counts down by the unscaled step, so the intro ended
after 220 updates (3.7 s instead of 7.3 s) and READY/GO came early.
Fix: 82421CF8 hook classifies each new shot (wait+1 != limit => authored)
and, while the jet race manager ran within 2 updates, halves single-step
decrements of authored waits. Logs "jet authored camera wait" per shot.
Priority after this: Beach Flags hitch (likely display cadence, see log
200 note), then jet-ski GPU/CPU cost with many racers on screen.

## 2026-09-30: log 205 + recording (Beach Flags hitch = frame delivery)

User recorded a Beach Flags race. Per-frame analysis of the recording: the
near girl's region changes by a uniform amount on every non-repeated frame
(pose/position update at 60 Hz), while ~25% of frames are whole-frame repeats
(girl, sky and sand unchanged) followed by a double-size step. Game log
for the same race: 57-60 unique updates/s with intervals 12-25 ms. The
alternation scan found no Beach Flags-only 30 Hz transforms (only a pose
record pointer and fiber-table bookkeeping). User: the hitch also occurs
at 30 FPS; visible in Beach Flags because the camera pans continuously.

Cause: guest vblanks come from GraphicsSystem's Sleep(1) catch-up loop in
the renderer plugin and the host present is not synced to the display (no
VRR; 60 Hz monitor), so a 60 Hz display repeats/skips frames.
Fix (renderer plugin only, stable DLL untouched): `rexgpu-xenos60.dll` built
from the SDK tree with a display-locked vblank emitter
(`--gpu_display_locked_vblank`, default true): DXGI WaitForVBlank on the
game window's output, median-calibrated ratio (2 guest ticks per 60 Hz
display vblank at guest 120 Hz) emitted as a tight group right after the
display vblank, missed display vblanks re-added, high-resolution timer
fallback. Smoke test log 207: display 60.000 Hz, 0 missed vblanks.
Launcher: "Display-locked frame pacing (xenos60 renderer)" checkbox
(experimental modes; selects --gpu_plugin=xenos60). SDK source snapshot
before the change: archive/checkpoint-20260930-base-speed/
(sdk-before-xenos60.patch, graphics_system.cpp.before-xenos60).

## 2026-10-01: Beach Flags run stutter found (log 217) and fixed

Display-locked vblank (xenos60) evened frame delivery but the user still saw
the stutter (also present at 30 FPS). New xenos60 F10 burst capture (60
consecutive swap frames, any resolution): no repeated frames; every ~20
updates the background pans back ~1 px then jumps 3-4 px (camera aim step).
Race dump (Native60FlagsCameraDump, diagnostics only; 300 updates of camera
839BC1D0, race 8398A4A0, runners, pose records): the runner position
(runner +8/+40, pose root +20) moves 0 on the update where the run motion
loops or switches (pose +76 frame reset, runner +52 motion id 3->5->8->B)
and 2x on the next; the camera rotation follows one update later. Fix
(Native60FlagsRootMotionBegin/End around 826AC790): on a zero-movement
update while running, apply the previous update's movement to runner
+0/+8/+32/+40 and pose root +12/+20, then subtract it from the 2x catch-up
(only if the catch-up arrives). Logs "flags run stall carried".
Log 219: first version diverged (camera black, empty win-pose scene): the
pre-race placement holds runners still after large jumps, which were
treated as stalls and fed back. Now guarded: corrections only during steady
running (last 3 steps 0.02-0.3 units and within 40%), never chained, and a
second pattern (double step at some motion switches without a stall,
frame reset -2/-3/-3.5) has its extra step removed and fed back over 8
updates. Replayed offline on log 217: total travel unchanged for both
runners, zero/double steps gone, residual 0.7x/1.45x wobble at 3-5 switches.
Logs 221/223: correcting the runner record (end of 826AC790) smoothed the
camera (jerk events 14 -> 2) but not the girls: 826AF880 copies the runner
position from the player state 833B8520 (+12/+16/+20), and the draw data
was already built. Position-writer probes (417 functions referencing the
player-state table, innermost-writer attribution) found one writer:
82510218(player), the root-motion step, which adds the animation delta at
83426FD0 + player*116 (local x/y/z, cleared after) rotated by the facing.
The stall/double now gets corrected in that delta just before 82510218
(Beach Flags only, steady running only); the runner-record correction is
off. Logs "root motion stall carried" / "double step spread".

## 2026-10-01: Beach Flags fixed (user-confirmed); CPU strength option

User: running is smooth with the 82510218 root-motion correction. CPU felt
harder to beat at 60 FPS; its gauge gain/decay and decision timer already
run at the original per-second rates, so a difficulty option was added:
`--doax2_native60_flags_cpu_strength` (60 Hz mode; scales only the CPU's
per-update gauge gain, not its decay). Launcher dropdown "Beach Flags CPU
strength (60 FPS)": 100% original / 95% / 90% (default) / 85%.
User chose 85%: now the built-in default (cvar default 0.85); launcher selector removed.

## 2026-10-01: pose camera at 30 Hz (logs 231/233)

User: after-point poses (volleyball) and Beach Flags win/loss pose: girls
smooth, camera pan stepping at 30 Hz; replays fine. Writer probes (camera
block and active camera object view matrix +512): 824187E0 copies object
+512 to 839BC1D0 every update; 8241AF08 builds +512 from the object's eye/
target (+192); the mode dispatcher 8241EB78 sends mode 8 to 82430628, the
pose camera sequencer, which computes t = elapsed(+80, int) / total(+112)
of a pose record (834271A0 + k*288) once per call. The int elapsed ticks
every other update while the pose uses the float frame (+76). Fix: on a
half frame (frac(+76) = 0.5, floor = +80) present 2*elapsed+1 / 2*total for
the call and restore after: the camera's own path at elapsed + 0.5. Mode 7
(replays) untouched. Logs "pose camera half frame".
Log 239 trace: the record's elapsed advances on the half-frame updates
(frame n.5 -> elapsed n, frame n+1.0 -> still n), so the first version
doubled on the wrong parity (t = 1.5, 1, 2.5, 2 ...; the camera only moved
forward, still 30 Hz). Now the half step is presented when the frame is
whole and elapsed = frame - 1, giving t = 1, 1.5, 2, 2.5 ...
Log 241: even at the right parity the camera output did not change: the
sequencer blends integer camera keys and truncates (fctiwz) to integer
units (~0.01) before scaling, so half a unit is always dropped. Replaced
the elapsed/total doubling: on the lagging updates 82430628 is run once
with elapsed+1 (ctx copy) and then for real; camera outputs on the object
(+32..+127, +256..+303) are set to the mean of both (the camera's own
path at elapsed + 0.5).

## 2026-10-01: pose camera fixed (user-confirmed); Butt Battle (logs 243/245)

Pose camera averaging confirmed by the user. Butt Battle (camera mode 11):
minigame fiber 82699CC0 calls 8269A070(game 83CAF200) per update; state
machine 8269A138 (+4 current, +8 requested, handlers 82AC1CC8); girls moved
by 8269BF60 (per girl, battle record game + 208*p), not the root-motion
step. Counter scan widened to 83300000-83CFFFFF (player states 833B8520 and
83CAF200 were outside the old range). Per-update counters: +0x10 frames in
phase (intro shot of 180 entries cut after 91 updates -> READY/GO early),
+0x70 match clock, cooldowns +0x264, +0x274, +0x284..+0x2A0. Fix: hook
8269A070 holds every other +-1 step of each (never a step to zero).
If pushes still feel fast: 8269BF60 -> 8269ED40 movement is next.

## 2026-10-03: PAUSED at Butt Battle
Next: compare Butt Battle state/action durations (timeline logger in the
8269A070 hook) between a 30 FPS reference match and a 60 FPS match; the
remaining speed issues are general pace, the girls' intro animation and the
knock-into-water sequence. Probes found no further per-update gameplay timers.

## 2026-10-03: aspect ratio quick test (side task)
The emulator reports widescreen as width*3 >= height*4 (shared rexruntime).
The game reads XGetVideoMode; camera code 82411438 sets its widescreen byte
from fIsHiDef first, then fIsWideScreen. Tested 1439x1080 (HD, non-wide) and
640x480 (SD 4:3) with a 1440x1080 window: the game always renders a 1280x720
16:9 frame (no native 4:3 path), and the presenter fills the 4:3 window with
it (stretched). True 4:3 needs patching the 3D projection/viewport and the
UI layout (or pillarboxing UI); letterboxing is the only no-patch option.
Test script got -WindowWidth/-WindowHeight/-VideoWidth/-VideoHeight.

## 2026-10-04: Butt Battle done (user-confirmed); Tug of War CPU (log 265)
Tug of War (camera mode 12): script 8269FB38, state block 83957910 (+0 state,
+4 frames in state), per-update logic 826A0F80. 826A1FD8 runs 826A2670(p)
for CPU girls (826A2220 for the human): in pulling states (player state +62
4/5) a random per-character amount is added to accumulator 83957930+p*16+4,
a pull happens each time it passes 30 (player state +48 = pull value, else
1.0). Fix: 826A2670 runs on alternate updates per CPU girl; skipped updates
keep +48 at 1.0. (826A1FD8 also ages the controller-history buffer
83957960 per update; left unchanged - user reports controls fine.)

## 2026-10-04: Tug of War fixed (user-confirmed); jet camera + perf start
Renderer (xenos60) now logs per-frame GPU-emulation timing (busy, draws,
copies, submits, WAIT_REG_MEM, idle) and per-draw stage split. Log 271
(diagnostics off, Niki Marina): game logic ~5 ms; 1200-1450 draws at 60 FPS
cost 4.6-5.9 ms, 1600-1650 draws in heavy spots cost 9.7-14.2 ms (per-draw
cost rises to 6-8.6 us) -> CP busy for the whole frame -> 56-58 FPS.
Jet camera (log 270): camera object matrix/inputs change every update in
mode 9; the alternation scan shows a ~330 KB region 8387A84C-838FA848
changing only every other update (likely water/reflection data); census
(log 189) half-rate race-only functions: 826641C0, 82665350, 8276A8E0
(~14/30), 82718E70, 8277C9E0 (~17/30), all called through pointers.
Next: F10 burst in a race to see which screen parts update at 30 Hz.

## 2026-10-05: jet camera + resolution-scale perf finding
F10 bursts (log 273): every frame unique (no 30 Hz screen regions). Horizon
pan per frame shows a 30 Hz zigzag while steering (pairs like 3.2,3.1 |
5.0,5.0). Race dump (log 275): camera rotation/translation deltas alternate
~10% (camera+8: -0.0150 -0.0133 -0.0140 -0.0121 ...), camera object +44
(eye-target distance) oscillates every update (5.0489 5.0552 5.0509 ...).
Census: race camera, manager, physics all run every update. Dump format was
5 significant digits (eye/target precision too low) - now 9 digits, whole
camera object (4144 B) + player0 state; needs another steering run.
Perf: user saw no drops at 1x resolution scale; at 2x (2560x1440) drops
come with "submits" (waiting on GPU) 4.5-7.3 ms/frame -> GPU-side cost at
higher scale. Requirement from user: hold 60 FPS at 1440p (3090 baseline).
Log 280 (hardware write watch on 838C0000): writer is 826572A0, the jet
water wave simulation, 150 writes/300 updates. Callers: 82659568 runs 4x
per update (one per racer, patch = (table + counter) mod 4) and simulates
patches into ping-pong buffers (index 834F7EA8), so the half-rate writes
are ping-pong, not a 30 Hz sim. Log 278 (full-precision dump): horizontal
jet motion smooth; vertical (player0+16) alternates strongly only while
turning (+0.022 +0.015 +0.029 +0.013 +0.030 +0.009) and the camera follows
the height. Next: same dump in the 30 FPS reference to see if original.
Write watch tool: --doax2_native60_write_watch=<hex>[*] (launcher passes
838C0000 with diagnostics; '*' arms immediately).

Jet race 60 vs 30 comparison (logs 282 native60 / 281 30 FPS reference,
dump extended with jet physics 834CDB00 + 0x344*k and 834F7E00 + 0xA0*k):
- jetphys+336 = raw stick (+-0.65); jetphys+364 = smoothed steering. The
  smoothing sequence per update is identical in both runs (0.419 0.451
  0.480 ... -> 0.638), so at 60 Hz steering reaches full lock in half the
  real time: the "overturning" the user feels at 60 FPS.
- jetphys+356 (0..0.662) ramps +0.03 / -0.0133 per update in both runs:
  same unscaled per-update ramp (throttle/power?), twice as fast at 60.
- player0+16 (height) zig ratio 0.98 at 60 vs 0.42 at 30.
Write watch now takes up to four comma-separated addresses (DR0-DR3),
records writer + 2 callers and per-update write counts. The launcher's
diagnostics pass 834CDC6C,834CDC64,833B8530,834CDC58.

Log 283 (4-slot write watch): steering +780 and stick-y +760 written by
82631908 (jet control update, record 834CD960 + 0x344*k, once per update
per racer); throttle +772 by 82631818 (called from 82631908); player0+16
by 82643028 (rider/body update: player position = body record
834D99C0+0x00 (208/racer) position + rig offset from 83CA3054 (736/racer)).
Fix (jet controls): hooks 82631908/82631818 when tick scale is 0.5:
steering +796 rebuilt from a fractional state with per-update fraction
1-sqrt(1-rate); lean +744 delta halved; +672/+676 held alternately;
throttle +784 step halved with carry, +772/+776 rederived. Dump gained
"jetbody" (834D99C0, 4 racers) and "jetrig" (83CA3050, 2 racers) to find
the alternating input of the rider height.

Log 284 (controls fix): steering now reaches full lock in ~0.85 s at 60
like 30 (rate 0.14 -> 0.0726 per update). Rider bob: the alternating part
of player0+16 is only in player/camera fields; upstream the only
correlated input is the water buffer index 834F7EA8 (corr -0.80). Water:
82659568 (driver, once per update per patch 834F7ED0+160p) -> 82657020
(boundary), 826572A0 (step: reads buffer [(idx+1)&1], writes [idx];
256x256 cells x 8 bytes; per-update constants except the restoring term
which uses the game step), 82657B60 (wakes), patch+144++. Fix (water30):
step on alternate updates with step forced to 1.0; S1 kept host-side,
the buffer read next update gets (S0+S1)/2; on the skip update S1 is
copied where the next step reads it and the step block is suppressed
(patch +144 restored). Dump window now equal in real time at 60 and 30
(skip 200/100, record 300/150). Watch list: heading 833B8538, steering
834CDC6C, player0+16, water index 834F7EA8.
Open: user reports that holding a turn long makes it turn in too sharply.

Logs 285 (60, water30 + controls) / 286 (30 ref), equal 3.3-8.3 s window
with long holds: yaw rate during a ~2.5 s full-lock hold is steady at
both rates (60: 45-60 deg/s, 30: 70-80 deg/s, different speeds). The
smoothed steering differs: original stalls at 157 (0.638) for target
159.9 because one step (0.14 * 2.9 + 0.5) truncates back; the fractional
60 Hz state crept to 160 (0.650 = full stick). Fix: when the original
step from the current integer would not move, hold there (and never pass
the original step's landing value). Rider bob: the per-update alternating
part of player0+16 has the same amplitude at 30 and 60 (~0.007-0.009)
and survives water30 (still corr -0.79 with idx parity, now also with
patch step counters), so it is intrinsic to the rider update; camera
view-matrix alternation ratio is comparable at 60 (0.05-0.17) and 30
(0.03-0.30).

User (after steering stall fix): turning good; intro and taunt animations
double speed again. Cause: water30 forced the game step 833B84CC to 1.0
around each 826572A0 call, but the water driver runs on several worker
threads in parallel (log lines from t22800/t22220): other threads read
1.0 meanwhile and overlapping save/restore could leave it at 1.0. Also
the force/suppress flags were plain globals. Fix: flags thread_local,
counters atomic, no global writes; the step's only game-step term
(rest[+44] - a0) * step * K[821CD4D8] on the second float is completed to
step 1.0 in the post-step loop. Turn-in (steering growing toward the
target) 30% quicker by request; stall point unchanged (157).

Logs 289 (30 ref) / 290 (60), jet intro dump: the intro fly-over (camera
object 0, mode 3) lasts 211 updates at both rates (30: 64.1-71.2 s; 60:
51.0-55.0 s), true in every build since log 203 (authored-wait fix only
halved +136; the shot's phase +128 advances by the unscaled step and the
intro ends at phase ~211 of 240). The girls' grid animations follow the
held clock 839865CC (correct length), so the jets (scripted on the shot
timeline) mismatched them. Fix: during the jet race, authored shots also
get their phase advance halved (curve sampled fractionally). Slot 5
(82655288) is the water fiber (toggles 834F7EA8, has its own catch-up
sim path), not the intro script. Race setup 82642510 (after lead-in wait)
calls 82642D08 which requests camera mode 9. New marker "jet controls
start" (first 82631908 of racer 0 per race) at both rates for READY/GO
timing (60 log 290: controls 1.68 s after mode 9).

Logs 291 (60) / 292 (30): phase halving worked (phase 106 at the cut) but
the fly-over still ended after 211 updates: camera object 0 +1940 ("gate")
counts down 1 per update (~420 -> 209) and ends it. READY/GO: race camera
-> first control update 2.94 s at 30, 1.57 s at 60. Counter census in the
intro (both rates): records 834237A8 + 680k (16), +4 = integer angle
0..359 (random start in 825069C0, used as a sine phase in 82506C60) +1 per
update -> jet sway/handlebar at double speed. Fix (ApplyNative60JetIntro
Pacing, per update after the game step, jet race only): halve 1-step
drops of object 0 +1940 in mode 3; hold alternate +1 steps of the 16 +4
angles. Write watch now arms at the jet intro; list: 834237AC, 839E9CC4
(object 0 +1940), player0+16, 83423A54.

Logs 293 (60) / 294 (30): gate halving active (gate 314 at the cut) but
the fly-over still ended after 211 updates, so +1940 is not the end
condition (writer 82424558 <- 8241EB78). Sway angle writer: 826CDA50 in a
fiber (8258CD28). User: in-race handlebars were already fine; the problem
is the intro, where the handlebars lower as the girl mounts, mistimed
(too fast at 60). Likely tied to the intro timeline that still ends at
211 updates. Census: 833B8ED8 reaches 212 at the cut at both rates.
Write watch now supports "<hex>r" (read+write trap); launcher list:
833B8ED8, 833B8ED8r, 833B8EC4r, 833B8F78r.

Log 295 (watch 833B8ED8 r/w): only 824C4450 touches those counters (a
generic counter block), not the intro end. User: intro worse after the
phase/gate halving (camera slowed but cut at the same moment), handlebars
in the intro still mistimed; in-race handlebars fine with the sway hold.
Reverted the phase and gate halving (sway hold kept). The race camera is
applied by 8241B640 (every update via 8241A630) whenever +2932 is set, so
whoever posts the request (+2932 = 1, +2928 = 9, e.g. 82642D08 from race
setup 82642510) ends the intro. Watch list: 839EA0A4 (obj0 +2932),
839EA0A0 (+2928), 839EA32C (+3580), 834D9950 (race phase).

Log 296 (watch on obj0 +2932/+2928): the race camera request comes from
826412A8 (racer message handler, case 1) called by 82640B70, the per-racer
state machine (records 834D8CA0 + 44k, +16 state). +20 = frames in state,
+3 per update (90/s at 30 Hz); state 0 (intro) ends at +20 >= 630 (=210
updates; 82631E30 = skip buttons after 15), state 1 (READY/GO) counts
+24 down by 3 per update with digit +28 = +24/90 + 1 -> state 2. Fix: hook
82640B70 (probe wrapper removed): +20 steps of 1/2 alternately while the
state is unchanged; in state 1, +24 steps of 1/2 and +28 recomputed. With
the intro full length again, the authored shot phase halving and the
obj0 +1940 halving are restored (fly-over reaches ~210 of 240 as at 30).

Logs 297 (60) / 298 (30): intro length and READY/GO now match (intro dump
211 samples both; controls 3.02 s vs 2.96 s after the race camera). User:
intro animation still out of sync / "all over the place". Rig (83CA3050)
float/physics fields differ (e.g. rig+340 ~1.45 at 30 vs +-0.2 at 60,
rig+144..+328 dynamic at 60, static at 30), looks like the float/bob
physics rather than the animation timeline. Intro dump now also records
pose players 834271A0 (288 x8), sway records 834237A8 (680 x4), clock
839865C0..DF, race state 834D8CA0 (44 x4), root motion 83426FD0 (116 x4).

Logs 299 (60) / 300 (30), extended intro dump: girls' pose players
(834271A0), race clock 839865CC, race state, root motion, sway angles all
match per real time. Jet record +604 = input history ring index (32 x 16
bytes at +92), +1 per update at both rates but not animation. User: the
girls hold the handlebars the whole intro; the jet-ski animation runs
double speed so the bars leave their hands. New diagnostic: intro rate
scan (snapshots 83300000-83CFFFFF at race-state frames 90/270/450, logs
steadily changing floats) to compare 60 vs 30 for double-rate values.

Logs 303/304 rate scan (water excluded): no steady value at double rate
except wake particles and object 6 tracks (double-length 600/1000 tables,
correct in real time). Log 305 F10 bursts (60): girls hold the raised
steering pole overhead (burst 1), ~0.7 s later the pole is already down
while the black-haired girl's hands are still high (burst 2). rig+336..344
is a position (y = +340), not the pole. New diagnostic: transition scan
(every 0.1 s of intro time each float word keeps the time of its largest
single change; logged at intro end) to find the pole value moving early.

Logs 306 (60) / 307 (30) transition scan: words 833B5C30/5C40/5D10 and
copies (833B6140/6190/6240/6290/7938/7BB8/7C38) make the same change with
the same end values at both rates but 1.8 s earlier at 60 (sample 38 vs
56 of 70, 0.1 s each): the steering-pole drop. Watch list now 833B5D10,
833B5C30, 833B5C40, 833B6190 to find the writer.

Log 308 (watch): pole matrix words written by 82514BC8 (+FEE/+14FE) and
825153A0 -> 8262C8C0, both from 82513930 = per-character model update
(4 layers at char+60, stride 76; 825153A0 places the jet attachment).
82513930 steps blend counters with 30 Hz constants per update: +378 (u16)
+1 toward +376 with +372 = +378/+376; +400 (u8) +3 to 30 / -10; +401 (u8)
+5 to 30 / -10. Fix: hook 82513930 (probe removed) during the jet race at
tick scale 0.5: halve each step with carried remainders, +372 rederived.

Log 309: character blend pacing never triggered (+378/+400/+401 static in
the jet race), so not the cause. 82514BC8 is the 4-limb IK (the 833B5xxx
words are the girl's arms following the pole); the pole itself is
825153A0 -> 8262C8C0 (rotation interpolation) with factor = byte
833B7330 + 4p (player) x constant (821C... -9964). Static writers of that
byte are setup code only (82513058, 8248DF68), so the stepping is through
a pointer: watch list 833B7330/34/38/3C, intro dump block "poleblend".

Logs 310/311: pole blend bytes 833B7330.. never change (constant factor).
8262C6A8 builds the pole rotation from two of the girl's bone positions
(per-player block 833B5310 + p*2048, +256/+320), so the pole follows the
girl's skeleton; her final bones move 1.8 s early although the pose
players are on time. Next: per-limb IK records in the character struct
(82513930 arg, 4 limbs at +60 stride 76, read by 82514BC8). Intro dump now
adds each character struct seen by 82513930 (512 bytes, "charN").

Logs 312/313 (character structs 8342CA30 + 412p): all limb/blend fields
advance at the same real-time rate at 60 and 30 (travel ratio ~1.0).
Watch list now the pole aim inputs 833B5410/833B5450 (p0 +256/+320) and
833B5C10/833B5C50 (p1).

Log 314: the pole aim positions (833B5410/5450, 5C10/5C50) are written
only inside 82513930 (82514BC8 IK, 82514FD8, 8262C8C0 via 825153A0).
Timing: pole drop at ~5.6 s (30) vs ~3.8 s (60), consistent with an
unpaced per-update counter starting ~2 s into the intro. Watch list:
census counters ticking per update at both rates in the intro:
839E94E0, 83947210, 838FA908, 8398521C.

Log 315: 839E94E0 = 824190F8 (bone history ring), 83947210 = 8266E618
(global), 838FA908 = water driver, 8398521C = 825E3800 (mod-40 effect
counter): none is the trigger. Transition scan: the early words include a
bone x position (833B5C40) i.e. the girl's own bones move early. 82513930
builds each girl's root from 83C0E340 + 384p (8241EA40/8241E808 matrix
helpers); the census counter 83C0EFD0 (same structure) ticks once per
update through the intro (423 at 60 vs 213 at 30). Watch list:
83C0EFD0, 83C0EFD0r, 83C0E370, 83C0E4F0.

Log 316: 83C0EFD0 only touched by 824F03B8 (generic); girls' placement
matrices 83C0E340+384p written by 8263B1A0 (jet module, from rig 83CA3050
+160 and body). Log 317 (30 ref, F10 bursts): the pole lowers gradually
(~2 s) with the hands on it as the girl stands up; at 60 it drops in
~0.7 s. Transition scan: 834C0DF4/0E14/0E20 (+1088 copy 834C1234...)
end at the same values but change ~3.3x faster per 0.1 s at 60 -> watch.

Log 318: 834C0DF4/0E14/0E20 are a shared scratch matrix (8262CC10 called
from IK, 82471AE0, 8264C480...), not state. Intro dump extended to all 4
jet control records, 4 rig records and the girls' bone blocks 833B5310
(2048 x4) to find the pole angle that eases 2-3x faster at 60.

Logs 319.1/320.1 (all riders): girls' bone block values ease from the same
start at the same time to the same end, but in half the samples at 60
(e.g. bones+1764: 74 vs 143). 825153A0 keeps smoothed rotations blended by
8262C8C0 (slerp(a, b, t)) once per update with constant factors:
+1792/+1856 = slerp(new, self, f28) (retention), +768/+384 =
slerp(self, target, f31) (weight). Fix (jet race, tick 0.5): hooks
825153A0 (thread-local player block) and 8262C8C0: retention -> sqrt(t),
weight -> 1 - sqrt(1 - t) for those four outputs only. Probes on both
functions removed. Note: logs over 5 MB rotate (*.1.log holds the start).

Log 321: pole smoothing conversion applied (retention 0.90 -> 0.949,
weight 0.70 -> 0.452) but the user saw no change, so not the cause (left
in, harmless). New diagnostic: jet intro heap scan - snapshots all
committed RW guest pages 40000000-BFFFFFFF at race-state frames 225/414/
612 (2.5/4.6/6.8 s) and writes changed float words to
out/game/logs/jet_heap_60.txt / jet_heap_30.txt (no log rotation).

Logs 322/323 (+ jet_heap_60/30.txt): the runs are deterministic - girls'
bone values identical at 60 and 30 until intro sample 161 (race-state
frames 483, 5.3 s), then diverge; only bones/rig diverge (pose players,
char structs match). Integer counter sweep of the intro dump: jet records
+68 and +664 (all 4 racers) count +3 per update at both rates (1260 vs
630 over the intro); +664 reaches 480 (grab/pole lowering) at 2.7 s at 60
vs 5.3 s at 30. Body +132 (3 racers) +2/update at 60 vs +3 at 30 (1.33x).
Fix: hook 82630458 (probe removed) halving +68/+664/+668/+728 (+3),
+660 (+1), +720/+88 (-1) with carried remainders (not +604, the input
ring); hook 82643028 (probe removed) paces body +132 to 1.5/update. Heavy
scans (heap, transition, rate) disabled by a constexpr switch.

Animation memory sweep kept as launcher modes "60 FPS + animation memory
sweep (diagnostics)" (61) and "30 FPS reference + animation memory sweep
(diagnostics)" (32): cvar doax2_native60_anim_sweep enables the heap
snapshot sweep (automatic in the Niki Marina intro, or F9 x3 anywhere ->
out/game/logs/anim_sweep_<60|30>_<n>.txt) plus the intro transition/rate
scans. Compare with scripts/anim_sweep_compare.py <60 file> <30 file>.
The write-watch list was removed from normal diagnostics.

Log 324 (60 with jet counter pacing) vs 323 (30): jet counters now match
but the girls' bones still diverge from sample ~148 (player 2 attachment
slots +512/+1664/+1920, which are plain copies of the girl's bone). The
character update 82513158 evaluates the skeleton (82513430) from the pose
pointer at root 83426FD0 + 116p + 24 = motion struct 8342B418 + 1512p
(constant pointers) - not the 834271A0 records compared so far. Intro
dump now records the four motion structs ("motion", 1512 x4).

Logs 325/326 motion structs 8342B418 + 1512p: the girls' motion matches
(differences are Euler wraps +-pi/2 / +-pi / 3pi/2). New: cvar
doax2_native60_intro_autocapture (launcher: F10 capture + diagnostics +
xenos60) presses F10 at race-state frames 330 and 520 so 60/30 bursts
cover the same intro moments; xenos60 burst files now carry a per-session
HHMMSS tag (burst-<HHMMSS>-<n>-frame-<swap>.bmp). Old bursts moved to
out/game/captures/old.

Logs 327/328 auto bursts (same intro times): at 3.7-4.6 s both girls
stretch holding the raised pole; at 5.8 s at 60 the pole is already flat
and the girl's hands are still up, at 30 she still holds the raised pole
(it lowers only near 6.7+ s). Camera shots differ between runs (likely a
random shot choice). Transition-scan words 8342D0A4/D0B0/D0C8 (2x steeper
at 60) are in character record 4 (8342CA30 + 412*4); the 82513930 hook
only sees characters 0-2, so records 3+ may be the jets: intro dump now
records chars 0-7.

Logs 329/330 (sweep modes): character records 0-2 change identically in
both runs between snapshots; char3 only changes at 60 (the 30 run had one
racer fewer - runs are not identical setups). Switched to a visual bisect:
launcher modes 71-75 "60 FPS intro test A-E" set doax2_native60_intro_bisect
1-5; the chosen group runs only on alternate updates during race state 0:
1 jet update 82630458, 2 rider 82643028, 3 placement 8263B1A0+8263C310,
4 race management 82642E08/826448A8/8263D340/82638CE0/826328D8,
5 girls' character update 82513158 (probes removed on 8263B1A0/82513158).

Intro bisect A-E (user): none changed the pole/hand sync (E halves the
girls and drops hair physics, as expected). Test F added (bisect 6):
82421CF8 (track update, wrapped in Native60UnscaledStep for every caller)
runs at the scaled step for non-camera objects; diagnostics log each
non-camera caller ("track user").

Boot FMV skip (all experimental modes; stable exe untouched): CRI Sofdec
MWSFD 3.97 - 829BF270 mwPlyStartFname, 829BD948 mwPlyGetStat (2 playing,
3 play end). Boot plays ninja_vi_hd.sfd then promotion_video.sfd. While
those (or opening.sfd) play, A/Start on any pad (emulator input system,
plus XInput), Enter or Space makes GetStat report 3 for that handle;
the game moves on (verified with Enter: logo ended at 3 s, title next).

Test F (log 341, diagnostics): no non-camera caller of 82421CF8 at all,
so jets are not animated by scene tracks; user: no change. Rig +100/+116/
+132 are water bob oscillations. Next: write watch on the rendered model
matrices that move early at 60 in the sweeps (839CA090, 839CACE0,
839CB9B0, 839CD090) to find the model/animation that writes them.

Log 342 (watch on early render matrices): written only by 824190F8 (bone
history ring), dead end. Time-ordered bursts: the girl's motion is right
at 60; the pole is already down by 5.8 s (30: raised until ~6.2 s, then
lowers slowly with the hands). Abrupt-change search in the intro dumps:
body+104/+312 (rider blend, racers 0/1) jump at sample 141 at 60 vs 193
at 30. 82643028 in jet state 2 sets body+104 = lerp(..., clamp(int(jet
+724) * k)); jet +724 is a -1 per update countdown (clamped at 0) in
82630458 that the counter census had missed (load and decrement 4 lines
apart). Added {724, -1} to the 82630458 counter pacing. (body+148 is an
event index, not a counter - left alone.)

Log 343 (with +724 pacing): user - still mismatched. body+104 drop still
starts ~sample 136 at 60 vs ~180 at 30 (only slightly slower); jet +724
never changes in the intro, so the state-2 branch uses table values
indexed by body+116/+120 (constant) - the table itself changes earlier.
Test G added (bisect 7): intro script slots 1/9/12 (frame-counting waits
826CC5CC / 82511C24) resume on alternate updates during the jet intro.

Test G (intro script slots 1/9/12 half rate): the girls froze but the jet
/pole still moved early - the pole timeline is independent of the girls
and scripts. Watch on heap model matrices (log 344): render scratch only
(8249B750/8249A618, 829EFE60 memcpy from 82477470). Girl 0 bone block:
everything diverges together at ~161; nothing inside leads. The rider
blend body+104 (drop starts 136 at 60 vs 180 at 30) in 82643028 state 2
= lerp(T[body+116], T[body+120], ...) with T = global float table
834D9968 (22 floats, right before the body records): intro dump adds
834D9900-834D99BF ("blendtab"), watch list 834D9968/6C/70/B8.

### 2026-10-13: Niki Marina intro pole clock (82633FE0)
Blend table 834D9968 never changes (write watch: no writers). body+104 (rider/pole
blend) follows the same waveform at both rates but 4/3 faster at 60. In intro
states it is sampled from an animation (825D2D68) at time [[82B498D4] + 2528k + 1260].
82633FE0 advances +1260 by trunc(a*step + b): 3 per update at 30, 2 at 60 (same
rounding as body+132). Hook paces it to 1/2 alternately (1.5 per update) during the
jet race. Intro dump gained char_anim0..3 blocks (+1240..+1295) for verification.
Follow-up: the intro uses the second path (+1252 += +1256, +1260 = +1252), rate 2 at 60 vs
3 at 30 (30 FPS: +1252 rises exactly 12 per 1/30 s). Pacing only the 2s fixed the gross
mismatch (user-confirmed) but the clock still ran ~4% ahead (+104 frames by the end of the
intro, from occasional 3s). Now every advance is scaled by 3/4 with a carried remainder.
Follow-up 2: with 3/4 scaling the clock still led by ~35 frames (~1.4%) at the end of the
intro; 60 FPS raw steps are 2 with jitter-driven 3s, while 30 FPS is always exactly 3. The
hook now ignores the raw step and advances exactly 1.5 per call (1/2 alternately).

## 2026-10-13: Niki Marina intro pole FIXED (user-confirmed "perfect")
Final fix: `sub_82633FE0` hook (src/doax2_60fps.cpp). Record r3 = [82B498D4] + 2528k
(+0 = racer index). The steering pole is sampled from an animation (825D2D68, called
from 82643028 at lr 826436B8) at time [record+1260] * K[823EB960]; result -> body+104
(834D99C0 + 208k + 104), which positions the pole the girls hold.
- Intro path: +1252 += +1256 (rate = trunc(a*step+b): always 3 at 30, 2 with jitter
  3s at 60), then +1260 = +1252. Other path: +1260 += trunc(...) directly.
- Fix (half rate, jet race active): ignore the raw step, advance exactly 1.5 per call
  (1/2 alternately via a per-racer carry), mirror into +1260 when the intro path ran.
- Tuning history: pacing only 2s -> gross fix but ~4% fast; 3/4 scaling of raw step ->
  ~1.4% fast (jitter 3s); fixed 1.5/call -> matches 30 FPS.
Lessons (for future animation mismatches):
- Compare the waveform of the visible output (body+104) at both rates: same shape but
  time-compressed by 4/3 means a "3 per update at 30 / 2 at 60" rounding counter.
- trunc(a*step + b) rate patterns appear in several places (body+132, +1256); search
  for fcfid/fctiwz pairs around global step 833B84CC.
- The earlier dead ends (blend table 834D9968 constant, character blend counters,
  smoothing, bisect groups A-G) were all downstream of this one clock.
Diagnostics kept: intro dump char_anim0..3 blocks (+1240..+1295), blendtab block,
anim memory sweep (launcher 61/32), intro tests A-G (launcher 71-77), intro autocapture.

## 2026-10-13: 1440p optimization start (jet race, Butt Battle)
Known before this session: at 1x no drops; at 2x (2560x1440) drops coincide with
"submits" 4.5-7.3 ms/frame (CPU waiting on GPU). CP draw cost ~6-8 us/draw at
1600 draws (9.7-14.2 ms) in the heaviest jet race spots.
Finding: EndGuestOcclusionQuery (xenos d3d12 command processor) ends the submission
and CheckSubmissionFence()s it, i.e. a full CPU<->GPU sync for every guest occlusion
query; GPU work grows with resolution scale, so each sync costs more at 2x.
xenos60 changes:
- cvar d3d12_doax2_async_occlusion_query: record EndQuery + ResolveQueryData in the
  open submission, no wait; answer the guest with the latest completed result for the
  same sample-count address (fake count until the first lands). Launcher checkbox
  "Async occlusion queries (performance, xenos60)", default on, passed only with xenos60.
- New log line every 300 swaps: "DOAX2 GPU sync": CPU fence-wait ms/frame + waits,
  occlusion queries/frame and their sync wait.
Backups: archive/checkpoint-20260930-base-speed/*.before-async-occlusion.
Log 354 (1440p, async on, diagnostics off, Niki Marina 48-54 FPS): occlusion queries = 0
per frame (the game uses none). Race: CP busy 15.5 ms of 16.7 (draws 5.7 ms for ~1830
draws; "submits" 7.0 ms, of which fence waits only 0.8 ms), WAIT_REG_MEM 3 ms. Draw-stage
heaviest: vertex/memory 2.25, setup ~1, primitives 0.74, textures 0.7.
Next instrumentation (xenos60): GPU timestamps per submission ("DOAX2 GPU work" line: GPU
busy ms/frame) and submit split (deferred command list replay vs ExecuteCommandLists),
to tell CPU-bound CP from driver throttling on a busy GPU.
Log 355 (1440p): race GPU busy 8.4-9.2 ms/frame (GPU timestamps) -> NOT GPU-bound. CP thread
busy 13-15.5 ms; ExecuteCommandLists 4.2-4.6 ms/frame in the race (0.08 ms in Butt Battle),
replay 1.7-2.2 ms, draw "setup" 1.2-2.6 ms (0.15 in Butt Battle). Debug layer off.
User: swimsuit selector (menu/hotel) locks at 47-50 FPS = reproducible static test scene.
Added xenos60 sampling profiler (cvar d3d12_doax2_cp_sampler, launcher passes it with
xenos60): 1 kHz stack samples of the CP thread, logs top modules / leaf / inclusive
functions every 10 s. Release renderer has no PDB; SDK now links with -Wl,/MAP
(CMAKE_SHARED_LINKER_FLAGS_RELEASE) and the matching map is archived as
archive/checkpoint-20260930-base-speed/rexgpu-xenos60.cp-sampler.map (symbolize offsets).
Log 356 (sampler): swimsuit selector = 24 ms frames, 1426 draws, GPU busy 12.8 ms, CP thread:
39% inside win32u!NtGdiDdDDISubmitCommand (kernel submit, under ExecuteCommandLists 9-10
ms/frame), 21% WaitForSingleObject (fence waits ~5 ms/frame), 12% Sleep, rexgpu own code
~14%, NVIDIA UMD 6%. Diagnosis: whole frame recorded into one deferred list and submitted at
the end (~1.7 submissions/frame) -> GPU idles while CPU records, then the next big submit
blocks in the kernel while the GPU drains: CPU and GPU serialize (11 + 12.8 ms).
Fix attempt: cvar d3d12_doax2_submit_draw_interval (xenos60): EndSubmission(false) every N
draws (when no pipelines are being created); launcher checkbox "Split GPU submissions
(performance, xenos60)" passes 192 (default on). Map archived as
rexgpu-xenos60.split-submit.map.
Log 357 (user, split submissions every 192 draws): still ~40 FPS in the selector. Self-tests
(title/attract, 1280x720 window): splitting raises CP per-draw cost ~4x (3 -> 12 ms/frame);
reverted (launcher now always passes interval 0).
MEASUREMENT BUG fixed: GPU timestamps were collected only every 300 swaps from a 512-slot
ring, so "GPU busy" undercounted. Corrected numbers (attract scene, 1429 draws, 61 resolves):
GPU 11.9 ms at 1x, 22-26 ms at 2x (MSAA off no better) -> GPU-bound at 1440p; large fixed
per-frame GPU cost even at 720p.
Barrier counts (new "DOAX2 GPU barriers" line): heavy scene ~1550 transitions/frame, ~708 on
the shared memory buffer (per-draw upload: read -> COPY_DEST -> read).
Upload batching (cvar d3d12_doax2_batch_uploads, launcher "Batch memory uploads", default on):
DeferredCommandList kHoistedUploads anchors; D3D12SharedMemory records uploads into the
current anchor group while buffer state stays read and no command since the anchor read the
pages (SharedMemory::NoteRangesRead after each RequestRanges); any shared-memory state change
ends the segment. Self-test: shared transitions -> 0, no crash; heavy-scene GPU effect not yet
measured (self-test runs stayed on the title menu). GPU section timestamps (cvar
d3d12_doax2_gpu_sections) inflate GPU time; now off by default.

### 2026-10-13: 1440p heavy-scene FIX (attract/swimsuit scene, 1430 draws, user-driven runs)
| run (log) | frame ms | GPU ms | notes |
| batch uploads OFF (372) | 24.5 | 23.2 | 1552 transitions/frame (708 shared memory) |
| batch uploads ON (371/373) | 20.0 | 18.5 | shared-memory transitions 0 |
| ROV path | ~100 | - | user: ~10 FPS -> rejected |
| skip draw transfers (376, diag) | 16.7 | 5.5 | models missing -> transfers are ~2/3 of GPU cost |
| + execute_unclipped_draw_vs_on_cpu(+_with_scissor) (378) | 16.67 locked | 10.5 | user: 60 FPS, looks normal |
Root cause (RT trace, cvar d3d12_doax2_rt_trace_interval, log 377): small offscreen targets
(thumbnails at EDRAM tile 1472, pitch 2/4/7, MSAA 1/2/4) had unknown height, so ownership was
claimed to the end of EDRAM and wrapped (1472-2048 + 0-1472) -> every bind transferred the
full 1280x720 frame (720 tiles) in and back (several per swimsuit/girl). Running unclipped
screen-space draw VS on the CPU gives exact extents (upstream Xenia's intended default).
Launcher (experimental builds): passes --execute_unclipped_draw_vs_on_cpu=true and
--execute_unclipped_draw_vs_on_cpu_with_scissor=true, batch uploads checkbox (default on),
async occlusion checkbox (game issues no queries; harmless), CP sampler only with Diagnostics.
Remaining diagnostics in xenos60 (all off by default): d3d12_doax2_gpu_sections,
d3d12_doax2_skip_draw_transfers, d3d12_doax2_rt_trace_interval, d3d12_doax2_cp_sampler,
d3d12_doax2_submit_draw_interval. GPU timing/barrier/transition log lines always on (cheap).
Next: verify Niki Marina (4-5 racers) and Butt Battle at 1440p with the launcher.

## 2026-10-13: Roulette crash fixed (user-confirmed "works")
Log 382: [FATAL] Call to invalid or unregistered function at guest address 0x8244EF10 when
starting Roulette. 0x8244EEF8 is a jump-table dispatcher (table 0x8228A15C: 827D8868,
8244EF10, 8244EF20, 8244EF30; bctr at 0x8244EF0C); the scanner absorbed the adjacent thunk
0x8244EF10. Manifest: 0x8244EEF8 = { size = 0x18 }, 0x8244EF10 = {}; regenerated (7 files
changed; generate-code.ps1 fails under PowerShell because rexglue prints its banner to
stderr - run sdk/rexglue-sdk/out/install/win-amd64/bin/rexglue.exe codegen
doax2_manifest.toml --ignore-stamp directly). Experimental exe rebuilt; stable doax2.exe still
lacks the fix (rebuild needs user approval). Backup: archive/checkpoint-20260930-base-speed/
generated-before-roulette-fix.

## 2026-10-13: v1.4.0 stable + "60 FPS stable v1" (user request: pause development)
out/game/doax2.exe = current build (SHA 06F9158C...), out/game/rexgpu-xenos.dll = current
renderer (CB9440F0...), rexruntime.dll unchanged (4E25FF8B...). Old stable (0FA4DC74 /
D5D5FD6E) archived in archive/stable-v1.3.86-before-2026-10-13. Launcher: new mode 65
"60 FPS stable v1" (doax2.exe, tick 0.5, refresh 120, gpu_display_locked_vblank=true, no
diagnostics); "30 FPS (stable)" now gets the same exe/renderer with
gpu_display_locked_vblank=false; both stable modes pass the renderer perf flags. Smoke tests
(logs 384/385): 30 FPS 33.33 ms frames, 60 FPS 16.67 ms, no fatal errors.

## PAUSED 2026-10-14: 4K (resolution scale 3) optimization, swimsuit menu
Goal (user): 60 FPS at 3x (3840x2160 internal) everywhere; everything else already runs 60
at 3x, only the swimsuit menu does not; also benefits lower-end GPUs. Plan steps 1-4: measure,
cut resolution-scaling transfers/resolves, scale offscreen targets selectively, cap MSAA cost.
Baseline (log 391, experimental exe + xenos60, 3x): swimsuit menu 19.1 ms/frame (~52 FPS),
GPU 18.0 ms, 1013 draws, 52 resolves, 724 transitions, 158 transfers = 20226 tiles/frame
(~28 full-screen equivalents). Biggest transfer pairs: main-frame MSAA variants (0t<16t>1x ->
0t<8t>4x 1440 tiles, depth 720t<16t>4x -> 736t<8t>4x 1408, etc.); thumbnail area 1472t
2xMSAA color <-> 4xMSAA depth 119 tiles x2 per item (small now).
GPU sections (log 392, inflated by the timers: 32.5 ms total): RT update/transfers 6.35 ms,
texture requests/loads 3.6 ms, resolves 4.0 ms (only 52 sections -> least inflated).
Texture trace (log 393, new: RT trace now logs per-draw texture fetch addresses from
TextureCache::RequestTextures): per swimsuit item (x15 per frame):
  thumbnail drawn in 1472t<7t>2xMSAA -> resolve 264x136 to 1CAD6000
  draw onto main 0t<16t>1x sampling 1CAD6000 -> resolve FULL 1280x720 to 1F577000
  next draw (1472t<4t>1x, a 320 px wide target) samples 1F577000 as 1280x720 -> ... resolve
  256x128 to 16930000 -> composited back onto main.
  So each of the 15 full-frame resolves IS read (not redundant), but only to produce a
  256x128 card: candidate optimizations on resume:
  a) find the UV region draw ~387 samples; if only a sub-rectangle, resolve just that region;
  b) cheaper resolve path at scale (resolve cost x9 at 3x), or keep these resolves/targets at
     a lower scale (step 3: selective scaling for offscreen/feedback targets);
  c) main-frame MSAA variant transfers (step 4: MSAA cost).
Tools: d3d12_doax2_rt_trace_interval=600 (binds/transfers/resolves/clears/texture reads),
d3d12_doax2_gpu_sections, transition histogram line, skip_draw_transfers (diag).
Stable binaries untouched (v1.4.0); xenos60 has the extra trace code (all off by default).

### 2026-10-14 (resumed): repeat-resolve cost at 3x, swimsuit menu (logs 394-399)
Geometry trace (vport/scissor/first vertices per draw): per swimsuit item, draw ~630 pastes
the thumbnail onto main 0t<16t> at screen (315,153)-(473,247); the full 1280x720 resolve to
1F577000 follows; draw ~634 (256x128 card target) samples ONLY uv 0.253-0.363 x 0.221-0.334
of it (~141x82 px, the pasted spot).
F8 three-state A/B, same scene (1429 draws, 61 resolves), cvar d3d12_doax2_skip_repeat_resolves
(diagnostic; F8 cycles OFF -> COPY-ONLY -> ALL):
  normal              GPU 18.6-18.8 ms, frame ~19.6 ms
  skip copies only    GPU 16.1-16.6 ms  -> repeated copies ~2.5 ms
  skip copies + invalidation (no texture reloads) GPU 13.1 ms -> reloads ~3.1 ms
Both halves matter. Exact fix candidates: (1) incremental resolve - copy only rows dirtied in
the source RT since the previous identical resolve (needs a min-Y extent; claims currently
start at row 0); (2) partial texture reload of only the invalidated rows. Heuristic option:
render these repeated full-frame feedback resolves/textures at 1x (both costs /9, card
backgrounds slightly softer at 4K).

## 2026-10-15: Pool Slide (water slide) 2x speed
Logs 401/402 (counter scan) + 403/404 (slide dump): slide update = sub_826A4E00 (r3 = manager
0x8395794C, +8 call counter; slide state 0x83957D00; sub_826A99C8 copies the transform to player0
+12..+40). Every field (velocity state+48, positions, rotation, timers, fail check) advances a
fixed step per call: 60 FPS sample k == 30 FPS sample 2k -> whole simulation 2x. At 60 the first
attempt ended after 42 calls (~0.7 s, user: instant fail / "barely finishes the first shot").
Fix (sub_826A4E00 hook, tick 0.5): simulate on alternate updates; on simulation updates show the
midpoint of old/new for player0 +12..+44 and the 1 KB state block (plain floats, |delta| < 50);
on in-between updates restore the exact step values (simulation never sees the midpoints).
Slide dump now records every attempt. Pending user test.

NOTE (2026-10-15): rexglue codegen rewrites generated/default/doax2_pch.h and drops the local census hook (#if defined(DOAX2_FUNC_CENSUS) include ../../src/doax2_census.h). Reapply it after every codegen (scratch script restore_census.py; archive copy generated-before-roulette-fix/doax2_pch.h).
Pool Slide follow-ups (2026-10-15): main dispatcher sub_826A4288 (mgr+0: 1 intro sub_826A4800,
2 countdown sub_826A4CF0 [setup sub_826A4AB8 sets a 90-update countdown], 3 slide sub_826A4E00,
4 results sub_826A75A8, 7 end sub_826A7C70; plus per-update HUD timer sub_826A7F90/sub_826A8238 and
sub_826AA728). All phases paced to alternate updates; camera-shot wait/phase halving gated on the
dispatcher (Native60SlideActive). Sound: slot functions sub_826F42B0.. are the girl's voice only;
833A10E4.. alternating floats are skinning scratch (8248A8F0/8248A640/82489D68). Census 60 vs 30
(census build; pch hook restored): splash/effect emitter sub_825F83D0 (-> 825F6BA8/75A0/7138/7960/
7F08 -> 82672E40 -> 826725F8/82672D10/8273FB98) runs at the same per-update rate at both rates ->
paced to simulation updates. Open: sub_826A34C8 called 6.4x more per update at 60 (pointer-called),
intro second preview shot not switching.

## Pool Slide robotic water sound (2026-10-15/16)

- Not the water-surface height field: 82659568 runs 4x/update but its step
  (82657020/826572A0/82657B60) never runs in Pool Slide (census 0 at both rates).
- 826A3440/826A34C8 (+826A9F28 matrix*vector): slide-spline point search, bursty
  at both rates; census "60-only" signal was window placement. Dead end.
- 826F3C90 is the controller RUMBLE player, not sound: 4 controllers x 3 slots
  (stride 40) of envelope entries (duration, left, right), distance attenuated,
  10 duration units consumed per update -> rumble patterns play 2x fast at 60
  everywhere (open bug). 826F42B0 IDs BD..C0 are rumble requests.
- Audio runtime parameter setters: 82715158(obj, list{u8 n,(u8 idx,f32)}, immediate)
  = speaker-matrix levels (6 ch) per voice, reached via 82724E68 -> vtable+56 from
  the 3D update 8274A980. Levels pan smoothly at 60, no alternation; no new voices
  start during the slide (water is a long-lived voice).
- Audio engine work 8273ACC0 (once per update, called indirectly; calls 8274A980
  and ~20 bank/stream/voice routines). FIX (test build): run it on alternate updates
  at tick 0.5 (alternate by call, not frame parity). F6 toggles live. User: first
  run "way less robotic", second run "fixed". F6 A/B not yet done.
- Some engine functions (8273FAB8, 827425A8, 82742808, 82743238, 82744218) already
  run at a fixed 30/s at both rates (engine real-time clock).
- 8273FB98/8273FAB8/8273FA10 = Mersenne Twister RNG; 8273BAC8 = D3DCOLOR->float4.

## Pool Slide finished (experimental, 2026-10-16 .. 10-21)

User-confirmed "perfect" in doax2.native60-poc.exe (not yet promoted to stable).
- Robotic water: sound events 826676A0/82666DA0 requested on the sim update AND
  the in-between update (16.00 ms comb in the captured mix); repeat of the same
  event on the next update is skipped (in stable v1.0.1).
- Stutter: phases run on alternate updates; render-facing blocks get midpoints
  on step updates: player0 +12..+44, state 83957D00, skeleton region
  [833FB0EC]+0x2FE8 (18424 words, split in 4), small blocks from hold scans.
  Restore only words still holding our midpoint (restoring all 73 KB wiped what
  other code wrote: no splashes, flicker). Never smooth the first step after a
  gap; flush at the dispatcher 826A4288 entry; heap pointers look like floats.
- Flicker: player0 +32 (facing) re-expressed by 2pi between steps (1.60 ->
  -4.68); plain mean turned her 180 deg for a frame. Native60Midpoint takes the
  short way when the difference is within 0.5 of whole turns. Found with a
  per-update trace + user video (lap timer -> engine frame).
- Drop into the lake = results phase 826A75A8 (89 updates; countdown mgr+8,
  t -> 826A69A8, random camera shake). Per-call motion: must stay paced;
  82510218 adds root motion to player0 every update -> additive restore.
- Intro: pan 1 waits for the entry animation, then mgr+8 = 200 (100: pan 2b,
  0: setup). Scene-track tables are step-sized at 60 (280 vs 140): no extra
  halving outside the slide gameplay.
- 826F3C90 = controller rumble (plays 2x fast at 60 everywhere: open bug).
- Open: slide speed tune (on hold), rumble pacing, xenos60 renderer untested here.
