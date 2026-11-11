# Body Slots

Body Slots development: independent slot management and interaction for Skyrim VR.

Body Slots owns definitions, stable keys, saved placement, body/equipment binding, zones, activation, content-provider dispatch and presentation. VRIK supplies the body and virtual-hand observations; HIGGS supplies authoritative hand states, input arbitration and granted physical operations. Offline visual authoring belongs in the Slots mode of [Morphbench](https://github.com/Vhodnoylogin/morphbench).

Body Slots recognizes named player, NPC, equipment, world and transient regions, with hysteresis, dwell and stale-source invalidation. A mod can offer an action using the actual HIGGS palm and resolved input in that region. HIGGS chooses the action and authorizes its executor. The winning binding can receive the matching release after its short grant expires. The native HIGGS mouth/stash detector retains its standalone fallback. [Interaction regions](docs/interaction-regions.md) · [Region actions and provider bindings](docs/region-actions.md)

For an NPC pocket, the owner can bind a PLANCK actor/body identity directly. Body Slots reads the moving body on the game main thread and compares its transform with the HIGGS palm. PPB can supply separate grouped evidence about a particular anatomical child. The SDK includes a complete binding and removal example. [NPC pocket](examples/npc_pocket_binding.cpp)

The [0.2.0-rc.3 developer candidate](docs/release-candidate.md) separates the native runtime and the mod-author SDK. Gameplay qualification is pending. The compact HIGGS grip example belongs to an earlier proposal and is not provided by the current full HIGGS contract.

The [runtime API](docs/runtime.md), [slot extensions](docs/runtime-extensions.md) and [independent game adapter](docs/game-adapter.md) implement sphere/capsule/box zones, atomic wearable slot sets, source-aware behavior/action contracts, saved form/keyword/form-list filters, HIGGS-authorized rigid dragging, correlated content receipts, removable callbacks and save-scoped customization. The native DLL builds without a changed VRIK DLL. Activation requires the additive HIGGS hand/routing API implemented in the cooperation fork; released HIGGS001 alone does not provide that service. Native/game qualification remains distinct from the production-core and protocol checks.

The additive [content-observation API](docs/content-observation.md) lets an instance owner publish its already observed logical assignment/stock for previews, with epoch/save/content revision checks and no invented inventory receipt. Unsettled action reservations survive local callback cancellation and bounded history pressure until a factual outcome resolves them.

The optional [ordinary inventory provider SDK helper](docs/ordinary-inventory-provider.md) composes those public slot contracts with HIGGS's shared native inventory executor. A consumer supplies selection/acceptance policy and content projection; it reuses asynchronous selection, transactions and actual grip/release receipts without implementing its own pickup or inventory mutation machinery.

Build the native adapter with `python tools/build_game.py --sdk-root <public-SKSE-source> --higgs-root <synergy-HIGGS-checkout> --output <external-directory>`. `tools/build.py` remains the headless foundation/SDK fixture. See the adapter page for dependency pins, provider registration, phases and remaining limits.

The historical [0.1.0 Nexus draft packaging](docs/release-draft.md) separates the unchanged runtime DLL from a mod-author SDK/example archive. Its label `0.1.0-alpha.1` does not change the existing plugin/ABI versions. Body Slots code, SDK and examples are available under the [MIT License](LICENSE). Third-party dependencies and assets retain their respective licenses.

The separate [configuration API](docs/configuration.md) stores save-scoped placement overrides without changing authored defaults or the original definition ABI. It exposes effective copied placement and eligibility, with provider/key persistence and revision-checked edits. This service remains definition-scoped; instance-specific placement uses the separate table below.

The additive [instance API](docs/instances.md) supports transient equipment-provider-owned logical instances of existing definitions, with independent activation, pinned template revisions and coherent copied effective placement. Equipment keys come from the caller. Separate runtime/extension tables provide equipment-node bindings, previews and set publication; content executors retain inventory operations and assignment persistence.

The separate [atomic activation table](docs/instance-activation.md) changes requested activation for a bounded set of one writer's existing instances in one revision-checked transaction. It permits deactivation of missing/changed templates without repinning them, preserves old ABI layouts and saved state, and does not infer worn equipment or presentation eligibility.

The separate [saved instance configuration API](docs/instance-configuration.md) stores stable-keyed position, rotation, access-region and enabled overrides, including dormant equipment keys. Its configured copies compose after definition-scoped overrides while old instance copies keep their inherited semantics. BSCI placement records and BSCF definition records load as one validated batch; runtime activation, handles and item assignments are not saved.

The additive [native14 placement compatibility service](docs/native-placement.md) owns the original fourteen raw local poses when VRIK explicitly attaches. It preserves their existing global INI persistence and does not mix them with per-save customization or generic registry identities. Native interaction/rendering and old equipment arrays remain in VRIK.

The separate [native14 policy service](docs/native-policy.md) owns per-slot visibility, physical-hand restrictions, six legacy weapon-class filters and hover-haptic preferences. Public copied observations are read-only; the compatibility owner commits complete revision-checked banks. Both native services retain their existing persistence semantics and remain distinct from generic slot instances.

The [source provenance note](docs/source-provenance.md) records the bounded history review before opening this repository. The original fourteen-slot compatibility banks are optional; reconstructed VRIK implementation remains separate.

The only maintained architecture concept and its [implementation ledger](https://github.com/Vhodnoylogin/body-pouches/blob/main/docs/trinity-synergy/implementation-status.md) are in [Body Pouches](https://github.com/Vhodnoylogin/body-pouches/tree/main/docs/trinity-synergy). This repository documents its own implemented contracts.
