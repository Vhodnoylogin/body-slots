# Saved instance placement configuration

`BodySlots_QueryInstanceConfigurationInterface` is a separately negotiated version-1 service for saved placement/enabled overrides keyed to a caller-declared equipment instance. The existing definition, configuration, runtime-instance and native compatibility ABI headers stay unchanged. It does not save runtime activation or infer equipment identities.

## Copied contract and authority

The table advertises configured-instance copies and saved instance customization. Its x64 size is 56 bytes. `InstanceCustomizationKey` is 320 bytes: owner provider, equipment key, slot key and template provider/key, each a canonical 64-byte identifier. `InstanceCustomization` is 400 bytes, `InstanceCustomizationRecord` 408 and `ConfiguredInstanceRecord` 792. The existing 48-byte `InstanceView` supplies the common scope/view and source revision stamps.

`ApplyCustomization` requires an existing dedicated customizer lease, exact nonzero save generation/view revision, and a matching runtime instance whose pinned template is Current. An inactive instance may be edited. Other writer/observer leases may copy/evaluate but cannot customize. `ClearCustomization` can explicitly remove an existing dormant key. Rejected edits preserve state and output. Identical edits preserve the view revision.

Only position, rigid rotation, spherical access region and enabled state are configurable, using the original customization masks and numerical limits. A submitted mask replaces the previous mask; unselected fields must be zero. Clearing a field from a replacement restores the current inherited value. Explicit selected fields are preserved even when equal to inheritance. Anchors, rig, hand/content policy and binding kind cannot be changed.

## Composition and old views

The configured result composes authored defaults, then definition-scoped overrides, then instance-scoped overrides. In `ConfiguredInstanceRecord`, `instance.effective` is this final result. `instance.customizationMask` remains the definition-scoped provenance; `instanceCustomizationMask` reports the second layer independently, including overlapping fields.

Old runtime-instance copies and eligibility retain their inherited definition-only behavior. Old definition/configuration views remain unchanged. New configured eligibility uses the final enabled state, requested activation and original hand/content policy. Enabling placement cannot activate an owner-inactive instance, restore a released instance, accept a changed/missing template or repair an unavailable anchor.

Configured records retain unavailable instance identities but publish zero effective placement and zero instance mask for Missing/Changed templates. Saved record copies include dormant keys. `available` means a matching Current runtime instance exists, even if inactive. Removing/releasing runtime ownership, removing providers or releasing a customizer never erases saved instance overrides.

All reads use one Registry lock. Zero expectations select latest; exact expectations reject stale views. Initialize the header's exact size/version and zero reserved field. Capacity/conflict failures update only the header; record arrays are copied all-or-none. The common instance view revision also advances when saved instance configuration changes, including dormant edits. Existing definition-configuration revisions keep their original meaning.

## Persistence

The existing `BSCF` version-1 encoding is unchanged. The additional `BSCI` version-1 payload uses explicit little-endian encoding: a 16-byte magic/version/record-size/count header followed by at most 256 portable 400-byte records (102,416 bytes maximum). Combined BSCF+BSCI payload is bounded to 155,680 bytes and at most one SKSE record of each type. Native structs are never dumped into the cosave. Handles, activation, process revisions, generations, callbacks and engine pointers are absent.

Saving captures both override stores coherently under the Registry mutex and encodes both before writing. If instance records exist, BSCI is written first and BSCF second. A failure stops further writes; no IO rollback is claimed. A complete retained pair is valid even if the host reported a failed write. With no instance records, saving emits the same BSCF-only shape as before.

Loading begins a fresh scope, clearing both override stores and runtime instances. A reset failure aborts before record reads. Records may arrive in either order. BSCI requires its BSCF partner; legacy BSCF-only and empty cosaves remain supported. Duplicate/unknown records, unsupported versions, invalid keys/geometry, duplicate compound keys, wrong lengths/counts or truncation reject the complete batch. No valid prefix is installed.

Both codecs validate into private candidates. The final joint Registry commit requires the exact scope/view captured by reset and preflights configuration/view limits before either store changes. A scope reset or legitimate concurrent edit during staging therefore prevents stale loaded data from overwriting the new state. Old provider/equipment keys remain dormant until their owner recreates a matching Current runtime instance. New game/revert clears both saved stores for the next scope; native global compatibility banks remain untouched.

Older Body Slots versions do not understand BSCI records. The compatibility promise is unchanged BSCF bytes and support for old saves in this service, not support for new instance saves in an older DLL.

## Verification boundary

The full offline build runs actual DLL API tests for inheritance precedence, both provenance masks, old-view/eligibility immutability, authorities, stale edits, dormant reattachment, capacity and pure portable codec rejection. The synthetic public-SKSE host exercises mixed-order and malformed batches, required partners, write failures, dormant resaving and scope changes during reading. These do not establish Skyrim equipment binding, an in-game customization UI or successful item interactions.
