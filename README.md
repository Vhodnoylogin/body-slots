# Body Slots

Private Body Slots development: independent slot management and interaction for Skyrim VR.

Body Slots owns definitions, stable keys, saved placement, body/equipment binding, zones, activation, content-provider dispatch and presentation. VRIK supplies the body and virtual-hand observations; HIGGS supplies authoritative hand states, input arbitration and granted physical operations. Offline visual authoring belongs in the Slots mode of [Morphbench](https://github.com/Vhodnoylogin/morphbench).

This first development snapshot is not a released or game-tested mod. It contains newly written code; reconstructed VRIK code stays in its separate private repository.

The new [runtime API](docs/runtime.md) and [independent game adapter](docs/game-adapter.md) implement spherical zones, versioned behavior/action contracts, correlated content receipts, removable callbacks and save-scoped binding/visual overrides. The native DLL builds without a changed VRIK DLL. Activation requires the additive HIGGS hand/routing API implemented in the cooperation fork; released HIGGS001 alone does not provide that service. Native/game qualification remains distinct from the production-core and protocol checks.

Build the native adapter with `python tools/build_game.py --sdk-root <public-SKSE-source> --higgs-root <synergy-HIGGS-checkout> --output <external-directory>`. `tools/build.py` remains the headless foundation/SDK fixture. See the adapter page for dependency pins, provider registration, phases and remaining limits.

The [unpublished Nexus draft packaging](docs/release-draft.md) separates the unchanged runtime DLL from a mod-author SDK/example archive. Its label `0.1.0-alpha.1` does not change the existing plugin/ABI versions. Licensing is to be finalized before publication; no license grant or public-source/visibility change is made by draft packaging.

The separate [configuration API](docs/configuration.md) stores save-scoped placement overrides without changing authored defaults or the original definition ABI. It exposes effective copied placement and eligibility, with provider/key persistence and revision-checked edits. This service remains definition-scoped; instance-specific placement uses the separate table below.

The additive [instance API](docs/instances.md) now supports transient equipment-provider-owned logical instances of existing definitions, with explicit body-anchor binding, independent activation, pinned template revisions and coherent copied effective placement. Equipment keys come from the caller; activation persistence, equipment-node binding, content previews and item transactions remain later services.

The separate [atomic activation table](docs/instance-activation.md) changes requested activation for a bounded set of one writer's existing instances in one revision-checked transaction. It permits deactivation of missing/changed templates without repinning them, preserves old ABI layouts and saved state, and does not infer worn equipment or presentation eligibility.

The separate [saved instance configuration API](docs/instance-configuration.md) stores stable-keyed position, rotation, access-region and enabled overrides, including dormant equipment keys. Its configured copies compose after definition-scoped overrides while old instance copies keep their inherited semantics. BSCI placement records and BSCF definition records load as one validated batch; runtime activation, handles and item assignments are not saved.

The additive [native14 placement compatibility service](docs/native-placement.md) owns the original fourteen raw local poses when VRIK explicitly attaches. It preserves their existing global INI persistence and does not mix them with per-save customization or generic registry identities. Native interaction/rendering and old equipment arrays remain in VRIK.

The separate [native14 policy service](docs/native-policy.md) owns per-slot visibility, physical-hand restrictions, six legacy weapon-class filters and hover-haptic preferences. Public copied observations are read-only; the compatibility owner commits complete revision-checked banks. Both native services retain their existing persistence semantics and remain distinct from generic slot instances.

The only maintained architecture concept and its [implementation ledger](https://github.com/Vhodnoylogin/body-pouches/blob/main/docs/trinity-synergy/implementation-status.md) are in [Body Pouches](https://github.com/Vhodnoylogin/body-pouches/tree/main/docs/trinity-synergy). This repository documents its own implemented contracts.
