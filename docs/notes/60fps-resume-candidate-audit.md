# 60 FPS resumption: candidate audit

## 2026-09-28 private R8 view validation in progress

**Completed as log 085.** The user entered relax; the game was closed via
its normal window-close route and logged `Execution complete`. There were
40 recorded private R8 copies, 40 fence-completed proofs, 20 cross-bank
witnesses, zero ownership rejections or witness failures. The two private
backings had distinct persistent unsigned/signed SRV pairs: bank 0 GPU
handles `...27E00/...27E20`, bank 1 `...27E40/...27E60`, native swizzle
`00000124`. The SRVs are **allocated but not yet bound to an isolated draw**.
Only pre-existing cache-device/XMPGetInfo error lines were present; no new
D3D12 error was identified. The stable DLL was restored and hash-verified.

Unlike log 083's static-white samples, the R8 readback hash changed across
relax-scene samples while each bank's witness exactly preserved its prior
hash. Thus the R8 producer is **not globally constant**: a fill-FF
substitution would be incorrect. This validates why the separate decoded
resource snapshot, not a guessed constant or guest-memory copy, is needed.

The new output-state audit observed two multi-draw clear-rooted right-half
passes: frame 4920 event 19 with 17 draws, and frame 5040 event 19 with 8
draws. In both, every draw used depth key `002842E0`, color key `00084000`,
surface info `0A010280`, 640x720 viewport/scissor, unchanged target
attachments/viewport/scissor, and native color/depth clear values
`FF000000FF000000` / `00000000FFFFFF00`. Host 2x MSAA was supported. This
establishes the shared attachment/viewport boundary for an isolated output;
it does **not** establish exact pixel parity, complete texture lifetime, or
a published midpoint. Next implementation must build a private 2x-MSAA
color/depth target, reproduce the native clear/resolve, and submit the
complete ordered pass from fresh CPU-owned inputs and descriptors, comparing
against the same frame's native right-half pixels before interpolation.

The two already proven independently owned R8 decoded-input banks now also
allocate fresh persistent bindless unsigned `R8_UNORM` and signed `R8_SNORM`
SRVs with the native binding's host swizzle. Reused banks reject a changed
swizzle, and source-copy layout is constrained to `R8_TYPELESS`. The view
dimension matches ReXGlue's native `TEXTURE2DARRAY` binding. This only
prepares a potential private shader input; neither view is bound to a new
draw, and no extra frame is submitted. SDK Release compiled and all four
root CTests passed. The opt-in DLL is preserved as
`out/game/rexgpu-xenos.audit-private-r8-srv.dll`, SHA-256
`9D2B8181A7F0A1EE84AAAD2CC66EB0FF2EC58F4AC78BA9D0B2A1A9993E8CC7BF`.
It is temporarily installed for PID 32436 and a user-assisted relax-scene
test; inspect the next `doax2_*.log` for nonzero distinct private SRVs,
fence-completed readbacks, unchanged witnesses, output-state records and
GPU/visual regressions. Restore `rexgpu-xenos.pre-owner-checkpoint.dll`
(stable SHA-256 `D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`)
after closure. No comparator or midpoint is working yet.

## 2026-09-28 remaining two small-pass textures classified (log 084)

One combined opt-in run sampled the relax scene, then closed through the
normal window-close path at the user's request. The user reported no visual
abnormality in the preceding ownership run; no separate visual report was
recorded for this second run. Log 084 has 56 decoded-input records/proofs,
28 unchanged cross-bank witnesses, 112 native-composite-source proofs,
zero ownership rejections, and normal `Execution complete`. The only error
lines were the pre-existing cache-device and XMPGetInfo messages; no new
D3D12 device failure was identified. The stable installed renderer was
restored and SHA-256 verified as `D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.

At actual draw bindings in the small clear-rooted right-half pass, both
previously unresolved inputs are independent decoded 512x1024 textures,
`DXGI_FORMAT_BC2_UNORM` (numeric 74), **10 mips**, one sample, shader-read
state 192, no separate signed resource or outdated mask. The `08D93000`
backing remained identity `000001F664EA1460`, load generation 1/serial
11658; `08EC7000` remained `000001F664EA01B0`, generation 1/serial 11655
across sampled scene frames. Neither had a same-frame overlapping resolve.
This supports treating them as persistent inputs for this run, but does not
prove globally immutable content or exclude later CPU writes. The active
GPU-produced R8 input (`1BD7C000`) remained uniform `FF` in its separate
fenced copies; the correct private binding must still come from an owned
texture/view, not the native descriptor.

Frame 4800 has a particularly small six-draw clear-rooted pass, draws
781–786, resolve event 13, native color owner `00084000`, three texture
identities, no missing shader/constant/pipeline data, no recorded draw side
effects. Its left-half producer event 11 is not clear-rooted; only the
right half is eligible for isolated comparison. The combined run saved a
native reference pair at frames 2039/2040 (`out/game/captures/owned-composite-source-frame-2039.bmp`
and `...2040.bmp`), but frame 2040 depicts a night beach panorama before
the user's relax entry, **not** a character image or a private output.
Diagnostic DLL preserved as `out/game/rexgpu-xenos.audit-all-small-inputs.dll`,
SHA-256 `D76BC0A492FBDD2D28BE22778A87D80E531AC20FF0FF410EE373D9218AC70B6C`.

Next engineering work is an opt-in exact-N+1 isolated pass, selected only
from a versioned clear-rooted span. Reuse CPU-owned snapshots and freshly
allocated descriptors from the existing owned-group path, substitute a
private SRV for the GPU-produced R8 input, retain full BC2 mip coverage,
match the native 2x-MSAA color/depth clear and resolve, and compare the
unpresented right-half pixels to a fenced native reference from the *same*
frame. The legacy group renderer currently rejects multisample outputs and
clears transparent black; neither guard should be removed until that exact
path is implemented. Do not route this partial output to the visible mailbox.

After log 084, a bounded read-only `DOAX2 clear-root output state` record was
added for each small span: first-draw viewport, scissor, attachment keys and
formats, native clear values, host 2x-MSAA capability, and counts of
attachment/viewport/scissor changes across its ordered draws. This will
distinguish a simple shared target from per-draw state changes before
extending the private group renderer. It compiled, all four root CTests
passed, and the DLL was saved as
`out/game/rexgpu-xenos.audit-clear-root-output-state.dll`, SHA-256
`87F09348575672ED9E2B929E81D772B9F6663BB8B475F362508A72A97FE18D36`.
It has **not** been live-tested. The stable DLL remains installed. The
existing `-CaptureOnSmallSpan -SourceCaptureAfter <frame>` combination can
delay the one-shot source pair until after gameplay entry; log 084 used the
default threshold 1800 and captured the night panorama at frame 2040.

## 2026-09-28 completed live decoded-input ownership test (log 083)

The user entered a relax scene and reported no visual abnormalities. The
opt-in decoded-input test closed normally. There were **8 recorded copies,
8 real ReXGlue queue submissions, 8 fence-completed readback proofs, and
4 cross-bank witness comparisons**; all witnesses reported `preserved=true`,
all failures stayed zero, and no bank reuse rejection or D3D12 device error
was found. Two independently committed backings (`000001F56DA3ED80` and
`000001F56ECA47C0`) alternated while the native source identity remained
`000001F56F236710`. The source at actual pixel binding was a 512x512
`DXGI_FORMAT_R8_TYPELESS` texture (numeric 60), one mip/sample, shader-read
state 192, no separate signed resource, and zero outdated mask. The latest
same-frame overlapping resolve was event 13 for the frame-10200 clear-rooted
pass. This proves one **decoded GPU-produced input can be independently
snapshotted and fenced in the live renderer**, not that the 12-draw output or
an intermediate frame can yet be reproduced.

All eight readback hashes were `9EA8483515962325`, equal to FNV-1a for
262,144 bytes of `FF`; this input is uniform in these samples, not proven
immutable across the game. The pre-existing cache-device and XMPGetInfo
errors remained; no new D3D12 error was identified. The test installed no
extra presentation and did not change game simulation. After `Execution
complete`, the stable 30FPS DLL was restored and SHA-256 verified as
`D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.

Next: use the existing isolated renderer only after extending it for the
native right-half pass's exact 2x-MSAA color/depth layout and `FF`-alpha
clear, and bind this snapshot with a **fresh private SRV**. The two other
texture inputs' contents/mips and the output resolve must be independently
accounted for. First target an unpresented pixelwise comparison against the
saved native frame-3360 right half. Do not claim a midpoint or 60FPS output.

## 2026-09-28 decoded GPU input ownership probe (live test pending)

The log 082 frame-3360 twelve-draw right-half pass depends on the texture at
guest `1BD7C000`, whose latest overlapping GPU resolve is event 13. A new
opt-in diagnostic (`-ResolvedInputCopy` in `scripts/run-candidate-audit.ps1`)
records numeric resource metadata at the **actual pixel binding** and, for
pose-capable draws on color owner `00084000`, copies that active decoded
texture to one of two independently committed D3D12 resources. The copy
uses the existing `D3D12TextureCache::RecordOwnedActiveTextureCopy` state
guard; no guest-memory reconstruction, native descriptor retention, extra
render, or presentation occurs. The source state is restored before the
native draw. A separate readback and cross-bank witness are protected by
the **real ReXGlue submission fence**; bank reuse requires observed fence
completion. Layout/state, size (at most 1024x1024), single-sample and
single-mip guards reject unsupported inputs. This is a test of one dependency,
not complete input coverage or midpoint feasibility.

The Release renderer compiled and all four root CTests passed. Diagnostic
DLL: `out/game/rexgpu-xenos.audit-resolved-input-copy.dll`, SHA-256
`A13C7C695DF18C3D31EC96488D0BB328B29D955A656008404C7C407D27BCBC9E`.
It is temporarily installed for log 083, PID 13004. At writing, the game
was still running but had sampled only the 34-draw title path; there is no
resource-copy proof yet. User interaction is needed to enter a relax scene.
After the game closes, inspect `owned resolved input recorded/submitted/proof/
witness` lines, active binding metadata and visual report, then restore the
stable DLL `rexgpu-xenos.pre-owner-checkpoint.dll` (SHA-256
`D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`).
If the copy is rejected, diagnose the actual state/layout; do not loosen the
guard speculatively. The 30Hz simulation and normal 30FPS renderer remain
the only presented path.

## 2026-09-28 triggered relax-scene reference and input edge (log 082)

The user entered a relax scene in the opt-in build. The first qualifying
small clear-rooted pass was frame 3360, resolve event 19: twelve ordered
guest-index, pose-capable draws 879–890, no missing PS/constants/pipelines,
no recorded side effects, three texture identities, 12,263,832 summed
logical dependency bytes. The source owner was `00084000`; event 17 had
cleared the matching 640x720 color/depth region after the left-half resolve,
and no owner transfer intervened. Event 19 resolves right-half destination
`1F923000`, 640x720, 2x MSAA source (enum 1); the full decoded consumer at
draw 891 reads both regional versions `[18,20]`. The left half is separately
produced by event 17 and is **not** proven clear-rooted. This is not a full
scene reconstruction candidate.

The content trigger selected frames 3359/3360. The existing two private
banks recorded the actual decoded 1280x720 native composite source at draw
891. The N readback was safely re-mapped after the N+1 audit selected the
pair; its hash remained `C222A8C3D2B92616` after observed fence 7579.
N+1 hash is `0E172CF6CA97EB39`; both BMP saves succeeded, bank failures
remained zero, and the run ended `Execution complete`. Visual inspection of
both BMPs shows the complete moving waterside environment (water/birds move),
not a blank title or transition. These are native **input** images before
the subsequent composite draw, not reconstructed output or necessarily the
final presented image. Left and right half SHA-256 hashes each differ
between the adjacent captures; the right half of N+1 is an exact native
pixel reference for event 19's resolved region. No independent reproduction
has yet been submitted or compared.

The twelve-draw pass reads three decoded texture identities. Pixel binding
0 at guest `1BD7C000` (262,144 bytes, 512x512 resolve destination) overlaps
two earlier resolves and is most recently produced by event 13 in this
native frame, before draw 879; it is **GPU-produced**, so copying guest
memory afterward is not a valid substitute for owned decoded texture
contents. Pixel binding 2 uses `08D93000` and `08EC7000` (524,288 bytes
each), with no overlapping same-frame resolves; this does **not** prove
immutability or absence of CPU writes. Their actual content stability and
all sampler/mip requirements still need validation. The native depth/clear
and 2x MSAA resolve must be reproduced exactly for pixel comparison.

The existing private group renderer already creates fresh color/depth
resources, geometry and descriptors, but `TryDrawOwnedRenderGroup` rejects
`output_samples != 1`. Its materializer also copies texture ranges from
guest memory, unsafe for the GPU-produced `1BD7C000` input. Its output
clear is transparent black, while the recorded native color clear for this
pass is `FF000000FF000000` (black with nonzero alpha). Consequently the
current private-group path **cannot** be used unchanged for an exact event-19
reproduction. Do not bypass these guards merely to submit a draw. First
capture the decoded 512x512 producer texture in a fenced private backing at
the draw's actual binding, classify the two other texture inputs, then add
an explicit 2x-MSAA/clear-matched isolated output path and compare its
right-half pixels to the saved native source. The original 30Hz simulation,
normal renderer and all presentation fixes remain unchanged.

