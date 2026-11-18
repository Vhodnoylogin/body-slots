"""Mode-independent bounded subject plan for the actual RC4 game consumer.

These are requested domain semantics, not a claim that a test platform has
qualified them. Polygon owns the actual mapping, controller driver and custody.
No provider commands, transport configuration or tool hashes belong here.
"""
import argparse
import json
from pathlib import Path


def make_plan():
    steps, roles = [], {}

    def add(name, operation, request=None, assertions=(), role="subject", timeout=20, poll=False):
        step = {"name": name, "operation": operation, "timeout": timeout}
        if request is not None:
            step["parameters"] = {"request": request}
        if assertions:
            step["assert"] = [{"field": field, rule: value} for field, rule, value in assertions]
        else:
            step["observe"] = True
        if poll:
            step["poll"] = True
        steps.append(step)
        roles[name] = role

    def state(name, assertions, **kwargs):
        add(name, "world.read", {"observation": "vr_interaction_framework.state"}, assertions, poll=True, **kwargs)

    def fixture_action(name, action, sequence, **values):
        add(name, "object.perform", {"action": action, "actionSequence": sequence, **values}, role="fixture", timeout=5)

    def applied(sequence):
        # This scenario uses a fresh process and exactly seven accepted commands.
        # Confirm each actual outcome; an intake acknowledgment is insufficient.
        return [("processedCommand", "equals", sequence), (f"commandResults.{sequence-1}.id", "equals", sequence),
                (f"commandResults.{sequence-1}.status", "equals", 0)]

    def pose(name, x, grip, seconds, timeout=20):
        add(name, "controller.perform", {"action": "pose_and_grip", "hand": "right", "grip": grip,
            "durationSeconds": seconds, "trackingPosition": {"units": "metres", "xyz": [x, 0.3, -0.55]},
            "trackingOrientation": {"quaternionXYZW": [0, 0, 0, 1]},
            "otherHandPosition": {"units": "metres", "xyz": [-0.3, 0.3, -0.55]},
            "referenceHeadPosition": {"units": "metres", "xyz": [0, 0, 0]}},
            [("inputIssued", "equals", True)], role="fixture", timeout=timeout)

    add("world fixture ready", "state.read", assertions=[("world.ready", "equals", True)], role="fixture", timeout=60, poll=True)
    state("public provider APIs connected", [("state", "equals", "ready"), ("frameworkConnected", "equals", 1),
        ("planckConnected", "equals", True), ("phaseRegistered", "equals", True),
        ("integrationStatus", "equals", 0), ("snapshotStatus", "equals", 0), ("higgsWorldReady", "equals", 1),
        ("epoch", "min", 1), ("saveGeneration", "min", 1), ("input.executed", "equals", 0),
        ("input.terminals", "equals", 0), ("highestCommand", "equals", 0)])
    pose("open right hand at reference pose", 0, "open", 0.3)
    fixture_action("arm fixed region from observed palm", "interaction-region.arm", 1, region="world-test", physicalHand=2)
    state("real palm enters the fixed region", applied(1) + [("world.observation.status", "equals", 0),
        ("world.observation.state", "min", 2), ("world.observation.anchorValid", "equals", 1),
        ("world.observation.probeValid", "equals", 1), ("world.capturedPalmPose", "min", 1),
        ("input.armed", "equals", True)], timeout=2)
    pose("press grip inside the region", 0, "closed", 0.3, timeout=2)
    state("one authorized logical effect", [("input.executed", "equals", 1), ("input.reportedCompleted", "equals", 1),
        ("input.actualResultStage", "equals", 3), ("input.decisionConsumed", "equals", 1),
        ("input.lastPressInput", "min", 1), ("input.lastRequest", "min", 1),
        ("input.reportFailures", "equals", 0), ("input.callbackOverflow", "equals", 0)], timeout=2)
    pose("withdraw while maintaining the original grip", 0.65, "closed", 0.5, timeout=8)
    state("left region without a second action", [("world.observation.status", "equals", 0),
        ("world.observation.state", "equals", 1), ("input.executed", "equals", 1),
        ("input.terminals", "equals", 0), ("input.armed", "equals", True)], timeout=2)
    add("release the original grip outside the region", "controller.perform",
        {"action": "release_hand", "hand": "right", "settleSeconds": 0.5}, role="fixture", timeout=3)
    state("one matching terminal from the original press", [("input.executed", "equals", 1),
        ("input.terminals", "equals", 1), ("input.matchedReleases", "equals", 1),
        ("input.lastCorrelation", "min", 1), ("input.lastTerminalInput", "min", 1),
        ("input.mismatches", "equals", 0), ("input.cancelled", "equals", 0),
        ("input.observationLost", "equals", 0), ("input.callbackOverflow", "equals", 0)], timeout=2)
    fixture_action("set saved placement A", "interaction-placement.set", 2, position=[11, 22, 33])
    state("placement A committed by configuration API", applied(2) + [("customization.status", "equals", 0),
        ("customization.mask", "equals", 1), ("customization.position.0", "equals", 11),
        ("customization.position.1", "equals", 22), ("customization.position.2", "equals", 33)])
    add("save disposable profile with placement A", "input.perform",
        {"action": "save_game", "saveTag": "vrif-placement-a", "scope": "owned-disposable-profile"}, role="fixture", timeout=60)
    add("owned save pair completed", "world.read", {"observation": "save.state", "saveTag": "vrif-placement-a"},
        [("save.completed", "equals", True), ("save.ownedByAttempt", "equals", True),
         ("save.essSha256", "exists", True), ("save.skseSha256", "exists", True)], role="fixture", poll=True)
    fixture_action("change current placement to B", "interaction-placement.set", 3, position=[44, 55, 66])
    state("placement B differs before load", applied(3) + [("customization.position.0", "equals", 44),
        ("customization.position.1", "equals", 55), ("customization.position.2", "equals", 66)])
    add("load owned placement A save", "input.perform",
        {"action": "load_game", "saveTag": "vrif-placement-a", "scope": "owned-disposable-profile"}, role="fixture", timeout=90)
    add("owned load transition completed", "world.read", {"observation": "lifecycle.state", "afterSaveTag": "vrif-placement-a"},
        [("lifecycle.worldReady", "equals", True), ("lifecycle.generationChanged", "equals", True),
         ("lifecycle.oldReferenceTagsInvalidated", "equals", True)], role="fixture", timeout=60, poll=True)
    state("SKSE restored A without fixture rewriting it", [("state", "equals", "ready"),
        ("customization.status", "equals", 0), ("customization.mask", "equals", 1),
        ("customization.position.0", "equals", 11), ("customization.position.1", "equals", 22),
        ("customization.position.2", "equals", 33), ("lifecycle.saves", "min", 1),
        ("lifecycle.preLoads", "min", 1), ("lifecycle.postLoads", "min", 1), ("lifecycle.lastLoadSucceeded", "equals", True)])
    fixture_action("bind disposable NPC real body", "interaction-region.bind-fixture-npc", 4, physicalHand=2, bodyIndex=0)
    state("NPC anchor names current PLANCK body", applied(4) + [("npc.boundMatchesLive", "equals", True),
        ("npc.observationMatchesBound", "equals", True), ("npc.boundWorldEpoch", "min", 1),
        ("npc.boundActorGeneration", "min", 1), ("npc.boundBodyGeneration", "min", 1),
        ("npc.liveObservedAtNs", "min", 1), ("npc.observation.status", "equals", 0), ("npc.observation.state", "min", 1)], timeout=30)
    fixture_action("request actual disposable NPC model reset", "fixture-npc.reset-model", 5)
    state("real replacement invalidates the previous anchor", applied(5) + [("npc.resetIssued", "equals", 1),
        ("npc.replacementObserved", "min", 1), ("npc.unavailableAfterReplacement", "min", 1),
        ("npc.boundMatchesLive", "equals", False), ("npc.observationMatchesBound", "equals", False),
        ("npc.observation.status", "equals", 0), ("npc.observation.state", "equals", 0),
        ("input.executed", "equals", 1)], timeout=30)
    fixture_action("explicitly bind the new real NPC body", "interaction-region.rebind-fixture-npc", 6, physicalHand=2, bodyIndex=0)
    state("new NPC anchor follows the new identity", applied(6) + [("npc.rebinds", "equals", 1),
        ("npc.boundMatchesLive", "equals", True), ("npc.observationMatchesBound", "equals", True),
        ("npc.observation.status", "equals", 0), ("npc.observation.state", "min", 1)], timeout=30)
    fixture_action("disarm the test action", "interaction-region.disarm", 7)
    state("final no duplicate or lost terminal", applied(7) + [("input.armed", "equals", False),
        ("input.executed", "equals", 1), ("input.reportedCompleted", "equals", 1),
        ("input.terminals", "equals", 1), ("input.matchedReleases", "equals", 1),
        ("input.reportFailures", "equals", 0), ("input.mismatches", "equals", 0),
        ("input.observationLost", "equals", 0), ("input.callbackOverflow", "equals", 0),
        ("customization.position.0", "equals", 11), ("customization.position.1", "equals", 22),
        ("customization.position.2", "equals", 33)])
    return {"interface": "polygon-actions/1", "fixture": {"cell": "QASmoke", "startMode": "new-game"}, "steps": steps}, {
        "start": {"kind": "check", "name": "public provider APIs connected"},
        "checks": [{"name": s["name"], "role": roles[s["name"]]} for s in steps]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit("Refusing an existing output")
    plan, testing = make_plan()
    args.output.write_text(json.dumps({"subjectPlan": plan, "testing": testing}, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared {len(plan['steps'])} bounded checkpoints; platform qualification is separate.")


if __name__ == "__main__":
    main()
