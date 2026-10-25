# Native14 placement compatibility

The additive `BodySlots_QueryNativePlacementInterface` export moves the mutable placement bank of VRIK's fourteen original holsters into Body Slots. It is a compatibility service, separate from authored slot definitions, generic eligibility and saved customization. A new slot continues to use the ordinary registry; a native slot number is never a registry slot ID.

## Ownership and data

One declared owner acquires the complete fourteen-pose bank. Activation imports all poses together or changes nothing. Other acquisitions fail instead of replacing the owner. Each pose contains the original local position and matrix as raw float representations. No rigid-matrix normalization, clamping, access sphere or content policy is invented. This preserves legacy values that the stricter authored-definition format would reject; it does not certify those values as usable geometry.

The copied ABI is in `include/body_slots_native_placement_api.h`: a 48-byte pose, 688-byte whole-bank snapshot and 48-byte x64 table, version 1. Snapshot buffers must be initialized with their exact `structSize` and `version`. Acquisition takes an initial revision of zero. A replacement takes the latest nonzero bank revision, copies all fourteen poses and commits once. Stale revisions, invalid headers and foreign owners leave state and output unchanged. Replacing identical bits preserves the revision. Output buffers must hold the complete declared record; the acquisition owner output must not overlap its snapshot output. Input/output snapshot aliasing is supported.

Leases and revisions are process-local and monotonic. Release invalidates the owner, clears inaccessible pose data and never recycles its identity. The next acquisition receives a later revision. No callbacks, Skyrim pointers, allocated objects or caller-owned buffers are retained. This is cooperation between plugins in one process, not a security boundary. Tables are valid only while the provider remains loaded; hot DLL unloading is unsupported.

`NativePlacementStore` encapsulates the bank. Pure transition builders construct acquisition, release and replacement candidates. Only its private `Commit` method publishes state under the store lock. ABI functions delegate to this owner.

## Existing persistence and VRIK boundary

The native bank has no cosave record and does not reset on load, revert or new game. VRIK retains its existing global `vrikslots.ini` serialization as a compatibility projection. Legacy raw placement and clamped INI settings can differ; Body Slots preserves the raw active bank rather than rebuilding it from the settings map. Direct legacy settings writes retain their existing meaning.

The VRIK adapter acquires ownership after initial INI loading. If the optional export is absent, malformed or unavailable at acquisition, all fourteen slots retain the original path. An attached service failure never silently activates the fallback arrays as another owner. Position/rotation patches are converted into a complete revision-checked bank replacement. VRIK serializes the bank commitment and its INI projection together, and reads one whole placement snapshot before processing a native holster frame. Eligibility, weapon-dependent distance, rendering, input behavior and the existing equipment executor stay in VRIK at this stage.

This does not complete native14 migration into generic definitions or provide arbitrary-slot item operations. Native content rules, equipment instances, item presentation and confirmed transfers remain separate work. The old ABI 001, Papyrus signatures and fourteen-element saved equipment arrays are unchanged.

## Verification boundary

`tools/build.py` exercises the actual module export, raw representation preservation, owner/revision failures and concurrent whole-bank reads. Its synthetic public-SKSE host also checks that customization save/load/revert/new-game callbacks leave the native bank unchanged and do not serialize it. The private VRIK repository contains differential actual-source checks against its previous commit for absent/present service behavior and exact INI output. These are offline checks; no Skyrim acceptance is implied.
