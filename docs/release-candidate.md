# Body Slots 0.2.0-rc.1 for mod authors

The runtime archive installs an SKSE plugin for Skyrim VR 1.4.15 and SKSE VR 2.0.12. It registers named interaction regions and slots. A consumer supplies the action, such as selecting a potion when the player reaches a pouch. The runtime has passed offline checks. In-game acceptance is pending.

Unpack the SDK into a development directory. Add `include/` and `third_party/higgs/` to your C++ include path. Use the optional `third_party/planck/` path for the actor anchor example. The SDK contains the 17 Body Slots headers exported by this runtime and provider headers pinned in `PROVENANCE.json`.

- Start with `examples/observe_definitions.cpp` for read-only discovery and `docs/region-actions.md` for registering an action offered through HIGGS.
- `examples/ordinary_inventory_provider.cpp` shows a consumer accepting or withdrawing ordinary items. The extended HIGGS hand, routing and inventory services are required for this path.
- `docs/planck-region-anchor.md` describes the optional PLANCK actor anchor example. This helper belongs to a consumer that supplies NPC targets.
- Run `python tools/author.py examples/potion-belt.json --output potion-belt.bslots` to create an example definition package. Creating a definition does not supply potion stock or a playable mod.

The runtime uses the HIGGS cooperation interfaces when available. Released HIGGS001 alone does not provide these services. The VRIK-backed player body-node path also needs VRIK. [HIGGS proposal](https://github.com/Vhodnoylogin/higgs/tree/64b334f287e8c55d37ad8530a4f7c1ec1eb03cfa) · [PLANCK proposal](https://github.com/adamhynek/activeragdoll/pull/8).

Body Slots code and examples use the MIT License in `LICENSE.txt`. The HIGGS and PLANCK headers retain their respective GPLv3 licenses. Third-party notices are in `licenses/`. The native build uses CommonLibSSE-NG, fmt, spdlog, rapidcsv and SKSE VR headers. Install SKSE VR from its [official distribution](https://skse.silverlock.org/).

To recreate the archives from a committed candidate checkout, run `python tools/package_candidate.py --native-build <verified-November-3-BodySlots-build> --dependency-sources <CMake-dependency-checkouts> --skse-root <SKSE-VR-2.0.12-src> --higgs-root <HIGGS-fork> --planck-root <PLANCK-fork> --output <empty-external-directory>`. The script validates the pinned DLL and native manifest and reads source and notices from the stated Git revisions.
