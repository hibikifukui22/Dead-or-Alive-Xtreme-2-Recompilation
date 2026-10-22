# Title render boundary audit — 2026-09-08

Status: no complete render-only frame entry point has been proven safe. Do not
call `8258E500`, `8258E778`, or `827571F8` twice based on this map.

## Log 058: callback resolution and world-matrix input

The run observed 10 distinct callback addresses across 11 caller/callback
pairs. There is a second queue consumer call site, return address `825C3164`,
as well as `8258C82C`. Empty periodic samples therefore do not establish that
the entire frame has no draws. Dispatcher frequency is not simulation frequency.

| Callback | Verified behavior |
|---|---|
| 8275BD70 | Payload object vtable +8 predicate/setup, then +32 render dispatch |
| 825C19A8 | Conditional tail call 8254B018; global manager flag dependency |
| 82538040 | Tail call 82536158 |
| 82543CF8 / 825457E0 | OR global flags (32/64), set render and manager flags |
| 82614038 / 826141E8 | Draw/pass helpers; mutate embedded object +3168/+3172 and allocate temporary linked node through 8275A6E0 |
| 825F6B50 | Global write, two subsystem calls, render-state updates |
| 826152B0 / 82615300 | Selection/lighting setup and different 825B07E8 / 825B0B30 calls |

`8275BC68` constructs the `8275BD70` record, stores the object pointer from
its first argument, and places it in a selected queue. The record is a small
command referencing an object, not a self-contained pose snapshot.

Static table entries in `logs/default-image.bin` locate candidate render
implementations `8275A8F0` (slots at 822A78E4 / 822A7950) and `8275A9B0`
(slot 823E97B0). Actual runtime types must be checked; table presence alone is
not proof these types were the logged payloads.

`8275A8F0` enumerates materials and calls `8275BDD0`; `8275A9B0` handles one
material. `8275BDD0` runs setup, material state, virtual +16, conditional
`8275C3F0` geometry submission, and virtual +20. It is still stateful.

The known setup method `8275A800` calls vtable +28 for a 64-byte matrix,
then `82740E08` with selector 2. Getters `827AB060` and `8248A848` return
object +12's pointer and object +32's address respectively. This establishes
a CPU object-transform input separate from raw GPU command capture. It does
not yet establish pose-buffer ownership or that this matrix alone moves bones.

Forwarders `82470660` and `8275EF00` send the material to an optional child
object at +4 through child vtable +16/+20. Those dynamic targets are the next
necessary classification inputs. v1.3.9 records wrapper tables and these child
tables (only slots through +20 for children; trailing logged slots are zero),
and copies known matrices into bounded host arrays. Same-address change counts
are diagnostic only, not persistent object identity or adjacent-frame proof.

Validation: game and launcher compile/link successfully. Runtime validation of
the new observations is pending. GPU replay and engine uncap remain disabled.

## Corrections to prior conclusions

The original task `Set up DOAX2 recomp tools` already required isolation of
host targets and EDRAM before GPU replay. v1.3.6/7 bypassed that prerequisite.
Log 057 establishes that the feature ran and the user observed corruption;
it does not identify each corrupted resource or prove a GPU performance cause.
120 guest swaps over approximately 7–7.55 seconds measure 16–17 guest swaps/s,
not displayed FPS. Neither `midpoint armed` nor matching shader layouts proves
object identity, correct interpolation, or two distinct displayed images.

The 352-byte record consumed by `82545A68` is lighting-related: two initial
vectors followed by four 80-byte records, including light-type branches and
direction calculations. Do not label all low shader constants camera data.
Likewise c84+ is only a pose candidate until its CPU producer and shader use
are linked. The earlier probe labels are hypotheses, not a type system.

## Ordered direct map of 8258E500

Addresses below are guest function entries; call return addresses identify
the corresponding call sites in generated C++.

| Call / region | Evidence and side effects | Repeat eligibility |
|---|---|---|
| 82782F78 / 82782C60 loops | Five handle slots and active-count-dependent synchronization | Exclude pending handle semantics |
| 8258CC00 (return E5A4) | Fiber scheduler: decrements record countdowns, switches fibers, may delete completed fibers | Exclude |
| 8252ABB0 (E604), 8252C068 (E608) | Polling and asynchronous state machines; handles, completion states and callbacks | Exclude |
| 825C19C8 (E618) | Manager transitions, virtual calls, writes and subsystem calls | Exclude pending callback classification |
| 8248D858 (E61C) | XOR-flips global selector; calls 824B10C0, four 8248A8F0 passes, resets counters | Exclude; selector alone does not prove pose double buffering |
| 8265E7B8 (E620) | Multiple fixed-step countdown decrements and callbacks when they expire | Exclude |
| 82740E08 (E670/E6A8) | Copies two 64-byte inputs from global +224/+288, computes derived matrices, writes renderer caches and uploads constants through 826FA7D0 | Transform publisher candidate; not a complete frame |
| 82546280 (E6B4) | Refreshes mode/selection cache; conditional 82545E18 / 82546778 | Mixed; exclude pending descendants |
| 825C1E88 (E6BC) | Calls selected object vtable +20 and writes manager +12/+32/+36/+60/+64/+68 | Not established as render-only |
| 825C2130 (E6C4) | Calls selected object vtable +24, selection helpers and 825C2A98 | Not established as render-only |
| 8258E778 (E6C8) | Multi-pass orchestration, render list consumption, clears/resolves, workers and unrelated mode/UI helpers | Candidate parent only; cannot repeat whole function |
| 8262D0F8 (E6D4) | Waits via 8262D038; locks, saves selected entries, clears 8192 bytes, rebuilds 512 slots and toggles selector at +18484 | Destructive queue rollover; exclude |
| E6F8–E754 loop | Visits 33 containers (stride 128), gets each element via 82492000, writes pointed object +160 and +196 | Mutates objects; semantics unresolved |
| 824894C0 (E768) | Locks; resets a global and 25 records of stride 20 | Queue retirement; exclude |

## Narrower CPU representation located

`8258C5F0` loads a 64-byte matrix from global base formed by
`lis -31844; addi -16144`, offset +224. It transforms positions at record +16
and writes depth-sort keys at record +0, then sorts pointer ranges. The queue
base is formed by `lis -31779; addi -30848`.

It calls `827571F8(queue, 1)` before resetting queue cursors through
`82757028` or clamping them through `827570C0`.

`827571F8` itself walks these six pointer ranges in order:

| Start pointer at queue offset | End pointer at queue offset |
|---|---|
| 116 | 24 |
| 120 | 40 |
| 124 | 56 |
| 136 | 104 |
| 128 | 72 |
| 132 | 88 |

Each entry points to a record with callback at +4, payload pointer at +8,
and 16-bit flags at +12. If flags bit 0 is set, it invokes
`callback(1, payload)`. It has no direct non-stack stores, but the callbacks
are arbitrary code: absence of stores in this dispatcher does not prove purity.

