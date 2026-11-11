# Anchor an NPC pocket to a named physical bone

A pickpocketing mod can place its pocket at the NPC's pelvis. PLANCK supplies a copied physical pose. Body Slots evaluates the pocket and wrist. HIGGS arbitrates the grip that opens the mod's inventory preview. The consumer decides which NPCs are eligible and what to show. PPB can help select a more specific anatomical surface.

## Sample the pocket

1. At PostPostLoad, negotiate `PLANCK_QUERY_ACTOR_POSE` with PLANCK and check the version-2 table. Read the selected actor handle and exact bone name once. Keep the nonzero physical body generation.
2. Register an NPC region using `RegionsApi::UpsertRegion`. Obtain its current epoch and save generation from `CopyRegions`. For a zero-capacity query, both `Ok` and `BufferTooSmall` return the scope. A zero epoch or save means the runtime is unavailable.
3. Construct [PlanckPoseRegionAnchor](../examples/planck_pose_region_anchor.h) with that scope, actor handle, bone name, body generation, maximum age in nanoseconds and current Skyrim units per metre. On each SKSE main-thread task, call `Sample` before taking engine locks. Its copied NPC anchor goes directly into a `RegionEvaluation`. It needs only the ordinary Body Slots regions interface and the compact PLANCK header.

```cpp
RegionsView scope{sizeof(scope), kRegionsApiVersion};
auto result = regions.CopyRegions(writer, 0, nullptr, 0, &scope);
if (result != Status::Ok && result != Status::BufferTooSmall) return false;

RegionEvaluation sample{sizeof(sample), 0, regionId, regionRevision};
const auto poseStatus = pocket.Sample(planck, scope, sample.anchor);
```

## Use a grip once

Sample the virtual wrist in the same game update. Supply its own measured position, identity, generation, observation time and sample ID. The public regions interface requires anchor and wrist to have the same game-frame stamp. The pocket maps PLANCK's raw 32-bit frame to a nonzero 64-bit frame, starting at raw + 1 and extending across wrap. Check the wrist's native frame before using that extended value. A wrist sample from another update cannot be made current by relabeling it.

Publish the anchor and wrist together with `RegionsApi::Publish`, then immediately copy observations for the exact view revision. When the pose becomes unavailable, `Sample` returns the bound anchor identity with `valid=0`. Include that pair as unavailable, or omit it from the writer's next complete batch to remove it. Keep other pairs in that batch. If sampling, publication or the revision-checked read fails, cancel eligibility before offering a grip.

Use the copied row with [GripRegionAction](grip-region-action.md). HIGGS selects one provider for the press. On the next main-thread update, sample and evaluate again. Execute the preview only if the helper returns true and the target still qualifies. Body replacement, an expired sample or a save change interrupts the action. New body generations require a new binding. Repeated reads in one game frame retain the original timestamp. A tracked corpse can supply a pose; its eligibility belongs to the consumer.

The [test](../src/test/planck_pose_region_anchor_tests.cpp) builds the compact reader with `BODY_SLOTS_PLANCK_POSE_SDK`. With `BODY_SLOTS_HIGGS_GRIP_SDK` as well, it runs the pocket through the production Body Slots registry and region evaluator and both consumer helpers. The test supplies controlled provider tables, body and wrist observations. It covers one successful activation and invalidation between grip selection and execution. Use `ctest -R body_slots_planck_pose_region_anchor`. The [extended PLANCK example](planck-region-anchor.md) covers the separate client using the actor/body snapshot table.
