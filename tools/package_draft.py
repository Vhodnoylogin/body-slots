#!/usr/bin/env python3
"""Create explicit-allowlist Nexus DRAFT archives; never rebuild or publish a DLL."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import sys
import zipfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
HEADERS = (
    "body_slots_api.h", "body_slots_configuration_api.h", "body_slots_instance_api.h",
    "body_slots_instance_configuration_api.h", "body_slots_instance_activation_api.h",
    "body_slots_native_placement_api.h", "body_slots_native_policy_api.h",
)
DOCS = ("configuration.md", "instances.md", "instance-configuration.md", "instance-activation.md", "native-placement.md", "native-policy.md")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def archive_bytes(entries):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data, compresslevel=9)
    return stream.getvalue()


def prepare(root, dll, expected_dll_sha256, source_commit, label):
    if not re.fullmatch(r"[0-9a-f]{64}", expected_dll_sha256):
        raise ValueError("expected DLL SHA256 must be 64 lowercase hex digits")
    if not re.fullmatch(r"[0-9a-f]{40}", source_commit):
        raise ValueError("source commit must be a full 40-digit lowercase commit ID")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", label):
        raise ValueError("draft label must be a bounded filename-safe label")
    binary = dll.read_bytes()
    if digest(binary) != expected_dll_sha256:
        raise ValueError("DLL hash mismatch; the frozen input must not be replaced or rebuilt")
    runtime = {
        "SKSE/Plugins/BodySlots.dll": binary,
        "SKSE/Plugins/BodySlots/README.txt": (root / "docs/runtime-draft-readme.txt").read_bytes(),
    }
    sdk = {"README.md": (root / "docs/sdk-draft-readme.md").read_bytes()}
    for name in HEADERS:
        sdk["include/" + name] = (root / "include" / name).read_bytes()
    for name in DOCS:
        sdk["docs/" + name] = (root / "docs" / name).read_bytes()
    for name in ("tools/author.py", "examples/potion-belt.json", "examples/observe_definitions.cpp"):
        sdk[name] = (root / name).read_bytes()
    spec = importlib.util.spec_from_file_location("body_slots_draft_author", root / "tools/author.py")
    author = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(author)
    sdk["examples/potion-belt.bslots"] = author.compile_project(author.load_project(root / "examples/potion-belt.json"))
    archives = {}
    for kind, entries, manifest_name in (("runtime", runtime, "SKSE/Plugins/BodySlots/PROVENANCE.json"), ("sdk", sdk, "PROVENANCE.json")):
        provenance = {
            "format": "body-slots-nexus-draft-v1", "draftLabel": label, "archiveKind": kind,
            "purpose": "Unpublished mod-author foundation snapshot; not gameplay or released VRIK integration",
            "licensing": "Not finalized before publication; no license grant is included",
            "productionSourceCommit": source_commit,
            "productionDll": {"name": "BodySlots.dll", "sha256": expected_dll_sha256, "bytes": len(binary), "rebuiltForPackaging": False},
            "legacyPluginInfoVersion": 2,
            "abiVersions": {"registry": 1, "configuration": 1, "instances": 1, "instanceConfiguration": 1, "instanceActivation": 1, "nativePlacement": 1, "nativePolicy": 1},
            "packagingToolSha256": digest(Path(__file__).read_bytes()),
            "contents": [{"path": path, "bytes": len(data), "sha256": digest(data)} for path, data in sorted(entries.items())],
            "qualification": "Offline actual-DLL and synthetic-host checks; no Skyrim runtime acceptance claimed",
        }
        entries[manifest_name] = encoded(provenance)
        filename = "BodySlots-" + kind + "-" + label + ".zip"
        content = archive_bytes(entries)
        archives[filename] = {"bytes": content, "sha256": digest(content), "contents": sorted(entries)}
    return archives


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--expected-dll-sha256", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents or any((p / ".git").exists() for p in (output, *output.parents)):
        parser.error("output must be outside Git")
    if output.exists() and any(output.iterdir()):
        parser.error("output must be new or empty")
    try:
        archives = prepare(ROOT, args.dll, args.expected_dll_sha256, args.source_commit, args.label)
        output.mkdir(parents=True, exist_ok=True)
        checksums = []
        result = {"draftLabel": args.label, "productionSourceCommit": args.source_commit, "licensing": "not finalized; no grant included", "artifacts": {}}
        for name, artifact in archives.items():
            with (output / name).open("xb") as stream:
                stream.write(artifact["bytes"])
            checksums.append(artifact["sha256"] + "  " + name)
            result["artifacts"][name] = {"sha256": artifact["sha256"], "bytes": len(artifact["bytes"]), "contents": artifact["contents"]}
            print(name + " SHA256 " + artifact["sha256"])
        (output / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="utf-8")
        (output / "draft-artifacts.json").write_bytes(encoded(result))
    except (OSError, ValueError) as error:
        parser.exit(1, "Draft packaging failed: " + str(error) + "\n")


if __name__ == "__main__":
    main()
