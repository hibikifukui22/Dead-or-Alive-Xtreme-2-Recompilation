# Resource ownership checkpoint — 2026-09-10

Latest increment: see `60fps-live-fxaa-bank.md`. v1.3.26 connects the proven
owner to ONE live resource: the host FXAA intermediate. It is built for hardware
testing but live acceptance remains pending. The previous inventory below is
historical; its statement that Signal is unchecked is superseded by this change.

Status: initial inventory, NOT a complete scene resource graph. v1.3.25 stays
installed. No game interpolation submission is enabled. Same-phase host
deformation was accepted in log074 and by the user's visual report; it is
not evidence of an interpolated character frame.

Categories overlap: A persistent read-only candidate; B native-frame data;
C transient allocation; D shared/aliased backing; E requires GPU completion.
An A candidate is shareable only while its backing and descriptors are immutable
and its owner is retained. Pointer equality alone does not establish that.

| Resource family | Categories | Known ownership / required action |
| --- | --- | --- |
| EDRAM buffer and views | D,E | render_target_cache.cpp:219 creates one committed buffer; multiple typed views alias it. Private views alone do not isolate writes. Additional bank needs independent backing and ownership-transfer metadata. |
| Native color/depth targets | B,D,E | CreateRenderTarget (same file:1646) creates committed resources, but contents participate in EDRAM ownership transfers. Need bank-local targets AND transfer state; depth/stencil format must come from actual title keys. |
| Post-processing texture headers 834562F4/83456328 | B,D,E | 8253EC50 / 8253D470 use shared arena backing. Need independent pixel ranges, headers, surfaces and downstream texture routing. Do not rerun the initializer to allocate a second bank. |
| Other resolve destinations | B,D,E | Log068 contains 400015E0, 401D5BC0..401D5D00, 83456728 and others. Labels are run-specific. Pass-to-resource mapping exists partially; full allocation/consumer coverage remains open. |
| Texture cache resources | A or B,D,E | texture_cache.cpp has committed allocations at1572/2031; texture cache identity/content invalidation follows guest backing. Immutable assets may be shared; resolve-updated textures may not. Per-texture mutability classification is incomplete. |
| Shared guest-memory GPU buffer | D,E | shared_memory.cpp may create a reserved/tiled resource or committed buffer. Guest addresses resolve into shared backing. Remapping render targets alone does not isolate memexport/resolve writes here. |
| Built-in primitive index buffer | A,E | primitive_processor.cpp initializes dedicated default/upload resources. Candidate read-only reuse after initialization; dynamic conversion output is a separate lifetime question. |
| Dynamic vertices / primitive conversion | B,C,D,E | Observed five vertex descriptors in layout7; immutable/deformed backing ownership not completely mapped. Must retain immutable input or snapshot/version mutable input. |
| Constant/upload pool pages | B,C,E | BeginSubmission/BeginFrame reclaim using completed frame indices derived from submission fences. Intermediate frames need their own protected ranges. |
| Shader-visible descriptor heaps | C,D,E | Bindful pools and bindless heaps have different lifetimes. Fresh descriptors can still alias old resources; both descriptor storage and referenced resources need retention. |
| Command allocators/lists | C,E | EndSubmission queues allocator retirement by submission fence. Never reset allocator while commands remain in flight. New command lists do not isolate render data. |
| Temporary CPU wrappers / render lists | C | 8275A6E0 allocations retired by8275AE18, possibly within rendering. Copy required CPU values before retirement; do not retain wrappers as model identity. |
| Shader / PSO objects | A,E | Candidate immutable reuse once created, with host references retained. Does not imply their descriptor bindings or constant data are immutable. |
| Queries / readback / presentation output | B,C,E | Must identify all consumers and fence after their final use. Existing replay cannot safely duplicate query lifecycle. Full query coverage remains unresolved. |

## Explicit two-bank model

Logical native bank N contains native frame outputs. Logical intermediate bank
N-to-N+1 contains outputs built from copied/interpolated inputs. Each bank owns
mutable targets, resolve destinations, descriptor storage, upload ranges,
command allocator and resource-state tracking. Sharing is limited to retained,
proven immutable inputs. A queue fence must cover the bank's last consumer,
including presentation, before reuse. Interpolation needing N+1 implies buffered
presentation latency; this is not extrapolation or an additional simulation tick.

FrameBanks uses generation-stamped leases, recording/submitted/free states,
and observed completion on one queue timeline. Signal failure or uncertain
submission quarantines banks. Native ReXGlue EndSubmission currently does not
check the HRESULT of its Signal call (command_processor.cpp:3618); integration
must not claim successful retirement based solely on incrementing that counter.
This audit does not change the SDK's existing submission implementation.

## Newly tested native allocation boundary

`tests/frame_bank_gpu_test.cpp` connects FrameBanks to real D3D12 resources,
a direct queue, command allocators/lists and ID3D12Fence on the WARP adapter.
Each bank owns separate committed color, depth, copy-output, readback and
upload resources, plus separate RTV/DSV/SRV heaps. No descriptor clones or
placed-resource aliasing are used.

Test sequence:

1. Reserve two banks. Record fresh clear/copy commands into each bank's list.
2. Submit red and blue outputs, signal fences1/2, reject reuse without reported
   completion. No busy GPU bank is overwritten or allocator reset.
3. Await fence2 and verify every RGBA pixel in both readbacks.
4. Acquire completed bank with a new lease generation, reject the old lease,
   reset only that allocator, submit green, signal/await fence3.
5. Verify green output and that the other bank still reads blue.

Local result: PASS. This proves allocation isolation and reuse for this small
native test. The copy-output is single-sample CopyResource, NOT a DOAX2/Xenos
EDRAM resolve. Upload storage and SRV heaps are allocated but not shader-sampled.
Depth is cleared but not used by rasterized geometry. No swapchain, animation,
character, ReXGlue command processor or visible second frame is tested.

Build/run:

    . ./scripts/dev-shell.ps1
    cmake --build out/game --target doax2_frame_bank_gpu_test -j 4
    ./out/game/doax2_frame_bank_gpu_test.exe

## Next unresolved boundary

Move bank ownership into the actual ReXGlue target/resolve and descriptor
allocation paths, with pass reads/writes routed consistently. This requires
the remaining mutable-resource classification above; the smoke test is not
permission to resume whole-stream replay. CPU pose/visibility/material coverage
and the smallest safe normal-render entry remain open. No 60 FPS claim.