This is the smallest **located candidate for CPU render-list consumption**.
It is not a complete frame renderer: it needs pass setup, camera publication,
all relevant lists, final resolve and presentation. Payload ownership, pose
pointer layout, callback targets and callback writes remain unresolved.

`8258C408` is not harmless setup: it calls `82754BF0` (conditional state flips
and clears), resets lists, and invokes `8275AE18`, which may call virtual
destructors. `8258C868` performs conditional resolve/target changes. These must
be reconstructed from an explicit pass contract, not copied wholesale.

## Next proof steps

1. Observe callback targets and payload identities immediately before normal
   `827571F8` consumption. Resolve these functions in generated code, including
   indirect descendants; timing-only hooks cannot establish their semantics.
2. Trace the model callback's pose/world-matrix reads backwards to their writers.
   Identify owned CPU render data and frame lifetime. Copy only proven render
   fields into separately owned storage; never interpolate live world state.
3. Prove a single native scene can be rendered from that representation with
   exact current transforms, unchanged simulation counts, and matching output.
4. Establish pass setup/teardown and resource lifetime across two renders, then
   add interpolation. Match objects by persistent identity, not draw ordinal.
5. Validate presentation spacing separately. Two RefreshGuestOutput calls are
   not evidence of two displayed frames; measure/display image serials and times.

The read-only `scripts/inspect-guest-function.ps1 -Address ... -Assembly` helper
extracts bounded functions from generated sources for continuing this audit.

## Log 059: verified render delegates and frame-owned capture

The run exits normally. The final old world-matrix record reports 774144 samples
and 119560 changes. Those are **not adjacent-frame motion counts**: the old cache
compared repeated addresses, including multiple passes and potential reuse.

| Function | Verified behavior | Consequence |
|---|---|---|
| 8275AAB0 | Reads inline 64-byte matrices at object +16/+80; publishes selectors 3/4 via 82740E08; forwards child +4 virtual +8 | Two proven renderer inputs; not a proven skeleton palette |
| 8275AB70 | Same plus object +144, selector 5 | Three proven renderer inputs |
| 8273EAC8 | Uploads four constants at 50 + 4*index; for nonzero index also combines with cached matrix and uploads at 80 + 4*index, gated by renderer +2536 | Matrix-slot consumer; constant range alone does not establish bone semantics |
| 8248EC10 / 8254CC58 / 8254CCF8 | Uses inline object +16 matrix, combines with supplied base +64, publishes selector 2 and constants 21/25/29 | Matrix/render-state setup, not an identified animation updater |
| 825AB510 / 82551240 | Selects resource via state +28, binds texture descriptors, submits 826FA358; wrapper conditionally XOR-toggles state +28 | Repeating wrapper advances a resource selector |
| 825AB5F0 / 82551528 | Calls resource virtual methods with state +8; binds descriptors and submits 826FA358; wrapper increments state +8 modulo state +24 | Repeating wrapper advances a cursor; do not call it animation time without further evidence |
| 825B3E10 | Invokes callback at self +8 with material/self +12 before forwarding | Another unresolved indirect edge |
| 82613C80 / 82620E20 | Lazy shader creation and 82627348 setup using self +8-derived resource structures; forwards child +4 virtual +16 | Resource/pass setup, not proven replay-safe |
| 825D6818 / 826090A8 / 8261F428 / 8275ADA0 | Shader/render-state setup then child virtual +16 | Follow the chain; stopping at first delegate misses descendants |
| 824B5F48 | Large shader/texture/draw path; writes a cached texture field at pointed state +12516 | Not pure; full resource lifetime remains unresolved |

Implementation now holds two bounded **host-only** transform endpoints inside
the normal 8258E500 root scope. It records world matrices and the verified inline
selector 3/4/5 inputs. Repeated identical inputs collapse; conflicting values
within one root frame mark that key ambiguous. Missing endpoints, overflow and
changed address/type/source/model/selector tuples cannot form a comparison.
Every original function still executes once; no guest data is replaced.

This is intentionally observational: address tuples are NOT allocation-generation
identities, selectors are NOT pass identities, and thread-local capture excludes
worker callbacks outside the root scope. No interpolation API is exposed yet.
The new log marker is `DOAX2 transform history v2`; each report describes only
the latest root frame, not totals for the preceding 120 frames.

Host tests cover duplicate/conflicting samples, adjacent changes, missing frames,
model mismatch, overflow invalidation, abandoned capture and copy independence.
Build/run target: `doax2_render_history_test` (no title launch required).

Next implementation boundary remains blocked on character pose/vertex-resource
producer lifetime and pass contracts. Trace 82551240/82551528 resource producers,
824B5F48's character candidate path, and the full delegate chains before selecting
a replacement render evaluator. Do not request repeated FPS-only tests: the
current build cannot produce additional frames. A future runtime capture should
answer explicit ownership or per-frame ambiguity questions.

## CPU instance producer split (continued after log 059)

The texture-backed paths have a concrete CPU representation, but are **not yet
identified as character skeletons**. Do not expand this discovery to all animation.

| Stage | Double-buffer family | Triple-buffer family |
|---|---|---|
| Append matrix + optional scalar | 825AD8F0; matrix copied to *(owner+8)+64*count; optional f1 to *(owner+12)+4*count; increments owner+132+4*slot | 825DA020, count at owner+188+4*slot |
| Count getter (+40 virtual) | 825AD898 | 825D9FC8 |
| Pack/upload | 825ABAE0, write slot at owner+48 | 825AC548, write slot at owner+28 |
| CPU upload destination | *(owner+36+4*writeSlot) | *(owner+68+4*writeSlot) |
| Draw selection | 82551240, state=owner+16; state+28 read slot | 82551528, state=owner+16; state+8 read slot |
| Advancing draw wrapper | 825AB510 | 825AB5F0 |
| Non-advancing sibling | 825AB590 | 825AB678 |

The uploaders transpose each 4x4 input and overwrite output word 12 (byte +48)
with `1 - scalar`, or zero when the optional scalar array is absent. Constant
1.0 was checked against image word 82063DE0 = 3F800000. This extra channel means
these bytes cannot be blindly treated as ordinary affine bone matrices.

825AB868 establishes two render resource descriptors, backed by allocated data,
and allocates the input array at 64*capacity. 825ABAE0 calls 82782550 around the
packing loop and XOR-toggles the write slot. 825AC548 similarly wraps its write
slot modulo state+24. The read and write selectors are separate.

825ABC88/825ABED8 construct 12-byte delegates from 8275A6E0, with delegate+8
pointing at owner+16. They query owner virtual+40 and install table 823BFAFC.
825AC118 installs the non-advancing 823BFB20 table. 825AC708/825AC978 install
823BFB44 (ring advance). 823BFB68 points at non-advancing 825AB678. Allocation
through this title allocator is transient; a delegate pointer is not persistent
instance identity. The non-advancing siblings still invoke optional setup
callbacks and shared draw helpers. They are NOT a proven safe second frame.

