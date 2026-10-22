# Live presenter output ownership

## Continuous reconstructed presentation (v1.3.68)

Log 117 proves three sparse midpoint→exact pairs with both mailbox publications
successful and no visual or timing regression. Continuous mode therefore
selects an eligible complete target run on every adjacent frame pair instead of
once per 120 swaps. It removes the diagnostic CPU fence wait and color readback
from ordinary frames, relying on the now-proven direct-queue ordering and
fence-driven descriptor/resource retirement. A readback and verbose per-unit
proof remain sampled every 120 frames.

The reconstruction path still creates fresh resources and copies owned guest
inputs, so this checkpoint tests continuous correctness and true presentation
rate before allocation reuse and deduplication are optimized.

## First visible reconstructed midpoint (v1.3.67)

Log 116 passes the clean unpublished conversion and normal visual observation.
The first visible checkpoint therefore splits the current native presentation
without replaying it: the already-recorded native work is submitted once but
not yet published, a clean submission converts and publishes the reconstructed
N→N+1 midpoint, and a second clean conversion publishes the exact N+1 native
output. With V-sync, the presenter's immediate guest-output paint path schedules
the two mailbox images in midpoint-then-exact order.

This remains at the proven 120-swap diagnostic selection cadence. It validates
a genuine extra host presentation before continuous reconstruction is enabled;
simulation, animation, game clocks and guest draws remain single-execution.

## Clean post-native submission (v1.3.66)

Log 115 proves the complete reconstructed 2560x1440 group through execution,
nonzero color readback, exact command-processor fence completion, dependency
retirement and release. The abrupt end after the fence gate, before the first
mailbox-stage completion log, isolates the failure to the conversion path.

The earlier conversion saved and reset only the deferred command stream while
the rest of ReXGlue's native submission and frame state remained open. The
mailbox refresher ultimately calls `EndSubmission(true)`, incorrectly
finalizing caches and bookkeeping associated with the removed native commands.
The new path completes and publishes the normal native frame first, opens a
fresh `BeginSubmission(false)`, and performs the unpublished owned-image
conversion there. The reconstructed source is still protected through the
conversion fence, while the presenter's ordinary writable mailbox slot and
producer tracker retain their existing ownership rules.

## Final-color fence isolation (v1.3.65)

Log 114 confirms unique geometry descriptor tables but still ends before the
first display-sized group fence completes. The display mailbox candidate is
now synchronously gated on its real command-processor submission fence before
native recording resumes. This distinguishes an intrinsically invalid group
from corruption caused by an as-yet-unclassified global allocator or heap being
changed while the reconstructed group is in flight. The wait is diagnostic and
will not be the final 60 Hz scheduling model.

## Per-unit geometry view ownership (v1.3.64)

Log 113 proved the copy-source producer sequence across 15 groups, but exposed
that every draw in a group still referenced the fixed
`kOwnedUnitRawSRV/kOwnedUnitNullRawUAV` descriptor pair. Rewriting that pair
while recording a group changes what earlier draws see when the GPU later
executes them. v1.3.64 instead requests a unique contiguous SRV/UAV pair per
unit from ReXGlue's one-use bindless allocator. The allocator already attaches
each index to `submission_current_` and releases it only after observed fence
completion, so no parallel descriptor-lifetime mechanism is introduced.

## Copy-source handoff correction (v1.3.63)

Log 112 ended after the 329-draw group was submitted and its final-color source
was armed, but before fence completion/color proof and before the mailbox
consumer ran. The producer-side `COPY_SOURCE -> RENDER_TARGET` transition added
in v1.3.62 is therefore removed. The proven reconstruction/readback path again
leaves the private color target in `COPY_SOURCE`; the unpublished mailbox pass
now consumes and restores that explicit state. This test remains non-visible
and leaves the native 30 Hz renderer and simulation unchanged.

## Reconstructed final-color staging (v1.3.62)

