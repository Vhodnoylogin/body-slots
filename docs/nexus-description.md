# VR Interaction Framework — Nexus description draft

Draft page: https://www.nexusmods.com/skyrimspecialedition/mods/196769

This is local description copy for the existing draft page. It does not attest or change the page's current external state. The current candidate packaging label is `0.2.0-rc.4`; historical `0.1.0-alpha.1` and RC3 files keep their original names and qualification.

## Description

VR Interaction Framework supplies shared spatial interaction and slot services for Skyrim VR consumer mods. It adds no standalone gameplay feature. A content mod can describe an NPC pocket, player pouch, tool holder, mouth region or world interaction without maintaining another placement registry, region detector and save format. The long-term direction follows the [existing synergy concept](https://github.com/Vhodnoylogin/body-pouches/tree/main/docs/trinity-synergy).

Implemented source includes named regions, sphere/capsule/box geometry, validated authored packages, hand/content filters, saved placement overrides, equipment-provider instances, copied versioned APIs, atomic requested activation, consumer action bindings, matching input release, optional buttonless dwell and separate native14 placement/policy compatibility banks.

The framework owns logical regions, placement, binding, detection and consumer dispatch. HIGGS owns hand state, input arbitration, grants and physical/inventory operations. VRIK supplies player pose; PLANCK supplies optional NPC body observations; PPB supplies optional anatomical evidence. Gameplay consumers own their content, selection rules and persistent assignments. Native VRIK holsters remain separate. Runtime activation is transient; equipment providers supply and restore durable identities.

The runtime and mod-author SDK are packaged separately. The SDK contains copied public contracts, authoring tools, consumer examples and documentation. Candidate archives include the framework's MIT licence and applicable provider/dependency notices. Reconstructed VRIK code, game assets, test executables and build output are excluded. The [public source repository](https://github.com/Vhodnoylogin/body-slots) retains its compatibility name.

Runtime target: Skyrim VR 1.4.15, SKSE VR 2.0.12 and the Microsoft Visual C++ x64 runtime. Install the runtime as a separate MO2 mod; no ESP is supplied. Authored packages use `SKSE/Plugins/BodySlots/`. The DLL remains `BodySlots.dll`, and APIs, namespaces, messaging recipients and persistence keys remain compatible with the former Body Slots name. New archive names begin `VRInteractionFramework-`.

Released HIGGS001 alone does not supply routed action/dwell arbitration. Those features require the exact compatible cooperation-fork services; NPC and anatomical features require their documented PLANCK/PPB services. Public upstream acceptance is not a separate gate when matching dependencies are supplied. See [release readiness](release-readiness.md) and [candidate requirements](release-candidate.md).

Offline core/protocol and actual-DLL checks do not establish loaded Skyrim, hardware input, visual fit or consumer gameplay. A new candidate needs its own source/build/package evidence. Generating or editing this description does not upload or publish anything.

Vhodnoylogin, with AI-assisted development and review. SKSE by the SKSE team. This proposal is not an agreement with the VRIK, HIGGS or PLANCK maintainers. No closed VRIK implementation is distributed.
