# Owned-input render dependency graph

## Log 110 acceptance and display-target admission (v1.3.61)

Log 110 proves that the v1.3.60 private sampler heap removed the last shared
sampler-allocation dependency: all 27 materialized complete runs reported exact
private sampler allocation, submitted, produced nonzero color, retired through
the live fence, and released. There were no allocation shortages, preparation
rollbacks, coherence rejections, or D3D12/resource-state errors.

The reconstructed runs remained on target `00205122/00005000` because the
concrete 2560x1440 target `002082D0/00008000` contained 253–373 fully
fresh-rebuildable draws, above the 192-unit admission ceiling. v1.3.61 raises
only the unit ceiling to 512 while preserving the 256 MiB group bound and all
owned-resource/fence requirements. The next proof is a complete offscreen
submission and fenced color readback for `002082D0/00008000`.

## Log 108 acceptance and private group samplers (v1.3.60)

Log 108 proves eleven complete, ordered target runs containing 25–73 draws.
Every successful run has equal candidate and selected counts, one root packet
per unit, non-empty private color, and fence-ordered release, with no visual or
timing regression. These are complete contiguous attachment runs rather than
selected moving-draw subsets.

A twelfth 25-draw run stopped safely at 9 root packets. Its exact failure was
sampler exhaustion: reconstruction consumed the unused tail of the native
frame's 2,000-descriptor rotating sampler heap until requests saw three and
then zero entries. v1.3.60 allocates an exact-sized private shader-visible
sampler heap for the group, writes all fresh samplers there, binds it for the
private draw sequence, retains it through the group fence, and releases it
with the group. Failed preparation now atomically removes every partial packet
and private heap before any submission, permitting a later frame to select the
larger 2560x1440 `00008000` target run.

## Log 107 acceptance and first complete target run (v1.3.59)

The exact-target census shows the concrete 2560x1440
`002082D0/00008000` attachment pair as one contiguous run with complete fresh
builder coverage in every reported sample (up to 409/409 draws). The 1600x928
`00205122/00005000` pair also has complete coverage, though it may occur in
multiple runs. The large MSAA `002842E0/00084000` pair reaches 1078/1102; its
remaining 24 draws are auto-indexed and have no pixel shader, not transient
converted geometry or shader side effects.

v1.3.59 selects the largest concrete-viewport run that is fully rebuildable,
bounded to 192 draws and 256 MiB for the first expansion, equal in length
across N/N+1, and at least 75% position-compatible. Every current draw is
materialized in order. Recipe-compatible positions interpolate the validated
transform ranges; incompatible current visibility positions hold exact N+1
state at both interpolation endpoints. Failure to materialize any unit drops
the entire run before GPU allocation.

One transition in log 107 reordered the old unique-recipe group and correctly
failed coherence, but empty slot shells then emitted 562 futile retries.
Because direct materialization cannot repair an already rejected pair, the
failure path now destroys the whole group atomically and permits selection of
a new adjacent pair.

## Log 106 acceptance and complete-pass census (v1.3.58)

Log 106 proves exact target identity in live reconstructed submissions. The
depth/color pairs `002082D0/00008000`, `00205122/00005000`, and
`00206B40/00006800` remain isolated, all complete groups produce non-empty
private color, resources retire behind observed fences, and the user reports
no corruption or timing change. In particular, `00008000` produces the
2560x1440 output observed in this run while the other identities produce
different extents.

The next blocker is coverage, not ownership: the validated group contains at
most 32 unique moving recipes while native frames contain hundreds of draws.
v1.3.58 adds a read-only census per exact attachment pair. For each pair it
records total draws, contiguous runs and ordinals, viewport extent, N/N+1
recipe matches, current fresh-builder coverage, index-buffer source classes,
pose-capable draws, and all known eligibility blockers. The render target keys
are also decoded to EDRAM base and pitch tiles. This establishes which one
complete pass can be rebuilt next and whether auto-indexed or host-converted
geometry, rather than resource ownership, is its limiting dependency.

## Log 105 acceptance and exact render-target identity (v1.3.57)

