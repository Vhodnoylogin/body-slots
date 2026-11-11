# Name for the spatial interaction service

The service now manages named logical regions on the player, NPCs, equipped items, world objects and temporary interaction frames. A region can offer actions and acquire optional storage, visuals and saved customization. A mouth, pocket and keyhole use this role. The current name Body Slots describes part of that scope.

| Candidate | Meaning and tradeoff |
|---|---|
| **Interaction Regions VR** | Names the managed entity directly and covers player, NPC and world targets. Recommended product name. |
| Spatial Interactions VR | Readable as a broader interaction framework. It could imply ownership of physical interactions and input that belong to HIGGS and PLANCK. |
| Interaction Spaces VR | Covers temporary and persistent regions, although “space” gives less information about a bounded queryable region. |

Interaction Regions VR is the current recommendation for owner review. HIGGS continues to arbitrate hand use and input, VRIK supplies the player pose, PPB supplies anatomy, and PLANCK owns its NPC physics. The name does not assign their responsibilities to this service. The [maintained concept](https://github.com/Vhodnoylogin/body-pouches/blob/main/docs/trinity-synergy/concept-en.md#responsibilities) defines those boundaries.

A product rename can preserve `BodySlots.dll`, existing exported getters, registration recipients, persistent keys and SDK namespaces for compatibility. The display name can change independently. Any later technical rename needs an explicit migration for consumers and saves.
