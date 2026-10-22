# Private second-frame prototype

Log 077 proved that live gameplay is not one fixed six-target graph. Across
120-frame windows it used roughly 23–32 host render targets and 20–36 resolve
destinations, including depth resolves and resolve-clears. Adjacent frames can
also insert or reorder draws. A positional full-scene replay is therefore not a
safe render boundary.

v1.3.29 introduced the first real additional render submission without another
guest update. After the native frame and presentation have closed, ReXGlue:

1. selects the most-used single-color target plus its depth target for that
   native frame;
2. acquires one of two fence-owned banks;
3. creates genuinely independent committed color/depth resources and views;
4. executes only graphics state and draws that bind that exact target pair;
5. substitutes midpoint camera/instance/pose constant buffers where adjacent
   frames have a compatible draw contract;
6. signals the normal direct-queue submission fence and assigns ownership of
   the bank to that fence.

The filtered executor does not issue guest resolves, copies, compute dispatches,
queries, original clears, or original resource barriers. It cannot modify live
simulation state, native render targets, EDRAM, shared memory, or the visible
output mailbox. The private targets are cleared directly before use.

Expected log records are `DOAX2 second-frame private pass prepared` followed by
`DOAX2 second-frame submitted`. A valid submission has nonzero
`executed_draws`, `ownership=true`, alternating/distinct banks, monotonically
increasing direct-queue fences, and no D3D12 removal or command-list errors.

This checkpoint is intentionally offscreen. If it is stable, the next step is
to add private pass-local SRV dependencies and route the completed color output
through the already-validated FXAA/output-mailbox ownership path. It is not yet
a complete 60 FPS patch.

## v1.3.30

Log 078 proved 3,390 live private submissions, evenly split between the banks,
with monotonically increasing direct-queue fences and zero ownership or D3D12
failures. v1.3.30 removes seven observed empty submissions, accepts the valid
single-RT descriptor-range encoding, throttles per-frame diagnostics, records
the selected target key/format/size/sample count, and resolves a private
pipeline-statistics query after fence completion. Nonzero raster statistics
will prove that the private pass produces actual GPU geometry work before its
color output is connected to presentation.

## v1.3.31

Log 079 proves that the private passes reached rasterization: all sampled
pipeline-statistics results had nonzero input-assembler and pixel-shader
invocations, with valid bank ownership and no reported visual or timing
regressions. It also shows why frequency is not a sufficient output selector:
the dominant target changes between menu and gameplay passes and may be an
intermediate rather than the final image.

v1.3.31 records the host render target or targets dumped by every color resolve
and correlates the newest matching resolve with the exact guest frontbuffer
pointer passed to `IssueSwap`. The private pass is allowed only for an exact
single-producer match that also has a complete observed one-color draw pair.
Multi-owner, missing, and unmatched paths are skipped and logged. This proves
the private color resource is the producer of the actual swap input before a
private form of that one resolve is connected to the validated output mailbox.

## v1.3.32

Log 080 identifies one stable final host producer for both alternating guest
frontbuffers: color target key `00008000`, a single-sample RGBA8 committed
resource. At least 1,920 filtered private submissions completed with valid
ownership and nonzero raster work. Some gameplay intervals report zero host
contributors because the resolve range is already EDRAM-owned; those intervals
remain on the exact native path.

The new opt-in presentation mode expands the private target bank to include
every depth surface observed with the final color target. It renders midpoint
state into that bank, executes the complete native stream exactly once, and
uses ReXGlue's ordinary gamma/FXAA producer to publish the private color into a
presenter mailbox before publishing exact state N. A new fence extension moves
bank retirement from the private-render fence to the later mailbox-consumer
submission fence. The private color texture is transitioned to shader-resource
state only for that read and returned to render-target state before reuse.

## v1.3.33

Log 081 rejected direct sampling of the private host render target. Ownership
fences stayed ordered and the process remained alive, but the visible output
flickered, showed corrupt textures and intermittently repeated the screen four
times. The host target is not the resolved guest texture: the normal path
passes through render-target dumping, EDRAM addressing, resolve-copy and
texture loading. That transform may not be replaced with an SRV reinterpretation.

Log 081 also exposed incomplete producer coverage. The private executor received
only the deferred command segment still open at `IssueSwap`, while the title had
already caused ReXGlue to submit earlier parts of the same scene. v1.3.33
accumulates all native command segments within the guest frame and executes the
combined graphics-only stream into private banks before closing the frame.
The visible route is forcibly disabled, including for saved mode 67 settings.
New submission diagnostics report source segment and encoded-element counts.

## v1.3.34

Log 082 validates the multi-segment capture mechanism but rejects executing all
graphics pipelines merely because their color/depth outputs were redirected.
Three-segment gameplay frames reached near-complete draw coverage, while the
native image and text still became corrupt despite no private presentation.
Therefore at least one repeated graphics shader has observable writes outside
the redirected attachments.

The filtered executor now inspects the original vertex and pixel shader behind
each guest pipeline handle. Pipelines containing Xenos memory export are not
executed in the private pass. Direct external pipelines, which lack sufficient
guest shader metadata for a safety proof, are also excluded. Diagnostics count
these as `rejected_side_effect_draws`. The pass remains before native
presentation specifically to test whether this is the missing isolation
boundary; its output remains offscreen.

## v1.3.35

Log 083 showed the correct v1.3.34 filter was active and rejected memory-export
draws, but visible corruption was unchanged. This closes all recorded-draw
replay using native descriptor or resource references. v1.3.35 forces private
render and replay switches off and returns presentation to the normal 30 Hz
path.

The replacement census stores only CPU-owned values and keys: packed vertex
and pixel constants, complete used texture/vertex fetch words, texture view
keys, sampler values, index-source metadata and shader identities. It stores no
native command stream, descriptor handle or resource pointer and performs no
additional GPU submission. See `60fps-owned-input-dependency-graph.md`.
