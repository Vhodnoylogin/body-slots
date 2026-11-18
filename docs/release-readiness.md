# VR Interaction Framework release readiness

VR Interaction Framework is infrastructure for consumer mods and adds no standalone gameplay feature. The current candidate packaging label is `0.2.0-rc.4`. Existing RC3 archives and their qualification remain historical and unchanged.

## Checked boundary

The source implements named player/NPC/equipment/world/transient regions, revision-checked placement and configuration, HIGGS-authorized actions, matching input release and optional RegionDwell1 offers in the same HIGGS shared ordered-proposal version 2 round. The repository contains offline production-core, provider-protocol and actual-DLL discovery checks. Passing those checks establishes only the cases and source/provider revisions recorded by their run; it does not establish a Skyrim session.

The packagers keep runtime and SDK separate, retain `BodySlots.dll` and all compatibility paths, include MIT and applicable third-party notices, and write content hashes and source/build provenance. The candidate packager rejects dirty or mismatched source checkouts and altered tested DLL bytes. A package must be built from the exact verified committed source; changing a display label does not create native-build evidence.

On November 18, 2026, `python -B src/test/package_branding_tests.py` passed four package-output contract tests: both archive types, product headings and filenames, retained DLL/path identities, licence payloads, provenance/checksums, deterministic bytes and refusal of dirty source or changed DLL bytes. These tests use an inert DLL fixture and substituted candidate Git/provider discovery, so they verify assembly rather than a real provider build. Both packager `--help` commands and Python syntax parsing also passed.

The complete native adapter was then built against the proposed HIGGS `fd226f6` and PLANCK `bcfa8f8` contracts and PPB `4496302`. All 14 Release-configuration CTests and actual-DLL export/disconnected-negotiation checks passed. This includes 72 checks of stable slot targets and pending decisions with the actual HIGGS broker, and 1465 region-action checks. The latter include provider loss/replacement/movement immediately before button and dwell dispatch, and once-only cancelled/lost-input notifications. The NPC dispatch regression failed before the repair. Its late provider change is injected at the real reader boundary; the separate PLANCK test uses its production registry. Neither is a Skyrim NPC-lifetime test.

Pending shared decisions now remain queued until HIGGS selects a winner or the existing 100 ms observation limit expires. Expiry explicitly cancels the candidate. Both ordinary slot offers and editing submit persistent target keys; editing resolves overlapping slots by the persistent provider/equipment/slot identity, independently of runtime handle allocation. Owned PLANCK sources are reread at final authorization, outside the region-service mutex. Existing public ABI declarations and persistence identities are unchanged.

## Dependencies and remaining qualification

Released public HIGGS001 alone lacks the hand arbitration, routed input and shared ordered-proposal version 2 services required by the new action/dwell path. That path therefore requires an exact compatible HIGGS cooperation-fork build. NPC anchors additionally require the documented PLANCK revision 2 services, and optional anatomical evidence requires the matching PPB observations. Player body-node placement uses VRIK. Consumers must disable a dependent feature when its service is unavailable.

Supplying those exact fork dependencies resolves the service-availability requirement; upstream maintainer acceptance is not a separate technical gate. Skyrim VR loading, scene binding, physical input, visual fit, headset ergonomics and complete consumer gameplay remain unqualified until actual game evidence covers them. Each RC4 package must carry its own immutable source/provider revisions and DLL/archive hashes. No Nexus upload, public release, repository rename or game launch is performed by the packaging tools.

## Remaining release work

1. Qualify SKSE loading and one consumer scenario in Skyrim VR: enter/leave, one button or dwell effect, matched release, save/load, and an NPC source disappearing or being replaced. Existing offline tests do not replace that check.
2. Distribute the exact provider builds and document feature-scoped dependencies. Complete HIGGS `fd226f6` and PLANCK `bcfa8f8` test archives are now built and independently verified, and installed with RC4 in the development MO2 instance. Released HIGGS alone cannot activate the new action path. Unavailable semantic-command or unconventional-equipment backends must remain disabled; they do not prevent a release of the supported region services.
3. Review and update the unpublished Nexus draft with the new product name, candidate files, dependency versions and measured qualification. Existing VRIK holsters remain independent; overlapping native holsters require explicit user configuration because they are not silently migrated into this broker.

A developer preview can expose the supported SDK with these limits. A generally usable runtime release needs the game evidence above; it must not be described as completing every service in the target synergy concept.

## Prepared one-shot game test

The separate [game consumer](../src/test/game_fixture/README.md), [scene compiler](../src/test/autotest_fixture_generator.py) and [bounded subject plan](../src/test/autotest_subject.py) are maintained test source, excluded from the runtime candidate. Native build/export smoke checks and independent source review pass. The consumer observes actual HIGGS selection and matching release, SKSE saved customization and exact PLANCK body generations; its logical action is not an inventory or physics receipt.

Order `vrif-rc4-provider-integration-20261118-01` contains 286 installed pins and 28 checkpoints. The bundle is independently installed/verified in `mo2 test vr` with stock VRIK 0.8.8, and handed to Polygon under one owner-authorized autotest release. Polygon verified every installed pin, then refused dispatch before any game/native session because the VR platform lacks actual qualification and the new semantic mappings. All gameplay checks remain not run. Tooling-recipient routing and further one-shot authority need review before continuation; the original order/release stays immutable. Preparing this fixture does not qualify the testing platform or authorize automatic retries.
