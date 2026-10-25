Body Slots 0.1.0-alpha.1 -- UNPUBLISHED NEXUS DRAFT

This is a foundation for mod authors, not a playable new-slot mod. Released VRIK integration, generic slot interaction, rendering, item handling and an in-game configuration UI are not supplied. Installing this DLL alone creates no visible pouches or new gameplay.

Install this archive as a normal Data-based mod: SKSE/Plugins/BodySlots.dll and this BodySlots folder. The provider targets Skyrim VR 1.4.15 and is compiled against SKSE VR 2.0.12; its runtime guard accepts that SDK version or newer with the required messaging/serialization interfaces. The current binary imports the Microsoft x64 C++ runtime (MSVCP140, VCRUNTIME140, VCRUNTIME140_1 and UCRT). It has no runtime VRIK, HIGGS, PLANCK, CommonLib or Python dependency.

This folder is intentionally supplied WITHOUT an enabled demonstration package. Authored .bslots packages placed here are discovered once at SKSE PostLoad. Raw JSON is an offline authoring input and is not loaded by the game. The SDK archive contains an optional example outside the runtime install tree; install it only when deliberately developing/testing a consumer. No equipment identities or runtime activation are inferred.

BodySlots.log is appended in this folder after PostLoad when package status is reported. An empty/default installation has no authored definitions. Configuration overrides use SKSE cosave records; runtime handles and activation are not saved. Do not edit cosave bytes or assume downgrade compatibility for newer instance configuration records.

This snapshot has offline actual-DLL and synthetic-host evidence; Skyrim runtime acceptance is still pending. The archive retains the existing tested DLL unchanged: legacy SKSE PluginInfo version 2 remains distinct from this draft file label. PROVENANCE.json records its source commit and SHA256.

Licensing is to be finalized before publication. No license grant is included or implied by this unpublished draft. No third-party SDK source or reconstructed VRIK source is included.
