# Test-only in-game consumer

`test_VRInteractionFramework.dll` is an independent SKSE VR consumer for RC4
runtime source `a9b3ac9`. It does not ship in the framework runtime. Its domain
effect is a bounded execution record appended by a real authorized region action.
It never injects input, invents a provider epoch, changes inventory, or creates a
physical receipt. Compilation and the exported ABI smoke check are not gameplay
qualification.

Install the DLL as `SKSE/Plugins/test_VRInteractionFramework.dll` in its own test
mod, alongside the separately prepared `test_VRInteractionFramework.esp` scene.
Runtime dependencies are stock SKSE VR 2.0.12/CommonLib VR support, the pinned
HIGGS additive hand/routing/input contracts, PLANCK revision 2 with actor capture
timestamps, and BodySlots RC4 including its PLANCK source adapter. Stock
CommonLib engine calls (fixture actor lookup, handle, and Reset3D) require the
VR address data supported by that CommonLib build. No production code is linked
into this fixture. No game files are written by the build.

## Discovery and observation

The fixture discovers official HIGGS001 using its public SKSE message and uses
`AddPostVrikPostHiggsCallback` for every update. It discovers additive HIGGS
contracts and BodySlots contracts through their named exports, and PLANCK rev2
through its public SKSE message. Registration occurs at SKSE PostPostLoad. Leases
and callback code remain alive for the process; hot unloading is unsupported.

With DevBench build >=10500, its public MIT cross-plugin API registers:

- `inspect` extension kind `vr_interaction_framework_test`: copied domain state.
- tool `vr_interaction_framework_test_command`: bounded command intake.

These names are real fixture registrations, not an assumption about a Polygon
adapter. Polygon must qualify the installed host and map its public tool surface.
The listener returns copied JSON immediately and never calls engine/provider APIs.
Missing DevBench leaves the file contract below available. Missing required
interfaces/leases/main phase yields `state: unavailable`, never a success verdict.

`SKSE/Plugins/VRInteractionFrameworkTest/status.json` is also atomically replaced
every 30 observed HIGGS phases (and at connection). Its sibling `command.json`
accepts the identical command object, polled every ten phases. Write command JSON
through an atomic rename. Relative paths are under the game's Data directory;
the mod manager decides the physical output location. The fixture does not remove
the command file; duplicate IDs are rejected and cannot execute twice.

The exported C ABI provides `VRInteractionFrameworkTest_Version() -> uint32` (1)
and `VRInteractionFrameworkTest_CopyStatus(version, char* buffer, uint32 capacity,
uint32* needed) -> uint32`. The latter returns BodySlots status 5 with the required
NUL-inclusive capacity, 2 for unsupported versions, or 0 after copying. Before
SKSE initialization the state is `loading` with no claimed observations.

## Commands

Read the current `processBirth` (string), `epoch`, and `highestCommand` first.
Send an object such as:

```json
{"id":1,"command":"arm_world","expectedBirth":"<observed string>","expectedEpoch":1,"hand":2}
```

`id` must be a positive uint64 strictly greater than the accepted high water mark.
Do not reuse an ID within the same process. `expectedBirth` must match this actual
Windows process's creation FILETIME; `expectedEpoch` must match the last copied
BodySlots/HIGGS integration epoch when execution occurs. The intake response
`accepted: true, pending: true` is only a queue receipt. Wait for that exact ID in
`commandResults` and `processedCommand`; status 0 confirms command execution, not
the requested later effect. Status 4 means the process/world scope changed. Every
dequeued command is run at most once and failures are final. Intake is bounded
to 16 pending commands and the last 32 results. Mutations execute in a later
stock SKSE main-thread task after the HIGGS phase returns. Stalled phases/tasks
must be reported as a tooling/run failure, never inferred successful.
Every accepted command has a five-second steady-clock deadline. The eventual
task re-copies actual integration/snapshot state and checks scope/deadline before
calling the operation. Expired commands get status 8 and are permanently discarded.

