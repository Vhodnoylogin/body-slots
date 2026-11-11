# Body Slots 0.2.0-rc.2 for mod authors

The runtime archive installs an SKSE plugin for Skyrim VR 1.4.15 and SKSE VR 2.0.12. It makes named interaction regions and slots available to mods. For example, a pouch mod can register the opening of a bag and show its contents when the player reaches inside and presses grip. The mod supplies the pouch, its contents and the action. This build has passed offline contract tests. Gameplay qualification is pending.

Install the runtime with a mod manager. Unpack the separate SDK in your development directory. Each C++20 example uses the Body Slots headers in `include/`. Choose additional include directories according to the operation.

| Example in the SDK | Additional include directories | Runtime service |
|---|---|---|
| `examples/observe_definitions.cpp` lists registered definitions | None | Body Slots |
| `examples/grip_region_action.cpp` opens a preview once for a grip inside a region | `third_party/compact/higgs/` | Proposed HIGGS one-shot grip API |
| `examples/planck_pose_region_anchor.cpp` keeps an NPC pocket on a named physical bone | `third_party/compact/planck/` | Proposed PLANCK actor pose API |
| `examples/ordinary_inventory_provider.cpp` deposits and withdraws ordinary items | `third_party/higgs/` | Extended HIGGS hand, routing and inventory APIs |
| `examples/planck_region_anchor.cpp` supplies an anchor to the extended region-action service | `third_party/planck/`, `third_party/higgs/` | Extended PLANCK actor snapshots and Body Slots' HIGGS-backed region actions |

The two compact examples can be used together. The pocket mod samples the bone and the virtual wrist in the same game update. Body Slots decides whether the wrist is inside the pocket. HIGGS selects one eligible handler for the grip. The mod checks that the same pocket is still within reach before opening its preview. The SDK walkthroughs are `docs/planck-pose-region-anchor.md` and `docs/grip-region-action.md`. The corresponding provider changes are available for review. [HIGGS proposal](https://github.com/adamhynek/higgs/pull/8) · [PLANCK proposal](https://github.com/adamhynek/activeragdoll/pull/8).

Released HIGGS 001 supplies updates for generic regions when the extended hand service is absent. A consumer can supply its own copied anchor and wrist observations. The stock HIGGS API does not arbitrate the one-shot grip. The player body-node adapter uses VRIK. Sustained actions and inventory transfers require the corresponding extended services. The extended examples target the pinned [HIGGS prototype](https://github.com/Vhodnoylogin/higgs/tree/b388d2ea77a075168ddfc1940ecf066721fb4787) and [PLANCK prototype](https://github.com/Vhodnoylogin/activeragdoll/tree/ab6291e949d1b00fcb3ebac8ce7d17d983445e1e).

To create an example definition package, run `python tools/author.py examples/potion-belt.json --output potion-belt.bslots` from the SDK root. The resulting package defines a belt slot. The consumer supplies potion stock and handling.

Body Slots code and examples use the MIT License. The HIGGS and PLANCK headers retain their respective GPLv3 licenses. The archives include `LICENSE.txt`, third-party notices in `licenses/` and a `PROVENANCE.json` with source revisions and file hashes. The native build uses CommonLibSSE-NG, fmt, spdlog, rapidcsv and SKSE VR headers. Obtain SKSE VR from its [official distribution](https://skse.silverlock.org/).

The release packager in the source checkout accepts `--native-build` (the November 11 verification directory containing `compact-integration-verified.json` and `body-slots-planck-anchor/cmake/Release/BodySlots.dll`), `--dependency-sources`, `--skse-root`, `--higgs-root`, `--planck-root` and `--output`. The provider checkouts must contain both the compact and extended commits recorded in the manifest. Run `python tools/package_candidate.py --help` for the command. The archived [RC1 source and packaging recipe](https://github.com/Vhodnoylogin/body-slots/tree/b4db42ab1d2eb96ab0c7286ba06ad08601729cbd) correspond to the earlier candidate.
