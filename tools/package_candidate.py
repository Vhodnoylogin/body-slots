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
LABEL = "0.2.0-rc.1"
PRODUCTION = "16c33f8b5f838a7802aa4c54d207715bbc83c4d7"
HIGGS = "64b334f287e8c55d37ad8530a4f7c1ec1eb03cfa"
PLANCK = "d824997163e9a458be95f76b4fcb010a42afd475"
DLL = "09685e7270896dea315d66996937115b0b459f9bd5788228dd729ae4e1fe6980"
MANIFEST = "4b549c7424c4f82d3232b5562ddc55a5f59a77687238f525380fab8f73043351"
DEPENDENCIES = {
    "commonlibsse": "c4ab853d095e81e3390b282d7ba01ab2f24ebf25",
    "fmt": "0c9fce2ffefecfdce794e1859584e25877b7b592",
    "spdlog": "8e5613379f5140fefb0b60412fbf1f5406e7c7f8",
    "rapidcsv": "68f57cc6c83d5e0992904398822453489d8dfac1",
}
HIGGS_HEADERS = ("higgshandapi.h", "higgshandrouting.h", "higgsinventory.h", "higgstransactions.h")
DOCS = ("configuration", "content-observation", "game-adapter", "higgs-native-regions", "instance-activation",
        "instance-configuration", "instances", "interaction-regions", "native-placement", "native-policy",
        "ordinary-inventory-provider", "region-actions", "runtime", "runtime-extensions", "planck-region-anchor")


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
    for option in ("native-build", "dependency-sources", "skse-root", "higgs-root", "planck-root", "output"):
        parser.add_argument("--" + option, type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if any((p / ".git").exists() for p in (output, *output.parents)):
        parser.error("Choose an output directory outside Git.")
    if output.exists() and any(output.iterdir()):
        parser.error("Choose a new or empty output directory.")

    candidate = git(ROOT, "rev-parse", "HEAD").decode().strip()
    # The DLL predates the packaging/SDK changes. Do not pair it with different runtime code.
    if git(ROOT, "diff", "--name-only", PRODUCTION, candidate, "--", "src", "include", ":(exclude)src/test"):
        raise ValueError("Candidate runtime sources or headers differ from the native build.")
    if git(ROOT, "status", "--porcelain", "--", "tools/package_candidate.py", "tools/package_draft.py"):
        raise ValueError("Commit the packaging recipe before preparing the archives.")
    source = lambda path: blob(ROOT, candidate, path)
    native_manifest = json.loads(checked(args.native_build / "manifest.json", MANIFEST))
    binary = checked(args.native_build / "native/Release/BodySlots.dll", DLL)
    if native_manifest["dll"]["sha256"] != DLL or native_manifest["dll"]["bytes"] != len(binary):
        raise ValueError("Frozen manifest and runtime binary disagree.")

    notices = {"LICENSE.txt": source("LICENSE"),
               "licenses/HIGGS-GPL-3.0.txt": blob(args.higgs_root, HIGGS, "LICENSE")}
    for name, sha in DEPENDENCIES.items():
        notices[f"licenses/{name}.txt"] = blob(args.dependency_sources / (name + "-src"), sha, "LICENSE")
    notices["licenses/SKSE-common.txt"] = checked(args.skse_root / "common/common_license.txt",
        "68668adf7fb2457ff39292d2fe064ff9b79a41ad528bf9609389592719722511")
    runtime_dir = "SKSE/Plugins/BodySlots/"
    runtime = {runtime_dir + path: data for path, data in notices.items()}
    runtime["SKSE/Plugins/BodySlots.dll"] = binary
    runtime[runtime_dir + "README.txt"] = (
        "Body Slots 0.2.0-rc.1 for Skyrim VR 1.4.15 and SKSE VR 2.0.12.\n"
        "Install the runtime archive with your mod manager in a development profile.\n"
        "This candidate supplies services for mod authors. A consumer supplies the slot content and interaction.\n"
        "Interactive actions require the extended HIGGS provider. Player body-node placement requires VRIK.\n"
        "Consumer examples and their provider requirements are in the separate SDK archive.\n"
        "Offline verified. The game integration test is pending.\n"
        "Body Slots source: https://github.com/Vhodnoylogin/body-slots/tree/" + candidate + "\n"
        "See LICENSE.txt, licenses/ and PROVENANCE.json for components and source revisions.\n"
    ).encode()
    sdk = dict(notices)
    sdk["licenses/PLANCK-GPL-3.0.txt"] = blob(args.planck_root, PLANCK, "LICENSE")
    sdk["README.md"] = source("docs/release-candidate.md")
    for path in git(ROOT, "ls-tree", "-r", "--name-only", candidate, "include").decode().splitlines():
        if path.endswith(".h"):
            sdk[path] = source(path)
    for name in ("observe_definitions.cpp", "ordinary_inventory_provider.cpp", "ordinary_inventory_provider.h",
                 "planck_region_anchor.cpp", "planck_region_anchor.h", "potion-belt.json"):
        sdk["examples/" + name] = source("examples/" + name)
    sdk["tools/author.py"] = source("tools/author.py")
    test_source = f"](https://github.com/Vhodnoylogin/body-slots/blob/{candidate}/src/test/".encode()
    for doc in DOCS:
        path = f"docs/{doc}.md"
        sdk[path] = source(path).replace(b"](../src/test/", test_source)
    for name in HIGGS_HEADERS:
        sdk["third_party/higgs/" + name] = blob(args.higgs_root, HIGGS, "include/" + name)
    sdk["third_party/planck/planck_synergy.h"] = blob(args.planck_root, PLANCK, "include/planck_synergy.h")
    common = dict(candidate=LABEL, packageSource=candidate, productionSource=PRODUCTION,
                  runtimeSha256=DLL, nativeBuildManifestSha256=MANIFEST, legacyPluginInfoVersion=3,
                  nativeHiggsSource="cf1cf3ecddeaa5750b6e470eaf4972f2619344c2",
                  sdkHiggsSource=HIGGS, sdkPlanckSource=PLANCK, buildDependencies=DEPENDENCIES,
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