Log 105 repeatedly materializes and submits groups directly from exact
adjacent frame records. All groups are common-pair and ordered, every GPU
preparation completes, private outputs are non-empty, and resources retire
behind their submission fences without native visual regressions.

The prior grouping contract included attachment masks and formats but omitted
the actual host render-target keys. v1.3.57 captures the five complete
`RenderTargetKey` values from `RenderTargetCache` at every original draw. Those
keys encode EDRAM base, pitch, MSAA, depth/color class and resource format and
are included in both recipe identity and group equality. Only draws targeting
the same actual depth/color pair may share a reconstructed output. The bounded
group size then increases from sixteen to thirty-two units as the next
increment toward one complete independently reconstructed pass.

## Log 104 acceptance and direct adjacent-frame capture (v1.3.56)

Log 104 confirms that the v1.3.55 coherence gate submits no partial or
mismatched group. The selected recipe set did not recur completely, however,
so repeated all-group retries could not progress.

The frame census now owns the remaining compact draw-time inputs required by
the fresh renderer: bool/loop and complete fetch blocks, system constants,
viewport/scissor, blend/stencil values, and surface configuration. Group
selection already has exact unique recipe matches in adjacent N/N+1 CPU frame
records. v1.3.56 materializes both render states and midpoint values directly
from those records and copies current guest backing immediately. The later GPU
preparation therefore consumes a coherent packet without depending on a future
occurrence of each draw.

## Log 103 acceptance and group coherence gate (v1.3.55)

Log 103 validates repeated zero-texture reconstruction: all sixteen units in
the tested groups received fresh geometry/root packets, reached non-empty
private color, and retired after the real fence with no native regression.

It also records transition-time groups whose independently captured units do
not share one N/N+1 pair. Resource ownership remains correct, but such a group
cannot represent a temporal midpoint. v1.3.55 therefore requires every unit
to be captured in the same two adjacent guest frames and in stable order. An
incomplete or incoherent frame discards the entire partial CPU group before
allocation and retries the recipe set together. `TryPrepareOwnedRenderGroup`
is reachable only after that gate succeeds.

## Log 102 acceptance and null-texture scene coverage (v1.3.54)

Log 102 proves the sixteen-unit textured group and range deduplication in the
live renderer. Its 13,310,388 logical guest bytes collapse to 7,666,100 unique
bytes, all sixteen root packets complete, the shared private output is
non-empty, and its resources retire after the group fence with no user-visible
regression.

The next captured group is also a complete, ordered sixteen-unit CPU snapshot,
but contains no texture ranges. Texture reconstruction rejected all units
solely because the helper required a non-empty key/source set. v1.3.54 admits
only a zero-valid-key draw class: the descriptor-index vectors retain the
translated binding cardinality using invalid sentinels, and the fresh root
packet resolves those to ReXGlue's immutable dimension-specific null SRVs.
No private texture allocation is needed. Valid keys still require captured
guest backing and fresh texture resources, so this does not reintroduce a
native descriptor or resource dependency.

## Log 101 acceptance and deduplicated group scaling (v1.3.53)

Log 101 repeatedly reconstructs ordered groups of 3-8 draws into shared
private targets. The largest successful packet draws 159,319 vertices; all
reported outputs are non-empty and fence-retired without native corruption.
The final 45,716,478-byte captured dependency set is rejected during upload,
identifying redundant per-unit guest backing as the next capacity limit rather
than a render-correctness failure.

v1.3.53 deduplicates captured guest ranges only when address, size and content
all match. Constants, remapped fetch blocks, geometry buffers and descriptors
remain per unit. The ordered group limit increases to sixteen, with diagnostics
separating logical guest bytes from unique uploaded bytes. This tests whether
shared immutable inputs allow materially wider reconstructed scene coverage
without increasing the allocator footprint proportionally.

## Log 100 acceptance and shared-target group submission (v1.3.52)

