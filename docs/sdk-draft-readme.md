# Body Slots SDK — 0.1.0-alpha.1 unpublished draft

This snapshot is a foundation for mod authors and discussion with framework authors. It is not a playable new-slot mod, a released VRIK integration or an author-approved framework SDK. Generic presentation, hand detection, input, item assignment/transfer and an in-game editor are not delivered. Licensing will be finalized before publication; no license grant is included.

## Contents and provider

`include/` contains seven newly written copied-record C++ headers with separately queried version-1 tables. Consumers need x64 C++17 and the standard library; the simple Windows discovery example also uses Windows headers. They do not need private VRIK code, CommonLib or third-party SDK source. The runtime provider is the separate Body Slots archive. Its unchanged DLL targets Skyrim VR 1.4.15, is compiled against SKSE VR 2.0.12 and requires messaging/serialization plus the Microsoft x64 C++ runtime. Python is needed only for the optional offline authoring tool, not in Skyrim.

The available foundation includes definition ownership and snapshots, strict authored packages, saved definition/instance placement overrides, transient caller-keyed instances, atomic requested activation and independent native14 compatibility banks. A native bank can be Unavailable until an actual compatibility owner attaches. No released VRIK integration is bundled or required to use the generic service tables.

## First read-only consumer

`examples/observe_definitions.cpp` discovers an already-loaded provider, checks the exact base table, acquires an observer, copies definitions and releases that observer. Call it after provider initialization/package publication, for example from your consumer's PostPostLoad handling, and tolerate an absent provider or an empty snapshot. It does not force-load a DLL, invent equipment identities or mutate definitions. It is a small utility example, not a complete SKSE plugin. Keep the provider loaded throughout calls; hot DLL unloading is unsupported.

```text
cl /nologo /std:c++17 /EHsc /W4 /Iinclude /c examples/observe_definitions.cpp /Fo:<external-output>/observe_definitions.obj
```

For writing, register a separate canonical provider namespace and release only your own writer. For runtime instances, supply durable equipment/slot keys your mod actually owns, inherit an authored provider/key and use exact save-generation/view/entry revisions. Recreate runtime activation after scope changes rather than saving process handles. A requested-active bit does not prove that equipment is worn, the template is current, an anchor binds or a model renders. See the focused API documents in `docs/` before using mutation tables.

## Optional authoring example

```text
python tools/author.py examples/potion-belt.json
python tools/author.py examples/potion-belt.json --output <external-output>/potion-belt.bslots
```

The compiler uses only Python's standard library and validates the explicit schema/units/axes/rotation/spherical access region. `examples/potion-belt.bslots` is the same compiled demonstration supplied for convenience. Neither it nor the JSON is installed by the runtime archive. Deliberately copy the .bslots file to `Data/SKSE/Plugins/BodySlots/` only for a consumer development session; a definition alone does not create visible pouches or gameplay. A package owns its declared provider namespace and may conflict with another owner using that name.

## Snapshot and publication boundary

`PROVENANCE.json` records the unchanged production DLL hash/source commit and each payload file hash. This file label does not change legacy PluginInfo version 2 or any ABI version. Offline actual-DLL, concurrency, save-host and source-boundary evidence exists; Skyrim behavior remains unqualified. Private reconstruction, third-party SDK sources, test executables and fake game hosts are excluded. Repository visibility and public source publication are separate owner decisions; this package is prepared only for an unpublished Nexus draft.
