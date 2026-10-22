# Second-frame prerequisite census — v1.3.28

Status: implemented, SDK/game/launcher builds and host tests pass; live DOAX2
evidence pending. This is observation-only and produces no additional frame.

## Proven input to this checkpoint

Log075 passed two private FXAA intermediates. Log076 passed the existing three-
texture presenter output mailbox: three stable committed backings, 49 inactive-
slot byte comparisons with zero mismatches, producer fences observed, more than
20,000 paint submissions signaled without ownership failure, and clean visuals,
transitions and timing. D3D12 debug-layer availability remains absent on the
test PC, so no checkpoint claims debug-layer proof.

## Consolidated census

`D3D12RenderTargetCache` aggregates 120 guest frames at the real `IssueSwap`
boundary. For the host-RT path it records each bound color/depth target by
resource identity and full `RenderTargetKey` (EDRAM base, pitch, MSAA, format),
binding counts and ownership-transfer range counts. Resolve requests are grouped
by destination base/extent with depth/color copy and clear counts. The single
EDRAM scratch/ROV backing is logged explicitly. The probe observes existing
objects and creates no render resource.

The existing interpolation capture remains recording-only. It now retains both
vertex and pixel packed constant buffers per draw, vertex/pixel shader hashes,
used texture masks, vertex-fetch masks and an FNV diagnostic hash of only the
referenced texture/vertex fetch descriptors. Every 120th pair reports:

- draw-count delta and shader-sequence mismatches (visibility/order proxy),
- changed low vertex constants (camera/instance candidate ranges),
- changed c84+ vertex constants (pose/deformation candidate range),
- changed pixel constants (material/effect inputs),
- changed used fetch descriptors (geometry/texture-resource inputs),
- unique vertex and pixel CBV counts.

The command stream is retained only as an existing frame description. Replay
is explicitly disabled by the launcher and no override is submitted. Fetch
hashes detect change but are not resource snapshots. Visibility is currently
represented by the emitted draw sequence, not a classified title-side object
list. Those distinctions prevent this census from overclaiming readiness.

## Decision enabled by the log

The resource windows will show whether a practical private second scene needs
a small stable set of host targets or whether all output depends on broad EDRAM
ownership/resolve state. The consecutive input comparison will show whether a
stable draw topology can consume buffered camera/pose constants, or whether
visibility, materials and fetch sources require a title-side render evaluation.

After live results, choose exactly one implementation branch:

1. Stable target and draw sets: privately bank the minimum scene color/depth +
   EDRAM/resolve backing, then invoke a newly constructed normal draw submission
   from complete buffered inputs—never the old whole-stream replay.
2. Unstable draw/resources: return to the smallest classified title render-
   evaluation boundary and snapshot its render-only inputs, rather than trying
   to infer/replay destructive GPU operations.

Both branches keep simulation at 30 Hz and route completed output through the
already validated FXAA and presenter mailbox ownership paths.