The diagnostic process was closed and the stable installed DLL restored to
SHA-256 `D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.
Existing cache-device and XMPGetInfo log errors were present; no new D3D12
error or device removal was identified. The user confirmed the relax scene
started; no separate visual-regression report was requested for this run.

## 2026-09-28 content-triggered native capture (in progress)

Fixed swap-count capture thresholds twice sampled non-character visuals, so
the opt-in reference capture now supports `-CaptureOnSmallSpan`. At the
existing sampled N/N+1 audit, it selects the first clear-rooted color span
of at most 24 all-guest-index, pose-capable draws with shader/constant/
pipeline data present and no recorded side effects. Selection is still a
*reference-image trigger*, not a claim of complete dependency coverage or
eligibility for rendering. The source copy remains the original fenced,
two-bank copy; this edit adds **no** GPU commands or presentation. It can
re-map N's already fenced private readback after N+1 selects the pair,
checks that its hash is unchanged, and writes both source BMPs once. The
normal fixed-threshold capture mode remains available independently.

The SDK Release DLL compiled and all four root CTests passed; the launcher
syntax check passed. The diagnostic DLL is saved as
`out/game/rexgpu-xenos.audit-triggered-native-capture.dll`, SHA-256
`4B826FF482BB874BDB58EC4498E15F1089F40EDCEF0A22A111D8C51DDFF9A6C2`.
It is temporarily installed for a live, user-assisted relax-scene test
(`out/game/logs/doax2_081.log`, PID 30248). Restore the stable renderer
SHA-256 `D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`
after the run. At checkpoint-writing time, only the opening 34-draw path
had been sampled; no trigger or provenance record was yet validated.

## 2026-09-28 follow-up capture limitation (log 080)

The new bounded small-span texture-provenance audit compiled and all four
root CTests passed. A second opt-in run used the source-capture threshold
3960, but the actual capture at frames 4079/4080 showed a moving mountain
background, not the character relax path. The sampled scene remained at
30–34 draws, so **no small clear-root span or texture-provenance record was
exercised**. The source BMPs are valid nonzero, changing images, but do not
serve as a reference for log 079's six-draw character-scene pass. This run
was cleanly closed (`Execution complete`) without drawing or presenting an
extra frame. The stable renderer was restored and hash-verified at
`D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.
The opt-in diagnostic DLL is preserved as
`out/game/rexgpu-xenos.audit-small-span-provenance.dll` (SHA-256
`1F99CB59E7A170F03C3EFD2969FFA37D84A89B9B95BF7F41BB7F3ABEB716C7F3`).
Do not count this as provenance validation. A later single interactive run
must explicitly navigate to a character relax scene *before* collecting the
small-span provenance and settled native image. No midpoint output exists.

## 2026-09-28 relax-scene clear-root input test (log 079)

The combined opt-in run reached a relax scene and closed normally. It sampled
46 frame pairs, produced 41 clear-root input records and 92 native-source
copy proofs. All 92 source proofs reported `failures=0`; the new BMP pair at
frames 1919/1920 was mapped only after fence 1920 and contains nonzero,
changing RGB (`2762640` nonzero RGB bytes in frame 1920). Visual inspection
of frame 1920 shows a blurred transition, not settled relax content, so this
pair is **not** a sufficient native reference for the selected relax pass.
The user confirmed the game was in relax; there was no separate visual
regression report for this diagnostic run. The existing cache-device and
XMPGetInfo log errors were present; no new D3D12/device error was found.
After clean `Execution complete`, the stable DLL was restored and verified at
SHA-256 `D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.

The previously selected 94-draw pass is scene-dependent, not invariant.
Frame 4080's corresponding color producer event 13 had **24** all-guest-index
draws, 5 texture identities, no missing shaders/constants/pipelines and no
recorded draw side effects; its summed logical dependencies were 13,299,576
bytes. More importantly, frame 4440 event 13 had a **six-draw** right-half
color pass from draws 785–790, after a matching full 640x720 clear at event
11 and without an intervening owner transfer. Its six draws have complete
shader/constant/pipeline metadata and three texture identities, with
6,164,484 summed logical dependency bytes. The other half is a separate
producer/version 12; the composite consumer reads both `[12,14]`. The
six-draw pass is an isolated color-output candidate, **not** the scene or a
publishable midpoint. Pixel coverage, its texture contents, depth behavior
and all downstream composition are still unproven. By contrast, frame 3000's
97-draw candidate contains 24 non-guest-index / pixel-shader-less draws,
which the existing owned unit renderer cannot treat as ordinary geometry.

Next source edit: bounded, read-only per-texture provenance for clear-rooted
spans of at most 24 draws. It logs guest range, first binding and same-frame
overlapping resolve events (including the latest since the clear). This is a
*candidate* producer edge, not a content-version or immutable-resource proof;
CPU writes and actual sampled footprint remain unresolved. It compiled in
SDK Release and all four root CTests passed, but has not yet been exercised
in gameplay. The stable installed renderer is still in place. The next
combined diagnostic run should capture a settled-scene native source image
and exercise this provenance census, preferably with capture threshold beyond
frame 3600; avoid a separate user test for either item alone. Only then
select and implement an independently owned exact-output reproduction of
the smallest fully classifiable unit. No extra frame was rendered/published.

## 2026-09-28 native-source capture smoke test (log 078)

An opt-in BMP dump was added to the existing independently backed, fenced
native composite-source readback. It writes the first sampled native pair
after the configured frame threshold, without recording an extra draw,
presenting another frame, or touching simulation. The launcher script exposes
`-SourceCopy -SourceCapture -SourceCaptureAfter N`; capture requires source
copy. The source BMP is a reference for a future exact-output comparison,
not a reconstructed frame. The new clear-root span input census is also
compiled into this diagnostic DLL.

A short unattended title-only smoke run (`out/game/logs/doax2_078.log`)
ended normally and wrote 1280x720 frame-119 and frame-120 BMPs through
distinct private banks. Both readbacks waited for observed fence 120;
bank-preservation checks reported zero failures. Both images have hash
`52BDA05E66A4A325`, full alpha and **zero nonzero RGB bytes**. Visual
inspection confirmed black RGB. This proves the file/write/readback path,
not that the chosen source is the visible title image or a useful gameplay
reference. No moving-scene input census was exercised in this short run.
SDK Release compilation and all four root CTests passed. The stable
installed DLL was restored and hash-verified after the smoke test.

A combined opt-in gameplay run is now in progress to test whether the
chosen source has actual scene pixels and to classify the 94-draw
clear-rooted span's inputs. Its diagnostic DLL is preserved as
`out/game/rexgpu-xenos.audit-clear-inputs-source-capture.dll` (SHA-256
`956979336B9CD05699ECAEBB31D44607FF5911D1060576C2EC3AB55FA1E9E45F`).
The regular stable DLL must be restored after this run. No midpoint render
or publication is enabled.

## 2026-09-28 post-resolve clear root (log 077)

The next read-only audit records the **actual submitted host-RT clear** after
each sampled resolve: color/depth target keys, guest pixel rectangle, and
clear values. The regional producer analyzer then seeks a preceding clear
of the same source owner and exactly matching rectangle/layout, rejecting
intervening ownership transfers. It does not infer clear coverage from a
resolve flag alone, retain GPU objects, submit draws or present a midpoint.

An opt-in candidate-audit-only build ran through a moving relax scene. The
user confirmed the scene was running; once the needed samples were captured,
the diagnostic process was closed and the stable installed DLL restored to
SHA-256 `D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.
The tested DLL is preserved as `out/game/rexgpu-xenos.audit-clear-root.dll`.
The user did not provide a visual assessment for this specific run. Log
`out/game/logs/doax2_077.log` ended with `Execution complete` and no
D3D12/device error or terminal-lineage rejection.

The log contains 628 regional binding records, all with
`tracked_regional_version_edge=true`; 180 were multi-producer records. Of
211 producer-to-span records, 170 matched a preceding same-owner color draw
span and 45 additionally had a same-owner, same-rectangle post-resolve
color-clear root with no intervening owner transfer. The remaining 41 span
records had no adjacent same-owner draw span under this narrow rule, often
because another operation intervened; they remain unresolved, not silently
accepted. Ninety submitted resolve-clear records were captured. All 58
terminal owner lineages matched the actual final owner, but all remained
content-incomplete and nonpublishable.

In frame 1680, resolve event 8 submitted a clear of color owner `00084000`
and depth owner `002842E0` over exactly `(0,0,640,720)`, with color value
`FF000000FF000000`. The subsequent 546-draw color span (draws 580–1125)
uses `00084000`, a non-clearing depth resolve is skipped, and event 10
resolves the same 640x720 source. No owner transfer intervened. This is a
known **initial color value** for that second-half pass, not proof of final
pixel coverage or of the draw inputs needed to reproduce it. It is not yet
a complete independently reproducible image, and no 60 FPS output exists.

The smallest clear-rooted span in this log is **94 draws**, not 546. Frame
4080 resolves a first 640x720 half after 70 draws (event 3), submits the
same full-region color/depth clear, then resolves the second half after
94 draws (event 5). The second-half pass has the same source owner, clear
rectangle and value and no intervening owner transfer. The native
1280x720 consumer reads the combined versions `[4,6]`. The existing
largest-run audit for that frame reports 164 draws across the two halves,
107,116,532 summed logical dependency bytes, and 19,922,706 unique guest
backing bytes for the combined run. Those bytes are not yet independent
owned inputs for the 94-draw half, and the first half does not have the same
proven clear root. The 94-draw half is the better **isolated** native-output
comparison candidate, but not a complete scene or midpoint frame.

A focused read-only input census has now been added for clear-rooted spans:
index classes, shader/constant/pipeline gaps, side-effect draws, bindless
state, pose-capable draws, texture references/identity hashes and logical
dependency bytes. It has compiled but has **not** been exercised in a
moving scene. A new CPU lineage regression proves that a known clear root
followed by a draw with unproven inputs remains incomplete. SDK Release
compilation and all four root CTests passed; the stable renderer is
installed. Next: use one combined moving-scene run to validate the 94-draw
input census and the other source-side graph edges, then choose the smallest
exactly reproducible unit for owned rendering and native-image comparison.
Do not promote the 94- or 546-draw span as complete merely because it has a
clear root. Avoid another manual run for only one diagnostic bit.

## 2026-09-28 live regional producer check (log 075)

