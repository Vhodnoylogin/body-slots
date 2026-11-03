# Provider content observations

`include/body_slots_content_observation_api.h` adds an independent v1 table for the instance owner's transient logical assignment or stock projection. A provider can publish the stock it has already observed, including initial state after creating an instance, without inventing an action or a HIGGS transfer receipt. The service performs no inventory mutation, pickup, withdrawal, assignment selection or persistence. Physical actions continue through the existing behavior, HIGGS grant/executor and correlated content-commit contracts.

Resolve `BodySlots_QueryContentObservationInterface(1, sizeof(ContentObservationApi))` from the runtime DLL. The copied C++17 POD request is 56 bytes and the x64 table is 24 bytes. Capability bit 0 means owned, revision-checked logical projection. Unknown versions and table sizes return null. All existing API tables retain their layouts and versions.

## Publishing a domain observation

Copy the target `RuntimeRecord` and its `RuntimeView` through `RuntimeApi::CopyRuntime`. Retain the epoch, save generation, instance handle and content version when starting the provider's domain observation. Publish those exact values along with the resulting identity and quantity:

```cpp
body_slots::ContentObservation observation{};
observation.structSize = sizeof(observation);
observation.epoch = view.epoch;
observation.saveGeneration = view.saveGeneration;
observation.instanceHandle = row.configured.instance.instanceHandle;
observation.expectedContentVersion = row.contentVersion;
observation.contentIdentity = observedAssignment;
observation.quantity = observedStock;
std::uint64_t publishedVersion = 0;
const auto status = content->Publish(writer, &observation, &publishedVersion);
```

The runtime must have received a nonzero epoch from its game adapter. Ownership comes from the live writer lease that owns the instance; authoring its template, observing runtime state or holding a customization lease does not confer ownership. The identity is a provider-domain value. It does not establish a Skyrim reference identity, HIGGS grant or evidence of transfer. Empty content has both identity and quantity zero; nonempty content has both nonzero. `reserved` must be zero.

Publication compares the epoch, save generation, handle and expected content version with current runtime state. The initial version is zero. Changing identity or quantity advances it by one, increments runtime revision, invalidates existing offers and marks presentation pending with displayed content version zero. It emits one `RuntimeEventKind::ContentChanged` event through the existing runtime subscription service. Runtime content changes do not change the registry's configuration/view revision.

Equal identity and quantity with the current expected version succeed as a no-op: the returned version is unchanged, with no event, offer invalidation or presentation/revision change. Equal values with a stale expected version still fail. A failed publication leaves the output version untouched. On conflict, obtain a fresh domain observation against a fresh runtime snapshot; copying a new version onto an old asynchronous domain result can overwrite a later assignment.

## Actions, presentation and lifecycle

A changed projection is refused while that slot has a pending action, an unresolved HIGGS hand commitment awaiting its mirrored content receipt, or an active authorized drag. A local terminal action stage cannot bypass an unresolved hand commitment. A current-value no-op remains legal during those reservations. Callback failure, behavior removal and client removal do not prove that the assigned HIGGS action made no change. Their reservations survive local terminal stages and bounded history pressure. They block new offers, selected dispatch, drag and changed content publication until a factual noncommit, confirmed compensation or validated terminal provider receipt resolves the action. Missing HIGGS history and generic cancellation without outcome evidence remain uncertain. Late commitments are retained, and a validated late content mirror can finish a locally cancelled action without invoking its executor again. An observation does not complete or cancel an action, create action progress, install a content-commit validator, or authorize `ReportContentCommit`.

After a change, publish `SlotVisualSpec` with the returned content version and the appropriate preview model through `RuntimeApi::SetVisual`. Presentation reports for an earlier content version fail; presentation also requires a matching visual revision and visual content version. A preview creates no stock.

Content notifications run outside runtime locks. Callbacks may reenter copied reads or publish an exact current-value no-op. The existing removable-callback contract applies: removal from a callback returns conflict; remove after returning. `Unsubscribe` drains an already-running callback before it returns and suppresses future calls. Providers must avoid recursively publishing changed content from their own notification without a bounded domain policy.

Observations belong to a live instance in the current save scope. Removing and recreating the same durable key produces a new handle with zero content/version. A save-scope reset also removes live instances and their content projections. Body Slots does not serialize logical assignments or stock. The provider restores its own domain state and republishes a fresh projection after recreating instances. An epoch transition rejects delayed observations from the prior epoch; it does not itself erase the existing instance's logical projection.

## Results and offline checks

| Result | Meaning |
| --- | --- |
| `Ok` | Applied a changed projection or accepted an exact current-value no-op. |
| `InvalidArgument` | Missing pointers, nonexact request size, nonzero reserved field, zero epoch/save/handle, or inconsistent empty identity/quantity. |
| `Unauthorized` | Writer lease does not own the instance, or is absent/revoked/not a writer. |
| `NotFound` | A valid writer named a retired or unknown instance handle. |
| `Conflict` | Epoch, save or content version differs, or a changed projection meets an action/recovery/drag reservation. |
| `LimitExceeded` | Advancing an already maximal content version would overflow. |

`body_slots_content_observation_tests` links the actual production registry, runtime, serialization and manipulation cores. It checks negotiation, ownership, epoch/save/version conflicts, initial preview without receipt authority, no-ops, stale offers/presentation, reservations, removable/reentrant events, instance replacement and save reset. `tools/build_game.py` builds/runs this target with the existing suites; `tools/probe_game_dll.py` negotiates the new table from the actual DLL. These offline checks do not qualify native inventory operations or game, scene, pose or headset behavior.

