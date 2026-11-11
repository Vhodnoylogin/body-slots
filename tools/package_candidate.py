#!/usr/bin/env python3
"""Package the verified Body Slots 0.2.0 candidate from committed source and its original native build."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from package_draft import archive_bytes, digest, encoded

ROOT = Path(__file__).resolve().parents[1]
LABEL = "0.2.0-rc.3"
DEPENDENCIES = {
    "commonlibsse": "c4ab853d095e81e3390b282d7ba01ab2f24ebf25",
    "fmt": "0c9fce2ffefecfdce794e1859584e25877b7b592",
    "spdlog": "8e5613379f5140fefb0b60412fbf1f5406e7c7f8",
    "rapidcsv": "68f57cc6c83d5e0992904398822453489d8dfac1",
}
HIGGS_HEADERS = ("higgshandapi.h", "higgshandrouting.h", "higgsinputroutes.h", "higgsauthorizeddispatch.h", "higgsgrantmutation.h", "higgsinventory.h", "higgstransactions.h")
DOCS = ("configuration", "content-observation", "game-adapter", "higgs-native-regions", "instance-activation",
        "instance-configuration", "instances", "interaction-regions", "native-placement", "native-policy",
        "ordinary-inventory-provider", "region-actions", "runtime", "runtime-extensions", "planck-region-anchor", "naming")


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
    if len(commands) != 4 or any(command["exitCode"] for command in commands):
        raise ValueError("Incomplete native configure, build, CTest or DLL discovery evidence.")
    candidate = revisions["bodySlots"]
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
        f"Body Slots {LABEL} for Skyrim VR 1.4.15 and SKSE VR 2.0.12.\n"
        "Install the runtime archive with your mod manager in a development profile.\n"
        "This candidate supplies services for mod authors. A consumer supplies the slot content and interaction.\n"
        "Released HIGGS 001 provides updates for generic regions. Routed input needs the full HIGGS Hand 2 provider.\n"
        "Inventory operations use the extended HIGGS provider. Player body-node placement uses VRIK.\n"
        "Consumer examples and their provider requirements are in the separate SDK archive.\n"
        "Offline verified. The game integration test is pending.\n"
        "Body Slots source: https://github.com/Vhodnoylogin/body-slots/tree/" + candidate + "\n"
        "See LICENSE.txt, licenses/ and PROVENANCE.json for components and source revisions.\n"
    ).encode()
    sdk = dict(notices)
    sdk["README.md"] = source("docs/release-candidate.md").replace(b"](../examples/", b"](examples/").replace(
        b"](region-actions.md)", b"](docs/region-actions.md)")
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
    common = dict(candidate=LABEL, packageSource=candidate, productionSource=candidate,
                  runtimeSha256=digest(binary), nativeBuildManifestSha256=manifest_hash, sourceRevisions=revisions,
                  buildDependencies=DEPENDENCIES,
                  qualification="Offline build and contract checks. In-game qualification pending.")
    artifacts = {}
    for kind, entries, location in (("runtime", runtime, runtime_dir), ("sdk", sdk, "")):
        entries[location + "PROVENANCE.json"] = encoded(dict(common, archive=kind,
            sha256={name: digest(data) for name, data in sorted(entries.items())}))
        artifacts[f"BodySlots-{kind}-{LABEL}.zip"] = archive_bytes(entries)
    output.mkdir(parents=True, exist_ok=True)
    for name, data in artifacts.items():
        (output / name).write_bytes(data)
    checksums = "".join(f"{digest(data)}  {name}\n" for name, data in artifacts.items())
    (output / "SHA256SUMS.txt").write_bytes(checksums.encode())
    print(checksums, end="")


if __name__ == "__main__":
    main()