`doax2_render_packet.h` now implements host-only packing; the normal uploaders
are wrapped with a read-only equivalence verifier. It takes owned input copies
before the original call and compares packed words with the original output
afterwards. Exact known count getters only; at most 128 records per call;
unsupported kinds, capacities, slots and address ranges skip explicitly.
Every original uploader runs once. No guest replacement, interpolation,
extra resolve, extra draw or simulation tick has been introduced.

Runtime acceptance for this step: `DOAX2 instance pack check` must report
nonzero checked records and zero mismatched records in representative scenes.
All-skipped output is inconclusive, not success. Mismatches require checking
layout/float semantics before any substitution. Host tests verify transpose,
the overwritten scalar channel, default scalar and input-copy independence;
they do not establish runtime equivalence or stable per-instance identity.

Next: resolve the append callers' source identities, distinguish scene instances
from skeleton/particle data, establish ownership through the draw passes, and
test exact-copy substitution only for a fully identified packet. A safe complete
second-frame evaluator and independent presentation pacing remain unimplemented.

## Log 060 acceptance and combined capture

Latest cumulative reports grouped by (runtime thread ID, uploader) contain
706587 checked records, zero mismatches. This is a lower bound on completed
checks because reporting is periodic, not a shutdown flush. The packing
equivalence step succeeds for the captured, supported layouts only.

| Thread | Uploader | Last reported checked records |
|---|---|---|
| 2164 | 825ABAE0 | 168510 |
| 2864 | 825ABAE0 | 0 |
| 5208 | 825ABAE0 | 272226 |
| 13432 | 825AC548 | 132096 |
| 2164 | 825AC548 | 555 |
| 5208 | 825AC548 | 133200 |

The additional worker threads prove that root-thread-local transform history
does not cover all instance uploads. Root frame 5160's report contains 103
unique transforms, 2 repeats, no ambiguous/dropped samples, and 68 adjacent
address tuples (28 changed). That is not a whole-scene pose history.

Observed upload return addresses resolve to producer functions:
825A4774 -> 825A4550; 825B07AC -> 825B0050;
825B9178 -> 825B8AD8; 825F31FC -> 825F3110.
These are mixed producers, not newly approved render-only entry points.

v1.3.12 observes all four producer scopes, both append functions (825AD8F0,
825DA020), both uploaders and both draw helpers (82551240, 82551528).
The shared mutex-protected summary groups by function/caller/enclosing producer/
host thread tag, not object identity. It keeps total counts and latest raw entry
arguments r3/r4/r5 and return r3. Upload events instead use owner/destination/
checked-count and mismatch-count; presentation events have zero args/result.
An append return of 1 means accepted; raw returns for other functions must not
be interpreted as a Boolean success value. Producer tags identify lexical hook
nesting only and are not propagated to newly spawned workers.

Root epochs are atomic observational labels, NOT worker completion fences or
simulation tick IDs. Thread tags are host thread-id hashes, not log native IDs.
Addresses in summaries are integers only and are never dereferenced later.
The summary is bounded to 256 distinct edges; overflow drops events and reports
the count. Reports every 120 root calls are cumulative snapshots with latest
argument samples, not a lossless event timeline. Recent trailing events may
not be printed before shutdown. Collection can perturb performance; this mode
must not be used to judge final frame pacing.

Host tests cover aggregation, last-sample replacement, overflow reporting and
concurrent writers in addition to transform and packing tests. No new replay,
guest stores or additional virtual calls are introduced by these observers.
This combined mode reduces serial user tests; it cannot answer arbitrary future
questions, unvisited scene behavior, or allocation-generation ownership.

## Log 061: scene-entry fatal and source-path distinction

The terminal failure is `Call to invalid or unregistered function at guest
address 0x825D36F8` on worker t20052. It is not a reported buffer overflow or
packing mismatch. Original image table word 823C0398 points at this address.
Original instructions at 825D36F8..825D3708 are:
`cmplwi cr6,r4,3; bnelr cr6; lis r11,-32029; addi r3,r11,8512;
b 0x825B0050`. This is a valid conditional callback, previously undiscovered.

Added explicit discovery in doax2_manifest.toml, rebuilt, and verified generated
registration for 825D36F8. All aligned callback words in 823C0360..823C03A0
now resolve to generated functions. This fixes the known dispatch omission;
it does not prove every scene is stable. The log alone does not establish
whether instrumentation scheduling exposed the path sooner than earlier runs.

The last pre-crash combined summary has 11 edges, zero dropped events. It links
825A4550 -> 825DA020 (return site 825A4714) -> 825AC548; root draw helper
82551528 is reached from 825AB638. Separately, 825B0050 -> 825AD8F0
(825B06EC) -> 825ABAE0 is seen with 76-record batches. Raw return C0015A01
from a draw helper is not a reported error: no Boolean/status return contract
has been established there.

Further static tracing: 825A4550 traverses 12-byte group records; each group
contains a pointer to 20-byte source records and another parameter pointer.
825A3DA0(output, group, index) builds an output matrix from that indexed record
and group parameters (rotation/position math, including 82417A58/829ECD18).
The caller passes the resulting stack matrix into virtual +52, which reaches
the append path. Do not identify this as the skeleton palette: no bone hierarchy
or skinning link has been established. 825B0050 likewise builds a stack matrix
before append and partitions output into 140-byte instance owners.

Progress checkpoint: timing/mixed-frame boundaries mapped; render-instance
packing validated; selected producer/consumer chains identified. Character
pose identity/lifetime, complete isolated render pass construction, and actual
60 Hz presentation remain unresolved. No quantitative completion percentage
or claim that rendering is almost fully reverse-engineered is justified.

## Log 062: normal exit and hierarchy update/submit separation

Run ends with normal shutdown. Latest cumulative packing reports per
(thread, uploader) total 2125775 records, zero mismatches. No terminal fatal
is reported. The missing-callback fix held for this run, not every possible path.

The trace now confirms non-advancing draw siblings in actual execution:
82551240 from 825AB5D4 (825AB590), and 82551528 from 825AB6BC (825AB678).
That still does not make their resource setup or a whole frame replay-safe.

Hierarchy producer 825B8AD8 is observed from 825E6880 and 825E6B18. Its phases
are statically separable but the whole producer remains mixed:

* 825B7C08 updates node+0 vector by adding node+16, scales node+16 by a
  coefficient through node+44, then subtracts a restoring term. It subsequently
  builds the node+64 matrix. This is fixed-step, spring-like state advancement,
  not just pose evaluation; do not call it at doubled cadence.
* 825B87B8 recursively calls that updater over children: count node+36,
  child array node+136, node stride 144, passing parent matrix at +64.
