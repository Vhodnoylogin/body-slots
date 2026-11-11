# One grip press inside a player region

A pouch mod can open its own contents preview when the left hand is inside its registered rim and the player presses grip. [GripRegionAction](../examples/grip_region_action.h) connects this one-shot UI action to HIGGS's optional compact grip proposal. Body Slots evaluates the geometry. HIGGS selects one provider for the press. The pouch checks whether the preview can open and opens it on the game thread.

## Connect

1. Register the pouch's region with `RegionsApi::UpsertRegion`. Keep the returned region ID and revision. Bind a `GripRegionTarget` to that region, its epoch and save generation, the provider/source names, identities and generations of the pouch anchor and left-hand probe, and the maximum permitted sample age. Use the same monotonic clock for the region's timestamps and the helper. [Interaction regions](interaction-regions.md)
2. At SKSE PostPostLoad, look up `HIGGS_GetGripActions001` in the loaded HIGGS DLL. Include the compact HIGGS SDK header when compiling this example. Pass the discovered table and a stable priority ID such as `pouch.inspect` to `Connect`. If this mechanic already uses the full Body Slots region-action route, pass `fullRouteActive=true` and keep that route. To switch modes later, remove and drain the old registration first. Missing compact support leaves the action unavailable through this helper. [Full region-action route](region-actions.md)
3. Use a writer dedicated to this pouch/hand example. `Publish` replaces the writer's complete set of observations. A mod handling several regions on one writer publishes them together. Keep the scratch array of `kMaxRegionObservations` rows on the consumer instance. Evaluation runs synchronously on the main thread inside `Update`.

```cpp
bool EvaluatePouch(void* context, RegionObservation& out) noexcept {
    auto& pouch = *static_cast<Pouch*>(context);
    RegionEvaluationFrame frame{};
    RegionEvaluation sample{};
    if (!pouch.SampleCurrentRimAndWrist(frame, sample)) return false;

    RegionsView view{};
    view.structSize = sizeof(view);
    view.version = kRegionsApiVersion;
    if (pouch.regions.Publish(pouch.writer, &frame, &sample, 1, &view) != Status::Ok)
        return false;
    if (pouch.regions.CopyObservations(pouch.writer, view.revision, pouch.scratch.data(),
                                      kMaxRegionObservations, &view) != Status::Ok)
        return false;

    for (std::uint32_t i = 0; i < view.count; ++i) {
        if (pouch.scratch[i].regionId == sample.regionId) {
            out = pouch.scratch[i];
            return pouch.CanInspectNow();
        }
    }
    return false;
}
```

`SampleCurrentRimAndWrist` is the pouch's provider integration. It fills the public frame and evaluation structures from the current wearable and wrist. The exact view revision prevents the copy from silently using a later publication. Reusing a previously returned row fails the fresh-frame check.

```cpp
// Called on the game thread. EvaluatePouch publishes fresh anchor and probe
// samples, copies the matching Body Slots row, and checks this pouch's UI state.
if (inspectPouch.Update(EvaluatePouch, &pouch)) {
    pouch.OpenContentsPreview();
}
```

The controller callbacks only inspect atomics and record a selected ticket. At most one press is pending. On the next update the helper checks the region, sources, current visit, expiry and gameplay eligibility again. Changing bags, leaving and re-entering, or losing the sample cancels the old ticket. Call `Invalidate` at menu, load, tracking-loss and equipment transitions. Resume after a new, valid observation. If the save, region revision, or a bound source generation changed, construct a new binding. `Disconnect` drains the provider before callback state can be destroyed. Lifecycle calls are on the main thread outside callbacks; the HIGGS DLL must stay loaded until disconnect finishes.

The returned pulse is for the immediate local preview. An inventory transfer, physical release, sustained hold or deferred mutation needs the appropriate provider's execution contract. This helper carries no HIGGS grant. A competing legacy raw-button hook is outside HIGGS's participant arbitration.
