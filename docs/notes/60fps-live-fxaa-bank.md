# One-resource live ReXGlue integration — v1.3.26

Status: implemented for live testing, NOT yet demonstrated by a DOAX2 run.
No extra frame, 60 Hz loop, GPU-stream replay, or WARP-only test is introduced.

## Exact allocation and ownership path

Selected resource: host `D3D12CommandProcessor::fxaa_source_texture_`, the
post-gamma/pre-FXAA intermediate, NOT a guest target or EDRAM resolve.

1. Existing `IssueSwap` / `Presenter::RefreshGuestOutput` calls `PrepareFxaaBank`.
2. `CheckSubmissionFence` observes the actual `submission_fence_`; one tested
   `FrameBanks<1>` owner per resource grants a lease only after completion.
   Two owners allow the active texture and read-only witness to retire on the
   SAME submission. The implementation is shared in `include/rex/graphics/frame_bank.h`.
3. Separate `CreateCommittedResource` calls create two DEFAULT heap textures,
   no alias flags, `kFxaaSourceTextureFormat`, one mip/sample, UAV capable,
   initially NON_PIXEL_SHADER_RESOURCE. No shared placed/reserved backing.
4. Existing `RequestOneUseSingleViewDescriptors` supplies gamma UAV and FXAA
   SRV handles referencing the selected texture. Normal gamma writes it;
   UAV -> NON_PIXEL_SHADER_RESOURCE orders the write; normal FXAA samples it
   into the ordinary presenter-owned output. Selection alternates each frame.
5. Existing deferred Execute records a NORMAL command list. Checked allocator/
   list Reset and Close precede direct-queue ExecuteCommandLists. No stream replay.
6. `EndSubmission` checks `Signal(submission_fence_, signaled_submission)`;
   ONLY successful signaling transitions leases to Submitted. This protects
   all intermediate and witness-copy references. Subsequent presenter commands
   reference the output, not the bank, so they do not extend this bank's lifetime.
7. Reuse explicitly observes completion, not a CPU counter. Failure quarantines
   owners; UINT64_MAX device-removal completion is rejected. Resize releases
   textures only after both banks retire. Shutdown uses the existing queue drain.

## Existing mechanisms retained

Command allocator retirement, submission-fenced bindless one-use descriptors,
frame-fenced bindful pages and constant uploads remain the SDK's mechanisms.
No parallel allocator, queue or presenter is added. Since textures persist and
resize explicitly awaits completion, no new deferred deletion queue is needed.
The diagnostic waits for BOTH banks each frame: correctness, not concurrent
two-frame scheduling or performance. Two readback buffers are diagnostic-only.

## Evidence and test

`allocated` records distinct committed resource objects. `bind` records backing,
observed/required completion and upcoming submission. `submitted` records the
successful actual queue fence. `witness` compares the inactive initialized
texture before/after writing the active one, byte-for-byte excluding row padding,
ONLY after fence completion. First 15 eligible frames and every 120th frame are
sampled. A content hash identifies the witness; comparison is not hash-only.

`validation` states whether ID3D12InfoQueue is available. ERROR/CORRUPTION counts
are device-wide, with the first 16 descriptions captured. Missing debug support
is inconclusive; errors may be pre-existing and must be attributed.
`summary` reports recorded frames, completed checks and failures. Recording
counts alone do not establish successful presentation or correct visual output.

Mode63: relax and minigame, approximately 20 seconds each plus transitions,
then normal exit. Require both banks allocated/bound/submitted, completed checks
with failures=0, no attributable resource errors and no visual corruption.
SDK/game builds and pure host tests are not live acceptance evidence.

## What a second pass still needs

This intermediate could use another retired bank with fresh descriptors and
normal gamma/FXAA dispatches. Still unresolved: private presenter output,
scene/depth/postprocess targets, EDRAM backing/resolve routing, extra-frame
transient upload/descriptors, full pose/camera/visibility/material snapshots,
and a safe title-side render entry. None is proven by this one-resource test.
