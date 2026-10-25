# Body Slots

Private initial extraction of declarative body-slot management from VRIK.

Body Slots owns definitions, stable keys, local placement and generic content policy. VRIK remains responsible for body binding, hand detection, interaction and presentation. Definitions are authored outside Skyrim; visual authoring belongs in a separate Slots mode in [Morphbench](https://github.com/Vhodnoylogin/morphbench), using its existing model and skeleton viewer.

This first development snapshot is not a released or game-tested mod. It contains newly written code; reconstructed VRIK code stays in its separate private repository.

The separate [configuration API](docs/configuration.md) stores save-scoped placement overrides without changing authored defaults or the original definition ABI. It exposes effective copied placement and eligibility, with provider/key persistence and revision-checked edits. Equipment instances, content previews and inventory transactions remain later services.

The additive [native14 placement compatibility service](docs/native-placement.md) owns the original fourteen raw local poses when VRIK explicitly attaches. It preserves their existing global INI persistence and does not mix them with per-save customization or generic registry identities. Native interaction/rendering and old equipment arrays remain in VRIK.

The separate [native14 policy service](docs/native-policy.md) owns per-slot visibility, physical-hand restrictions, six legacy weapon-class filters and hover-haptic preferences. Public copied observations are read-only; the compatibility owner commits complete revision-checked banks. Both native services retain their existing persistence semantics and remain distinct from generic slot instances.

The only maintained architecture concept and its [implementation ledger](https://github.com/Vhodnoylogin/body-pouches/blob/main/docs/trinity-synergy/implementation-status.md) are in [Body Pouches](https://github.com/Vhodnoylogin/body-pouches/tree/main/docs/trinity-synergy). This repository documents its own implemented contracts.