* After updates, 825B8AD8 resets instance owners via virtual +44, then calls
  825B8810 for root nodes, then publishes via owner virtual +12.
* 825B8810 reads the already evaluated matrix at node+64, routes it to
  ownerBase + 140*node[+40] via virtual +52 with scalar 1.0, and traverses
  children. It has no direct spring advancement; the append mutates the render
  instance array/counters. Full descendant/resource lifetime is not established.

Also inspected 825F3110: it calls 825F22D0 before submitting each 176-byte
record, so it too is not a render-only candidate by association with a packer.

v1.3.14 implements `SubmissionPacket<512>` with owned 64-byte matrices and
numeric node/target labels, populated at the existing 825B8810 calls inside
each 825B8AD8 scope. No extra guest traversal, getter, update or submission is
executed. It rejects duplicate nodes, overflow and nested capture. Host tests
cover copy independence, closed capture, duplicates, overflow and nesting.
Each thread retains only its latest packet until the next Begin overwrites it;
there is no cross-frame history or renderer consuming these packets yet.

Acceptance: representative nonempty packets must be complete, with zero
rejections. Empty packets do not prove coverage. Captured labels are NOT
allocation-generation identities. This hierarchy could be scene/secondary
motion data; the evidence does not yet identify a character skeleton or cover
all animated objects. Next work is proving safe instance-owner submission from
these copies, identifying resource/pass ownership and classifying remaining
character pose consumers—not replaying the mixed producer or GPU stream.

## Log 063: hierarchy absent; follow the active producer statically

The v1.3.14 startup marker is present and the run exits normally. Latest
per-thread/uploader reports total 3907055 checked records, zero mismatches.
There are no hierarchy packet reports and no 825B8AD8 edge. This run does NOT
validate hierarchy packet capture. The linker map contains both new hooks;
the observed producer coverage differs from log 062. Treat this as an unvisited
path, not proof of packet failure or a request to repeat the same run blindly.

825B0050 and 825A4550 remain active; 825DAD18 is an additional ring append/upload
caller (return sites 825DB114/825DB160). Both non-advancing draw siblings are
again present. They still do not establish resource lifetime for a second frame.

Static image words at manager addresses 82E52D20, 82E52D54 and 82E32140 are
823BFBF0. This is initial-image evidence, not proof that the runtime never
changes the table. The table resolves:

| Virtual offset | Function | Verified role |
|---|---|---|
| +8 | 825AFF68 | Initializes owner state, resets manager+28, sets manager+48 active, dispatches virtual +16 |
| +12 | 825B0028 | If active, adds a global float delta to manager+28; explicit phase advancement |
| +16 | 825B0050 | Evaluates/batches instances; not approved for repeated full execution |
| +28 | 825AF158 | Evaluates with phase manager+28 + index * parameter[+28], calls 825B0E70, combines with indexed 96-byte source record |

825B0050 reads arrays through manager+8 and manager+12 with DIFFERENT strides:
96 and 80 bytes respectively. Therefore they must not be assumed to be a
previous/current pose pair just because two pointers exist. The producer's
virtual +28 result is used in further transform construction before append.

This locates another concrete advancement/evaluation split for the active
instance family: 825B0028 advances its phase; 825AF158 consumes it. It does NOT
prove either function controls character animation or global simulation time.
Next static work: classify 825B0E70 and the remaining 825AF158 side effects,
then trace the 96/80-byte records to their writers. No additional FPS-only test
or duplicate hierarchy run is required for those questions. A new runtime
probe should be requested only if the initial vtable assumption, source
identity or lifetime cannot be settled statically.

## Isolated procedural component implementation (v1.3.15)

825B0E70(output=r3, parameters=r4, phase=f1) initializes a 4x4 identity
matrix and writes one varying element at output+24 (word 6). It reads four
float parameters at +12/+16/+20/+24 and writes output/stack only. The formula is
`0.5*(sin(2*p12*phase)+1)*p20 + 0.5*(sin(2*p16*phase)+1)*p24`, with
single-precision intermediates and final fused multiply-add. No phase write
occurs here. The original constant words for 2.0 and 0.5 were read from image
821CE4A4 and 820BD90C, respectively.

Its only non-prologue helper is 829ECE48. This helper reads a static math table,
uses stack temporaries, reduces by pi (1/pi=0.3183098861837907), then evaluates
a sine polynomial beginning with -1/6 and 1/120. It has an exceptional-result
branch at magnitude 220000000. This classification is backed by instructions
and table contents, not just an inferred function name.

`EvaluateProceduralMatrix` is a host-only component that accepts copied phase
and parameters. It neither reads guest memory nor advances simulation. Native
sin is used, so bit-identical output is not claimed. Nonfinite inputs/results
and angles outside the title helper's ordinary domain fail closed.

The normal 825B0E70 hook copies four input floats, evaluates into a host array,
calls the original once, and compares all 16 original output floats. It never
replaces those outputs. Acceptance is nonzero samples with zero mismatches at
absolute tolerance `1e-5*(1+abs(original))`. NaN/Inf originals count as mismatch.
Reports carry caller/parameter addresses, phase, skipped count and maximum finite
absolute error. Host tests cover identity slots, phase-zero amplitudes, a
nonzero phase, zero amplitudes and nonfinite-input rejection.

This implements one isolated procedural render component. It does not yet
implement all of 825AF158 (which also combines source-record matrices), prove
source lifetimes, identify character skeleton animation, or submit a second
normal ReXGlue frame. Those remain necessary before a 60 FPS claim.

## Log 064 and host composition evaluator (v1.3.16)

Run exits normally. Reported procedural checks have no mismatches and zero
maximum finite error; the root-thread latest report alone contains 172032
checked samples. Nonzero phases are present. Original-image parameter blocks
82B78F04 and 82B78E60 contain frequencies .015/.028 and amplitudes .12/.045
at +12..+24, with per-index phase stride 30 at +28. Thus initial-image evidence
does not indicate an identity-only test. Runtime parameter mutation is not
excluded by reading initial data.

The surrounding 825AF158 builds phase with a single-precision fused operation
`manager.phase + float(index)*parameters.stride`. It evaluates S at stack+96,
constructs inverse(B) at stack+176 through the cofactor/reciprocal matrix
helpers, copies B to stack+240, then multiplies inverse(B) * (S * B).
B is the first 64 bytes of manager[+8] + 96*index. It writes its output and
stack; this does not establish purity of the containing 825B0050 producer.

`EvaluateInstanceMatrix` implements this composition on host-owned values.
It uses double-precision pivoted inversion and multiplication, so bit equality
with PPC vector arithmetic is NOT expected. Bad pivots/nonfinite values reject
the evaluation; no replacement matrix is synthesized as a fallback.
The normal 825AF158 hook copies only the inputs consumed by the original,
calls it once, and compares its 16 outputs using `1e-4*(1+abs(original))`.
No original output, phase or source matrix is overwritten by the observer.
Local tests exercise phase indexing, translation conjugation, identity from
zero amplitudes, input independence and singular rejection.