After the read-only per-texture regional classifier compiled, an opt-in
candidate-audit-only build was run through a user-entered relax scene. It
exited normally (`out/game/logs/doax2_075.log`); no extra frame was rendered
or presented. The user reported closing the test, but did not provide a
separate visual assessment for this specific run. The stable installed DLL
was restored and verified at SHA-256
`D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.
The tested DLL is retained as
`out/game/rexgpu-xenos.audit-regional-candidate.dll`.

All 348 sampled binding records had complete 1280x720 regional coverage,
zero unsupported overlaps, `uploaded_after_all=true`, a live native texture,
and 920/920 GPU-written guest pages. (Repeated binding entries mean these
are records, not distinct scene objects.) The old exact-whole-image edge
accepted 284 and rejected 64; all 64 rejected entries were complete
two-producer regional cases. In frame 2880, draw 1126 consumes versions
`[9,11]` from resolve events 8 and 10. Those are left/right 640x720 tiled
destinations at `1F90F000` and `1F923000`, each with an independently
recorded single source owner `00084000`. A later whole-image version replaces
the pair at draw 1155. All 39 terminal lineage samples had a prior owner
checkpoint and matched the final owner; all 39 remained content-incomplete.
No D3D12/device error or lineage rejection was found in this log.

The next source edit adds a **separate**
`tracked_regional_version_edge` diagnostic requiring complete regional
coverage, successful upload after every retained producer, one recorded
source owner per producer, and the same resource/page/side-effect checks as
the strict edge. The old strict verdict and all rendering behavior remain
unchanged. The SDK Release DLL compiled and all four root CPU tests passed.
This verdict is exercised only in the title check below; do not count it as
a validated moving-scene second render input, much less a complete output.

The same saved frame also supplies the immediate backward edge. Its ordered
operation sequence has 522 draws to color owner `00084000` (draws 58–579),
then color resolve event 8; after a non-clearing depth resolve, another 546
draws to that owner (draws 580–1125) feed color resolve event 10. The event-8
and event-10 color sources each report a single `00084000` owner. This
identifies the last preceding color draw spans, **not** complete source
pixel contents: prior owner transfers, clears, draw coverage, and shader
inputs remain to be traced. A bounded read-only diagnostic now joins each
regional producer to its preceding same-owner draw span, skipping only
non-clearing depth resolves. It logs a rejected match if any intervening
operation or owner identity is incompatible. This joins metadata only; it
does not execute draws or publish output. The latest SDK Release DLL compiled
and four CPU tests passed. An unattended 16-second title check
(`out/game/logs/doax2_076.log`) exited normally: 48/48 regional binding
records had `tracked_regional_version_edge=true`, and 12/12 source-span joins
matched their recorded owner. This tests the join in a simple scene only;
the two-producer 522/546-draw relax path has not been retested with the new
verdict. No D3D12/device errors were found. The stable installed DLL was
restored and hash-verified after the check; no human visual observation was
requested for the title-only run.

Next: verify the source-owner-qualified verdict and draw-span join together
in a later opt-in run. Work backward from those exact producer events into
their EDRAM draw/clear content versions and resolve layout; the single
source-owner key is not itself a pixel-content proof. Then classify
the other sampled draw inputs and final resolve. Avoid another user-driven
run for this one logging bit alone if the next graph stage can be prepared
offline first. No 60 FPS presentation path is enabled.

## 2026-09-28 dynamic dependency checkpoint (both parts of log 073)

The opt-in source-copy audit ran through a moving relax scene and exited
normally. The user reported no visual abnormalities. The run rotated into
`out/game/logs/doax2_073.1.log` and `doax2_073.log`; counts below combine
both. It did not reconstruct or present an extra frame, and normal gameplay
remained at 30 Hz. The installed DLL was restored to known-good SHA-256
`D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`;
the tested diagnostic DLL is retained as
`out/game/rexgpu-xenos.audit-dynamic-lineage.dll`.

There were 140 terminal-lineage samples. 75 matched their actual final RT
owner; 65 had no observed owner in the suffix, rather than a contradictory
owner. The alternate depth-keyed final route `003082D0` occurred in 26
samples. There were no rejected suffixes. Every sample remains content-
incomplete and nonpublishable. There were 1,312 decoded-texture candidate
binding records (some duplicate bindings): 980 had the exact resolve →
successful-upload version edge; 332 had no eligible producer. All 332 failures
had `producer_event=SIZE_MAX`; none were caused by invalid/non-GPU-written
pages, prior captured memexport draws, or a bad native binding layout. The
run also recorded 280 fence-protected private-source proofs with zero
failures, and 140 preservation witnesses all marked `preserved=true`. These
are scoped resource and lineage checks, not evidence of complete output.

Frame 11160 demonstrates why the 332 cannot be accepted by a simple byte
overlap: 640x720 resolves at event 8 and event 10 overlap the bounding byte
extent of a later 1280x720 texture; event 11 is a subsequent whole-frame
resolve. Their tiled destination rectangles/offsets must be modeled as
separate producer regions. The current exact-whole-image selector deliberately
rejects such mixed versions. Do not relax it until exact tile/rectangle
coverage and producer order are proven.

A follow-up read-only change now snapshots the generic RT cache's uniform
tile-0..720 owner after successful sampled resolves. The terminal graph may
seed the preceding resolve's owner as an **unknown-content** version when this
checkpoint exists. This can distinguish an unchanged-owner suffix from an
unobserved owner but cannot make the image complete. The SDK Release DLL
compiled and all four root CTests passed, including a new regression that
checks owner identity survives an unchanged suffix while content remains
incomplete. A 22-second opt-in title run (`out/game/logs/doax2_074.log`)
exited normally: all nine sampled terminal suffixes had zero new
claims/transfers, prior checkpoint `00008000`, traced owner `00008000`,
`owner_match=true`, and `content_complete=false`. This is a live ownership
classification check, not a reconstructed image or a human visual test. The
stable DLL was restored afterward and its hash reverified.

The existing `RegionVersions` pass in saved frame 11160 had already reported
complete 1280x720 coverage at composite operation 24 from producer versions
`[9,11]`: two differently based 640x720 tiled resolves, not one whole-frame
resolve. The per-texture audit now independently reports this regional
producer set, unsupported overlaps, missing pixels, and whether the native
texture was successfully uploaded after all named producers. It does **not**
promote a regional candidate to the strict `tracked_version_edge` verdict or
enable reconstruction. A frame-11160-shaped CPU regression (left half, right
half, invalidation, full replacement) and all four root CTests pass. The new
per-texture regional logging compiles but still awaits a live moving-scene
check; the stable DLL has not been replaced.

Next: validate the per-texture regional candidate in a moving scene. Require
exact coverage and successful upload before promoting any currently rejected
texture edge. Continue backward through
remaining draw inputs and exact final-resolve layout; no 60 FPS output yet.

## 2026-09-28 resolve → decoded-texture title edge (logs 069–072)

At the native four-vertex composite draw boundary, the sampled audit now
selects a candidate producer only when the texture binding and most recent
overlapping resolve have the same exact 1280x720 tiled RGBA8 base-level layout
and 3,768,320-byte guest extent. Any later overlapping partial/unsupported
resolve removes the candidate. The active `D3D12Texture` resource is described
but never retained by this audit. `TextureCache` now counts successful base
uploads per texture and globally; each resolve records the global counter
*before* its GPU copy, letting the consumer prove it loaded afterward.
`SharedMemory::DescribeRangeOrigin` samples existing page flags under the
existing lock; it does not change page state. The verdict also rejects a
pending outdated texture, failed/absent resource, invalid pages and any
captured earlier memory-export draw. A source audit found the D3D12 writes
into this shared-memory abstraction at resolve dispatches and draw memory
exports. The verdict is scoped to that tracked path and exact layout, not a
claim about arbitrary textures or the complete scene.

Unattended title-only runs `out/game/logs/doax2_069.log` through `_072.log`
each exited with `Execution complete`. In log072, all 112 sampled binding
records (two repeated binding entries per composite draw) had
`uploaded_after_producer=true`, all 920/920 guest pages valid and GPU-written,
and zero preceding captured memory-export draws. For frame120, draw2 names
resolve event0/serial6, with global upload serial 479→480; draw3 names
resolve event1/serial9, 480→481. The *same native resource pointer* is
reloaded, so resource identity is not content identity. Fourteen terminal
owner samples remained incomplete/nonpublishable. No D3D12 error, device
removal or crash appeared in these title logs. These are process/log checks;
no human gameplay/visual inspection or midpoint frame was performed.

The existing private-copy/fence proof is separate: it copies the native
decoded texture into independent banks, but the versioned edge and private
copy have not yet been exercised together in a dynamic scene. The newly
compiled audit labels only the above exact tracked edge as such. It has not
been deployed for a gameplay run. The installed game DLL was restored after
each title test to known-good SHA-256
`D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`;
the tested diagnostic DLLs are saved beside it as
`rexgpu-xenos.audit-decoded-producer.dll`,
`rexgpu-xenos.audit-upload-generation.dll`,
`rexgpu-xenos.audit-gpu-page-origin.dll`, and
`rexgpu-xenos.audit-resolve-upload-serial.dll`. SDK Release builds and all
four root CTests passed. Stable gameplay/audio/pacing code is unchanged.

Next: run the current read-only audit in one dynamic relax scene to see
whether full exact-layout decoded-texture edges and both terminal ownership
routes persist beyond the title. Report rejections by specific condition.
Then feed one proven intermediate producer version into `OwnerLineage` rather
than treating the entire pre-final suffix as an unknown seed. The remaining
full-frame blockers include other intermediate formats/layouts, draw
inputs/side effects, transfer conversions, final resolve layout and private
resource ownership for each required output. No 60 FPS visual output exists.

## 2026-09-28 terminal-suffix lineage and draw-use checkpoint (logs 067–068)

The CPU `OwnerLineage` model is now joined read-only to one live candidate:
the last exact 1280x720 swap resolve and the ownership-plan suffix after the
preceding resolve. The analyzer normalizes each draw's exact cache claim and
scheduled transfer, then compares the traced final tile owner with the real
cache owner rectangle. It does **not** submit, reconstruct or present a second
frame. The final resolve layout, draw inputs, prior producer images, transfer
conversions and GPU lifetimes remain explicitly unproven, so `content_complete`
and `publishable` remain false. An iterative dependency walk replaces recursive
DFS to handle thousands of draw versions without stack overflow. Same-owner
overlapping claims and claim-after-transfer sequences are rejected.

The first local title-screen test (`out/game/logs/doax2_067.log`, diagnostic
DLL `00E735AA…`) had ten sampled frames (120–1200). Each terminal graph
matched the recorded `00008000` final owner, covered all output pixels, and
reported nine failed/unproven producer versions. It counted 24 suffix draws
with no ownership claim. The game exited normally; no second visual image was
produced. This was process/log validation, not human visual inspection.

Review of `RenderTargetCache::Update` showed that a draw can have zero claims
because it has **no used render target**, not because the recorder missed a
transfer. A bounded per-draw cache-update record now captures rasterization
disposition, color mask and claim count before the normal draw. The second
local title-screen test (`out/game/logs/doax2_068.log`, DLL `E02B5C0F…`)
recorded exactly 34 cache updates for 34 captured draws, zero drops in each
sample. All 24 claim-free draws in the terminal suffix had zero render-target
claims (`no_target_draws=24`, `outside_terminal_draws=0`). Six sampled frames
through frame 720 matched the same final owner and remained incomplete as
intended; the process closed with `Execution complete`. No user visual report
was requested for these brief title-only tests. The diagnostic does **not**
yet classify non-RT side effects of those 24 draws or prove their absence.

The compiled diagnostic DLLs are saved as
`out/game/rexgpu-xenos.audit-terminal-lineage.dll` and
`out/game/rexgpu-xenos.audit-draw-target-usage.dll`. After both tests, the
installed `out/game/rexgpu-xenos.dll` was restored to the known-good SHA-256
`D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.
SDK Release builds and all four root CTests passed, including an 8192-draw
lineage walk. Stable 30 Hz simulation, NVIDIA pacing, audio and default
rendering were not modified.

Next: join the preceding intermediate resolve/version to the first dependent
composite draw using **exact decoded-texture identity**, not guest-byte
overlap. Track native shader side effects and render-target-less draws
separately. Then run this read-only graph in a user-entered relax scene to
validate the alternative terminal owner route (including the saved frame-5880
shape) before selecting a self-contained, independently backed reproduction
unit. A complete image or 60 FPS output is not established.

## 2026-09-28 live ownership-event checkpoint (logs 065–066)

The user ran two opt-in, read-only candidate-audit sessions with a relax scene
and reported no visual abnormality; both exited with `Execution complete`.
Neither submitted a second frame or changed 30 Hz simulation. The first
session (`out/game/logs/doax2_065.log`, DLL `A370EFFC…`) sampled 29 frames,
maximum 2231 ownership plans/frame, zero drops. For every sample,
`plan_count + resolve_count == event_serial_max`. It again captured a
color→depth-keyed→color handoff, e.g. frame 1920 around draws 1088–1089.

The second session (`out/game/logs/doax2_066.log`, DLL
`ABBFE938E0571FC4500649CAB974457DC33016E4D656DC43906A793486163CC5`)
also captured the **exact EDRAM claim ranges** passed by the generic
`RenderTargetCache::Update` draw-extent calculation to `ChangeOwnership`.
These are separate from scheduled source→destination transfers: an ordinary
draw claim need not copy pixels, and a depth-keyed RT can carry unchanged
color-tile versions outside the depth draw's own claim. The 28 sampled
frames had maximum 2476 plans, zero drops, `timeline_valid=true` throughout,
and `plan_count + resolve_count == event_serial_max` throughout. Thus the
bounded read-only event recorder and its sequencing are validated in both
title and a complex relax scene. They are **not** proof of completed GPU
content versions or a rebuildable full frame. Two `XMPGetInfo?` kernel
messages and one early `ResolvePath(cache:\)` error were nonfatal in these
runs; no new D3D12 error or crash was observed. The underlying audio/cache
messages were not diagnosed by this render audit.

