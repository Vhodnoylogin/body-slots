# Transient equipment-provider instances

The additive `BodySlots_QueryInstanceInterface(1, sizeof(InstanceApi))` table creates logical instances of existing authored definitions. It advertises runtime instances and body-anchor binding only. It does not infer an equipment identity, verify that an item is worn, follow equipment model nodes, persist per-instance overrides or execute item interactions.

## Identity and ownership

The stable key is `(writer provider, equipmentKey, slotKey)`. The existing registered writer supplies its provider; the caller supplies canonical bounded equipment and slot identifiers. A caller must establish and restore equipment identity itself, including split, merge and reassignment rules. A definition provider/key supplies the template. Distinct bags can use one template without duplicating definitions.

The fixed copied ABI is in `include/body_slots_instance_api.h`: a 280-byte spec, 784-byte effective record, 48-byte view and 48-byte x64 table. Observers and customizers may read/evaluate instances, but only writers create, activate or remove their own instances. Mutation never widens authored hand/content policy or changes an anchor. Body-anchor binding is explicit; other binding kinds return `UnsupportedVersion` rather than silently using a body node.

`UpsertInstance` requires the exact nonzero save generation. Expected instance revision zero creates only; an existing key requires its exact revision. A no-op preserves revisions. A live key cannot change template keys or binding kind: remove/recreate explicitly for that transition. `RemoveInstance` requires the handle's owning writer and exact generation/revision. Handles use runtime domain 3, are never reused or serialized, and are distinct from definition slot IDs and native-bank owner leases.

## Effective snapshots

Each record retains requested activation independently of template readiness. Matching pinned/current authored revisions yield `Current` with copied effective placement and customization mask. Definition removal yields `Missing`; a different authored revision yields `Changed`. Both unavailable states clear the effective payload. Recreating the same template key does not reactivate the old binding; the equipment writer must explicitly upsert against the current authored revision.

Existing definition-scoped customization is inherited by every instance of that template. This is not per-instance customization. Inactive/effectively disabled instances retain identity and placement; eligibility is false. Eligibility also requires a current template and the existing single physical-hand/content-category checks.

`CopyEffectiveInstances` accepts zero expected scope/view values for latest reads, or exact nonzero values. Initialize the view's exact size/version and zero reserved field. Conflict or insufficient capacity updates only the header; record arrays are copied all-or-none. Other failures leave outputs untouched. `UpsertInstance` requires an initialized output record size and leaves output untouched on failure.

The view carries save generation, view revision, authored registry revision and configuration revision from one registry lock. Every actual authored/configuration revision change invalidates the instance view, including unrelated templates and an empty instance set. Instance mutations also invalidate it. Identical replacement/customization/upsert operations preserve their established no-op behavior. Instance revisions track explicit instance edits; inherited configuration and template changes do not masquerade as those edits.

## State and lifecycle

`InstanceStore` privately owns bounded entries/counters under the existing Registry mutex. Pure candidate builders preflight mutations, invalidation and reset before coupled definition/configuration writes; only its private commit publishes instance state. There is no second lock, definition bank, engine callback or allocator ownership in the public ABI.

Releasing a writer removes its runtime instances atomically with its authored definitions, including writers that own no definitions. Other owners' references to removed templates remain explicitly missing. Save-scope reset clears all instances and advances generation/view revision together with the existing configuration reset. Preload, revert, load (including empty/malformed saves) and new game invalidate old handles. Multiple callbacks may advance generation more than once; consumers compare the captured generation rather than assuming an increment count.

Saving does not write runtime instances or activation. Existing `BSCF` bytes, author packages and both global native compatibility banks remain unchanged. Only definition-wide customization is restored by BSCF. The separate [saved instance configuration service](instance-configuration.md) adds placement-only BSCI records and a joint two-record dispatcher without changing this runtime-instance API. Equipment providers recreate runtime activation in the destination scope; no automatic bag restoration is promised. A failed save-scope reset aborts loading new customization rather than applying it to old runtime instances.

## Verification boundary

`tools/build.py` runs the actual DLL instance fixture, canonical-key/authority/stale-output checks, distinct equipment identities, pinned-template removal/recreation, inherited effective configuration, coherent concurrent snapshots, capacity/release behavior and handle ABA rejection. The existing synthetic public-SKSE host checks instance invalidation through actual plugin callbacks, unchanged BSCF output and preservation of both native banks. Native policy tests reject instance-domain IDs at both native banks. These are offline contract checks, not Skyrim equipment or interaction qualification.
