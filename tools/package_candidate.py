#!/usr/bin/env python3
"""Package committed VR Interaction Framework source and its original native build evidence."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from package_draft import ARCHIVE_PREFIX, PRODUCT_NAME, archive_bytes, digest, encoded

ROOT = Path(__file__).resolve().parents[1]
LABEL = "0.2.0-rc.4"
DEPENDENCIES = {
    "commonlibsse": "c4ab853d095e81e3390b282d7ba01ab2f24ebf25",
    "fmt": "0c9fce2ffefecfdce794e1859584e25877b7b592",
    "spdlog": "8e5613379f5140fefb0b60412fbf1f5406e7c7f8",
    "rapidcsv": "68f57cc6c83d5e0992904398822453489d8dfac1",
}
HIGGS_HEADERS = ("higgsabitypes.h", "higgshandapi.h", "higgsinput.h", "higgsphysics.h", "higgsobservations.h", "higgstransactions.h", "higgsnativeinterop.h")
DOCS = ("configuration", "content-observation", "game-adapter", "higgs-native-regions", "instance-activation",
        "instance-configuration", "instances", "interaction-regions", "native-placement", "native-policy",
        "ordinary-inventory-provider", "region-actions", "runtime", "runtime-extensions", "planck-region-anchor", "naming", "release-readiness")


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])


def blob(root, sha, path):
    return git(root, "show", f"{sha}:{path}")


def checked(path, expected):
    data = path.read_bytes()
    if digest(data) != expected:
        raise ValueError(f"Unexpected input hash: {path.name}")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("native-build", "dependency-sources", "skse-root", "higgs-root", "planck-root", "ppb-root", "output"):
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--build-only", action="store_true", help="Package an explicitly untested pretest build, never an offline-verified RC")
    args = parser.parse_args()
    output = args.output.resolve()
    if any((p / ".git").exists() for p in (output, *output.parents)):
        parser.error("Choose an output directory outside Git.")
    if output.exists() and any(output.iterdir()):
        parser.error("Choose a new or empty output directory.")

    manifest_path = args.native_build / "manifest.json"
    verification = json.loads(manifest_path.read_bytes())
    manifest_hash = digest(manifest_path.read_bytes())
    roots = {"bodySlots": ROOT, "higgs": args.higgs_root, "planck": args.planck_root, "ppb": args.ppb_root}
    revisions = verification["sourceRevisions"]
    for name, root in roots.items():
        if git(root, "rev-parse", "HEAD").decode().strip() != revisions.get(name) or git(root, "status", "--porcelain"):
            raise ValueError(f"{name} differs from the verified committed source.")
    for name, expected in verification["sourceSha256"].items():
        checked(Path(name), expected)
    commands = verification.get("commands", [])
    if args.build_only:
        if (verification.get("buildOnly") is not True or verification.get("testsRun") is not False or
                verification.get("dllLoaded") is not False or verification.get("gameStarts") != 0):
            raise ValueError("Build-only packaging requires explicit untested, unloaded, zero-game-start metadata.")
        if len(commands) != 2 or any(command.get("exitCode") != 0 for command in commands):
            raise ValueError("Build-only packaging requires two successful configure/build commands.")
        configure, build = commands
        native = str(Path(verification["buildDirectory"]).resolve())
        argv = configure.get("argv", [])
        if (configure.get("name") != "configure" or build.get("name") != "build" or len(argv) < 9 or
                argv[1:9] != ["-S", str(ROOT), "-B", native, "-G", "Visual Studio 18 2026", "-A", "x64"] or
                any(not option.startswith("-D") for option in argv[9:]) or
                build.get("argv") != [argv[0], "--build", native, "--config", "Release", "--target", "BodySlots", "-j", "6"]):
            raise ValueError("Build-only evidence must configure this checkout and build only the BodySlots target.")
        definitions = dict(option[2:].split("=", 1) for option in argv[9:])
        required = {"BODY_SLOTS_SKSE_SDK": str(args.skse_root.resolve()),
                    "BODY_SLOTS_HIGGS_SDK": str(args.higgs_root.resolve()),
                    "BODY_SLOTS_PLANCK_SDK": str(args.planck_root.resolve()),
                    "BODY_SLOTS_PPB_SDK": str(args.ppb_root.resolve()),
                    "BODY_SLOTS_PLANCK_POSE_SDK": "", "BODY_SLOTS_HIGGS_GRIP_SDK": ""}
        dependency_options = {"FETCHCONTENT_SOURCE_DIR_" + name.upper(): str(
            (args.dependency_sources / (name + "-src")).resolve()) for name in DEPENDENCIES}
        if (any(definitions.get(name) != value for name, value in required.items()) or
                set(definitions) - set(required) - set(dependency_options) != {"BODY_SLOTS_HIGGS_TEST_MANIFEST"} or
                any(name in definitions and definitions[name] != value for name, value in dependency_options.items()) or
                definitions["BODY_SLOTS_HIGGS_TEST_MANIFEST"] not in verification["sourceSha256"]):
            raise ValueError("Build-only configure evidence differs from its pinned SDK/provider/dependency inputs.")
        required_pins = [ROOT / "CMakeLists.txt", args.skse_root / "sksevr/skse64/PluginAPI.h",
                         args.skse_root / "common/common_license.txt", args.planck_root / "include/planck_synergy.h",
                         args.ppb_root / "src/PpbTouchAPI.h"]
        required_pins += [args.higgs_root / "include" / name for name in (*HIGGS_HEADERS, "higgsinterface001.h")]
        required_pins += [args.dependency_sources / (name + "-src") / "LICENSE" for name in DEPENDENCIES]
        if any(str(path.resolve()) not in verification["sourceSha256"] for path in required_pins):
            raise ValueError("Build-only evidence lacks required SDK/provider/dependency input hashes.")
        if Path(verification["dll"]["path"]).resolve() != Path(native) / "Release/BodySlots.dll":
            raise ValueError("Build-only DLL must be the recorded BodySlots target output.")
        for command in commands:
            checked(Path(command["log"]["path"]), command["log"]["sha256"])
        for field, state_roots in (("sourceStates", roots), ("dependencyStates", {
                name: args.dependency_sources / (name + "-src") for name in DEPENDENCIES})):
            states = verification.get(field, {})
            if set(states) != set(state_roots):
                raise ValueError(f"Missing exact {field} pins in build-only evidence.")
            for name, root in state_roots.items():
                state = states[name]
                if (state.get("root") != str(root.resolve()) or state.get("statusPorcelain") != "" or
                        state.get("diffSha256") != digest(b"") or
                        state.get("revision") != git(root, "rev-parse", "HEAD").decode().strip() or
                        git(root, "status", "--porcelain")):
                    raise ValueError(f"{name} differs from its clean build-only source state.")
        if verification.get("buildDependencies") != DEPENDENCIES:
            raise ValueError("Build-only dependency revisions differ from the pinned packaging dependencies.")
    elif (verification.get("buildOnly", False) is not False or verification.get("testsRun", True) is not True or
          verification.get("dllLoaded", True) is not True or len(commands) != 4 or
          any(command["exitCode"] for command in commands)):
        raise ValueError("Incomplete native configure, build, CTest or DLL discovery evidence.")
    candidate = revisions["bodySlots"]
    label = f"0.2.0-pretest+{candidate[:12]}" if args.build_only else LABEL
    qualification = ("Build only. No tests or DLL loading; installation and Skyrim VR gameplay are unqualified."
                     if args.build_only else "Offline build and contract checks. In-game qualification pending.")
    source = lambda path: blob(ROOT, candidate, path)
    binary = checked(Path(verification["dll"]["path"]), verification["dll"]["sha256"])
    if len(binary) != verification["dll"]["bytes"]:
        raise ValueError("Native build length differs from the verified manifest.")
    notices = {"LICENSE.txt": source("LICENSE"),
               "licenses/HIGGS-GPL-3.0.txt": blob(args.higgs_root, revisions["higgs"], "LICENSE"),
               "licenses/PLANCK-GPL-3.0.txt": blob(args.planck_root, revisions["planck"], "LICENSE"),
               "licenses/PPB-MIT.txt": blob(args.ppb_root, revisions["ppb"], "LICENSE")}
    for name, sha in DEPENDENCIES.items():
        notices[f"licenses/{name}.txt"] = blob(args.dependency_sources / (name + "-src"), sha, "LICENSE")
    notices["licenses/SKSE-common.txt"] = checked(args.skse_root / "common/common_license.txt",
        "68668adf7fb2457ff39292d2fe064ff9b79a41ad528bf9609389592719722511")
    runtime_dir = "SKSE/Plugins/BodySlots/"
    runtime = {runtime_dir + path: data for path, data in notices.items()}
    runtime["SKSE/Plugins/BodySlots.dll"] = binary
    runtime[runtime_dir + "README.txt"] = (
        f"{PRODUCT_NAME} {label} for Skyrim VR 1.4.15 and SKSE VR 2.0.12.\n"
        "Install the runtime archive with your mod manager in a development profile.\n"
        "This framework supplies services for consumer mods; it adds no standalone gameplay feature.\n"
        "A consumer supplies the slot content and interaction.\n"
        "Released HIGGS 001 provides updates for generic regions. Routed input needs the full HIGGS Hand 2 provider.\n"
        "Shared button/dwell arbitration requires the HIGGS ordered proposal version 2 service.\n"
        "Inventory operations use the extended HIGGS provider. Player body-node placement uses VRIK.\n"
        "Consumer examples and their provider requirements are in the separate SDK archive.\n"
        f"{qualification}\n"
        "Formerly Body Slots. DLL, APIs, install paths, messaging recipients and saved keys retain their compatibility names.\n"
        "Framework source: https://github.com/Vhodnoylogin/body-slots/tree/" + candidate + "\n"
        "See LICENSE.txt, licenses/ and PROVENANCE.json for components and source revisions.\n"
    ).encode()
    sdk = dict(notices)
    sdk["README.md"] = source("docs/release-candidate.md").replace(b"](../examples/", b"](examples/").replace(
        b"](region-actions.md)", b"](docs/region-actions.md)").replace(
        b"](naming.md)", b"](docs/naming.md)").replace(b"](release-readiness.md)", b"](docs/release-readiness.md)")
    if args.build_only:
        _, _, guide = sdk["README.md"].partition(b"\n")
        sdk["README.md"] = f"# {PRODUCT_NAME} {label}\n\n{qualification}\n".encode() + guide
    for path in git(ROOT, "ls-tree", "-r", "--name-only", candidate, "include").decode().splitlines():
        if path.endswith(".h"):
            sdk[path] = source(path)
    for name in ("observe_definitions.cpp", "ordinary_inventory_provider.cpp", "ordinary_inventory_provider.h",
                 "planck_region_anchor.cpp", "planck_region_anchor.h", "planck_anchor_transform.h",
                 "npc_pocket_binding.cpp", "npc_pocket_binding.h", "potion-belt.json"):
        sdk["examples/" + name] = source("examples/" + name)
    sdk["tools/author.py"] = source("tools/author.py")
    test_source = f"](https://github.com/Vhodnoylogin/body-slots/blob/{candidate}/src/test/".encode()
    for doc in DOCS:
        path = f"docs/{doc}.md"
        sdk[path] = source(path).replace(b"](../src/test/", test_source)
    for name in HIGGS_HEADERS:
        sdk["third_party/higgs/" + name] = blob(args.higgs_root, revisions["higgs"], "include/" + name)
    sdk["third_party/planck/planck_synergy.h"] = blob(args.planck_root, revisions["planck"], "include/planck_synergy.h")
    common = dict(productName=PRODUCT_NAME, candidate=label, packageSource=candidate, productionSource=candidate,
                  runtimeSha256=digest(binary), nativeBuildManifestSha256=manifest_hash, sourceRevisions=revisions,
                  buildDependencies=DEPENDENCIES,
                  qualification=qualification)
    if args.build_only:
        common.update(buildOnly=True, testsRun=False, dllLoaded=False, gameStarts=0,
                      sourceStates=verification["sourceStates"], dependencyStates=verification["dependencyStates"])
    artifacts = {}
    for kind, entries, location in (("runtime", runtime, runtime_dir), ("sdk", sdk, "")):
        entries[location + "PROVENANCE.json"] = encoded(dict(common, archive=kind,
            sha256={name: digest(data) for name, data in sorted(entries.items())}))
        artifacts[f"{ARCHIVE_PREFIX}-{kind}-{label}.zip"] = archive_bytes(entries)
    output.mkdir(parents=True, exist_ok=True)
    for name, data in artifacts.items():
        (output / name).write_bytes(data)
    checksums = "".join(f"{digest(data)}  {name}\n" for name, data in artifacts.items())
    (output / "SHA256SUMS.txt").write_bytes(checksums.encode())
    print(checksums, end="")


if __name__ == "__main__":
    main()