Log 100 proves simultaneous use of both explicit banks: bank 1 was acquired
while bank 0's submission fence was still pending, and all output/readback
backing identities were distinct. It also captures an ordered two-unit group
from frames 1561/1562 with common frame ownership and stable ordinals 8/9.

v1.3.52 uploads each group unit independently, reconstructs its textures,
geometry, descriptor tables and root packet, then draws every unit in captured
order into one freshly allocated color/depth target. A single submission owns
the ordered output. The color target is copied and inspected only after the
group fence completes, then all group packets are released after ReXGlue's
subsystems process that completion. No output is presented at this checkpoint.

## Log 099 acceptance and ordered group capture (v1.3.51)

Log 099 records six successive private packets with distinct attachment
identities, non-destructive native output, and fence-ordered readback and
release. Every packet completed before the next adjacent state pair could be
captured, so normal timing never occupied the contingency bank concurrently.
This demonstrates that GPU throughput, rather than bank availability, governs
the tested single-unit cadence.

v1.3.51 selects up to eight substantial, uniquely identifiable moving draws
sharing the proven depth-plus-color contract. The selection is bounded to
64 MiB of described dependencies and retained in native draw order. Each unit
copies its constants and referenced vertex/index/texture bytes for the same
adjacent N/N+1 pair and builds midpoint transform values. Completion verifies
that the group remains ordered in both frames before any shared output or
ordered GPU submission is implemented.

## Log 098 acceptance and two-bank ownership (v1.3.50)

Log 098 completes all four requested cycles on frames 1442, 1445, 1448 and
1451. Their nonzero-pixel counts range from 636,617 to 646,040 and all four
color hashes are distinct. Every image was mapped only after its draw fence was
observed complete, and the user reported no visual or timing abnormalities.

v1.3.50 moves each submitted CPU snapshot, GPU upload packet, private
descriptors, attachment heaps and readbacks into one of two explicit ownership
banks. The next adjacent state pair may be built in the free bank without
destroying the submitted bank. Retirement and readback are evaluated per bank
against its own submission fence. No third packet may overwrite two occupied
banks. This establishes the lifetime container that will hold ordered scene
packets as coverage expands beyond the first reconstructed draw.

## Log 097 acceptance and dynamic midpoint cycles (v1.3.49)

Log 097 proves actual private image production: 4,611 pixel invocations changed
3,433 pixels from the known clear, yielding hash `43BF259AB830FE46` after fence
2076 completed. This closes the one-shot owned render path from CPU snapshots
through private D3D12 image content.

v1.3.49 re-arms the same recipe for four fresh adjacent state pairs. Re-arming
occurs only after output/query readback and all subsystem fence retirement have
completed, so the dedicated raw-geometry descriptor and packet-owned heaps are
never overwritten while in flight. Distinct output hashes will prove that the
renderer consumes changing midpoint state over time rather than repeatedly
rendering one frozen snapshot.

## Log 096 acceptance and private color readback (v1.3.48)

Log 096 proves a substantial fresh render reached the private output: 30,676
indices yielded 16,679 rasterized primitives and 402,526 pixel-shader
invocations. All inputs and attachments retired behind submission 2080, and the
native image remained stable.

v1.3.48 transitions only the independently owned single-sample color texture
from render-target to copy-source state after the draw, copies it through a
device-derived placed footprint into a dedicated readback buffer, and maps it
only after fence completion. A nonzero-pixel count against the known
transparent clear and a full active-pixel hash provide direct evidence that the
fresh renderer generated private image data before output routing begins.

## Log 095 result and visible-candidate selection (v1.3.47)

Log 095 reports 31 input-assembler vertices, 29 assembled primitives and 18
vertex-shader invocations for the private draw. Zero clipped primitives and
pixel-shader invocations mean the smallest stable unit was fully culled, not
that reconstruction failed before shader execution.

v1.3.47 changes only the candidate policy. Among unique side-effect-free moving
pose draws it requires bindless inputs, guest-DMA indices, depth plus color 0,
a pixel shader and at most 16 MiB of dependencies, then chooses the largest
vertex count. The owned-input, descriptor, attachment, submission and query
paths are otherwise unchanged.