Runtime acceptance is pending. Passing this comparison would validate one
full instance evaluator, not source lifetime, character skinning coverage,
frame-resource isolation, or independently paced 60 Hz presentation.

## Log 065 and guarded consumption test (v1.3.17)

Run exits normally. The reported full-evaluator comparisons show zero
mismatches within the declared tolerance. The last root report contains
139264 checked samples and max finite absolute error 2.9161572e-05.
Additional callers 825B0860/825B0BA8 are observed; their outputs are NOT
authorized for substitution merely because the evaluator agrees numerically.

The new opt-in `doax2_host_instance_test` substitutes only when caller is
825B016C, the enclosing traced producer is 825B0050, and the output exactly
matches entry stack+720 (with checked address arithmetic). That stack temporary
is explicitly constructed by the audited call site. Host evaluation must be
supported and the original-output comparison must pass. All other cases retain
the original result. Tests cover each gate and overflow rejection.

The original evaluator still executes once to preserve its PPC context side
effects and provide the comparison oracle. Then the host matrix's 16 words
replace only that caller stack temporary. No source matrix, manager phase,
guest queue, resource selector or timestep is patched. This is consumption
validation, not eliminating the original evaluator's cost or adding a frame.

Launcher mode 61 enables both existing capture and this new opt-in switch;
mode 60 remains observation-only and mode 30 original. Existing mode selection
is preserved; users must explicitly select `Host evaluator test (30 FPS)`.
Runtime acceptance requires nonzero substituted count, no new mismatches or
visual/speed regressions. A successful test still does not establish character
pose coverage, independent pass ownership, or 60 Hz presentation.

## Log 066: scoped substitution accepted; pass contract next

Latest per-thread counters report 53537 substitutions (1 + 288 + 53248),
with zero evaluator mismatches. 61152 retained outputs are excluded call sites,
not failed substitutions. The user reports no graphical issues or crashes and
no unusual speed at the low test framerate. This accepts only the narrow
same-phase matrix-consumption test, not simulation correctness at 60 Hz.

Pass audit: 8258C868 flushes when render-target object +24 is set and clears
that flag, may call 82754E68, and changes color/depth bindings. 82754E68 chooses
82708CC0 or 826F90B0 based on target+16; if target+20 is set, it clears +20
and flips +16. Thus pass teardown consumes mutable target state. Repeating the
whole routine is not a resource-isolated frame. 8258E0D8 also performs title
counter/cache updates after present; it is not simply a host Present call.

v1.3.18 adds a thread-local ordered CPU trace scoped to each 8258E778 call.
It records entry r3/r4/r5 and return-site caller for 82703438, 82703768,
82708CC0, 826F90B0 and 82754E68. It counts normal 826FA358 invocations between
boundaries. Arguments remain numeric labels, never retained dereferenceable
resource references. The original function executes once at every hook.

128 boundary events are retained per render invocation; excess events count as
dropped. Reports sample every 120 render invocations plus the first with draws.
The trace covers only calls on the same thread inside the scoped renderer.
Inherited bindings, worker calls outside that scope, other draw entrypoints,
resource generations, clears not routed through these functions, and GPU
completion are not covered. Do not equate the CPU call sequence with a complete
render graph or replay it. Unit tests cover inactive capture, chronology, draw
counts, capacity overflow and nested-scope incompleteness.

The next purpose is to connect normal target/resolve call sites to their
specific resource paths and establish allocation/retirement requirements.
Whole GPU-stream replay remains disabled and ruled out for live resources.

## Log 067: resolve contract and inline post-processing (v1.3.19)

Run exits normally. Last four sampled 8258E778 invocations contain 35
boundary events each, zero dropped events and 507/496/340/359 calls to
826FA358. The flat draw counter across late passes is NOT proof those passes
contain no draws: 8253EFE8 calls 8253FB18 and 8253F238 calls 82540010.
Both latter routines call 826F9E58 with primitive 13, count 4, stride 20,
fill 80 bytes of returned inline vertex space if non-null, then commit the
device cursor from +13076 to +40. They bypass the previously counted helper.
826F9E58 itself flushes dirty device state and modifies device bookkeeping;
it is not a pure host vertex generator.

The two containing passes bind incoming r5 as color surface 0, disable depth,
and resolve into incoming r4 (preserved in r28, forwarded as r6) at return
sites 8253F228 and 8253F478. They then call 8273C720 to restore bindings.
Log 067 labels their bound color surfaces 400FDA40 and 400FDA80 in the
latest sampled frame. The destination texture identities were NOT recorded
by v1.3.18; source-surface labels are not destination-texture labels.

826F90B0 selects source from flags r4 low three bits: 4 selects depth at
device+12184; other values index device+12168+4*slot. Destination is r6:
the original reads texture metadata at +28/+32/+36/+40/+48 and writes
destination +8 (and conditionally +12) during submission bookkeeping.
Thus repeating the resolve consumes/mutates live resource state independently
of simulation. This supports retaining the resource-isolation requirement.
The implementation also takes stack arguments; its complete API contract
and allocation generations have NOT yet been established.

v1.3.19 records numeric r3-r10 on the existing boundary events and counts
826F9E58 entries separately. It adds no guest pointer dereferences or extra
original calls. Unused registers can contain stale values; only audited
arguments have meaning. Inline begin counts do not establish allocation
success, final commit, GPU execution, or coverage of every drawing route.
Tests cover copied extended register values, separate chronology and reset.

Next runtime question: which destination texture labels are used by these
specific resolves, and how many inline begins occur between bindings?
Then trace their allocation/retirement and texture consumption statically.
No second-frame execution is enabled by this diagnostic. Character pose
coverage and independently owned full-frame resources remain unresolved.

## Log 068: allocation and consumer chain; no repeat test needed

Normal termination. Offline parsing finds 43 sampled invocations, each with
the expected number of logged boundary records and no reported drops.
This validates log completeness only, not completeness of GPU coverage.
Final sample: 364 geometry-helper calls, 26 inline begins, 35 boundaries.
The final post passes advance the inline count 22 -> 23 -> 24, resolving:

| Return site | Observed bound surface | Destination texture header |
| --- | --- | --- |
| 8253F228 | 400FDA40 | 834562F4 |
| 8253F478 | 400FDA80 | 83456328 |

Both fixed header labels occur in all 43 samples. Eight samples ALSO use
8253F228 with destination 83441974 and 8253F478 with 83441460. These shared
helpers serve multiple consumers: hooking either helper unconditionally is
not sufficient to isolate the final post-processing chain.

Static initializer 8253EC50 explains the final pair without another probe:

- r31 = 834561F4; header addresses r31+256 and +308 are exactly 834562F4
  and 83456328. A third header lives at r31+360 = 8345635C.
