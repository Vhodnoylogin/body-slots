# Independent Skyrim VR adapter

When the optional HIGGS Context v1 table is present, the adapter subscribes to its copied context generation. Its callback only publishes a monotonic atomic stamp. Old spatial input caches refuse new offers immediately; the next main frame cancels provisional drag and undispatched candidates even if the UI both opened and closed between frames. It retains already dispatched content operations for their real receipts/recovery. Older cooperation providers without this table retain the existing hand-availability/fence checks; they do not gain the new context-boundary behavior. No native VRIK interaction or public Body Slots table changes are introduced.

Body Slots owns slot binding, spatial detection, interaction dispatch and presentation. VRIK remains the body and virtual-pose owner. HIGGS owns hand state, grip/trigger input, arbitration, grants and physical execution. The [canonical concept](https://github.com/Vhodnoylogin/body-pouches/blob/main/docs/trinity-synergy/concept-en.md) is maintained only in Body Pouches; this page documents this repository's implemented adapter.

The adapter uses public game-node observations through CommonLibSSE-NG and the public HIGGS post-VRIK/post-HIGGS phase callback. No reconstructed VRIK implementation or changed VRIK DLL is used. VRIK's original holsters remain independent: this adapter does not silently disable them, import assignments or promise that unchanged native holsters participate in HIGGS's new broker.

## Dependencies and build

The native build requires the public SKSE VR 2.00.12 source headers, the HIGGS synergy checkout, MSVC and CMake. CommonLibSSE-NG v3.7.0, fmt 11.0.2, spdlog 1.15.0 and rapidcsv v8.99 are pinned CMake dependencies. Compatible Havok SDK headers are not needed to build Body Slots; they are still required by the original HIGGS/PLANCK native projects. This task does not extend CommonLib.

Run `python tools/build_game.py --sdk-root <SKSE-source-root> --higgs-root <HIGGS-checkout> --output <external-build-directory>`. An optional `--dependency-sources` directory reuses the four existing `*-src` checkouts. The manifest records source/header hashes, commands, the actual DLL and the offline qualification boundary. Build output must stay outside Git. The older `tools/build.py` command builds the foundation/SDK fixture without this engine adapter; its DLL must not be confused with the native game build.

Only the public HIGGS headers are imported into the build tree. Its complete include root contains other engine definitions and must not shadow CommonLib's `RE/` headers. The narrow compatibility declarations in `src/compat/` supply names required to compile the unmodified public 001 contract; they implement no engine behavior.

## Provider registration

The independent optional [HIGGS native mouth/stash connection](higgs-native-regions.md) registers its spatial callback before checking for the VRIK-backed player-slot integration described below. Its callback receives real current-phase HIGGS sources, owns only region geometry and returns to HIGGS's existing native executor. It does not require a VRIK DLL, subscribe a second input recognizer or implement another inventory consumer.

1. Register a definition writer and create an active instance through the existing registry/instance APIs. Author packages supply definitions; they do not invent equipment identity or automatically create a live slot for every installed bag.
2. Register the content provider's own client and executor with HIGGS. Read `BodySlots_QueryGameInterface` to obtain the Body Slots recognizer client. Explicitly authorize that recognizer for the provider executor using HIGGS's routing table.
3. Bind the instance to a body node, a worn-item-gated body node, or a node beneath the exact worn armor model. Register its immutable action-offer callback and selected execution callback with the provider's HIGGS client/executor identifiers. Hand states are forwarded from HIGGS; providers do not invent more occupancy states.
4. A HIGGS executor notification for a Body Slots candidate is a selection notification only. The provider waits for its Body Slots execution callback on the main update before changing gameplay state. Body Slots revalidates the actual slot, hand, geometry, configuration and grant first.
5. The assigned provider reports actual stages to HIGGS under its own client. It then reports the matching content receipt to Body Slots. The adapter verifies that HIGGS has acknowledged the corresponding stage before accepting a content change. A preview creates no inventory stock. Failure after selection never invokes another action for the same input.

The observer/runtime APIs expose zones independently of actions. An empty slot can offer an inventory-selection action; a held object or drawn game item can offer a different action through the same binding. The provider retains selection rules, item-instance properties, inventory mutation and assignment persistence.

## Observation and execution phases

The main update samples the named virtual-hand nodes and body/equipment anchors after HIGGS publishes its hand snapshot. The adapter retains the associated HIGGS observation ID, a distinct node sample ID, body/model generations and a monotonic sample age. A controller-thread recognizer uses only copied data. It submits no candidate if the input's HIGGS snapshot differs from the cached association or if that association is over 100 ms old. This age bound is a refusal policy, not a hardware timestamp or tracking-freshness guarantee.

HIGGS arbitrates before its default input processing. The selected candidate is retained for main-phase execution. The adapter polls the decision, confirms the assigned executor and current grant/occupancy, updates spatial geometry and dispatches the provider only while the action remains eligible. It cancels its own uncommitted contextual action when revalidation fails. HIGGS results are synchronized before configuration/zone updates so a physical or inventory commitment is not mistaken for an unstarted action; the content mirror changes only from the provider's correlated receipt.

Body roots and resolved equipment nodes are retained while used as identity cookies. Replacing a model changes its generation even when a later allocation could reuse an address. Equipment lookup is scoped beneath the matching worn armor's part clones; ambiguous nodes are unavailable. An unresolved saved form binding remains dormant. A missing node is not replaced with an invented origin.

## Presentation and persistence

Each visible holder/preview uses a distinct cloned model under a private wrapper. The adapter removes cloned collision objects; it never registers a second physics driver for the visual. Equipment-rendered holders are not duplicated. Visual/content revisions and availability control readiness, culling and replacement. A failed resource load waits for a changed visual/model revision before retrying.

Save-scoped binding, visual, shape and content-policy overrides use the `BSRT` record alongside existing `BSCF`/`BSCI` geometry overrides. The three stores are validated and committed as one registry batch. Equipment/policy form IDs pass through SKSE resolution. Runtime handles, grants, callback pointers, activation, inventory assignments and content versions are not serialized. Providers recreate instances under their durable equipment keys after loading.

## Qualification and remaining work

The native DLL links and its exported tables can be inspected offline. Production-core tests and a combined HIGGS/Body Slots protocol test exercise routing, deferred validation, committed-content authority, duplicate suppression and epoch invalidation. They do not qualify the visible hand-node choice, Skyrim scene updates, asset cloning, equipment lifecycle, hardware input or headset ergonomics. Current HIGGS source is required for activation; with the released HIGGS API alone the runtime remains without its new authoritative input service.

The [extension contracts](runtime-extensions.md) provide sphere/capsule/box geometry, separate point-probe queries, saved filters, atomic equipment sets, source-aware offers and authorized dragging. The runtime still has one behavior binding per instance and virtual-hand activation probes. A built-in editing menu, generic inventory handlers, PPB self-body anchors, multiple concurrent activation probes/providers and native14 assignment migration remain separate work. No public release or game acceptance is implied by this source delivery.