## Log 094 acceptance and private raster proof (v1.3.46)

Log 094 proves that the first completely reconstructed draw was accepted by the
live D3D12 queue on submission 1998 and that its independently owned geometry,
textures and attachments remained protected until completion 1999. The native
swap path remained visually normal after the forced command-list boundary.

v1.3.46 adds a pipeline-statistics query whose begin/end scope contains only
the reconstructed indexed draw. Its private readback is mapped only after the
same submission fence completes. IA, vertex-shader, clipping and pixel-shader
counters will therefore establish whether the fresh render actually reached
rasterization and the private attachments before any output routing is added.

## Log 093 acceptance and first reconstructed draw (v1.3.45)

Log 093 proves that a private 2560x1440 color/depth attachment pair can be
created, cleared and retired through the live ReXGlue submission path. The
resources were distinct, used compatible formats, and remained protected until
submission 2242 completed. Native rendering and game timing were unaffected.

v1.3.45 combines the already-proven owned inputs into one fresh indexed draw.
It binds the exact immutable PSO/root signature, all seven private CBVs, the
owned raw-geometry/null-UAV table, fresh texture/sampler descriptor indices,
private RTV/DSV, and captured fixed-function state. Pending geometry barriers
are emitted before the draw. The command list is then submitted and closed
before normal swap routing, forcing ReXGlue to establish clean native state on
the next command list. The result is deliberately not resolved or presented.

## Log 092 acceptance and private output allocation (v1.3.44)

Log 092 proves the complete fresh root packet for the selected unit. It contains
seven owned CBVs, four private texture indices, two private sampler indices, a
private geometry table and exact immutable pipeline/root-signature identities.
The captured output contract is depth plus color target 0 (`rt_bits=03`) at
1600x928. Resource retirement occurred only after the protecting live ReXGlue
submission completed, and gameplay showed no visual or timing regression.

v1.3.44 derives the color/depth formats and MSAA count from the captured N+1
state, creates independently backed committed color and depth textures, creates
dedicated RTV/DSV heaps, and records real clears through the live deferred
command list. The textures use the existing submission-indexed deletion queue;
their descriptor heaps remain owned by the packet. Nothing is bound or drawn,
so this isolates attachment creation, command recording and fence retirement
before the first reconstructed draw.

## Log 091 and null-view completion (v1.3.43)

Log 091 reproduced complete private texture and geometry creation but emitted
neither root-packet success nor rejection. The uninstrumented exits were after
those layers. v1.3.43 now mirrors native bindless null-view selection for every
unavailable signed, invalid or incompatible texture binding and reports the
exact failing assembly stage if another condition remains.

The same packet now carries the native pipeline's color/depth attachment mask
and all five format words. A successful record therefore supplies the minimum
format census needed to allocate compatible private output attachments next.
It remains observation/allocation-only and performs no reconstructed draw.

## Log 090 acceptance and root-packet implementation (v1.3.42)

Log 090 proves the private geometry buffer and its bindings are independently
owned and fence-safe. The 2,112-byte buffer was protected by submission 2789
and was not retired until completed submission 2791. Its private fetch CBV,
raw SRV table and 62-byte index view were all nonzero; texture retirement obeyed
the same completed-fence boundary.

v1.3.42 carries the shader's exact texture/sampler binding metadata into the
owned snapshot. Texture reconstruction now returns the fresh signed/unsigned
bindless indices in input order. The root-packet builder combines those with
fresh sampler descriptors and private vertex, pixel, system, bool/loop and
remapped-fetch CBVs. It also verifies the immutable PSO/root-signature identity
is identical for N and N+1 and records topology, viewport, scissor, blend and
stencil state. No root binding or draw is executed at this checkpoint.

If the live log reports `complete=true`, the unresolved dependencies for the
smallest real draw reduce to independently owned color/depth output attachment
creation, render-target views and safe transition/routing of that output.

## Log 089 acceptance and owned geometry reconstruction (v1.3.41)

