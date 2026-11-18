# VR Interaction Framework 0.2.0-rc.4 for Skyrim VR mod authors

The runtime provides named logical regions and slots as infrastructure for consumer mods; it adds no standalone gameplay feature. A pocket mod can attach a region to a physical body on a moving NPC, offer a grip action when the player reaches it and receive the release for that same press. The pocket mod owns what opens, what the player can take and what its inventory records. This page describes the current candidate packaging recipe; it does not attest that an RC4 archive has been built or released. See [release readiness](release-readiness.md).

## Install and negotiate

Install the runtime ZIP in a development Skyrim VR 1.4.15 profile with SKSE VR 2.0.12. Unpack the SDK separately. Discover the tables after SKSE PostPostLoad and check the requested version, capabilities and current observations. See the two archives’ `PROVENANCE.json` for the exact build and provider commits.

| Feature | Required service |
|---|---|
| Authored definitions and interaction geometry | VR Interaction Framework; HIGGS001 supplies the runtime’s independent scene update phase when available |
| Input arbitration and matching release | Full HIGGS Hand 2, HandRouting 2, AuthorizedDispatch 2, InputRoutes 1; released HIGGS001 has no arbitration contract |
| Player and wearable visual nodes | VRIK body/hand presentation with current model and save scope |
| NPC body anchor | Optional full PLANCK revision 2 with actor samples and capture timestamps |
| Anatomical evidence | Optional PPB observation revision 2 grouped digest. Each reply carries coverage and its own clock; it supplies no cross-world hit identity |
| Ordinary inventory transfers | HIGGS Inventory 2 and Transactions 2, in addition to the action services |
| Release into a slot and draw by pulling out | BehaviorApi2 content-contract capability with24-byte options, GameApi3 frame age, HIGGS HeldRelease1 and accepted-receipt retention capability |
| Button and buttonless dwell offers in one decision window | HIGGS HandRouting 2 and shared ordered proposal version 2; RegionDwell1 is queried separately from the framework |

The HIGGS and PLANCK requirements above refer to the reviewed full providers. The older compact HIGGS grip example targeted a separate experiment. A mod can handle unavailable services by disabling the dependent feature. After an accepted action, a provider loss invalidates that action’s outstanding authority.

`BodySlots_QueryRegionDwellInterface` exposes optional dwell bindings on owned regions. A dwell offer carries the consumer's action, executor and stable target into HIGGS's shared round, where it competes with button and native eligible offers before execution. It does not synthesize controller input. The raw callback and removal/drain contract is documented in [region actions](region-actions.md).

## NPC pocket example

Register a VR Interaction Framework writer and an NPC-shaped interaction region. Register the gameplay mod’s HIGGS client. Resolve the desired actor/body from PLANCK’s copied snapshot. Retain its provider world, actor generation, body generation, handle and body index in a `RegionPlanckBody`.

Call `NpcPocketBinding::Attach` with the negotiated tables, both registrations and the selected body. The example subscribes to terminal input before activating the anchor. VR Interaction Framework copies the target and HIGGS palm in their provider phases. The winning callback receives the genuine executor request. The mod can open its interface and report the factual result. Closing a preview on release uses only the original press notification. A subsequent game mutation needs valid HIGGS authorization.

Call `Detach` outside callbacks and retry any pending drain before freeing callback code or context. Recreate the binding after replacing the body or changing the loaded save. The example is compiled into the offline build. [NPC pocket binding](../examples/npc_pocket_binding.cpp) · [Region actions](region-actions.md)

Other SDK examples cover definitions, inventory and manual anchor publication. Add `include/`, `third_party/higgs/` and, for direct PLANCK reads, `third_party/planck/` to your include paths. VR Interaction Framework code and examples use the MIT License. Provider materials use the licences shipped in the archives.

## Offline verification and packaging

In the source checkout, run `tools/build_game.py` with `--output`, `--sdk-root`, `--higgs-root`, `--planck-root`, and `--ppb-root`. A checked-in test manifest compiles the HIGGS production core from the same checkout as the headers. An existing external CMake directory can be reused with `--build-dir`.

Pass the resulting build-evidence directory as `--native-build` to `tools/package_candidate.py`, together with the provider roots, `--skse-root`, `--dependency-sources` and a new empty `--output` directory. The packager checks the tested DLL, source hashes and provider commits. New output names begin with `VRInteractionFramework-runtime-` and `VRInteractionFramework-sdk-`; the installed DLL remains `BodySlots.dll`. See [naming and compatibility](naming.md). Offline checks cover contracts and exported tables. Skyrim VR gameplay, visual fit and hardware input remain unverified.

## Build-only pretest delivery

When tests, probes and DLL loading are not authorized, explicitly pass `--build-only` to both `tools/build_game.py` and `tools/package_candidate.py`. Use a fresh evidence output directory; `--build-dir` can reuse an external compiler cache without overwriting earlier evidence. The builder runs only CMake configuration and a Release build of `--target BodySlots`; it does not build test binaries, run CTest or load the DLL. Configuration still declares the repository's other targets. The SDK examples are packaged as source and are not compiled by this target.

Supply the same SDK, HIGGS, PLANCK and PPB roots as above, with `--dependency-sources` pointing to the four pinned Git checkouts (`commonlibsse-src`, `fmt-src`, `spdlog-src`, `rapidcsv-src`). If the builder fetches them itself, pass its `native/_deps` directory to the packager instead. Build-only evidence records provider and dependency revisions, exact Git states, input hashes, and hashes of the two successful command logs. Source or working-tree changes during the build invalidate that evidence. Packaging requires those exact clean committed checkouts and the pinned dependency revisions; a dirty build must be rebuilt after committing its inputs.

The manifest must explicitly say `buildOnly=true`, `testsRun=false`, `dllLoaded=false` and `gameStarts=0`. The packager rejects normal four-command evidence in this mode, verifies the two actual configure/BodySlots-only build records and logs, and emits separate runtime and SDK archives labeled `0.2.0-pretest+<12-character-source-SHA>`. Their readmes and provenance state that tests, DLL loading, installation and gameplay qualification were not performed. The existing licence notices remain in both archives. These artifacts are preparation for later authorized testing and must not be presented as offline-verified RC4 archives. Normal packaging keeps its four successful configure/build/CTest/DLL-discovery requirements and accepts the older manifests without the new metadata fields.

Source review, actual build and package execution are recorded separately in their manifests. A successful build-only command establishes compilation/linking; it does not qualify runtime behavior.

## Historical RC3 and current development

The published RC3 source/tag and its existing `BodySlots-…` draft archives remain the November 11 snapshot, with their original names, hashes and qualification. The display rename does not relabel or replace those artifacts. RC4's source adds RegionDwell1 and negotiates HIGGS shared proposal version 2, with consolidated public HIGGS headers. Those additions are not in the unchanged RC3 archive. Build the current source with exact compatible provider revisions; full DLL/offline verification does not qualify Skyrim gameplay. A new candidate must retain its own build and package evidence before delivery.
