# Saved placement configuration

Tags: body-slots, configuration, serialization, abi

The experimental configuration service keeps save-scoped player placement overrides
separate from provider-authored defaults. It does not create equipment instances,
move inventory, bind world/body objects, render models or implement input gestures.
Providers register their authored defaults again after a restart; saved overrides
reattach intentionally to the same `provider:key`. Missing providers leave dormant
overrides that remain saved until the player explicitly clears them.

## Additive contract

`BodySlots_QueryConfigurationInterface(1, sizeof(ConfigurationApi))` negotiates the
separate table in [body_slots_configuration_api.h](../include/body_slots_configuration_api.h).
An unsupported version or size returns null. The original definition API retains
its sizes and behavior: `Definition` 408 bytes, `SlotRecord` 424 and x64 `Api` 80.
Its copied definitions and eligibility results still describe authored defaults.

The x64 configuration table is 72 bytes. Its capabilities advertise effective
configuration snapshots and saved customization separately from the original table.
`EffectiveSlotRecord` is 440 bytes: `.slot.definition` is effective placement;
`.slot.definitionRevision` remains the authored revision; `.configurationRevision`
is the global effective snapshot revision. A VRIK adapter uses this table when
available and explicitly falls back to authored v1 snapshots otherwise.

`CopyEffectiveDefinitions` accepts an original writer/observer lease or a dedicated
customizer lease. Each returned record carries one coherent configuration revision.
Passing zero selects the latest snapshot. A stale revision returns `Conflict`;
insufficient capacity returns `BufferTooSmall`. Both return count/revision for retry
and leave record memory untouched. Definition mutations and override mutations
invalidate effective revisions under the same registry lock. Revisions are process
values and never restored from a save.

`RegisterCustomizer` supplies a separate declared customization authority, with at
most 32 unique bounded ASCII identities. An original observer or provider writer
does not thereby acquire customization rights; a customizer cannot call original
definition mutation functions. This establishes ownership between cooperative
plugins, not isolation from arbitrary native process code. No callback or supported
hot DLL unload protocol exists in these initial tables.

## Customization

`ApplyCustomization` requires a currently authored stable key, a customizer lease
and the exact nonzero configuration revision. It commits synchronously; callers
invoke it outside frame callbacks and use its committed copied record. A rejected
drag leaves the committed geometry unchanged. The record's field mask replaces the
previous override, allowing a field to return to its current authored default.
Only position, proper orthonormal rotation, explicit spherical access geometry and
enabled state are editable. Unselected fields must be zero. Struct sizes, masks,
reserved fields, canonical bounded strings and numerical/geometry limits are checked.

Anchor, reference rig, slot kind, scaling mode, physical-hand permissions and content
filters always come from the author. Overrides cannot widen authored content policy.
`EvaluateEffectiveEligibility` checks the effective enabled state with the unchanged
authored hand/content masks and requires the exact configuration revision.

`CopyCustomizations` includes dormant keys and marks whether an authored definition
is currently available. `ClearCustomization` can remove an existing dormant override
with the same expected-revision rule. Removing a provider/definition or releasing a
customizer never erases saved overrides. Recreating the same stable key recreates a
process slot ID while preserving player placement. VRIK still decides whether the
declared anchor/rig can bind to the actual body; an override cannot repair a missing
anchor. Full save-scoped instances and equipment identities are later services.

There is no per-instance lifecycle or enabled state, content-preview publication, slot reservation, inventory movement or transaction API in these tables. A placement reader can use this service now; full Body Pouches migration needs those later contracts. Offline visual authoring belongs to the separate Slots mode in Morphbench, while a future in-game customizer can use the revision-checked runtime configuration service.

## Save/load/revert

The plugin requires the public SKSE serialization interface version 4, registers
unique ID `BSLS` and writes one version-1 `BSCF` record. Its explicit little-endian
payload has a 16-byte magic/version/record-size/count header and at most 256
208-byte customization records (53,264 bytes total). Records contain stable provider
and key strings, field masks and local values only. Tokens, slot IDs, revisions,
callbacks, pointers, runtime bindings and equipment handles are never serialized.

Saving copies one coherent override set. Loading begins a new save scope, stages
the entire record batch and validates its version, exact lengths, record sizes,
duplicate keys, string bounds and geometry before one registry commit. A malformed,
unknown, duplicate or truncated record leaves author defaults effective and logs the
rejection. No valid prefix is published. An empty cosave leaves no overrides.
Revert, `PreLoadGame` and `NewGame` clear the previous save's override set and advance
configuration revision. Loaded unknown-provider keys remain dormant and are saved
again. Author packages and their registrations are not overwritten by a cosave.

## Verification

`tools/build.py` builds the standalone DLL and tests using public SDK headers, with
all output outside the repository. Native configuration tests exercise authority,
unchanged v1 records, all four edit fields, replacement/reset semantics, stale
revisions, snapshot atomicity, provider reattachment, save corruption/duplicate
validation, limits and concurrent author/configuration changes. A synthetic host
calls the production DLL's registered SKSE callbacks for save/load/revert,
preload/new-game and malformed batch isolation. These checks establish the native
contract; they do not establish deployment, body ergonomics or live game behavior.