| command | extra arguments | effect |
|---|---|---|
| `read` | none | Refresh actual effective saved placement. |
| `set_position` | `position: [x,y,z]` | Exact configuration-revision CAS on stable `VRInteractionFrameworkTest:saved_position`; finite coordinates within +/-1000. |
| `arm_world` | `hand: 1` left or `2` right (default) | Capture that observed HIGGS palm as a fixed world sphere, radius 24 world units; arm a grip-Down binding for 30 seconds. |
| `disarm` | none | Stop offers; retained observations/counters remain visible. |
| `bind_fixture_npc` | `hand`, `bodyIndex` (default 0) | Resolve only `test_VRInteractionFramework.esp` ACHR local `0x801` to its actual actor handle, copy PLANCK physics and bind the exact actor/body generation. |
| `reset_fixture_npc` | none | Call stock `Actor::DoReset3D(false)` only on that loaded fixture actor, and only when its current actual handle is the bound handle. |
| `rebind_fixture_npc` | `hand`, `bodyIndex` | Explicitly remove/recreate the binding using the fixture actor's currently copied PLANCK identity. |
| `bind_npc`, `rebind_npc` | `actorHandle`, `bodyIndex`, `hand` | Same read/bind operation with the exact supplied real handle. No mutation of that actor is available. |

Only the most recently armed binding offers an action. `arm_world` requires a
valid tracked actual palm in the current ready HIGGS world. The anchor stays at
the captured position; later main-phase publications use the genuine observed
HIGGS frame and fixture source provenance. This is not a supplied/fake probe.
BodySlots obtains the probe directly from HIGGS. Observe the region as inside or
ready, then cause a real grip Down and its matching Up through the qualified
controller path. `offered` alone means no action succeeded.

The executor queues its copied actual execution under HIGGS's authorized main
dispatch fence. A later HIGGS phase reports factual completion of that logical
test record after the callback returned, with **zero physical/inventory flags**.
`executed`, `reportedCompleted`, actual copied `actualResultStage` (3), selected
decision/request, and `matchedReleases` are independent evidence. Matching a
release requires the same binding, hand, button, epoch, press, request, action,
executor and nonzero correlation, independently re-read through HIGGS
`CopySequence`; its terminal input must follow the original press. Cancellations,
observation loss, mismatch, report failures and overflow have separate counters.
This validates routing/continuation, not grabbing or inventory behavior.

## Save and NPC qualification

The stable authored definition is registered once, with position `[0,0,0]`.
Only explicit `set_position` commands change its save-scoped customization.
New-game and load notifications never write an override. To test serialization:
set position A, observe mask/revision/actual position, save through the game,
set distinct position B, load that exact save, observe `postLoads` advancing,
`lastLoadSucceeded: true`, the new save generation and actual effective position
A with customization mask 1. Record real game-save and cosave evidence externally.
Neither the fixture nor its own status persists a substitute value.

For an NPC bind, first observe a successful live PLANCK copy and a non-Unavailable
BodySlots observation with PLANCK anchor source. `bodyIndex` and `boneName` name
the actual body; copied anchor/probe world positions support controller alignment.
After `reset_fixture_npc`, `resetIssued` means only that the engine call returned.
Require `replacementObserved` to advance from a real changed world/actor/body
generation, and `unavailableAfterReplacement` to advance for the old binding
after a previously available observation. A period with no actor/body copy is
reported separately, not treated as a new generation. `rebind_fixture_npc` must
then show new actual bound generations and available observations again.
`boundMatchesLive` compares the exact copied PLANCK handle, index and all three
generations with the current bound identity; `observationMatchesBound` requires
the copied BodySlots source to name that body in the current epoch/save scope.
Require both after rebinding, rather than relying on cumulative availability.
If Reset3D does not cause a factual generation change in this game build, report
the scenario as unqualified/failed; there is no manufactured replacement fallback.

Counters are cumulative per process; compare baselines, exact command IDs and
epochs. Observations are copied evidence, not promises of current freshness.
Status schema is version 1, and no overall pass field exists. Polygon owns the
actual semantic assertions, bounded input/stimulus, timing and final report.

## Build and provenance

Run `python build.py --output <external-build> --rc4-native <completed-RC4-native>
--higgs-root <pinned-HIGGS> --planck-root <pinned-PLANCK> --json-include <directory>
` (arguments on one shell command). The JSON directory must contain
`nlohmann/json.hpp`. CMake reads the completed RC4 cache's dependency source paths
and imports its Release CommonLib/fmt/spdlog libraries without rebuilding them.
The native cache must come from the same compiler/architecture/runtime settings.
The build writes logs and `fixture-build-manifest.json` externally, pins the input
headers/source, and smoke-checks loader imports, version and the honest pre-SKSE
status through the real DLL. It neither initializes SKSE nor launches Skyrim.

`DevBenchAPI.h/.cpp` and `DevBenchAPI.LICENSE.txt` were copied verbatim from the
public MIT consumer contract in repository `body-pouches/contract/` on 2026-11-18.
No DevBench implementation is copied. Other public headers are consumed from
their checkouts; the test consumer's own code uses the framework MIT license.
