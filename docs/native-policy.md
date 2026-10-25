# Native14 policy compatibility

The optional `BodySlots_QueryNativePolicyInterface` service owns the nine numeric configuration values for each original VRIK holster. Together with the [placement bank](native-placement.md), it transfers native14 per-slot configuration to Body Slots while retaining the old global INI persistence. Assigned/equipped items, suspension, actual rendered visibility, weapon-distance calculations, input and haptic execution remain with their existing runtime owners.

## Copied records

`include/body_slots_native_policy_api.h` defines version 1: a 72-byte row, 1024-byte fourteen-row snapshot and 48-byte x64 table. Rows use the declared field enumeration:

| Field | Legacy setting | Meaning retained by the runtime adapter |
|---|---|---|
| Visible | `visibleSlotN` | Presentation preference `value > 0.5`; hiding a slot does not itself disable its interactions |
| Hand | `handSlotN` | Existing `(int)(value + 0.5)` physical-hand restriction; separate from dominant-hand mapping |
| Small through Torch | Six `allow<Class>SlotN` values | Native weapon-class compatibility, preserving `!(value < 0.5)` and its unordered behavior |
| HoverHaptics | `hoverHapticsN` | Existing integer preference: 1 requests a hover pulse, 2 requests it for an empty/equipped hover, other values request none; execution/global haptic controls remain outside this bank |

Values are raw legacy doubles, including representations that a strict authored definition would reject. Do not merge six weapon classes into generic `Content::Weapon`, normalize a visibility preference into slot activation, or derive item acceptance from this table alone. The inline pure predicates cover only the named configuration rule; they do not reserve a hand or confirm an action.

## Ownership and lifetime

Acquisition imports one complete bank with initial revision zero, or changes nothing. A single owner may replace a complete candidate at the exact current revision. Unchanged bits preserve the revision. Releasing ownership invalidates the handle; subsequent owners receive fresh IDs/revisions. Mutation failures leave the bank and output unchanged. Buffers must hold the complete record and initialize exact size/version; owner and snapshot outputs must not overlap.

`CopySnapshot(0, output)` is public read-only introspection and retains no observer, callback or resource. A nonzero first argument additionally requires that exact still-live owner, which lets the VRIK adapter detect lost ownership. Without an active owner, public reads return `Unavailable` without changing output. Always check the status before interpreting a record: an unavailable service is distinct from a user-supplied NaN value.

Native placement and policy owner IDs use separate opaque token domains; the registry allocator reserves those domains. This rejects accidental wrong-service handles while preserving all existing table/record sizes. Counters are bounded and never wrap or reuse a live-process ID. These are cooperative in-process handles, not authentication. The two native banks have independent revisions, not a promised cross-service atomic snapshot. VRIK serializes its own configuration edits and frame sampling through a shared compatibility transaction.

Both bank types use the private `OwnedNativeBank` implementation: pure acquisition/replacement/release candidates and one locked `Commit`. There are no Skyrim objects or addresses in their state. The policy bank has no cosave, and generic customization load/revert/new-game callbacks do not reset it. VRIK keeps the existing INI projection in the original `IniSetting` objects so public001 and cached references retain their identity.

## Verification

The normal `tools/build.py` run includes actual-DLL policy, cross-domain, raw-bit, allocation-boundary and concurrent-copy checks, alongside unchanged placement/base/configuration suites. A synthetic public-SKSE host verifies that both native banks survive save/load/revert/new-game and that the existing customization record remains separate. Private VRIK differential fixtures compare original/current methods, exact INI output, base-pointer writes/restores, thresholds, names, failed commits and standalone availability gates. These checks do not establish Skyrim runtime acceptance.