After testing, `out/game/rexgpu-xenos.dll` was restored to the prior
known-good SHA-256
`D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.
The tested diagnostic DLLs are preserved alongside it as
`rexgpu-xenos.audit-no-claims.dll` and
`rexgpu-xenos.audit-with-claims.dll`; the initial stable backup is
`rexgpu-xenos.pre-owner-lineage.dll`. The latest *compiled, not live-tested*
SDK diagnostic DLL (`8E6FE3644F4BC082DF0942689E4CCEA89CAA00419FAFBCB7CE3D5DF90E4EF346`)
only clarifies the event census by separating draw claims from transfers and
limiting the 32-line preview to transfers. All four root CTests pass.

The CPU lineage regression was tightened: tile 0–720 can transfer into a
depth-keyed RT and later return without depending on a disjoint depth draw.
The test's synthetic draw at tiles 720–900 illustrates version isolation;
the *actual* depth-draw footprint still requires validation from exact live
claims before that assumption can be applied to frame 360 or 5880.

Next: join exact draw claims, scheduled transfers, clear geometry and
resolve-owner rectangles into the CPU lineage at their shared serials.
Treat a claim as ownership intent; it may precede a planned GPU transfer in
the event sequence. Verify the final swap resolve's owner and every output
tile, and explicitly reject any missing draw input, source content,
unmeasured transfer conversion, clear footprint or intermediate texture
version. The 32-line transfer preview is not a complete event export; avoid
inferring a full graph from it. A private native-output reproduction remains
blocked until a complete, independently backed producer subgraph is found.

Subsequent CPU-only graph work: `ApplyOwnershipBatch` validates that every
scheduled transfer lies inside an exact same-destination claim and that
transfer spans do not overlap ambiguously. It processes transfer content
before finalizing the claim, matching the ownership-intent versus GPU-copy
ordering in the actual renderer. A transfer source observed for the first
time gets an explicit **unknown-content seed**, not a fabricated reusable
image. Focused regressions reject unclaimed/overlapping transfers and prove
that unknown bootstrap pixels prevent output acceptance. All four root
CTests pass. `ClaimRemainderUnknown` applies a batch's
transfers first, then marks only the untransferred portion of the exact draw
claim unknown. A regression proves a 0–720 color-tile version survives a
disjoint 720–900 depth draw while the latter's unproven input does not leak
into the final color trace. This is a normalization rule, not a finding that
all live depth draws are disjoint. The live log066 final resolve-owner records
show complete, single-owner rows 0–44 / width 16 at frame 360 event 4 and
frame 1920 event 21, both key `00008000`; the earlier saved frame 5880 route
remains the depth-keyed alternative. This confirms why final-output selection
must be frame-specific. The exact *intermediate* resolve-to-texture versions,
draw input binding lifetimes and transfer conversion outputs are still not
joined into the graph, so no unit qualifies for private image reproduction.

## 2026-09-28 checkpoint: CPU-only final-resolve ownership lineage

Implemented `OwnerLineage` in
`sdk/rexglue-sdk/include/rex/graphics/render_owner_lineage.h`. It uses the
existing `RegionVersions` primitive for 2048 EDRAM-tile ownership, each host
render-target key's content versions, intermediate guest-texture versions,
and output-pixel coverage. Ordered draw spans continue to come from
`BuildSequence`; the new `doax2_render_owner_lineage` test checks its saved
frame-360 terminal operation shape (34 draws, five resolves, final 28-draw
color span) before tracing the lineage. The graph has producer nodes for
seeds, claims, actual-source transfers, draws, clears, resolves and
GPU-produced intermediate-texture consumers. Each resolve records all terminal
owner keys, not just the last tile owner. No GPU resource, descriptor or
command stream is retained or submitted by this implementation.

**Observed routes from saved logs, not reproduction proofs:**

- `out/game/logs/doax2_064.log`, frame 360: before draw 5, tile 0–720
  transfers `00008000 -> 003082D0`; draw 5 uses the depth-keyed target;
  before draw 6, the same range transfers `003082D0 -> 00008000`; draws
  6–33 use the color target; resolve event 4 at before-draw 34 writes the
  swap guest base and its actual dumped owner is `00008000`.
- `out/game/logs/doax2_063.log`, frame 5880: before draw 1250, six source
  ranges claim `003082D0`, including `00008000` tile 0–720; draw 1250 uses
  the depth-keyed target; resolve event 23 at before-draw 1275 writes the
  swap guest base and its dumped owner is `003082D0`. Twenty-four null draws
  precede the last resolve. These two normal routes invalidate any fixed
  final-color target assumption.

The test checks both routes **twice**: an unknown/unowned starting content
version must be rejected; a separately *hypothesized* owned seed and proven
inputs may complete the small tile-0–720 structural path. The latter is a
unit-test contract, **not** evidence that the live DOAX2 frame has those owned
inputs. Further negative regressions cover missing transfer source, wrong
owner, partial output, unproven resolve layout, unproven color/depth
conversion, unproven draw input or clear, uninitialized no-transfer claim,
and partial GPU-produced intermediate texture. Focused Release build and
all three render CTests passed. Stable executable/DLL and opt-in launch flags
were not changed; no game launch or image comparison was performed for this
milestone.

**Acceptance boundary:** the graph data structure and both observed *terminal
routes* are regression-tested, but a complete live-frame graph is **not yet
validated**. The existing log's `version_proven=false` correctly remains a
rejection, including earlier swap-base resolves and their later texture
consumers. The actual per-draw pixel footprint, complete draw inputs,
clear/claim geometry, resolved-texture layout, transfer conversion content,
and independently owned backing/fence for every dependency remain unproven.
The smallest genuinely self-contained native-output unit therefore cannot
yet be selected; no private reproduction or interpolation is enabled.

Next evidence-based step: add a bounded, structured, audit-only live event
record at the existing ownership-transfer/resolve boundary with a shared
operation serial, including no-transfer claims and clear rectangles. Join it
to `BuildSequence` and the captured draw/resolve metadata, fail closed on
dropped or ambiguous events, and backtrace the actual final swap resolve.
Only once that graph identifies a complete producer subgraph should a private
fence-owned reproduction be attempted and compared pixel-for-pixel with its
native reference before any interpolation or presentation. Do not infer
full-frame coverage from the 28-draw title run or the 930-draw relax run.

The first part of that next step is now compiled but **not deployed or
hardware-tested**: `InterpolationFrame` stores up to 4096 sampled ownership
plans (source/destination key, tile range, before-draw ordinal, resolve-clear
flag and exact guest-pixel clear rectangle, and shared resolve/ownership
serial). The D3D12 RT cache records these
at `PerformTransfersAndResolveClears`; plans with no source retain an unknown
tile footprint. `AuditOwnedRenderCandidates` reports counts/drops and at most
32 terminal-tile records, explicitly tagged `planned_metadata_only`.
`rexgpu-xenos` Release compilation succeeded; all four root CTests passed.
New SDK DLL SHA-256:
`A370EFFC6C691FFBADD6F57AD5B9ADC787B5928E5C41E63103A15BF8165AAEC6`.
The deployed `out/game/rexgpu-xenos.dll` remains the previously tested
`D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.
No live result is claimed for the new hook. The missing clear rectangle and
per-draw footprint mean a full-frame lineage cannot yet be calculated from
these records alone. The next live test must verify zero drops and consistent
serial ordering before extending the event contract; clear geometry is
captured but not yet mapped to exact affected EDRAM content versions.
Failed/incomplete
records must not be used to select reconstruction work.

Build/test reproduction (PowerShell from project root):
`& 'E:\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build out/build/win-amd64-release --config Release --target doax2_render_owner_lineage_test doax2_render_operation_sequence_test doax2_render_region_versions_test --parallel 4`
then `& 'E:\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir out/build/win-amd64-release -C Release -R 'doax2_render_(owner_lineage|operation_sequence|region_versions)' --output-on-failure`.

The current project copy has no `.git` directory; preserve this checkpoint
and the new files when synchronizing it to another machine.

## Current checkpoint — 2026-09-28, after log061

### Log062 live resolve-aware audit (2026-09-28)

### Log063 targeted final-owner transfer trace (complete)

### Log064 two-way tile ownership transfer (complete)

Expanded the sampled trace to record a `00008000` claim only when its actual
source includes `003082D0`; the source search is gated off on the normal
non-audit hot path. Release compilation and all three focused CTests passed.
The deployed DLL SHA-256 is
`D5D5FD6EF6A07EB3EECC0979FBFEA7E32EBB354DD681C7A8B8D71FF9C881384D`.
The user was released from another manual relax test once title-frame
evidence arrived; the game closed normally with `Execution complete`.
The only `[error]` line is an early `ResolvePath(cache:\)` device-not-found
filesystem message, not a D3D12 failure; its impact was not investigated.

Frame 360 proves the exact *two-way* handoff. At before-draw 5, target
`003082D0` receives three real transfers, including `00008000 -> 003082D0`
for tiles 0–720. After one draw to `003082D0`, before-draw 6 transfers those
same tiles back `003082D0 -> 00008000` with no resolve clear. Twenty-eight
draws then target `00008000`, and final resolve event 4 reads owner
`00008000` and writes the guest swap base. This is not descriptor aliasing:
ReXGlue's actual `ChangeOwnership` transfer vector is executed by
`PerformTransfersAndResolveClears` between the two committed host RTs.
Frame 5880 in log063 demonstrates the alternate terminal route where the
final resolve still reads `003082D0`. The dependency graph must therefore
track *versions* of EDRAM ownership and target contents through both routes.
An attachment-key-only draw run, even if fully rebuildable, is not a complete
output dependency.

Next implementation boundary: build a conservative versioned ownership and
resolve lineage for sampled frames, anchored at the actual final resolve.
It must include each source and destination RT, transfer tile ranges, draws,
clear operations and guest-texture consumers, and reject paths with missing
or ambiguous inputs. Use the resulting graph to identify a genuinely
self-contained unit; do not enable second-frame publication based on the
nominal run matcher.

