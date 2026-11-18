# VR Interaction Framework: name and compatibility

**VR Interaction Framework** is the product display name, replacing **Body Slots**. It identifies infrastructure that consumer mods use to offer spatial interactions. Installing the framework alone adds no standalone gameplay feature.

The framework manages named logical regions on the player, NPCs, equipped items, world objects and temporary interaction frames. A consumer can attach actions, optional storage, visuals and saved customization to a region. HIGGS arbitrates hand use and input and owns physical execution, VRIK supplies the player pose, PPB supplies anatomical evidence, and PLANCK owns its NPC physics. The [maintained concept](https://github.com/Vhodnoylogin/body-pouches/blob/main/docs/trinity-synergy/concept-en.md#responsibilities) defines those boundaries.

The earlier candidate **Interaction Regions VR** was not selected because it did not make the service's role as infrastructure clear enough. **VR Interaction Framework** is the chosen name; it is no longer a pending proposal.

## Compatibility identities

This is a display rename. Existing consumers and saves continue to use:

- `BodySlots.dll`, `BodySlots.log` and `Data/SKSE/Plugins/BodySlots/`.
- `BodySlots_Query…` exports, `body_slots` namespaces, `body_slots_*.h` headers and existing SKSE messaging recipients.
- Existing provider/action keys, package formats, serialization IDs and save records.
- The existing [body-slots source repository](https://github.com/Vhodnoylogin/body-slots), build/test target identities and historical tags.

Newly generated archive filenames use `VRInteractionFramework-runtime-<label>.zip` and `VRInteractionFramework-sdk-<label>.zip`. Internal runtime paths retain their compatibility names. Existing `BodySlots-…` archives, receipts, hashes and source revisions are historical evidence and are not renamed or requalified. Any future technical rename requires its own consumer and save migration.