- Publishes header pointers through globals 83984A58 / 83984A5C (and the
  third through 83984A54). Initialization is skipped if 83984A58 is nonzero.
- Creates two surfaces via 824DC2C8 -> 826F6CA0, output pointer slots
  834562F0 and 83456390. Dimensions are W/4 by H, then W/4 by H/4,
  using integer shifts; format word 18280186. Headers are separate from
  surface objects and are not pixel-storage copies.
- Resets scratch arena used-byte count at 834561F4 and cursor/base fields
  at +4/+8 from a title-global pointer, then initializes three texture
  headers via 8253D470. Calling this initializer again would reset shared
  allocation state, not create an independently owned frame context.
- 8253D470 calls 8286EDA8 for header layout/size, checks cumulative size
  against 00E80000 bytes, rebases via 8286EE80, then advances shared arena
  count and cursor when input descriptor +12 is zero. All three calls here
  set it to zero. A copied header would still reference live backing memory.
- 8286EE80 dispatches by resource type; texture path delegates address
  rebasing to 8286EE18. This is another reason not to memcpy descriptors
  and assume isolation.

Consumer 8253EE10 uses current renderer-selected source texture, passes it
through 8253EFE8 into 83984A58, then uses that as the input to 8253F238
with destination 83984A5C. Other callers use these helpers too. Subsequent
reads of 83984A5C include 824DDC34's texture-binding path; this output must
remain associated with its corresponding frame, not swapped globally.

Added scripts/analyze-pass-contract.ps1 to reconstruct the observed
binding-to-resolve edges from v1.3.19 logs offline. It keeps unknown inherited
bindings unknown, separates device/slot keys, resets binding knowledge at
each sampled invocation, and rejects incomplete/out-of-order lists. Self
tests cover destination extraction, depth source flags, chronology and
sample boundaries. It does not replay anything or touch the running game.

Implementation constraint now established for this chain: a second context
needs its own texture backing ranges, texture headers, render surfaces and
frame-specific consumer routing. Merely allocating headers or calling the
existing lazy initializer is unsafe. This chain is post-processing, not
character-pose evaluation; isolating it alone would not produce interpolated
60 Hz geometry. Full render boundary and pose coverage are still unresolved.
No new game executable or repeated log request is justified by this offline
step. Preserve the v1.3.19 runtime until a substantive runtime change is ready.

## v1.3.20: unresolved render-input provenance

Return to the geometry-side gap rather than extending post-processing audits
indefinitely. Log 068 observes 825B3E10 and 824B5F48 as render delegates.
825B3E10 unconditionally loads self+8 as callback and self+12 as payload,
calls callback(material,payload), tests its boolean result, then conditionally
forwards child+4 virtual+16. The callback identity is absent from existing
type logs. It cannot be resolved solely from the wrapper vtable.

Static cross-check: 8248EB68 only changes render state before forwarding;
826028B0 binds a cached shader before forwarding. 825C3B40 publishes two
matrices from self+8 through 8273EAC8, indices 0/1; this still does not
establish a skeleton palette. Do not use these routines as animation clocks.

New targeted instrumentation records the actual 825B3E10 callback address,
payload and material labels before the original call. It scopes constant
upload 826FA7D0 observations beneath that wrapper and 824B5F48, recording
start register, count and source address only. The separate bounded 128-edge
summary avoids consuming the existing producer summary's capacity. Thread
and caller are part of the key; counters are cumulative, source is latest.
Overflow is reported. No constant-buffer contents are read or modified.

IMPORTANT: the scope covers the entire 825B3E10 wrapper, including forwarding
after the callback returns. Its 'parent' callback label is a wrapper-context
label, NOT proof the callback directly issued a given upload. Use the logged
upload caller and static call graph to distinguish these. Nested scopes
restore the previous label. Other workers and direct inline constant stores
are not covered. Absence of upload events does not exclude CPU skinning.

Next test purpose is actual callback resolution and upload provenance in
relax/minigame, not framerate or interpolation validation. Original routines
execute exactly once; no new frame, resource replay or timestep change.
The 60 FPS implementation remains incomplete and character coverage unproven.

## Log 069 and v1.3.21: close the narrow-probe coverage gap

Run terminates normally. Last provenance report has 284996 events, four
edges, no drops: 71249 wrapper entries and 71249 of each of three uploads.
825B3E10's observed callback is 8254C040. It replaces incoming r3 with a
global address and calls 8254C1E8, preserving incoming r4 (payload).

8254C1E8 reads a matrix at input r3+64; if r4 is nonzero it also reads the
64-byte matrix there and composes the two via vector dot products. At
renderer flag +2748, the composed result is copied to cache +1088 and
uploaded at constant 21; cache +1216 is uploaded at constant 25. The base
matrix is always uploaded at constant 29 from a stack copy. Thus the exact
three logged call sites 8254C3D0/8254C3EC/8254C42C are explained. This does
not identify bones or animation time. No other upload helper edges were
observed in the two instrumented wrapper scopes, which does NOT exclude
direct device writes, vertex streams, or deformation elsewhere.

Do not prolong testing this callback as a skeleton candidate. v1.3.21 adds
a separate 256-edge input summary for normal calls inside the thread-local
8258E778 scope: 826FA7D0 start/count/source/device and 82703AD0 binding
selector/buffer/device. The constant start or raw binding selector is part
of the key, so distinct ranges at one call site are not collapsed together.
Latest count/source are still samples, not a record of every historical value.
Counters are cumulative, overflow is explicit, no source data is dereferenced.

82703AD0 consumes r5's resource descriptor +24/+28 and writes a device fetch
descriptor. Its r4 selector is transformed using (95-r4)/3 and the remainder;
the log's 'slot' is the RAW argument, not an asserted semantic vertex-stream
number. This new observer reads registers only, not those descriptor fields.
Every original function runs once. Worker calls outside scope, inline device
writes and other binding helpers remain uncovered; this is provenance, not
a complete frame snapshot. Existing host tests pass and both binaries build.

Next decision: use the newly covered caller/range and resource-binding paths
to locate deformation inputs, or establish evidence for CPU vertex production
if no palette upload appears. Do not infer skinning from high constants alone.

## Log 070: deformation recipe producer/consumer linked (v1.3.22)

Normal exit. Last geometry summary: 2530935 events, 31 edges, zero drops.
56292 calls each at 824B68C0/D4/E8/FC and 824B6910 bind five resources;
last slots 3/4 share F84D2ABC. This is not proof of previous/current buffers.
82740E08 delegates selectors to 8273EAC8, explaining constants 84/88/92
without establishing a skeletal palette.

Concrete CPU-to-render link: layout==7 branch of 824B5F48 gets resource array
from *(material+12), then its +0. Entry zero is base; four more 16-byte entries
are selected by four words of a 48-byte recipe at 833B4330+48*self[+16].
Each resource entry's +8 supplies the descriptor to 82703AD0. The recipe's
two float4s at +16/+32 are written DIRECTLY to device constants 49/105
(device+2576/+3472). Thus upload-helper tracing could not see these weights.

