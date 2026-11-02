# Slot geometry, equipment, content and editing

These additive tables implement slot services without changing the original definition, registry, runtime or native14 layouts. The independent game adapter uses current public game nodes and the new HIGGS input/grant services. It contains no reconstructed VRIK implementation. Full DLL linking and source/protocol tests are separate from game qualification.

## Shapes and point probes

`BodySlots_QueryZoneInterface(1, sizeof(ZoneApi))` supplies saved sphere, capsule and oriented-box access shapes. Coordinates are slot-local Skyrim units; the slot transform and positive uniform anchor scale apply. A sphere uses `center` and `radius`, a capsule uses `center` and `end` plus `radius`, and a box uses `center` and positive `halfExtents`. Fields that do not apply must be zero. Coincident capsule ends form a sphere. Boundaries are included.

Absent shape overrides retain the existing effective definition sphere. `SetShape` and `ClearShape` require the instance owner or a dedicated configuration customizer, with exact nonzero save/view revisions. Old `RuntimeRecord.regionCenter/radius` describe the definition sphere; use `ZoneRecord.shape` and `slotWorld` to reconstruct extended geometry. `insideHands` and actual action admission use the exact configured shape.

`QueryZones` tests one copied point against the entire coherent zone snapshot. Its explicit probe source can name virtual hand, resolved palm, controller, item anchor, weapon tip or another point supplied by a caller. Epoch/frame must match the sampled runtime. A probe query supplies spatial evidence only: it cannot publish tracking, change the interaction probe or authorize an action. Built-in actions continue to use the adapter's virtual-hand probes. Unavailable zones remain observable with `containsProbe=0`; a caller subscribing after entry can query them immediately.

## Wearable slot sets

`BodySlots_QueryEquipmentInterface` creates and binds up to 32 slots for one equipment key in one registry publication. Entries name current definition revisions and carry a required worn form plus body-node or exact equipment-node binding. Validation, identity, template, revision, capacity or late-entry failure publishes none of the set and leaves outputs unchanged. The existing owner-derived instance key remains provider/equipment/slot; the caller must validate particular-item identity, stack splitting, merging and transfer rules.

`SetSlotSetActive` changes the provider's matching slots atomically. Model/worn-form availability remains independent of requested activation. `RemoveSlotSet` retires live instances while preserving stable saved customization. Rebinding the same equipment and template keys creates fresh runtime handles and restores existing user binding overrides rather than replacing them with package defaults. Equivalent base forms do not establish equivalent item identities.

For an equippable potion bag, its provider supplies a persistent particular-bag key, publishes the opening/side-pocket slot set with the bag's worn-form gate, and declares the exact model nodes. Body Slots follows those nodes, checks both hands against the slot shapes and presents requested models. The provider retains potion selection and inventory ownership. A torch belt or smithing tool belt can use the same services with different content policies and behavior callbacks.

## Generic content policies

`BodySlots_QueryContentPolicyInterface` refines the definition's category mask with up to 16 base forms, 16 required keywords, 16 alternative keywords and 16 form lists. Base forms and form lists are alternatives; all required keywords must match, and a nonempty alternative-keyword set needs at least one match. Empty lists do not add restrictions. These filters apply to incoming items, so they cannot prevent an empty-hand withdrawal from a potion-only slot.

Game form and keyword/list checks run on the main post-HIGGS phase. The service publishes copied evidence tied to the exact HIGGS observation, slot configuration and physical hand. Recognizers perform no engine form queries. Missing dependencies produce unknown/refused incoming eligibility rather than permissive fallback. The selected executor is revalidated against current evidence before dispatch.

Filters, shape overrides, bindings and visuals share the bounded saved runtime store. BSRT v3 uses 1148-byte portable records and reads the earlier 816-byte v1 and 864-byte v2 forms. BSCF, BSCI and BSRT are still decoded, remapped and staged before one publication. Policy form IDs use SKSE resolution; an unresolved policy stays explicitly dormant until its provider/editor repairs or clears it. Runtime samples, grants, provisional drags, action handles and inventory are not serialized. Old runtime saved-copy calls expose only their binding/visual fields; new services expose their own saved fields.

## Separate held and game-equipped inputs

`BodySlots_QueryBehaviorInterface` registers source-aware behavior callbacks. `SourceActionContext` exposes two immutable item contexts: HIGGS-held and game-equipped. A proposal names one incoming source, or `None` for withdrawal/another action without insertion. The registry applies the category and content policy to that chosen source. A hand simultaneously holding a physical item and a drawn game item therefore does not lose valid actions merely because the other item's category differs.

The winning `SourceActionRequest` preserves the chosen source, reference/base/equipment identities and observed eligibility. HIGGS still grants the hand and input. The provider performs only its selected gameplay operation and reports the correlated result; Body Slots updates its content mirror from the validated receipt. Removal uses the existing draining `RuntimeApi.RemoveBehavior`. Original behavior callbacks remain supported with their original primary-item semantics.

## Authorized in-game dragging

`BodySlots_QueryManipulationInterface` exposes an explicit editing mode owned by one live dedicated configuration customizer. The user interface or configuration mod arms the desired physical hands and grip/trigger button with an expected mode revision; Body Slots owns recognition and manipulation. Editing mode does not read raw OpenVR input or grant itself a hand.

An empty eligible hand inside a slot offers one drag candidate to HIGGS. After HIGGS selects the Body Slots executor, the safe main phase captures the virtual-hand/slot/anchor transforms and reserves the slot against content actions. Rigid hand movement produces a provisional world transform; the holder/preview follows that transform while saved placement remains unchanged. The same consumed button release commits local position/rotation using the captured save/view revision. Existing access-region and enabled overrides are retained. No second action is submitted for release, and the original Down request/input identity identifies the outcome.

Changing/disabling editing mode, missing tracking or anchor/model, body/world/load replacement, a changed hand state, expired/revoked grant, changed configuration or explicit cancellation ends the provisional drag. A failed edit restores the committed presentation. API clients can copy the current provisional/committed/cancelled/rejected state. The grant is bounded to 60 seconds; a new gesture is needed afterward. There is no built-in MCM or headset menu in this service.

## Coverage and remaining boundaries

Ordinary zones, filtering, worn-equipment sets and dragging do not require PPB. Anatomical anchors require PPB to advertise actual player-target coverage and coherent lifecycle observations; the current public PPB contract supplies no such complete coverage. This implementation does not manufacture anatomical observations or infer a free hand from missing PPB data.

Native VRIK holsters remain separate and are not automatically replaced or migrated. Generic inventory policy, stock selection, original/proxy item semantics and assignment persistence remain with gameplay providers. These tables remove slot and input reconstruction from those providers; they do not implement the 20 consumer mods or qualify their assets, VR ergonomics or game behavior.
