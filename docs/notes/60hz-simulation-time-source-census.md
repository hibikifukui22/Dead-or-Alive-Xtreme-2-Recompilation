# DOAX2 simulation timing-source census

This is a bounded side investigation. It does not change the update cadence,
any timestep, a phase, a counter, or a render submission.

## Static result before runtime census

`sub_8258E000` publishes four values at 834A2C08..834A2C14: two measured
value/rate pairs. Log 085 shows the rate members behaving plausibly (channel A
near 60 Hz and channel B near the gameplay cadence), but the paired values are
far too large to label as seconds. Their units remain unresolved. A scan of
direct memory operands in the generated title
code found only the writes in `sub_8258E000`; no direct title consumer of those
four locations was found. Indirect pointer access is not excluded, so they
must not currently be called a centralized game timestep.

A different float at 82E2DD44 has thirteen direct static readers. It is a real
shared advancement input. Representative consumers include:

- 825A4550: advances keyframe/interpolation records before instance matrix
  construction and upload;
- 825B0028: adds the value to a procedural-instance phase;
- 825E04C8: advances a scene/camera-like transform phase before matrix work;
- 82605298: advances multiple wrapped oscillator/effect phases.

This shared value is not universal:

- 825B7C08 performs the proven spring-like hierarchy update by per-call vector
  additions, damping and restoring terms, with no read of 82E2DD44;
- 8265E7B8 decrements multiple integer gameplay counters once per call and
  fires callbacks at zero, also without reading 82E2DD44;
- 82740E08 publishes camera/view matrices but is a consumer/publisher boundary,
  not evidence that the upstream camera simulation uses the shared float.

Log 085 establishes that 82E2DD44 is normally exactly `1.0`, so it is a shared
per-tick unit scalar rather than a delta expressed in seconds. Animation
keyframe advancement ran about once per rendered gameplay frame, while the
spring hierarchy ran roughly 112 nodes per frame and gameplay countdowns ran
once per frame without reading the shared scalar. The selected procedural and
effect probes were inactive in the tested scenes, so they remain unclassified
at runtime.

The architectural result is hybrid timing: a shared unit scalar feeds
several animation/effect-style systems, while proven physics-like and gameplay
timing paths remain fixed per update. Runtime counters in v1.3.36 will show
which representative paths are active in menus, relax scenes and minigames,
and whether 82E2DD44 numerically corresponds to either measured timing channel.
Even a match would not make a broad 60 Hz simulation conversion safe because
the independent fixed-step paths remain.
