# Interaction regions v1

`BodySlots_QueryRegionsInterface(1, sizeof(RegionsApi))` negotiates the additive copied-POD table in `include/body_slots_regions_api.h`. Existing registry, slot, runtime and zone tables keep their layouts. This is a bounded source implementation; it has not been qualified in Skyrim VR. Generic regions do not replace the existing slot runtime's activation or HIGGS action route.

The separate [region-action service](region-actions.md) adds HIGGS recognition and authorized dispatch to these owned definitions. It supplies actual HIGGS palm probes and accepts independent copied target anchors. `RegionsApi.Publish` remains the original strict synchronous observation contract; its table and frame semantics are unchanged. Spatial readiness alone never grants an action.

A region is a named logical volume owned by a live `Api::RegisterClient` writer. Its stable identity is `provider:key`; process-local region ids and revisions are never persistent identities. A region can describe a player, NPC, world or transient anchor, and it requires no inventory content, rendered holder or saved placement. This first slice has no region persistence or content/presentation subsystem.

The service owns definitions, copied spatial observations, hysteresis and dwell. HIGGS retains input, hand-use classification, arbitration, grants and physical actuation. VRIK supplies poses; PPB supplies anatomy, fitting and query evidence; PLANCK supplies physics/contact/hit evidence. These owners are architectural boundaries, not automatic adapters implemented by this API. Callers supply snapshots they already obtained through those providers' own contracts. The copied `provider/source/identity` declaration preserves the caller's stated provenance; it does not authenticate an external provider or turn a point test into a Havok contact, HIGGS grant, PPB surface query or factual gameplay event.

## Definitions and geometry

`UpsertRegion` creates only with `expectedRevision=0`. Updating requires its exact current revision. The region provider must match the existing registry writer's provider. `RemoveRegion` requires the same owner and exact nonzero revision. Updating/removing a definition drops its observations and dwell immediately. Releasing its writer removes the definitions and observations. Every public operation also prunes released writer tokens, so a new writer with the same provider cannot inherit the old token's region identity or dwell. A pending lease release temporarily denies that writer's publication; rows remain until its lease is actually retired, so a failed release cannot silently delete live definitions.

The shared `ZoneShape` supports sphere, capsule, oriented box and finite cylinder (`ZoneShapeKind::Cylinder=4`). A cylinder uses `center` and `end` as its cap centers and `radius` as its radial extent; coincident caps are invalid. Its caps are flat: a point just beyond a cap is outside even when it would be inside a capsule. Box axes are the anchor snapshot's axes. Shape coordinates, entry inset and exit outset are region-local Skyrim units; the anchor's positive uniform scale applies to them together. World coordinates declare `RegionUnits::SkyrimWorld`.

Transforms use row-major proper orthonormal rotation (determinant +1), translation and positive finite uniform scale. Geometry rejects reflections, skew, zero or negative scale, non-finite input and invalid shape fields. The evaluator transforms the world point into region-local coordinates and uses the shared signed distance function. All surfaces and hysteresis thresholds are closed.

For signed local distance `d`, a fresh entry requires `d <= -enterInset`. A continuously observed occupant remains inside while `d <= exitOutset`. Both distances are nonnegative and bounded at 1,000,000 local units. An inset may make a shape impossible to enter; the service does not silently shrink the inset.

## Complete synchronous publication

The trusted runtime selects the current nonzero epoch/save generation through `SetInteractionRegionsScope`. Reset/invalidation clears the scope and all copied observations/timers; callers cannot choose a new active scope through publication. Until the runtime establishes a scope, `Publish` returns `Unavailable`. Wrong epoch/save generations return `Conflict`. A scope change preserves live logical definitions and clears all observation history. Public calls also compare the accepted save with the registry's current save generation; a direct save reset clears the old scope before observations are copied or published. Delayed internal setters for an old save cannot reopen that scope or overwrite a current one. No timer crosses a save or epoch.

`Publish(writer, frame, evaluations, count, view)` publishes that writer's complete current set of region/target/probe pairs. The writer can publish only its own regions, with exact region revisions. One definition can have independent target and probe identities: a pair key is the region id plus each source's `provider/source/identity`. This supports several NPC targets and probes without deriving anatomy, physical hand state or provider ownership from geometry. Duplicate keys in a batch are invalid. Omitted pairs are removed; consumers treat absence as unavailable. Reintroducing an omitted pair starts fresh dwell. `count=0` clears this writer's observations. Other writers' observations remain.

