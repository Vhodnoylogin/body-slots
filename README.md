# Body Slots

Body Slots is an independently written Skyrim VR service for configurable body slots, wearable slot sets, interaction zones and content-provider dispatch.

**Development source and implemented API:** [feature/extract-body-slots](https://github.com/Vhodnoylogin/body-slots/tree/feature/extract-body-slots). This is the one active development branch; `main` is a landing page until a release is integrated. No finished or game-qualified release is advertised here.

HIGGS owns hand state, input and granted physical operations. VRIK supplies the body and virtual hand poses; it does not need a modified DLL for the independent adapter. Body Slots owns all slot mechanics. Activation currently requires the additive services in the [HIGGS cooperation fork](https://github.com/Vhodnoylogin/higgs/tree/development), rather than released HIGGS001 alone.

The maintained [English concept](https://github.com/Vhodnoylogin/body-pouches/blob/main/docs/trinity-synergy/concept-en.md), [Russian concept](https://github.com/Vhodnoylogin/body-pouches/blob/main/docs/trinity-synergy/concept-ru.md) and [implementation status](https://github.com/Vhodnoylogin/body-pouches/blob/main/docs/trinity-synergy/implementation-status.md) are in Body Pouches.

This repository contains newly written Body Slots code. Reconstructed VRIK implementation and assets are not published here. Compatibility with the original fourteen holsters is an optional, separately documented service; the independent slot runtime does not require it. Source visibility does not add a license grant; licensing remains to be finalized before distributing a release.