Log 089 proves that two private texture source ranges were processed into two
distinct default-heap textures and two fresh SRVs. Their protecting submission
and observed retirement completion were both 1695, with no D3D12 failure.

v1.3.41 packs the selected unit's owned vertex and guest-DMA index bytes into a
fresh default-heap buffer. It emits live copy commands, transitions the buffer
to combined shader/index read state, remaps only the used vertex addresses in a
private 192-word fetch-constant block, and creates a contiguous owned raw-SRV /
null-UAV table plus an independent index-buffer view. The resource enters the
existing submission-indexed deletion queue and logs when its protecting fence
has completed. These bindings remain unbound and no private draw is issued.

## Log 088 acceptance and owned texture reconstruction (v1.3.40)

Log 088 proves v1.3.39 allocated the complete 274,222-byte, nine-slice owned
packet through ReXGlue's live upload pool at submission 1893. Its ranges were
non-overlapping and its midpoint CBV did not alias the native N+1 constants.

v1.3.40 introduces a private source boundary in the existing D3D12 texture
loader. Exact snapshotted texture bytes are exposed through bounded SRVs over
their frame-fenced upload slices, then processed by the normal ReXGlue detile,
endian/format conversion and texture-copy path into fresh default-heap texture
resources with fresh SRVs. Those resources are tagged with the real submission
index and retired only after observed fence completion. They are not bound to
a draw, and no second frame is submitted at this checkpoint.

## Log 087 acceptance and owned GPU upload packet (v1.3.39)

Log 087 proves the selected unit has a complete adjacent pair: frames
1561/1562, 18 changed vertex words, 20 midpoint transform words, identical
fixed output state and an unchanged exact four-vertex indexed draw recipe.
Guest backing later changed while the owned hash remained intact, directly
proving snapshot independence from title-memory reuse.

v1.3.39 copies the complete reconstructed input packet into fresh slices from
ReXGlue's existing frame-fenced D3D12 upload pool. It covers midpoint/pixel,
system, bool/loop and fetch constants and every vertex/index/texture-backing
range. This validates actual GPU-visible allocation and its normal lifetime
mechanism without binding a descriptor or issuing a private draw. Texture
detiling/default-heap creation and fresh shader-visible views remain the next
boundary.

## Earlier Log 086 snapshot and adjacent state pair (v1.3.38)

Log 086 proves one selected unit can be fully copied without the historical
diagnostic slowdown: 2,048 vertex bytes, 62 index bytes, 270,336 texture bytes,
352 vertex-constant bytes and 32 pixel-constant bytes. The owned backing hash
remained intact. The live guest backing did not change during the run, proving
that this unit's changing pose is separated from its static geometry and
textures.

v1.3.38 strengthens the identity with complete primitive/index metadata and
captures two strictly adjacent render states. It stores all GPU registers and
the derived fixed-function/system state, then computes the same bounded 50%
camera/object/pose midpoint used by the validated deformation path. This will
determine whether the unit has a complete, stable draw recipe suitable for
fresh upload, descriptor and target allocation.

## Earlier Log 085 census and snapshot (v1.3.37)

Log 085 confirms that the draw-order-independent recipe survives real gameplay
and transitions. Stable samples expose 6-38 unique, side-effect-free moving
pose units. One transition changed the frame by 16 draws and produced 687
ordinal shader mismatches, yet still exposed 8 recipe-matched moving units.
This proves draw ordinal is not required to identify the initial unit.

v1.3.37 ranks those candidates by their distinct referenced guest-byte
footprint. It arms the smallest candidate, then copies the candidate at its
next live draw boundary into a host-owned snapshot containing constants,
fetches, texture/SRV and sampler descriptions, and the exact vertex, index and
texture bytes. It retains no native descriptor handle or resource pointer and
does not submit a second frame. Periodic hashes verify that the owned copy
remains intact and report whether the corresponding live guest ranges have
subsequently changed.

The next reconstruction step, after a complete snapshot is observed, is to
capture the missing fixed-function/draw parameters and recreate fresh uploads,
views and descriptor entries for this one unit against private color/depth
targets.

