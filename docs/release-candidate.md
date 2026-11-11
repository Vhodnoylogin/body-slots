# Body Slots 0.2.0-rc.3 for Skyrim VR mod authors

The runtime provides named logical regions and slots. A pocket mod can attach a region to a physical body on a moving NPC, offer a grip action when the player reaches it and receive the release for that same press. The pocket mod owns what opens, what the player can take and what its inventory records.

## Install and negotiate

Install the runtime ZIP in a development Skyrim VR 1.4.15 profile with SKSE VR 2.0.12. Unpack the SDK separately. Discover the tables after SKSE PostPostLoad and check the requested version, capabilities and current observations. See the two archives’ `PROVENANCE.json` for the exact build and provider commits.

| Feature | Required service |
|---|---|
| Authored definitions and interaction geometry | Body Slots; HIGGS001 supplies the runtime’s independent scene update phase when available |
| Input arbitration and matching release | Full HIGGS Hand 2, HandRouting 2, AuthorizedDispatch 2, InputRoutes 1; released HIGGS001 has no arbitration contract |
| Player and wearable visual nodes | VRIK body/hand presentation with current model and save scope |
| NPC body anchor | Optional full PLANCK revision 2 with actor samples and capture timestamps |
| Anatomical evidence | Optional PPB observation revision 2 grouped digest. Each reply carries coverage and its own clock; it supplies no cross-world hit identity |
| Ordinary inventory transfers | HIGGS Inventory 2 and Transactions 2, in addition to the action services |

The HIGGS and PLANCK requirements above refer to the reviewed full providers. The older compact HIGGS grip example targeted a separate experiment. A mod can handle unavailable services by disabling the dependent feature. After an accepted action, a provider loss invalidates that action’s outstanding authority.

## NPC pocket example

Register a Body Slots writer and an NPC-shaped interaction region. Register the gameplay mod’s HIGGS client. Resolve the desired actor/body from PLANCK’s copied snapshot. Retain its provider world, actor generation, body generation, handle and body index in a `RegionPlanckBody`.

Call `NpcPocketBinding::Attach` with the negotiated tables, both registrations and the selected body. The example subscribes to terminal input before activating the anchor. Body Slots copies the target and HIGGS palm in their provider phases. The winning callback receives the genuine executor request. The mod can open its interface and report the factual result. Closing a preview on release uses only the original press notification. A subsequent game mutation needs valid HIGGS authorization.

Call `Detach` outside callbacks and retry any pending drain before freeing callback code or context. Recreate the binding after replacing the body or changing the loaded save. The example is compiled into the offline build. [NPC pocket binding](../examples/npc_pocket_binding.cpp) · [Region actions](region-actions.md)

Other SDK examples cover definitions, inventory and manual anchor publication. Add `include/`, `third_party/higgs/` and, for direct PLANCK reads, `third_party/planck/` to your include paths. Body Slots code and examples use the MIT License. Provider materials use the licences shipped in the archives.

## Offline verification and packaging

In the source checkout, run `tools/build_game.py` with `--output`, `--sdk-root`, `--higgs-root`, `--planck-root`, and `--ppb-root`. A checked-in test manifest compiles the HIGGS production core from the same checkout as the headers. An existing external CMake directory can be reused with `--build-dir`.

Pass the resulting build-evidence directory as `--native-build` to `tools/package_candidate.py`, together with the provider roots, `--skse-root`, `--dependency-sources` and a new empty `--output` directory. The packager checks the tested DLL, source hashes and provider commits. Offline checks cover contracts and exported tables. Skyrim VR gameplay, visual fit and hardware input remain unverified.