An alternative worth a bounded quality/performance experiment, not yet part
of this patch, is NVIDIA's hardware-assisted FRUC. Its official SDK says it
can interpolate [30 to 60 FPS](https://docs.nvidia.com/video-technologies/optical-flow-sdk/read-me/index.html)
and uses the optical-flow accelerator plus CUDA for the interpolation
([programming guide](https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvfruc-programming-guide/index.html)).
Our owned final-source pair gives it valid full-frame inputs, but its output
quality on moving DOAX2 characters, latency, resource interop, licensing,
fallback for non-NVIDIA GPUs, and live cost are all untested. The prior CPU
FFmpeg flow's edge artifacts do not prove this hardware implementation would
look the same; they do rule out shipping that CPU shortcut. An offline FRUC
quality test using the existing 2397/2398 BMP pair would be the least costly
way to judge whether it is a viable fallback before integration.

Added `GetCandidateAuditPosition` and a sampled read-only claim/transfer
record at `D3D12RenderTargetCache::PerformTransfersAndResolveClears`, limited
to key `003082D0`. Release compilation and all three focused CTests passed.
Deployed DLL SHA-256:
`7A34DC1039EBA11C17C17B687398B387ACFFF9EBE36A38E758D3022E7F6A4B06`.
The game ran via the opt-in candidate-audit script; the user entered a relax
scene and closed normally (`Execution complete`). Early title-frame sample
719 shows this final depth-keyed owner claiming three EDRAM ranges:
`000022D0 -> 003082D0` for tiles 720–900,
`00002384 -> 003082D0` for 900–948, and
`002082D0 -> 003082D0` for 948–1440. There is no resolve clear on this
claim. This proves a nontrivial real transfer into that owner in the title
sequence.

The relax sequence proves why the final-color producer cannot be fixed by
target identity alone. At frame 6000, immediately before draw 1188,
`003082D0` claims **six** source ranges with no resolve clear:
`000022D0` tiles 720–900, `00002384` 900–948, `003015AC` 948–1452,
`00800DAC` 1452–1964, `003015AC` 1964–2048, and `00008000` wrapping to
tiles 0–720. Draw 1188 uses `003082D0`; then 27 draws target `00008000`,
and final color resolve event 21 reads `00008000`. By contrast frame 5880
has the analogous depth-keyed claim but final resolve event 23 reads
`003082D0`. Both are normal frames in the same test. The ownership path
therefore branches with late color draws/overlays; selecting one fixed host RT
or one draw run cannot cover both. The source list is a sampled *actual
transfer vector*, not just a hypothetical tile-map lookup. We have not yet
proven all transfer shader outputs or private ownership for an extra frame.

Next: trace the later `00008000` reclaim after the depth-keyed claim, then
derive a frame-specific dependency DAG from final resolve backwards through
actual owner claims, transfers, draw spans and texture inputs. Reject any
candidate that lacks an exact route to the final resolved source. Do not
present or relax the bounds based only on nominally eligible runs.

Added a **read-only** largest-run diagnostic in
`sdk/rexglue-sdk/src/graphics/d3d12/command_processor.cpp` inside
`AuditOwnedRenderCandidates`. It logs resolve boundaries inside the run,
draw/byte totals per resulting span, unique guest address-range bytes and
index classes. Production selection, simulation, submission and presentation
are unchanged. The Release `rexgpu-xenos` build succeeded; all three focused
root CTests passed. Deployed diagnostic DLL SHA-256 is
`F8D5F0E351ED81E0D09B0F2CAECB5B9CAF436C1F7381A40FC2F3A800ADD2852D`.
Opt-in launch was `scripts/run-candidate-audit.ps1` with all reconstruction
and final-source-copy flags off. The user entered a relax scene and closed
the game. Log062 ends in `Execution complete`; no crash was reported. No
separate visual-quality report was supplied for this run.

In the heavier sequence, the largest attachment-identity run held 882–1284
draws and crossed one or three resolves. For example frame 9480 has 1260
draws, 2,024,237,120 bytes summed per draw, but only 46,399,802 distinct
guest address-range bytes, split by a resolve into 618- and 642-draw spans.
Frame 9960 has 932 draws, 1,558,997,476 summed bytes, 44,940,638 unique
guest-range bytes and a resolve between 454- and 478-draw spans. These
unique-byte counts are *address-footprint estimates*, not owned GPU backing,
cache hits, or actual upload cost. They demonstrate heavy repeated texture
references and that grouping solely by attachment identity is not a safe
whole-pass boundary. In frame 9960, 24 draws lack a pixel shader and 24 use
the non-DMA index class; relaxing only the 512-draw/256-MiB thresholds would
not make the run executable.

The final full-width resolve in both the moving minigame (frame 2400 event
21) and the relax sequence (frame 9960 event 23) writes the decoded swap
base `1F90F000`. In both cases the CPU render-target-cache dump associates
its source tiles with key `003082D0`. Per `RenderTargetKey` bit layout, that
key has the **depth** bit set (`0x00200000`) even though the resolve command
is a *color* copy. The lookup immediately follows a single depth-keyed draw.
This is an alias/ownership-transfer ambiguity, not evidence that the final
image can be reconstructed from that draw or retained host resource. The
existing private-source selection deliberately excludes depth owners, so it
cannot be assumed to route this final resolve. Need trace the actual host
EDRAM ownership transfer and backing contents through this point before
selecting a private final-color producer.

Source tracing narrowed this further: `D3D12RenderTargetCache::DumpRenderTargets`
obtains the exact `ResolveCopyDumpRectangle` owners from
`GetResolveCopyRectanglesToDump`, transitions each owning host RT to shader
resource, binds its SRV (including depth/stencil-specific views when needed),
and dumps its data into the EDRAM UAV before the guest resolve. Therefore a
depth-keyed owner in a color resolve is a *real source of raw EDRAM bits*,
not merely a formatting typo in the audit. The final-image dependency is
owner RT -> dump/conversion pipeline -> EDRAM buffer -> guest resolve ->
decoded swap texture. A fresh second frame cannot simply replay the final
resolve or clone its descriptor; it needs independently owned backing and
correct converted contents at every edge of this chain.
`RenderTargetCache::GetResolveCopyRectanglesToDump` reads the tile-range
`ownership_ranges_` map; `ChangeOwnership` can transfer existing tile contents
from the previous color owner into a later depth-keyed owner when that target
claims overlapping EDRAM. The final `003082D0` owner can therefore carry
color bits through a depth-typed host texture. This explains why choosing the
last ordinary color draw/run or filtering out depth-keyed owners loses the
actual final source. The exact per-tile transfer sequence and any destructive
clear during the claim still need a private replay-free reconstruction plan.

Next evidence-based action: inspect the render-target-cache ownership
transfer/resolve path for the `003082D0` alias and identify the exact backing
and operation sequence that produced the final color bits. Preserve the
post-resolve private source copy as a known-good endpoint reference. A
resolve-aware, resource-backed dependency DAG must precede any new private
midpoint submission. No 60 FPS patch is yet active.

The immediate-F10 capture was validated live in log061. F10 armed at swap
2397 during a beach minigame with visibly moving characters, and saved the
consecutive resolved endpoint images `owned-final-swap-frame-2397.bmp` and
`...-2398.bmp`. Their distinct hashes are `469C43BF72163E7D` and
`DBDE4E730A905253`. Both private copies were read only after the real
submission fence completed (4006 and 4009); the bank-0 witness remained
bit-identical after bank-1 was copied. Both proofs report zero failures.
The pair shows actual character movement, not a static result screen. It
still contains **no** reconstructed or published midpoint.

Offline quality controls are under
`out/game/captures/midpoint-analysis-2397/`. A 50/50 image blend visibly
ghosts moving limbs and hair. FFmpeg motion-compensated interpolation reduces
double imaging but distorts moving edges/hands; the three-output diagnostic
took about 446 ms wall time including process startup, decoding and encoding.
This is neither production-quality nor a real-time ReXGlue performance test.
Do not ship a plain blend or interpret an offline flow image as proof of a
working 60 FPS renderer.

The same log's read-only candidate audit shows why a nominally selectable
run is not a whole-scene frame. Near the capture, pair 2399/2400 has 929
draws per frame and 22 target runs; zero production-eligible runs. Its
dominant run 10 has 658 draws targeting `002842E0/00084000`, about 1.996 GB
logical dependency bytes and 24/28 unsupported entries in two categories;
it exceeds the 512-draw/256-MiB production limits and is not the hard-coded
`002082D0/00008000` candidate target. The 118-draw run 4 is otherwise
compatible (65 ordered shared unique draws), but targets
`00206B40/00006800`, so selecting it would produce an intermediate target,
not the final image. At pair 2639/2640, the one eligible run is only 70 of
958 scene draws, though 69 have ordered unique matches. Thus `eligible=1`
cannot be equated with complete scene coverage. Pair 2399/2400's final
frontbuffer comes after 22 resolves to guest base `1F90F000`; draw-run keys
alone do not identify all post-resolve dependencies. Keep the matching safety
checks unchanged pending a full render-dependency plan.

Next: trace the specific 658-draw run's unsupported flags and dependency
inflation back to the actual draw inputs and resolve chain, and classify
which pieces can be snapshotted/rebuilt with owned resources. The final-swap
copy proves endpoint ownership, not midpoint rendering. Stable 30 Hz
simulation and the NVIDIA presentation fix remain unchanged.

## Prior pause — 2026-09-28, after log060

The current deployed `out/game/rexgpu-xenos.dll` is the compiled, opt-in
**immediate F10 adjacent-swap capture** build; SHA-256
`633BF04CB1AD38DD073568C7204DF94780D9BD006EEC8A79D02E179BB39B1089`.
`scripts/run-candidate-audit.ps1 -FinalSwapCopy -CaptureOnF10` launches it
without changing stable launcher settings. In this mode no private copy is
recorded before F10. Pressing F10 during moving gameplay arms a copy of the
current and next resolved guest-swap images, using two separate committed
textures and the existing real-queue fence/witness path. It does **not**
submit or present a midpoint or change simulation timing.

Log059's first F10 implementation waited for a four-second sparse sampling
boundary, so the saved 2879/2880 pair was the minigame's static "Too simple"
result scene rather than the moving action. The user confirmed the minigame
played. Log059 had 56 copies/readback proofs, 28 preservation witnesses and
zero probe failures/rejections. Offline-only motion compensation and blend
controls were generated for that pair; neither is a live game frame or proof
of real-time quality. The trigger was then corrected to copy the two swaps
**immediately** after F10; SDK compilation, PowerShell syntax check and all
three focused root CTests passed. Log060 launched this corrected build but the
user requested a pause before pressing F10. We closed the window normally;
log060 reports `Execution complete` and has no `capture armed` or BMP capture.
The corrected immediate-trigger behavior was subsequently validated live in
log061 as recorded above.

On resumption, first check no DOAX2 process is running, then launch exactly:
`pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/run-candidate-audit.ps1 -FinalSwapCopy -CaptureOnF10`.
Ask the user to enter a minigame, tap F10 **while action is visibly moving**,
wait about five seconds and close. Monitor the newest log for `capture armed
by F10`, two `owned final swap recorded`, two fenced `owned source proof`, one
`owned source witness preserved=true`, and two `owned final swap visual
capture` paths. Inspect the two BMPs before deciding whether a GPU-side
image-space midpoint could meet quality; do not mistake the static-result
log059 images for that test. A shipping 60 FPS patch still does not exist.

## 2026-09-28 final resolved swap-source boundary (logs056–057)

The read-only log056 census ran through a relax scene and closed normally.
At `IssueSwap`, after `RequestSwapTexture`, packet frontbuffer and texture
fetch0 agree and alternate between guest bases `1F577000` and `1F90F000`.
The exact returned D3D12 resources are 1280x720, single-mip/sample,
`R8G8B8A8_TYPELESS` textures with an `R8G8B8A8_UNORM` 2D view. The texture
cache sets each selected resource to `NON_PIXEL_SHADER_RESOURCE` before the
normal gamma/FXAA consumer. The separate `DescribeActiveTextureResource(0)`
identity must not be mistaken for the returned swap-resource pointer; its
availability can briefly be false even while `RequestSwapTexture` succeeds.
The two guest addresses and returned resource pointers recur, so retaining
either native resource across frames would not own a stable N/N+1 image.

Mode `scripts/run-candidate-audit.ps1 -FinalSwapCopy` now sparsely copies the
**exact** decoded, post-resolve swap source into two independent committed
textures, restores its shader-readable state before ordinary presentation,
and reads private copies only after the existing direct-queue submission
fence. It reuses the established `FrameBanks` lease/witness mechanism; no
new allocator, second draw, simulation tick, or midpoint publication is used.
Log057 completed a title/relax run and shut down normally: 112 records,
112 real-fence submissions, 112 fenced readback proofs, 56 bank-preservation
witnesses, and zero allocation/busy/layout rejections. The bank resource
identities differ. All 56 witnesses matched the original bank hash after
copying the other bank. Two early opaque-black intro frames had zero RGB and
were mistakenly counted as probe failures; their readback and preservation
checks succeeded. The diagnostic now treats black RGB as valid image content.
No device loss or command-list failure appeared in log057. The user's visual
report for this opt-in copy run should be recorded separately; a normal
shutdown is not itself a visual-quality report.

Fenced BMPs `owned-final-swap-frame-1800.bmp` and `...-1919.bmp` were
inspected and contain recognizable complete landscape/camera images, unlike
the black/partial images of the early composite-draw boundary. These are
native endpoint images, not interpolated midpoints. These first two captures
were 119 swaps apart, so they cannot evaluate midpoint quality. The diagnostic
now captures an actual adjacent sampled pair starting at a configurable swap
serial (`-FinalSwapCaptureAfter`). This capture rule is compiled but not yet
validated live. The previous captures do not yet prove
character/UI/effect coverage in a representative gameplay frame, nor any
60 FPS output. Some adjacent swap hashes are equal (legitimate static content
is possible); swap serial is not by itself proof of a distinct simulation tick.

Next: capture/compare representative character/UI final-source images and
determine whether a safe sparse intermediate can be constructed from owned
complete endpoints with acceptable motion quality. A 50/50 blend alone must
not be equated with correct motion reconstruction. Keep the old intermediate
shader draw unpresented and the 30 Hz/NVIDIA pacing path unchanged. Measure
simulation ticks, swap endpoints, midpoint publication, and actually distinct
displayed images separately before a 60 FPS claim.

### Log058 adjacent relax images and offline quality control

The user reported no visual abnormalities while the relax scene ran, then
closed normally. Log058 has 144 distinct private-copy records/submissions,
144 readback proofs, 72 preserved-bank witnesses, zero rejections and zero
probe failures. The corrected black-intro handling did not invent a failure.
`-FinalSwapCaptureAfter 4800` saved **adjacent** frames 4919/4920. Both images
visibly contain the character on her chair, water, background geometry and
effects; their hashes differ (`5F3A455FC3319AFC` and `5F1F35209C341B5E`).
This establishes complete resolved endpoint ownership in a character scene,
but there is still no midpoint GPU submission or publication.

An *offline-only* FFmpeg motion-compensated image was generated from the two
BMPs as a quality control, not integrated into ReXGlue. Its output differs
from each endpoint (PSNR about 32.54 dB to either); the endpoints differ by
about 26.75 dB. A plain-blend control was also saved. This near-static relax
pair looks plausible, but cannot establish quality during camera motion,
occlusions, minigames or UI. The CPU FFmpeg run took roughly 262 ms wall time
including decoding/encoding/process overhead, so it is not evidence of a
real-time implementation. [AMD's FSR frame-generation integration](https://gpuopen.com/fidelityfx-super-resolution-3/)
requires depth/motion-vector/UI inputs the game path has not supplied;
[NVIDIA Optical Flow](https://developer.nvidia.com/opticalflow/download)
offers a GPU-side frame-conversion path on this RTX system, but no SDK
integration or live cost/quality proof exists. Neither is selected as the
shipping architecture yet.

Next evidence: capture a moving-camera/character pair and measure a viable
GPU interpolation/reconstruction path before mailbox publication. Preserve
the 30 Hz simulation, normal 30 FPS renderer and NVIDIA pacing fix. Do not
count offline FFmpeg images as game frames.

## Completed v1.3.115 live source-copy test — 2026-09-27

The user closed the mode-72 game normally and observed no visual abnormalities.
Log040 spans five rotated files. The verifier now accepts an ordered file list,
because one recording/submission pair straddled a rollover. Across the full
sequence it verifies 966 private copies, 966 real-queue fenced nonzero readbacks,
965 opposite-bank preservation witnesses, distinct backing/SRV identities and
zero probe failures. There were 934 distinct source-content hashes. The process
logged normal execution completion. Cache-path and periodic XMPGetInfo errors
remain present; no D3D12 device error or removal was found in this log.

This proves the sampled GPU-produced composite source can be copied into two
independently backed, fence-gated textures on the live ReXGlue path without the
reported visual regression. It does NOT prove fresh descriptor binding to a
reconstructed draw, completeness of a midpoint image, or 60 FPS output.

Next: adapt the smallest isolated owned-render-unit path for a color-only
composite draw, and route one bank's fresh SRV explicitly (not its recycled
native SRV or guest-memory snapshot). Guard the bank through the additional
submission's real fence. Start with an unpresented output/readback; publish
only after its content and resource lifetime are proven. Keep 30 Hz native
simulation and stable pacing path unchanged.

## v1.3.116 adjacent-frame ownership probe — awaiting live result

The live v1.3.115 test sampled one source every 120 native frames, so it did
not establish that two adjacent simulation frames' GPU-produced inputs can
coexist for N-to-N+1 reconstruction. Mode72 now samples the same shader/input
at frames 119 and 120 of each 120-frame cycle. The existing two-bank lease,
real queue fence, readback, and witness logic is unchanged. The verifier accepts
rotated log files in chronological order and has an optional
`-RequireAdjacentPairs` check for distinct, fenced N/N+1 backing. The stable
30 FPS mode and its pacing/music behavior are unchanged; mode72 still never
submits a second draw or presents a midpoint image.

SDK Release build/install, launcher build and three root CTests (ordered
operations, region versions, title playlist) passed. The v1.3.115 runtime
DLLs were saved under `archive/60fps-resume-20260927/v1.3.115-runtime-before-adjacent-source`;
deployed runtime/GPU DLL hashes match the SDK install. Hardware behavior and
adjacent-frame proof are untested until the next real mode72 run.

### Completed log042 and v1.3.117 follow-up

The user ran relax/gameplay, observed no abnormality, and closed normally.
Log042 validates 36 recorded private copies, 36 fenced nonzero readbacks,
35 preserved-bank witnesses and no probe failures, but **zero adjacent pairs**.
All sampled copies came from frame numbers divisible by 120, not the preceding
119 frames. The selection code was enabled for both phases, so this is not a
bank-acquisition or fence failure; the exact shader/guest-base predicate did
not match any preceding sampled frame. Why is not established by log042.
Do not call the adjacent-frame boundary proven or expand resource ownership
on that premise.

v1.3.117 adds a bounded read-only paired-frame summary to the existing audit:
for each sampled N and N+1 frame it counts the relevant vertex quads, exact
composite shader, and target guest-base keys, with at most four draw details
per side. This will distinguish an absent draw from a changed shader, texture
base, or render-target routing. It submits no new work. SDK/launcher builds
and render-operation, region-version, and music tests passed. v1.3.116 DLLs
are backed up under `archive/60fps-resume-20260927/v1.3.116-runtime-before-pair-audit`.

### v1.3.118 log044 and v1.3.119 witness scheduling

Log043 established the root cause: the exact four-vertex composite shader
binds guest base `1F577000` on N and `1F90F000` on N+1. Their difference is
`0x398000`, one 1280x736x4 pitched image, consistent with adjacent buffers.
The shader and render-target route were otherwise the same in sampled pairs.

v1.3.118 accepted both observed bases at pixel texture binding zero. Log044
proved the first pair could be copied into distinct private backings, with
two real-queue fence signals and two nonzero post-fence readbacks. It then
rejected later N+1 copies at `stage=readback_pending`, without corruption or
device removal: the N copy had also scheduled a witness of the prior pair's
N+1 bank, keeping that bank occupied at the next draw. This was a probe
scheduling defect, not an allocation or GPU hazard.

v1.3.119 schedules the witness only when copying N+1, targeting the just
submitted N bank. D3D12 direct-queue order guarantees that witness copy
follows N's write; `ExtendSubmitted` carries N's bank lease through the
N+1 submission fence. The original and witness readbacks are compared only
after observed completion. This change is opt-in mode72 only and needs a
live hardware run before adjacent ownership can be called proven.

### v1.3.119 live log045 (completed)

Title and changing-scene telemetry verify 61 adjacent N/N+1 pairs, 122 fenced
nonzero readbacks, 61 N-bank preservation witnesses, distinct bank backing/SRV
identities and zero probe failures. The first N/N+1 pair submitted on
consecutive real ReXGlue queue fences. The copied content changed in gameplay.
The process closed normally and the user reported no visual abnormalities.
This proves the sampled alternating composite inputs can coexist in
independently backed GPU textures and that N remains unchanged when N+1 is
copied. It does **not** prove those private SRVs can be bound to a fresh
reconstructed draw, nor a complete or published midpoint.

Next smallest technical boundary: route the N+1 bank's immutable fresh SRV
through a reconstructed composite draw's descriptor-index CBV, and add a
private color-only output (the observed composite can have `rt_bits=0x2` and
must not bind an inherited DSV). Keep the draw isolated and unpresented,
read back its output after its own queue fence, and extend the source-bank
lease through that submission. Do not reuse a native descriptor or replace
the GPU-produced image with a guest-memory texture snapshot.

### v1.3.120 isolated composite draw — live capture-gate finding

An opt-in mode73 now builds a fresh composite root packet from the adjacent
render states, replacing only the GPU-produced pixel input with the copied
N+1 bank's private SRV. The output is a private color target/readback; it is
never presented. The SDK and launcher compiled and the three focused CTests
passed. The v1.3.119 runtime was backed up before deploying this experiment.

Log046 and diagnostic log047 did **not** submit the draw. They recorded the
correct N and N+1 source banks, but the N+1 capture gate saw `states=0`. The
cause is now established: the stale-pair check cleared state N on the next
draw **within the same N frame**, because it compared `N+1 != N`. It now
clears only when a strictly later, nonadjacent sample arrives. The corrected
GPU module compiles; its live result is pending deployment after log047's
process closes. Do not claim descriptor binding, isolated draw, or 60 FPS
output proven from these logs. Keep default 30 FPS mode unchanged.

Log048 proved the corrected N/N+1 capture (frame119/120), but the generic
upload packet rejected zero-length float-constant maps. Supplying private
zero-filled CBVs advanced log049 to the next gate. That log exposed a second
alias of the same 3.7 MiB GPU-produced composite input; treating only pixel
slot zero as owned left slot one incorrectly queued for guest-memory upload.
Log050 proved the complete CPU upload after excluding both source aliases,
then rejected slot one's signed-view requirement without drawing.

Log051 bound fresh unsigned and signed SRVs over the N+1 private typeless
backing (absolute bindless indices 105/106), with both pixel slots routed to
the owned source. **The process then crashed before a geometry/root/output
completion log or isolated-draw submission.** Windows Error Reporting records
an access violation in `nvwgf2umx.dll`; this does not establish which D3D12
call or GPU command was invalid. The v1.3.119 `rexgpu-xenos.dll` was restored
to `out/game` and hash-verified (`78649BC73B598BFE754889933959728C9CBD89E7FEA04E3C0A909DCFEE234363`).
The experimental source remains in the SDK only. Next: isolate the failure
stage with bounded markers and D3D12 debug-layer messages before trying any
further private draw. Do not describe log051 as a successful draw.

Logs052/053 isolated the failure: geometry reconstruction returned after a
vertex-fetch mismatch, but the old ordering had already recorded a copy into
a local geometry buffer. The two crashes had the same NVIDIA user-mode driver
fault offset. After moving fetch validation ahead of GPU recording, log053
stayed alive and reported N+1 fetch95 at `1EF436C0` versus the N snapshot's
different guest range. This strongly supports a dangling recorded-resource
lifetime as the crash mechanism, though no driver stack is available. The
debug-layer runtime is not installed on this machine (`Failed to enable the
Direct3D 12 debug layer`); DRED settings were accepted. Geometry commands are
now recorded only after fetch, upload, descriptor, and index checks pass.
The composite's CPU-side vertex range is resnapshotted from N+1.

**Log054 completed live proof:** the
exact composite consumed both fresh private unsigned/signed SRVs, rebuilt
an 80-byte N+1 vertex buffer and fresh root packet, rendered to a private
1280x720 color target, and submitted one extra four-vertex draw. The pipeline
query reported 921,600 pixel shader invocations and the post-fence readback
had 921,600 nonzero pixels on each of 26 sparse cycles. There were 26 fenced
color proofs, 52 fenced source readbacks, 26 preserved-bank witnesses, and
20 distinct output hashes after the user entered a moving relax scene. All
26 raster proofs shaded the full 1280x720 target; no probe failures or device
removal were logged. The user reported no flicker or visual abnormalities and
the process shut down normally. The three focused CTests passed again.

This proves one GPU-produced final-composite source can be copied into owned
N/N+1 banks and consumed through fresh signed/unsigned descriptors, a fresh
N+1 geometry snapshot and root packet, and one real extra ReXGlue submission
to a private fenced output. It does **not** prove that the private image is
visually correct by inspection (it was unpresented), or that a midpoint exists.
The composite currently consumes exact N+1 source/geometry and has zero
interpolated transform words. Native gameplay remains 30 Hz; host presentation
is unchanged. Next: capture/inspect private output versus the corresponding
native image, then provide genuinely intermediate full-scene input to this
boundary without replaying native commands. Only after sparse midpoint
correctness should the mailbox present it or continuous performance be tuned.
The experimental DLL remains deployed, but mode73 is opt-in and default 30 FPS
behavior is unchanged. The v1.3.119 backup remains recoverable.

### v1.3.120 private-pixel inspection — log055

The opt-in `-DumpComposite` run saved two post-fence BMPs from the private
output. `owned-composite-frame-120.bmp` is completely black in RGB with opaque
alpha; its 921,600 "nonzero pixels" came only from alpha. This corrects the
earlier interpretation of the count: it proved readback/raster coverage, not
useful final color. `owned-composite-frame-1200.bmp` contains visible textured
terrain/water-like imagery, but is not a complete game frame with UI and
characters. The sampled shader/input is an **intermediate pass**, not the
authoritative resolved frontbuffer. It must never be routed to presentation
as a complete midpoint. The user reported no native-frame abnormalities;
the private images were never displayed. Mode73 now captures at most two BMPs
only when explicitly requested. No continuous or published midpoint exists.

Next evidence-based boundary: trace the actual `IssueSwap ->
RequestSwapTexture` resource after the last resolve and own complete N/N+1
images with the proven fence-bank mechanism. Record its description, state,
format, dimensions and lifetime before copying. Then inspect exact native
images and investigate a genuine intermediate image (possibly image-space
interpolation or owned scene reconstruction) without replaying native GPU
commands. Do not assume the early composite is the final color target.

## Current checkpoint — v1.3.115 owned composite-source copy

Completed v1.3.114 log039: user reports no visual abnormality, normal closure.
The ordered audit validates76 sampled frames,17,420 captured draws,687 resolves,
no incomplete sample. All328 composite-source observations across76 frames
showed a bound1280x720 single-mip/sample 2D typeless resource in shader-readable
state; the SAME native resource identity was recycled throughout. This proves
that retaining its native pointer or descriptor across frames would not own a
stable image. The log has cache-path and XMPGetInfo errors, but no observed
D3D12 device removal; do not characterize it as error-free.

v1.3.115 adds an opt-in mode72/source-copy probe; mode71 stays read-only and
stable30 stays default. It targets the observed four-vertex non-indexed
composite shader pair and sampled texture fetch0 only, every120 native frames.
`D3D12TextureCache::RecordOwnedActiveTextureCopy` validates the exact live
texture description/state, transitions the native source to COPY_SOURCE,
records CopyResource into an independent committed allocation, restores the
native shader-readable state, and never retains its resource pointer or view.
Two private banks each have a fresh immutable SRV, readback and witness buffer.
FrameBanks leases become Submitted only after the real ReXGlue direct-queue
Signal; reuse and witness extension require observed completion. Device-loss
and signal-failure paths quarantine the banks. Nothing is bound to a second
draw or published. The diagnostic is deliberately sparse and may cost a small
amount of time every120 frames; it does not change simulation timing.

SDK Release install and launcher build passed. Render-region, operation-order,
title-playlist, and render-history/bank tests passed. The prior runtime was
backed up under `archive/60fps-resume-20260927/v1.3.114-runtime-before-owned-source-copy`;
deployed DLL hashes match the SDK install. Live log040 mode72 is running.
At the title screen, two banks have distinct backing and SRVs. The first24
copies had nonzero fenced readbacks;23 other-bank witnesses preserved their
original hashes. As the live run continued, the first71 copies and70 witnesses
all passed the automated `test-owned-composite-source-copy.ps1` with zero
failures; the source hashes began changing (29 distinct among the first55),
proving the copy is not merely reading one fixed menu image. This is still an
in-progress run: await normal exit and user visual report before declaring
the live boundary proven for gameplay. No second frame or60FPS claim follows.

Quick transition-stutter audit: the mode71/72 logs do not time transitions.
Existing mode70 already measures synchronous file reads, shader translation,
pipeline creation, GPU fence waits and presentation timing; no corresponding
mode70 trace is available for the reported pause. Cause remains unproven.
Per user instruction, sideline this as a separate later profiling run unless
a directly relevant stall emerges during60FPS work; do not change game pacing.

Next after dynamic-scene proof: route one independently owned GPU-source copy
through its fresh descriptor in an isolated reconstructed composite draw. The
current root-packet builder expects texture indices from guest-memory snapshots,
so this GPU-produced input must override that source explicitly, with matching
frame/version and fence protection. Also the observed composite is a color-only
`rt_bits=0x2` draw, while `TryCreateOwnedRenderUnitOutput` and group submission
currently require depth+color `0x3` and an owned DSV. A minimal fresh composite
must support a no-depth color target without accidentally binding an old DSV.
Keep the native draw and30Hz simulation unchanged; do not reuse native views.

## Current checkpoint — v1.3.114 source census, awaiting live run

The next diagnostic build adds a read-only `DescribeActiveTextureResource`
query to the D3D12 texture cache. For sampled mode-71, four-vertex `kNone`
draws whose pixel texture binds guest base `1F90F000`, it logs the actual
decoded host texture's resource identity, dimension, format, extent, mip count,
sample count and tracked state at the native draw boundary. The query does not
retain the pointer, create a descriptor, copy a resource or change state. It
will establish whether the final composite's input can be captured as one
already-materialized host texture before attempting a private GPU copy. SDK
Release build passed. A real log is still required; available/suitable source
content and safe ownership are NOT yet proven. The deployed DLLs were backed
up from v1.3.113 and hash-matched to the SDK Release install. Render-region,
ordered-operation and title-playlist CTests pass. Portable package:
`release/DOAX2-Recomp-v1.3.114-composite-source-census.zip`.

Early live log039 (PID7312, mode71, still running when recorded) confirms the
menu's composite source is available: dimension3 (2D), format27,1280x720,
single layer/mip/sample, tracked resource state192 (VS/PS shader-readable).
The same native resource identity appeared in every first-27 sampled frames,
so a retained native pointer/descriptor would refer to changing content across
frames. This supports a private per-version GPU copy at the original texture
binding boundary; it does NOT prove the copy, its contents, or its fence. The
menu path has34 draws, not the complex relax/minigame path; await user gameplay
and visual report before interpreting those scenes. One cache-path lookup error
is logged at startup; do not report the log as wholly error-free.

## Previous checkpoint — 2026-09-27, after completed log038

The user reports the v1.3.113 mode-71 run finished and closed normally with no
visual abnormalities. The completed log has 35 sampled frames, 13,461 captured
draws, 395 resolves, and no incomplete sample. The ordered-operations validator
and render-history test pass. This was an audit run: no extra visual frame was
submitted, and it does not validate 60 FPS.

The live region-version read at frame4200/op43 requires versions16 and18; at
frame3960/op21 it requires versions7 and9. Both are complete whole-base-level
provenance reads in the supported tiled RGBA8 layout, not measured shader sample
footprints. A new pure-CPU `render_composition_plan.h` maps each required version
to a declared producer image with clipped source and destination rectangles. It
fails closed for missing/ambiguous producers, incomplete coverage, invalid
regions, or excessive copy count. The region-version CTest covers split halves,
crop, later full overwrite, missing/ambiguous producers and limits. The saved-log
driver now emits those plans: frame4200/op43 is two 640x720 copies into left and
right halves of 1280x720; frame3960/op21 is the same shape with versions7/9.
Frame4200/op61 instead consumes one newer full-frame version26, and the final
surface is newer version30. These plans are metadata ONLY: producer textures,
formats, descriptors, resource states, and GPU fences are not yet connected.

The fresh owned-render geometry path previously required exactly one guest-DMA
index range. The observed final-composite class also includes `kNone` native
non-indexed draws. The dormant owned path now accepts exactly kGuestDMA or kNone,
allocates its private raw-SRV buffer even for fetch-free kNone shaders, omits the
index range/view/state for kNone, and uses DrawInstanced as the normal renderer
does. Group and isolated draw paths both branch on the exact class. Other host
index classes remain rejected. The SDK Release install and launcher build pass;
the region-version, ordered-operation, and music CTests pass. This branch was
NOT exercised in a live private draw, and the runtime was not redeployed for a
new gameplay test. Stable 30 Hz simulation/NVIDIA pacing remain unchanged.

The next hard blocker is GPU-produced texture input ownership. The existing
`CreateOwnedSnapshotTextures` reconstructs from CPU guest byte snapshots; those
bytes are not a valid source of truth for the resolved intermediate consumed by
the final composite. At the native draw boundary, `RequestTextures` materializes
a decoded `D3D12Texture` from the resolve, and active `TextureBinding` references
that resource. Investigate a fence-protected private GPU copy of that decoded
texture (or an equivalent independently owned producer image) at this exact
boundary, with fresh descriptors and no retained native view. Prove it for one
producer/version before connecting the two-piece composition plan to the fresh
draw. Then verify sparse complete midpoint images before continuous output.
Do not substitute stale guest memory or the native descriptor, and do not enable
the dormant draw path for whole-scene publication merely because it now accepts
non-indexed geometry.

Date: 2026-09-27. Launcher: v1.3.113. Phase: logical region versions, no extra frames.

## Current checkpoint — v1.3.113

Completed log037: normal shutdown,25 samples,12,230 captured draws,366 resolves;
chronology validator passed. User says done/closed; no separate visual report
for this run. Frame2880 events8/10 both survive to consumer ops24/42 in the
corrected conservative graph. Frame3000 likewise retains both half producers.

New reusable CPU model: `include/rex/graphics/render_region_versions.h`.
It tracks disjoint logical rectangles by producer version, splits older regions
on partial writes, replaces them on full writes, reports uncovered pixels and
version sets on reads. Copies preserve prior state. Invalid input or fragment
budget exhaustion invalidates provenance rather than retaining stale answers.
This is NOT a GPU allocator, resource snapshot, descriptor or fence mechanism.

New CTest `doax2_render_region_versions`: half-frame retention, later full-frame
replacement, partial overwrite, immutable copied state, invalidation, fragment
cap, and300 deterministic random writes checked against a16x16 per-pixel oracle.
Passed along with ordered-operation and music CTests, and render-history test.

`scripts/test-saved-region-versions.ps1` drives the same C++ model using saved
log events. It only normalizes the verified1280x720 tiled RGBA8 configuration;
unsupported overlapping writes invalidate the model. Replay of log037 frame2880:

- logical read at op24 requires versions9,11 (events8,10), no missing pixels;
- logical read at op42 requires version19 (event18), NOT the older halves;
- final surface requires version22 (event21).

These offline reads assume the whole surface; they do not prove actual fetch
layout, shader sample footprint, or pixel correctness.

The model is now connected to the existing LIVE AUDIT only. IssueSwap captures
the actual fetch descriptor. Supported scope is tiled1280x720 RGBA8,pitch1280,
single2D layer/base mip. The audit validates actual captured SRV keys before
querying the whole base-level footprint, normalizes half/full resolves using
the SDK GetTiledOffset2D, and invalidates on unsupported overlapping writes.
Version IDs are resolve event index+1, not submission fences. Logs explicitly
mark `publishable=false`, actual sample footprint unmeasured, and unobserved
writes untracked. No production selector, recipe, budget, rendering, resource
allocation or publication behavior changed.

SDK Release install and launcher builds passed. DLLs deployed/hash-verified;
previous runtime backed up under archive/60fps-resume-20260927/v1.3.112-runtime.
Live PID16772,log038. User asked for same relax scene20-30sec then close.
Early frames120/240 show model activation with3 writes,0 invalidations,2 actual
compatible read passes,0 unsupported reads; frame240 reads versions1 then2,
final version5. This is early telemetry, not a completed visual test.

Next: inspect completed log038 for a live read depending on both half versions
and their replacement by later writes. Then use these typed version edges when
planning owned intermediate textures; do NOT copy stale CPU guest bytes for
GPU-resolved inputs. Expand the minimum fresh non-indexed/color-only composite
path only with an explicit resource/version contract. Remaining blockers include
CPU-write/query/predication coverage, other layouts/aliases, full-scene inputs,
and actual sparse second-render correctness/performance. No60FPS claim.

## Previous checkpoint — v1.3.112

Completed log036: user reports no abnormalities; normal shutdown12:15:32.
The chronology validator passed all40 samples,26,493 captured draws,660 resolves.
No new complete-frame or midpoint correctness claim follows from that result.

Implemented `scripts/get-render-dependency-graph.ps1`: parses saved sequence,
resolve/layout and EDRAM-owner metadata; emits chronological candidate edges,
last swap-overlap root, conservative attachment history and explicit unresolved
conditions. Frame4320 has44 operations,55 candidate edges,root op43/event17;
its21-node candidate closure is NOT an executable plan. It is always marked
BLOCKED_METADATA_ONLY, because CPU writes/queries/predication, alias versions,
initial attachment contents and precise texture versions remain unresolved.
Generated example: `logs/render-dependency-frame4320.json` (legacy log036).
Do not use closure size as proof that omitted operations are unnecessary.

### Proven diagnostic defect and correction

Log036 frame4320 color events11/13 each resolve640x720 with pitch1280 to
1F90F000 and1F923000. The prior audit stopped consumer tracking at ANY later
overlapping byte extent. That incorrectly discarded earlier tiled-region producers.

`scripts/test-resolve-region-coverage.ps1` implements an offline RGBA8 reference
of the SDK GetTiledOffset2D formula. Exhaustive destination-address checks prove
460,800 pixels per half,zero intersection,921,600-pixel union equal to1280x720,
despite bounding-byte overlap3,604,480. The right-base delta0x14000 is the tiled
offset for x640,y0. This proves address coverage for the logged configuration,
NOT actual pixel contents, interpolation, or GPU execution correctness.

AuditOwnedRenderCandidates now retains potential resolve consumers through the
sampled frame instead of pruning on bounding-byte overlap. Logs first later
overlap separately, and reports actual consuming operation indices based on
captured texture-range overlaps (`consumer_ops`, `version_proven=false`).
These are still ambiguous byte-range candidates; false positives are expected.
Graph analyzer consumes these indices rather than filling first..last intervals.
Production selection/thresholds/recipe keys/reconstruction and publication are
UNCHANGED. This is a correction to diagnostic provenance, not a render fix.

### Verification / active test

SDK Release install + launcher build passed; ordered-operation/music CTests and
render-history executable passed. Offline split-region checks passed. Graph
analyzer exercised against both legacy log036 and new live log037.
DLLs explicitly deployed/hash-verified. Previous DLLs backed up under
`archive/60fps-resume-20260927/v1.3.111-runtime`.
Live RTX3090 PID11736, `out/game/logs/doax2_037.log`. User asked only for a
20-30 second relax scene and exit, not another full test cycle. Await completion
and visual report; check process before restarting. Early nine samples validated
278 draws/45 resolves, and frame360's11 operations parsed with four new
texture-byte-overlap candidate edges. No game-wide/60FPS validation performed.

Next: inspect completed log037 and confirm the two split resolve producers both
survive to the consuming pass. Build explicit per-texture region/version ownership
using existing native tiled addressing/resolve layout; resolve ambiguous edges
before emitting any reconstruction plan. Do not simply use the newest overlapping
resolve, drop old versions, remove matching fields or inflate budgets. The fresh
group path still assumes indexed draws + one RTV/DSV and lacks the required ordered
MSAA-resolve/composite path. CPU guest texture memcpy alone cannot establish the
contents of GPU-produced intermediate textures (resolve readback defaults false).
Continue toward owned intermediates and fresh non-indexed/color-only operations
only after that dependency contract is explicit. All closed paths stay closed.

## Previous checkpoint — v1.3.111

Implemented CPU-only ordered draw/resolve representation in SDK
`include/rex/graphics/render_operation_sequence.h`. Every recorded resolve
splits a draw span even when attachment identity is unchanged. Equal-ordinal
events retain order; invalid/out-of-order boundaries reject the sequence.
Attachment comparisons include exact mask, five keys, and five formats.
This diagnostic representation is NOT a GPU command stream and is not submitted.
Production candidate selection, budgets, recipe keys and mailbox gates are unchanged.

The sampled audit now reports the ordered spans/events, resolve source/destination
offsets, pitch/height, sample selection, command/format and clear values. The native
render-target cache's existing dump-rectangle ownership lookup supplies scalar
owner keys and tile-row bounds; no resource pointers or descriptors are retained.
Limits: 192 logged operations, 256 resolves, 64 owner rectangles per event;
truncation is explicit. Empty owner records do not prove absence of dependencies
(e.g. clear-only/unsupported/no-copy paths). CPU-cache ownership is not a GPU
completion proof. Query/predication/CPU-write coverage remains unresolved.

Validation executed:

- New CTest `doax2_render_operation_sequence` passed: empty frames/events,
  repeated boundaries, attachment/format changes, invalid ordering/ranges,
  and log035 frame5880's 366-draw regression split into 163/8/187/8.
- Music CTest and render-history executable passed.
- SDK Release install and launcher build passed. An initial include-placement
  error was corrected before successful build/deployment; no failed build shipped.
- New DLLs hash-verified against SDK install before launch. Previous DLLs kept
  in `archive/60fps-resume-20260927/v1.3.110-runtime`.
- Live RTX3090 process PID10324, log `out/game/logs/doax2_036.log`.
  User requested to test relax/minigame/hotel then close; completion/visual
  observations still pending at this checkpoint. Check process before relaunch.
- `scripts/test-ordered-render-audit.ps1 -LogPath out/game/logs/doax2_036.log`
  validated 22 completed samples, 8,618 captured draws and 270 resolves exactly
  once in order, with no record gaps at that snapshot. It can be rerun on the
  completed log and skips incomplete trailing live samples. This verifies
  recorded order/coverage only, not visual completeness or hidden guest operations.
- Later frames2880/3000 show key00084000 ownership for split MSAA resolves,
  53 ordered operations, no missing draws/resolves and no omitted operations.
  Sampled analysis around4.7-5.1ms; not a full-run performance result.

Next: analyze completed log036; build the source-owner / resolve-version /
consumer dependency graph for the actual last swap resolve. Use tile/rectangle
layout, not address overlap alone. Then identify the smallest complete path and
extend fresh resource/input reconstruction for the needed auto-indexed and
color-only/depth-only operations. Do not discard unsupported point draws, replay
native GPU submissions, or enable midpoint output yet. Original simulation,
music, NVIDIA pacing and Intel fixes remain unchanged.

## Baseline and scope

## RESET HANDOFF — completed log035, 2026-09-27

User completed the v1.3.110 test and requested continuation after a usage reset.
Game exited normally at 02:24:14; no game process remains. No new launch or
renderer behavior change is pending. Latest installed build is v1.3.110.
Log035 contains 57 sampled pairs, 11 eligible pairs, zero dropped resolve
records, and zero false resolve API returns. This is NOT proof of GPU completion
or visual correctness. User said "Done" for this run without a separate visual
description; the explicit no-hitches/no-visual-issues report belongs to log034.
No new D3D12 error was identified; existing cache-device/XMPGetInfo errors remain.

### Newly established structural blocker

Frame5880 (pair5879/5880) is the concrete resumption example:

- Selected run10: ordinals51..378 inclusive, 328 draws, keys002082D0/00008000.
  Resolve event8 at before_draw379 writes **16830000**, NOT the swap texture.
  Its address-range consumer is draw813. This accepted candidate is an
  upstream intermediate in the observed chain, not evidence of a finished image.
- Run13: ordinals404..769, 366 draws, keys002842E0/00084000. Resolve events9/10
  occur inside it at before_draw567/575; events11/12 at762/770. Color events10
  and12 clear BOTH color and depth. They resolve640x720 regions to1F90F000
  and1F923000. The physical byte extents overlap; do not infer disjoint image
  regions or complete output coverage without destination-coordinate/tile data.
- Draw770 is an auto-indexed (kNone=0), four-vertex quad-list, color-only pass.
  Event13 at771 resolves1280x720 to1F90F000. More postprocess/composite work
  follows, including the intermediate consumer at813.
- Event24, after draw841, is the final observed1280x720 resolve to1F90F000,
  equal to both swap fetch0 and the swap packet base.

Source collect_exact_target_runs/AuditOwnedRenderCandidates groups by contiguous
attachment identity alone. Resolves are not represented in frame.draws, so it
merges draw sequences separated by destructive operations. This is a demonstrated
limitation of the current reconstruction unit, independent of the25% threshold.
Changing only target keys or raising budgets cannot reconstruct that chronology.
The mailbox arming path ALSO hardcodes002082D0/00008000. Preserve the evidence;
do not blindly replace those keys with the MSAA pair.

### Exact next action after reset

1. Use saved log035 (no need to repeat it) to build the frame5880 dependency
   sequence. Add an ordered CPU operation representation / diagnostic pass
   segmentation at resolve/clear boundaries, reusing AuditResolve.before_draw.
   Keep production selection unchanged until the graph is validated.
2. Preserve source and destination rectangle offsets, MSAA/sample selection,
   copy command and clear values in that representation (ResolveInfo already
   captures them; existing log prints only dimensions/base). Address-overlap
   consumers are conservative candidates, not exact region provenance.
3. Classify auto-indexed rectangle/quad/point operations, depth-only passes,
   and render-target input dependencies. Never drop point/no-attachment draws
   as harmless without checking query/predication or other effects.
4. Identify the smallest COMPLETE ordered path to the final swap resolve,
   then extend fresh reconstruction only for its required primitive/input types
   and separately owned intermediates. Do not replay the native command stream.
5. Resolve duplicate identity / order ambiguity before interpolating those
   draws. Keep N+1 ordering, exact N+1 fallback where safe, and all existing
   bounds. Sparse whole-image validation must precede continuous publication.

Measured CPU audit overhead (not reconstruction time):114 captured frames,
mean1.519ms/max4.875ms;57 pair analyses, mean2.905ms/max9.470ms. Sampling occurs
once per120 swaps. No stable-baseline timing comparison was performed.

Evidence SHA256:
- log035: D989646A3C9B929B61A43F841696CBBABB143A0626878BC9AA96D574B0CA4811
- deployed rexgpu-xenos.dll: 0C136A9DC06F79C2609134CC180E130EB691EFADC7F82B954AC38F4FCA090816

Build/regression results remain those of the prior turn (passed). This turn
only analyzed the completed log and updated documentation; no new runtime code
or tests were executed. Stable30/music/NVIDIA/Intel behavior remains unchanged.

## Baseline details

The project root has no Git repository. The SDK repository contains substantial
existing audio, graphics, Intel-specific rendering, presentation, and shader changes.
Preserve all of them. The launcher was v1.3.108 with a transition profiler (mode 70);
STABLE-VERSION.txt still labels the stable baseline v1.3.86. Do not downgrade it
or remove the newer changes because that label predates them.

Pre-edit source files, launcher/config, and executable are copied under
`archive/60fps-resume-20260927`. Original SHA-256:

- doax2.exe: 0FA4DC74922759BECC59C2EB1D7088877EBDEB1BCE5651282EB7125ED767702A
- launcher: FA63F19E95F501D259E4C815CD54EAAF71AACEA0E1B83560DC4F7D2BD4D8990C

Existing render-history host test and title-playlist CTest passed before editing.
This is host verification, not a new live gameplay baseline.

## Findings from source

The old selection-deferred message is shared by many failures. It does not isolate
the 25% matching threshold. Continuous selection requires the exact target keys
002082D0/00008000, 8..512 draws, <=256 MiB logical dependencies per run,
<=32 MiB per unit, a bounded viewport, supported draw classes, a previous
same-target run, and >=25% greedy ordered recipe matches.

The exact recipe includes guest fetch/address state and translated binding slots.
Their runtime stability is unproven. Omitting a field diagnostically does not
establish safe object identity and is never used to admit or interpolate draws.
The existing matcher is greedy monotonic matching, not a general longest common
subsequence algorithm despite the old comment.

## Implemented audit

`d3d12_doax2_candidate_audit=true` (launcher mode 71) samples only pairs ending
at swaps 119/120, 239/240, etc. Capture stores CPU draw descriptions/constants.
Finalize returns through a separate read-only audit path before any owned-unit
preparation, allocation, draw, or publication. Guest backing bytes are not copied.
The existing matching thresholds and production recipe hash ordering are unchanged.

Sources: `RecordInterpolationDrawBinding`, `FinalizeInterpolationFrame`,
`AuditOwnedRenderCandidates`, and optional component output from
`GetOwnedRenderUnitRecipeKey` in D3D12 command_processor.cpp and its header;
mode wiring in src/launcher.cpp. Stable mode 30 leaves the audit disabled.

Each sampled pair reports independent and first-failing run rejection counts:

1. Unsupported draw input
2. Fewer than 8 draws
3. More than 512 draws
4. More than 256 MiB logical dependencies
5. Invalid/unbounded first viewport
6. Fixed final-target identity mismatch
7. No previous same-target run
8. Less than 25% ordered exact recipe matches
9. Per-unit dependencies above 32 MiB

Per-run unsupported counts are: attachment mask, non-bindless, shader side effect,
missing pixel shader, invalid constant index, non-guest-DMA index source, empty draw.
Separate missing-pipeline and skipped-CBV counts expose missing metadata.

Recipe field arrays use this order: shader/layout, texture fetch, vertex fetch,
texture identity/address, translated binding slots, sampler/binding semantics,
index/draw metadata/address, output contract. The audit measures exact ordered
matches, shared recipes, unique correspondences, duplicate recipes, ordering changes,
unique matches after omitting each component, and ordinal component differences.
Ordinal comparisons and omission matches are explanatory only, not safe identities.

All runs contribute to counters; at most 12 runs are detailed (fixed-target runs
first, then larger draw counts). Matching work is bounded to <=512 current and
<=4096 previous draws. Oversized matching is explicitly unmeasured. Capture CPU
time and analysis/log CPU time are reported. Resource allocation/fence lifetime
are explicitly not tested; submission numbers and existing group-slot occupancy
are observations only.

## Verification and next action

SDK Release build/install and launcher build succeeded. Deployed runtime/GPU DLLs
were hash-verified against the install directory, after backing up the old DLLs.
The executable did not relink, so its post-build DLL copy did not run; deployment
was performed explicitly to avoid testing a stale GPU plugin. Render-history
regression passed again. Live validation remains pending; no root cause proven.

Translated bindless slots are assigned by the shader translator's
GetBindlessResourceCount(), not by a per-frame descriptor allocation. Do not
remove them on an assumption of transient descriptor identity.

`scripts/run-candidate-audit.ps1` provides a reproducible 1920x1080/scale1/FXAA
launch with audit enabled and all reconstruction disabled; `-Baseline` disables
the audit with the same settings. Neither invocation changes launcher.ini.
## First live evidence (2026-09-27)

### Completed v1.3.109 run and follow-up

User reports no hitches or visual issues after testing. Log034 ended normally
at 02:16:02, with 47 sampled pairs, three eligible pairs (2400,3120,5640).
The earlier snapshot of zero eligibility was NOT the completed-run verdict.
The accepted runs were 70/1019, 183/926 and 288/871 recorded scene draws.
Those ratios do not establish pixel coverage or prove an incomplete image on
their own, but demonstrate why selection must be traced through downstream
render dependencies instead of being treated as complete-scene proof.

v1.3.110 adds metadata-only resolve provenance and unsupported-draw shape logging.
RecordCandidateAuditResolve copies up to 256 ResolveInfo records per sampled
frame, at the normal cache Resolve boundary. IssueCopy attaches its success
return (NOT a GPU completion or copied-pixel proof). IssueSwap records texture
fetch0's base, which RequestSwapTexture actually uses, and packet frontbuffer.
Audit reports extent-overlap consumers until the next overlapping resolve;
these are candidates, not validated EDRAM owners. CPU writes, partial-region
coverage, and EDRAM ownership transfers are still unresolved. Missing/truncated
events are explicitly counted. Up to 16 unsupported shader/index/count shapes
are logged per pair. No matching gate or submission behavior changed.

SDK Release/launcher builds and render-history/music regression tests passed.
Runtime DLLs were deployed explicitly and hash-compared to the SDK install.
ResolveInfo is value-initialized so empty early-return metadata cannot contain
uninitialized fields. Real RTX3090 run launched as PID25852, log035; user asked
to repeat relax/minigame/hotel and close. Await the completed report.

Early log035: frame360 final resolve after draw34 writes 1F90F000, the same base
as swap fetch0. Source color base0/pitch16/MSAA0, 1280x720. Earlier writes to the
same destination feed intervening texture draws before being overwritten. Thus
the output is a temporal resolve/consume chain, not simply an attachment label.
Observed unsupported index_class0 is kNone (auto-indexed), including quad-list
textured/color-only passes and rectangle-list draws with and without a PS.
These are NOT failed index allocations. Their semantics need classification;
do not discard them or assume every rectangle is an interchangeable clear.

Additional static blocker: TryDrawOwnedRenderGroup also hardcodes the same
002082D0/00008000 keys when arming the final-color mailbox. Fixing selection
alone would not address output routing. Neither occurrence was changed.

Next action: inspect the completed log035 dependency chain in actual gameplay;
identify the final composite operations, required owned input textures, and
depth-only/auto-indexed primitive reconstruction requirements before any sparse
midpoint attempt. Retain 30Hz simulation and all existing stable fixes.

### Initial partial-run observations (retained for chronology)

Launched PID 8080 using run-candidate-audit.ps1 on the real RTX 3090. Runtime
log: out/game/logs/doax2_034.log. At 02:15:14 it contained 36 sampled pairs.
No second-frame GPU resources allocated or published by the audit. Gameplay
navigation and visual correctness still require the user's scene/visual report.
The session was left running for that report; do not start another instance.

Pair 2519/2520: a 170-draw run matched 142 draws in order (>83%), yet was
rejected for unsupported operations and target mismatch. It contained 24 draws
without pixel shaders and 24 non-guest-DMA draws. Exact recipes were not unique
in that run (unique=0), so high ordered compatibility is NOT safe object identity.
Pair 2159/2160: a 1064-draw run also exceeded 512 draws and summed logical bytes
1059372276 exceeded 256 MiB. Matching was explicitly unmeasured for that run.
These summed bytes are NOT unique physical memory or actual upload measurements.

Observed dominant attachment keys: 002842E0/00084000. RenderTargetKey decodes
these as depth base736/color base0, pitch8 tiles, MSAA enum1 (2x). The selector's
002082D0/00008000 means depth base720/color base0, pitch16 tiles, MSAA enum0 (1x).
Thus the fixed attachment filter rejects this native-MSAA configuration. This
does not yet prove which attachment reaches the final output, nor justify simply
substituting keys. Trace resolve/output provenance before selecting dynamically.

Several sampled CPU capture times were 2-5 ms per captured frame and analysis
2-6 ms once per 120 swaps; small runs were about 0.04 ms capture/0.1 ms analysis.
These are CPU audit overheads, not midpoint reconstruction costs or a no-overhead
claim. The log includes cache-device/XMPGetInfo errors; their presence must not
be described as an error-free run. No new D3D12 error was identified in this slice.
Music playlist CTest passed again after deployment. No new live baseline comparison
or complete-scene visual verification has been performed.

Next: confirm scene labels/visual observations, trace non-DMA/depth-only operations
and attachment resolve provenance, distinguish clear/setup from genuine geometry,
and investigate recipe ambiguity. Do NOT lower the compatibility threshold or
raise budgets merely to admit these runs. Retain reconstruction=0/publication=0.

First gather mode 71 samples from menu, relax, minigame, and transitions, keeping
resolution/scale/AA equal to the baseline. Compare independent and first-rejection
counts, actual target keys, and unique matching before making behavioral changes.
Normal GPU allocation/submission failures must be investigated later if candidate
selection succeeds; this audit must not invent GPU ownership success.

After evidence identifies a failure, fix only that defect and verify sparse complete
images before enabling continuous reconstruction. Preserve the NVIDIA Present(0)
path, guest VSync, music bridge, and all Intel fixes throughout.
