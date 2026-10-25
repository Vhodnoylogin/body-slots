# Body Slots — Nexus draft

Draft page: https://www.nexusmods.com/skyrimspecialedition/mods/196769

Version: 0.1.0-alpha.1. Status: unpublished developer preview. Runtime and SDK uploaded separately; no public release or license grant has been made.

## Description

A separate Skyrim VR service for defining and configuring body slots. A content mod can describe a potion pouch, tool holder or other slot without maintaining another placement registry and save format. The long-term direction follows the [existing synergy concept](https://github.com/Vhodnoylogin/body-pouches/tree/main/docs/trinity-synergy).

Implemented: named slot definitions, validated packages, generic hand/content filters, independent access spheres, saved placement overrides, equipment-provider instances, copied versioned APIs, atomic requested activation, and separate native14 placement/policy compatibility banks.

This is a foundation for mod authors, not a playable bag mod. Released VRIK does not consume the service. Body binding, presentation and gameplay integration require an agreed adapter; the private prototype is not included. Body Slots does not handle controller input, physical grabbing, inventory transactions or NPC physics. Runtime activation is transient; equipment providers supply and restore durable identities.

The runtime archive contains the current DLL and package directory. The SDK contains seven copied headers, authoring tools, clean examples and contract documentation. Neither package contains test executables, game assets, reconstructed VRIK code or third-party SDK sources. Licensing and public release details remain to be finalized.

Runtime target: Skyrim VR 1.4.15, SKSEVR 2.0.12 or later, and the Microsoft Visual C++ x64 runtime. Install the runtime as a separate MO2 mod; no ESP is supplied. Optional packages belong in SKSE/Plugins/BodySlots. The preview alone creates no visible or interactive slot. HIGGS, PLANCK and CommonLib are not dependencies of this standalone service.

Actual-DLL/source checks cover ownership, failures, revisions, concurrent snapshots, saved configuration and lifecycle. Separate adapter checks use an isolated VRIK core and synthetic SKSE host. These do not establish loaded Skyrim, actual equipment or rendered output.

Vhodnoylogin, with AI-assisted development and review. SKSE by the SKSE team. This proposal is not an agreement with the VRIK, HIGGS or PLANCK maintainers. No closed VRIK implementation is distributed.