Each anchor and point snapshot is copied, including source identity, generation, sample id, sample frame/time, epoch/save generation and unit declaration. Generation must change when a source identity is rebound or replaced. Each valid new sample must advance sample id and sample frame. Structural records and identity keys must be valid even when `valid=0`; this allows an explicitly missing sample to identify the pair whose dwell it invalidates. Invalid identity/record layouts reject the whole batch; snapshot availability/transform failures produce copied `Unavailable` observations. The table stores no caller pointers and invokes no callbacks.

For each observation:

- Anchor kind must match the definition. Both snapshots must be valid and declare the accepted scope and Skyrim world units.
- Anchor and probe sample frames must match, and cannot exceed evaluation frame. Neither sample time can be in the future. Time skew cannot exceed the definition's `maxSampleSkewSeconds`.
- Each sample's age cannot exceed `maxSampleAgeSeconds`. Reusing or regressing a sample id/frame in the same generation gives `RepeatedSample`.
- A changed source generation breaks dwell and applies fresh entry hysteresis. Missing, invalid, mismatched, incoherent, stale or repeated evidence yields `Unavailable` and zero dwell.

The supplied evaluation clock is finite, nonnegative monotonic seconds, shared with snapshot times. Each writer must advance evaluation frame strictly and never decrease evaluation time. A frame/time rollback returns `Conflict`, **drops that writer's existing observations/dwell**, and keeps its previous high-water mark. This documented safety invalidation is the exception to failure-without-mutation; the failed call does not write its output view. Recovery requires a frame/time past that high-water mark or a trusted scope reset.

Continuous dwell requires consecutive evaluation frames, consecutive anchor/probe sample frames, unchanged generations/revision, and an evaluation time gap no larger than `maxContinuityGapSeconds`. Any skipped frame or longer gap starts fresh entry and dwell. Repeated calls cannot advance dwell from a frozen sample. Configured sample age/continuity limits are in `(0,10]` seconds; skew is in `[0,maxAge]`; dwell is in `[0,3600]` seconds.

`Outside`, `Inside`, `Dwelling` and `Ready` are spatial observations:

| State | Meaning |
|---|---|
| Unavailable | Evidence for this evaluation cannot establish containment. |
| Outside | Valid point does not meet the applicable spatial threshold. |
| Inside | Fresh entry, with a positive dwell interval still pending. |
| Dwelling | Consecutive valid containment continues before the dwell threshold. |
| Ready | Continuous spatial dwell reached the threshold; zero dwell is immediately ready. |

`Ready` never means gameplay eligible, authorized, selected, physically contacting, granted or executed. A consumer still uses the authoritative HIGGS/input/action contracts and revalidates the applicable current observations before actuation.

## Copies, capacity and concurrency

The process holds at most 128 definitions and 256 observations total, shared across writers. A complete publication replaces only its owner's rows atomically; it fails with `LimitExceeded` if the resulting total would exceed capacity. Definitions and observations use fixed arrays. Public functions serialize their state and validate live registry leases. The first slice uses polling and has no callback lifetime or unsubscription problem.

`CopyRegions` and `CopyObservations` accept existing writer or observer leases and return atomic snapshots. `expectedViewRevision=0` requests latest; otherwise it must match exactly. `RegionsView` supplies a monotonically advancing revision/sequence, accepted scope and count. Conflict or insufficient capacity writes the current view but no partial record array, allowing a bounded retry. Other failures preserve outputs. A successful publication returns the total copied-observation count, not only that writer's batch count. Every changed definition/publication/invalidation advances the common revision/sequence; each observation records its publication's sequence.

`CopyObservations` returns **last evaluated evidence**. It has no clock argument, does not expire rows or accumulate dwell, and makes no claim that those samples remain current at copy time. Consumers inspect the copied evaluation/sample times, frame, scope, revision and provenance and obtain fresh provider evidence as needed. They cannot use a polled `Ready` row as a long-lived grant.

## Deliberate first-slice limits

Only externally supplied point probes against finite primitives are implemented. The optional [HIGGS native mouth/stash adapter](higgs-native-regions.md) supplies one concrete source integration, with a selected spatial detector and HIGGS's existing sole executor. General provider registration, surface queries, meshes, rays, sweeps, composite shapes, physical colliders, content models, presentation, saved customization and generic gameplay behavior routing remain pending. There is no engine-pointer ABI, game mutation, input polling, contact injection or duplicate hand classifier in this service. The existing player-slot system remains its own implemented action-offer integration path. Headless tests establish bounded core behavior and protocol invariants; native linking does not establish game/experience quality.
