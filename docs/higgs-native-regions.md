# Optional HIGGS mouth and inventory-stash regions

The native Body Slots adapter negotiates HIGGS's additive `HIGGS_QueryNativeRegionsInterface` at `PostPostLoad`. It installs a spatial-only callback under its own HIGGS client, independently of the existing VRIK-backed player-slot adapter. There is no link-time Body Slots dependency in HIGGS. A HIGGS build without this table continues its normal behavior; a missing Body Slots DLL does not prevent HIGGS startup.

The canonical architecture remains in [Body Pouches](https://github.com/Vhodnoylogin/body-pouches/blob/main/docs/trinity-synergy/concept-en.md). This page documents this bounded implementation rather than another copy of the concept.

## Responsibilities and real source data

HIGGS captures its actual native hand-update inputs: the current HMD transform, configured mouth and shoulder offsets/radii, resolved palm position and hand position. The sphere centres already include the HMD transform and its scale; radii are already in Skyrim world units and must not be scaled again. Mouth uses the palm probe; both shoulder inventory-stash regions use the hand probe. These are HIGGS's existing HMD-relative approximations, not anatomical mouth/pocket measurements or a VRIK final-pose guarantee.

HIGGS owns source validity, native phase and sample/epoch/hand/source generations. Body Slots copies the input and registers/evaluates named logical regions through its existing region service. HIGGS keeps item eligibility, poison/book policy, controller-speed gates, release interpretation, hand authority, inventory/consumption execution and its existing callbacks. VRIK keeps the virtual body and pose; PLANCK keeps NPC physics/contact/hits; PPB keeps anatomical fitting and surface evidence. This integration acquires none of those other responsibilities.

## Registered regions and observations

The adapter maintains `mouth`, `stash-right` and `stash-left` under each physical hand's writer namespace, `higgs-native-left` and `higgs-native-right`. Separate streams accommodate the v1 service's complete-per-writer publication contract: updating one hand does not erase or replay the other hand's observations. They describe the same three native targets for different hand probes, rather than six inventory containers. At most six definitions/observations are needed. They are transient, with no saved customization or inventory stock.

Each region has a sphere centred on its supplied native target, identity rotation and unit scale, with zero dwell and zero hysteresis. The target centre is an anchor observation rather than a definition edit on every head movement. Radius changes revise the definition. Zero or negative configured radii disable only the corresponding region: its definition and observations are removed, while other regions remain usable. Oversized positive radii unsupported by the bounded geometry service decline the entire evaluation to the native fallback.

Copied observations identify HIGGS and the native target/palm/hand sources, physical hand, sample, generations, monotonic time, epoch and current Body Slots save generation. Publication and the correlated copy share the runtime invalidation fence. Missing, malformed, repeated or regressing evidence removes that hand's previous observations and produces a handled zero mask, never permission to activate from old readiness. A save reset clears old observations. The SKSE reset adapter additionally bars the previously observed HIGGS epoch until HIGGS advances it.

The region service's closed surface and HIGGS's strict native action boundary are distinct: the adapter filters readiness with the same Body Slots signed-distance kernel using `< 0`. This preserves the strict mathematical boundary. Its double-precision distance can differ at very close floating-point rounding boundaries from the original HIGGS float calculation; bit-identical numerical parity is not claimed.

## One selected detector and one executor

The detector returns a copied mask correlated with the exact input epoch/sample. A valid handled result, including all-outside or unavailable-source zero, is authoritative for that evaluation: HIGGS does not then run its local detector. A missing/unregistered provider, incompatible table, service refusal, exception or malformed result selects the native fallback exclusively. Invalid native source data selects neither activation path. No provider callback polls buttons, requests a grant, executes gameplay or calls back into HIGGS.

The existing HIGGS release branch remains the only executor: consume first, otherwise inventory stash, otherwise ordinary drop. Preview checks use the same selected spatial result. A transactional physical release retains HIGGS's `disableConsumeStash` guard so it cannot also eat or stash the item. Current HIGGS authority is checked at native action admission; proximity is not an authority token. This route does not fabricate a second controller input, generic action offer, successful inventory receipt or confirmed consumption result.

The detector context has process lifetime in the game adapter. The HIGGS registration contract supports owner-checked removal and client unregistration with callback drainage; callbacks themselves cannot synchronously destroy their own context. Dynamic plugin unloading is not promised by Body Slots. `CopyFrame` and region copies remain last-observed evidence, not a promise of current hardware tracking or future action eligibility.

## Verification boundary

The integration test links the production HIGGS broker, public detector contract, Body Slots adapter, registry and geometry. It checks both hands, strict boundaries, disabled regions, world scaling, copied provenance, replay/rollback rejection, save/runtime invalidation and optional detector selection. HIGGS's own native fixture checks the actual source capture and release call sites. Native DLL linking and public export/table probes are separate from these core checks.

No game installation, headset session, `SKSEPlugin_Load` call or Nexus release follows from these checks. Node timing, actual hardware freshness, consumption/inventory effects, haptics and save/load behavior still require game qualification. Generic region action offers, PPB/PLANCK adapters, arbitrary consumer rebinding, saved native-region customization and Body Pouches migration remain separate work.