## Earlier Log 084 census (v1.3.36)

Log 084 is a clean acceptance of the v1.3.35 observation-only path. The user
reported normal graphics, timing and performance. Gameplay samples contain
880-1663 draws, 767-1548 pose-capable draws, zero shader-side-effect draws,
and predominantly guest-DMA indices. Every observed draw uses bindless
descriptors. Transitions sometimes change draw count by 1-4 and can shift
hundreds of ordinal shader matches, so draw ordinal is not an ownership key.

v1.3.36 records the exact number of shader-visible texture keys and sampler
values rather than stale capacity retained in backend vectors. It resolves
each key to CPU-only guest base/mip ranges and reports distinct ranges and byte
coverage. It also builds a draw-order-independent recipe key from shader and
constant layouts, fetch constants, texture identities/backing ranges, samplers
and index source. A recipe is a candidate only when it occurs exactly once in
both adjacent frames, is pose-capable, has no shader side effects, and has
changing vertex constants. This remains a census: guest backing content is not
retained and no descriptors, resources, commands or second submission exist.

This graph replaces recorded GPU command replay. It describes a future render
unit reconstructed from CPU-owned descriptions and newly allocated GPU state.

```text
validated DOAX2 deformation recipe / instance producer
  -> camera + instance + bone float constants (CPU values, snapshot)
  -> material pixel constants (CPU values, snapshot)
  -> texture and vertex fetch constants (CPU values, snapshot)
       -> guest texture backing ranges (copy or explicitly pin)
       -> guest vertex stream ranges (copy)
       -> guest index range OR converted-index recipe (copy/rebuild)
  -> sampler values (recreate)
  -> immutable shader + pipeline identity (reuse cache object)
  -> fixed-function state and visibility/draw recipe (still unresolved)
       -> fresh constant uploads
       -> fresh descriptor heap entries and views
       -> independently backed color/depth targets
       -> private EDRAM dump / resolve / texture-load output
```

| Input | Classification | Rule for a reconstructed render |
|---|---|---|
| Shader and pipeline identity | 1: immutable/reusable | Reacquire from the pipeline cache; never retain a transient command encoding. |
| Sampler parameters | 1: immutable value | Store the value and create a fresh sampler descriptor. |
| Camera, instance and bone constants | 2: mutable/snapshot-able | Copy CPU values at the native draw boundary; interpolate only owned copies. |
| Material/pixel constants | 2: mutable/snapshot-able | Copy bit-exact values unless a field has an independently proven interpolation rule. |
| Texture and vertex fetch constants | 2: mutable/snapshot-able | Copy complete fetch words, not descriptor handles. |
| Guest vertex/index memory | 2/3: mutable or transient | Copy the referenced byte ranges, preserving format and endian metadata. |
| Converted index buffers | 3: transient | Retain a conversion recipe and rebuild in owned storage. |
| Constant-buffer uploads | 3: transient | Allocate and upload fresh buffers from captured CPU values. |
| Descriptor tables | 4: alias/view | Rebuild all CBV/SRV/UAV entries in a fresh owned heap. Never retain native GPU/CPU handles. |
| Texture SRVs | 4: alias/view | Retain CPU texture keys/swizzles only; create fresh views over copied or explicitly pinned backing. |
| Native texture/cache backing | 5 until proven | Do not retain across frames. Determine copy range or explicit immutable lifetime per binding. |
| Visibility/culling and draw order | 5: unresolved | Capture a renderer-owned draw recipe without re-running gameplay or retaining commands. |
| Color/depth/resolve output | Independent ownership required | Use private backing and the exact EDRAM/resolve/texture-load transform. |

The initial smallest candidate is one pose-capable character draw whose shader
pair, constant layouts, fetch words, texture keys, sampler values and index
source are stable in consecutive frames and which has no memory-export side
effects. It is not renderable until visibility/fixed-function state and every
referenced backing range have an owned representation.

v1.3.35 performs only a CPU-side census. It records value descriptions and
classification counts; it creates no second submission and retains no native
descriptor or resource pointer.