8248A348 produces that recipe from per-slot state at 833A10D0+384*slot:
x=+224, z=+232, y0=+4, y1=+116. It selects indices x<0?4:1,
z>0?2:3, and y<0?5:6 for each y. Each weight vector is
[1-(abs(x)+abs(z)+abs(y)), abs(x), abs(z), abs(y)], without clamping.
The original also updates a separate seven-weight representation. It must
still execute normally; replacing only one output would not preserve all effects.

8248A640 advances accumulator state+372, calls 82489D68 for whole steps,
interpolates three vectors from state+32 toward state+16 with that fraction
(blocks stride112), writes state+0, and calls 8248A348. This already separates
advancing state from a final blend recipe, but the published table is live
shared state. Do NOT interpolate into it. 8248A530 initializes default recipes.
824B78E0/824B7990 also consume the recipes to blend CPU float4 inputs; the
table is not exclusively GPU-owned. Full character/skeletal scope is unproven.

v1.3.22 implements the final recipe on copied host floats. A bounded slot0..3
observer of normal 8248A348 compares four indices and eight weights after
the original executes once. Nonfinite inputs skip, nonfinite outputs mismatch;
weight tolerance 1e-6*(1+abs(original)). No output replacement or new frame.
Host tests cover indices, both weight sets, negative residual weights,
signed-zero handling and nonfinite rejection. Runtime agreement is pending.
This provides a concrete recipe for future host-owned deformation evaluation,
not proof that independently rendered full frames or skeletal poses are solved.

## Log 071 accepted; host-only deformation interpolation implementation

Final report: 1920 checks, zero skips/mismatches, zero reported maximum error;
normal shutdown. Observed indices change during the run. This accepts the
recipe calculation for the exercised inputs, not runtime ownership or coverage.

Implemented InterpolateDeformationRecipe: interpolate signed x/z/y0/y1, then
re-evaluate resource indices and weights. Interpolating the four raw resource
indices or weights in isolation would mix different target meanings when an
input changes sign. Endpoints require the same nonzero lifetime generation
and consecutive simulation frame IDs; overflow, gaps, invalid alpha or
nonfinite input fail without modifying output. No runtime code currently
supplies these generations; an actor slot/address is NOT accepted as proof.

Implemented a host float4 consumer matching the arithmetic structure of
824B78E0/824B7990: base plus three selected vectors, using weight bank0/1
and the bank-specific third index. This allows local testing of the recipe's
effect on numerical vertex data. It does not call either guest routine and
does not execute a shader or replace vertex buffers.

Tests cover exact endpoints, simultaneous sign crossings, a 101-position
sweep, both banks, invalid indices, missing/reused identity, frame gaps,
frame-ID overflow, nonfinite values and extrapolation rejection. The sweep
checks homogeneous-coordinate preservation; it is not an in-game visual test.

These APIs remain host-only and unconnected to live rendering. A runtime
consumer requires established model/resource lifetime generations and a
separately owned render frame. The normal 30 Hz simulation, live deformation
table and executable behavior are unchanged. No repeated user capture is
needed for this local implementation step; retain v1.3.22 for now.

## v1.3.23: transient wrapper lifetime boundary established statically

824B7130 allocates 64 bytes through 8275A6E0, sets vtable 822A7B24, stores
incoming r4 at wrapper+16 (deformation slot), r6 at +8 and r8 at +36,
and appends it to the incoming r3 list (tail at list+4). It does not return
the wrapper as a conventional factory result. Observed static callers are
824908E0 return sites 82491844/82491854/82491880. That caller derives the
slot from byte +3 of a 152-byte title actor record, not wrapper identity.

8275A6E0 allocates from global arena at 83DC880C with a 16-byte linked
allocation header. 8275AE18 walks allocations, invokes virtual destructors,
and resets the arena cursor and last-allocation pointer. 8258E778 calls this
retirement routine within rendering. Therefore wrapper addresses must never
be carried across retirement as stable model identities.

8248A530 is called from 824B0F50, 824B1198, 824B3630, 8248D6F0 and
824900xx's slot3 path. It resets deformation but is not by itself a complete
model load/unload observer. No persistent generation is inferred from it.

Implemented bounded same-thread TransientRegistry with monotonically assigned
local allocation tickets, retirement invalidation, duplicate-address rejection
and whole-registry rejection on overflow. Tickets remain distinct after address
reuse but are NOT accepted by the persistent deformation interpolation API.
The constructor observer reads only the tail field written by the original;
normal 824B5F48 entries count matched/missing wrappers. Retirement invalidates
before and after the original destructor pass. All original calls run once.
This observer never authorizes drawing or retains dereferenceable resources.

Tests cover creation, address reuse after retirement, unknown lookup, null,
duplicate address and overflow. Next runtime test checks actual constructor/
consumer/retirement coverage; misses or rejections must not be patched around.
Remaining: persistent model/backing-resource lifetime, cross-thread ownership,
and independent frame resources. A matching ticket alone is not replay safety.

## Log 072 accepted; v1.3.24 normal-draw consumption comparison

Normal window close/title termination. Last ownership report: 147254 created,
zero rejected, 308764 matched wrappers, zero missing, 22394 retirements.
Latest deformation report: 2040 checks, zero skips/mismatches/error. Multiple
wrapper calls per allocation are expected; these are not presented-frame counts.

New draw packet is scoped to one normal 824B5F48 invocation. It requires
an observed current allocation, slot<4, geometry layout7, and matching
wrapper/table selection indices. It evaluates from copied signed inputs.
Reset/default-table disagreements are skipped instead of synthesized away.
Bindings through 82703AD0 fill five numeric resource labels; null or conflicting
rebinding invalidates the packet. Nested wrapper scopes restore prior packets.

At exact return-site caller 824B695C of 826FA358, recheck the allocation
ticket, require all five bindings, then compare eight device-cache floats at
+2576/+3472 (constants49/105) with host weights. These offsets are backed by
the audited direct stores. Tolerance is 1e-6*(1+abs(actual)); nonfinite values
reject. No guest write, buffer readback, replay, new frame or output substitution.
This closes the producer-to-normal-draw weight comparison, if runtime counts
are nonzero and mismatch-free. Numeric binding labels do not prove resource
ownership, shader semantics, allocation generations, or GPU execution.

Tests exercise missing bindings, matching/mismatching tickets, identical and
conflicting rebinds, weight mismatch and NaN. Runtime validation pending.
This check still does not implement the independently rendered second frame.

## Log 073 accepted; v1.3.25 same-phase host consumption

Normal termination. Last counters: 86760 prepared/checked, zero mismatches,
348199 skipped wrappers, 31 incomplete draw checks. No constructor rejections
or unmatched wrappers. Skips/incomplete calls are NOT authorized by passing
counts elsewhere; their exact reason distribution is not established here.