Log 111 proves four complete reconstructed `002082D0/00008000` target runs at
2560x1440, including 200–314 fresh draws, exact private sampler allocation,
nonempty fenced output, and clean release. v1.3.62 connects that resource to
the already validated presenter mailbox boundary without making it visible.

The completed private color resource is retained beyond its render submission.
At the following `IssueSwap`, the native deferred stream is copied and removed
from the active list, the private R8G8B8A8 resource is converted through the
normal gamma/FXAA guest-output refresher into the current writable mailbox
resource, and the presenter signals its normal producer fence. Publication is
suppressed, so the writable index does not rotate. ReXGlue then opens a new
submission, restores the untouched native stream, overwrites that same mailbox
slot in direct-queue order, and publishes only the native image.

The private source is retained until the command-processor submission covering
the mailbox conversion completes. Exact source/mailbox dimensions are required;
a mismatch is rejected without sampling. This checkpoint neither presents an
extra frame nor replays any native draw or retained native descriptor.

## Original mailbox ownership proof (v1.3.27)

Status: implemented and locally built, pending a real DOAX2 run. The preceding
FXAA intermediate checkpoint passed log075 and clean visual/timing observation.

## Why this is the next resource

The next write after the proven FXAA intermediate is the presenter guest-output
texture (`D3D12Presenter::guest_output_resources_`). An independent second
render/presentation will need a completed output retained while another output
is produced. ReXGlue already implements this as a three-slot mailbox, so this
increment validates and extends that abstraction instead of adding an allocator.

Each slot is a separate DEFAULT-heap `CreateCommittedResource`, R10G10B10A2,
UAV-capable, initially/ultimately PIXEL_SHADER_RESOURCE. `IssueSwap` receives
the selected resource through `D3D12GuestOutputRefreshContext`; the proven FXAA
path writes it through its normal UAV and transition sequence.

After command-processor submission, the SAME direct queue is signaled by
`guest_output_resource_refresher_submission_tracker_`. The slot records that
producer submission before publication. Failure is retried/awaited; if still
unproven the frame is not published and diagnostic objects are quarantined by
intentional failure-path retention.

The base Presenter atomically publishes ready/writable/acquired mailbox indices.
Painting takes its own ComPtr, creates/reuses an SRV, records the paint fence in
`guest_output_resource_paint_refs`, draws into a DXGI back buffer, then signals
`paint_submission_tracker`. A paint reference is released only after observed
paint completion. Producer and paint use the same ordered direct queue, so a
later producer write cannot overtake an earlier paint read even after the slot
becomes writable at the CPU mailbox layer.

## Dynamic isolation proof

The test checks that no two live mailbox slots contain the same resource object.
For sampled frames it chooses an initialized inactive slot, submits a full
texture readback before and after the normal active-slot write, restores the
inactive texture to its ordinary shader-resource state each time, and signals
the existing producer tracker after both copies. Comparison occurs only after
that exact fence is observed. Rows are byte-compared without padding; a content
hash is diagnostic identity only. First 15 eligible frames and every 120th are
sampled. Normal painting may read the inactive slot between submissions, which
is safe and non-mutating on the same ordered queue.

Acceptance requires three distinct allocations, normal rotation, producer and
paint signals without failures, completed equal witnesses in more than one slot,
normal exit, and no visual/transitional/timing regression. No output second
frame is generated by this test.

## Remaining after acceptance

This would prove independent post-FXAA output retention through presentation.
The next ownership boundary is any presenter paint intermediate actually needed
by the configured scaling/effect chain; direct bilinear presentation may use
none. Beyond presentation, a real interpolated render still needs independently
owned guest scene color/depth and resolve backing, extra-frame transient command/
descriptor/upload storage, complete render-input snapshots, and a safe title
render-evaluation entry. Existing midpoint crossfade code is not evidence of
geometry/pose interpolation and remains disabled.