Opt-in doax2_host_deformation_test / launcher mode62 supplies the already
validated host eight weights to exact normal draw caller 824B695C. All existing
guards and live-cache comparison must pass. It does not change any resource
indices, buffers, simulation inputs, recipe table or cadence. Both original
and host calculation still run; this is not a performance optimization.

Cache words for constants49/105 are saved, replaced from the host packet,
and marked dirty by ORing bits51/37 of device+0 (the bits used in the audited
824B5F48 stores). The original draw executes once. On normal return or C++
exception, restore eight words and OR the same dirty bits into the CURRENT
flags, preserving unrelated changes. This avoids silently leaving the GPU's
last constant state inconsistent with restored CPU cache values. Extra uploads
may occur, so this diagnostic is not suitable for performance measurement.

The supplied counter establishes the code path ran; differing-word count
distinguishes bit-identical input from small allowed numerical differences.
Neither proves 60 Hz animation, resource lifetime across frames, GPU completion,
or visual correctness. Pending test: explicitly select Host deformation draw
(30 FPS), relax/minigame and transitions, unchanged appearance/speed, no crash,
nonzero supplied and no new mismatches. Other modes leave this switch false.

## Log 074: same-phase consumption accepted; move to frame construction

Last reported counters: host mode enabled, 73499 supplied, 144 differing
words, zero draw-comparison mismatches. 285941 wrappers skipped and 201
incomplete checks remain excluded. Normal window close/title termination.
User separately reports no visual glitches or abnormalities. Accept the
narrow same-phase consumption path; do not request more identical captures.
This has NOT demonstrated a second frame or whole-character animation coverage.

Backend audit (sdk/rexglue-sdk/src/graphics/d3d12/command_processor.cpp):
BeginSubmission checks submission fences and frame-in-flight limits, resets
deferred command recording and binding caches, and calls the EXISTING render
target/texture/primitive caches. BeginFrame reclaims constant/descriptor pool
pages using frame completion derived from submitted fences. It does not
allocate private EDRAM, independent resolve destinations or cloned textures.
EndSubmission failure may leave a submission open because dropping commands
would leave resource state undefined. Treat uncertain submission as poisoned,
not as an unused frame bank that can simply be returned to a pool.

Added host-only FrameBanks state machine (not connected to the backend yet):
recording lease -> submitted fence -> reusable only once that queue's fence
completes. Lease generations reject stale cancellation/submission after bank
reuse. Regressing completion observations, non-increasing submission fences,
pool exhaustion and uncertain submission fail closed. Quarantine requires
resource-owner recreation, not local reuse. Cancellation is explicitly only
for work that has never been submitted. Caller must serialize access.

Tests cover overlapping banks, in-flight exhaustion, fence gating, stale
leases, cancellation, completion regression and quarantine. These are host
protocol tests with synthetic fence values, NOT native GPU synchronization
validation. The class neither allocates nor certifies isolation of resources.

Concrete next backend requirements:
1. Allocate independent mutable color/depth/resolve backing plus descriptors;
   a copied descriptor, a new command list, or a lease alone is not isolation.
2. Route each pass's reads AND writes to that bank, including downstream
   consumers of resolved outputs. Do not remap only final presentation.
3. Build fresh normal commands from an owned CPU render representation;
   no existing deferred-stream or mixed engine-loop replay.
4. Signal a queue fence covering all consumers including presentation before
   releasing the lease. Failure/uncertainty quarantines it.

CPU geometry/material coverage and persistent resource generations remain
unresolved alongside those backend tasks. This local step does not change
the game executable or justify a new user test package. v1.3.25 remains the
last runtime build; no FPS claim or second-frame switch has been added.

## Ordered ownership work / native allocation checkpoint

Follow the user's seven-step order and keep engine-loop uncap, deferred-stream
replay, and repeated same-phase substitution tests closed. Correction agreed:
log074 proves same-phase deformation, not an interpolated instance/character
frame. N-to-N+1 interpolation requires buffering until N+1 is available.

See 60fps-resource-ownership.md for the initial A/B/C/D/E resource inventory
and remaining gaps. The tested FrameBanks owner is now exercised against real
D3D12 allocations, queue submissions, fences and readbacks in a developer-only
WARP test. Separate banks keep distinct images; reuse waits for observed fence
completion; stale leases reject. Local test passes. No game runtime changes.
This connects allocation/reuse in the test path ONLY, not ReXGlue's live cache
path. Do not report step1 complete, a visible prototype, or 60 FPS from it.

## Live one-resource integration — v1.3.26

The next increment is now implemented in ReXGlue's actual IssueSwap callback,
not the standalone WARP utility. See `60fps-live-fxaa-bank.md` for the full
allocation -> binding -> submission -> fence -> reuse trace and acceptance gate.

Selected resource: the post-gamma/pre-FXAA host intermediate. Two independent
committed textures alternate through the original gamma/FXAA dispatches, with
existing one-use descriptors. Both resource owners retire only on successful
Signal of the real submission_fence_; reuse waits for observed completion.
Bounded before/after readbacks of the untouched bank test cross-bank isolation.
D3D12 validation errors and queue/command-list failures are diagnosed.

SDK Release/install and game/launcher builds succeeded; host tests passed with
the SAME FrameBanks header now shared by SDK and title tests. No new WARP run.
Mode63 enables this one live resource test and FXAA, disables older probes and
host substitution, and leaves normal simulation/single-frame behavior intact.
It intentionally serializes bank reuse. No second frame or FPS improvement.

User action pending: test relax + minigame/transitions, send log and visual
observations. This is a test package, not a passed live checkpoint. Do not
expand resource scope or mark step1 complete before that evidence returns.

Result: log075 contained stable distinct A/B allocations, 42 fence-completed
cross-bank byte comparisons with zero mismatch, and no ownership failures over
3240+ frames. The user reported normal visuals, relax/minigames and game speed.
The one-resource live FXAA ownership checkpoint is PASS. The D3D12 debug layer
was unavailable, so this pass does not claim debug-layer validation.

The next incremental implementation is documented in
`60fps-live-output-mailbox.md`: validation of the existing three-slot committed
presenter output mailbox and both its producer and paint ownership timelines.

Result: log076 and the user's visual report PASS. Three committed output slots
remained distinct; 49 producer-fence-completed cross-slot comparisons matched;
more than 20,000 paint-fence signals succeeded; no ownership, visual, transition
or timing regression occurred. Presentation output ownership is now proven.

The next observation-only checkpoint is `60fps-second-frame-census.md`. It
collects scene color/depth/EDRAM/resolve ownership and consecutive camera/pose/
material/fetch/draw-sequence coverage in one normal 30 Hz run. It will decide
whether minimal private scene banking is tractable or a classified title-side
render evaluation is required. No replay or second frame is enabled.
