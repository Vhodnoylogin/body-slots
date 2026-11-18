#!/usr/bin/env python3
"""Exercise package output contracts with inert DLL/provider fixtures, never a game build.

Candidate Git/provider discovery and the fixed SKSE-licence pin are substituted;
archive assembly, source payloads, manifest hashes and DLL hash checks run normally.
All temporary artifacts are created outside Git and removed after each test.
"""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import package_candidate as candidate
import package_draft as draft


class PackageContracts(unittest.TestCase):
    def setUp(self):
        output_root = ROOT.parent.parent / "Builds" / "body-slots-package-contract-tests"
        output_root.mkdir(parents=True, exist_ok=True)
        self.workspace = tempfile.TemporaryDirectory(dir=output_root)
        self.addCleanup(self.workspace.cleanup)
        self.directory = Path(self.workspace.name)
        self.binary = b"Inert package-contract fixture; not a native DLL.\n"
        self.dll = self.directory / "BodySlots.dll"
        self.dll.write_bytes(self.binary)
        self.sha = hashlib.sha256(self.binary).hexdigest()
        self.license = (ROOT / "LICENSE").read_bytes()

    def read_archive(self, content):
        with zipfile.ZipFile(io.BytesIO(content)) as archive:
            self.assertEqual(archive.namelist(), sorted(archive.namelist()))
            self.assertTrue(all(entry.date_time == (2020, 1, 1, 0, 0, 0) for entry in archive.infolist()))
            return {name: archive.read(name) for name in archive.namelist()}

    def verify_hashes(self, entries, manifest_path):
        manifest = json.loads(entries[manifest_path])
        self.assertEqual(manifest["productName"], "VR Interaction Framework")
        payload = {name: content for name, content in entries.items() if name != manifest_path}
        if "contents" in manifest:
            expected = {item["path"]: item["sha256"] for item in manifest["contents"]}
            self.assertEqual({item["path"]: item["bytes"] for item in manifest["contents"]},
                             {name: len(content) for name, content in payload.items()})
        else:
            expected = manifest["sha256"]
        self.assertEqual(expected, {name: draft.digest(content) for name, content in payload.items()})
        self.assertNotIn(str(self.directory), json.dumps(manifest))
        return manifest

    def test_draft_archives_preserve_identity_licence_and_provenance(self):
        artifacts = draft.prepare(ROOT, self.dll, self.sha, "1" * 40, "contract-test")
        self.assertEqual(set(artifacts), {"VRInteractionFramework-runtime-contract-test.zip",
                                        "VRInteractionFramework-sdk-contract-test.zip"})
        repeated = draft.prepare(ROOT, self.dll, self.sha, "1" * 40, "contract-test")
        for name, artifact in artifacts.items():
            self.assertEqual(artifact["bytes"], repeated[name]["bytes"])
            entries = self.read_archive(artifact["bytes"])
            location = "SKSE/Plugins/BodySlots/" if "-runtime-" in name else ""
            manifest = self.verify_hashes(entries, location + "PROVENANCE.json")
            self.assertEqual(entries[location + "LICENSE.txt"], self.license)
            self.assertIn("MIT", manifest["licensing"])
            self.assertEqual(manifest["productionDll"]["name"], "BodySlots.dll")
            self.assertEqual(manifest["productionDll"]["sha256"], self.sha)
            if location:
                self.assertEqual(entries["SKSE/Plugins/BodySlots.dll"], self.binary)
                self.assertTrue(entries[location + "README.txt"].startswith(b"VR Interaction Framework"))
                self.assertEqual(len(entries), 4)
            else:
                self.assertTrue(entries["README.md"].startswith(b"# VR Interaction Framework SDK"))
                self.assertIn("docs/naming.md", entries)
                self.assertIn("examples/potion-belt.bslots", entries)
                self.assertEqual(entries["include/body_slots_api.h"], (ROOT / "include/body_slots_api.h").read_bytes())

    def test_draft_rejects_changed_dll(self):
        self.dll.write_bytes(self.binary + b"changed")
        with self.assertRaisesRegex(ValueError, "DLL hash mismatch"):
            draft.prepare(ROOT, self.dll, self.sha, "1" * 40, "contract-test")

    def candidate_fixture(self, output, dirty=False):
        providers = {name: self.directory / name for name in ("higgs", "planck", "ppb")}
        revisions = {name: str(index) * 40 for index, name in enumerate(("bodySlots", "higgs", "planck", "ppb"), 1)}
        manifest = {"sourceRevisions": revisions, "sourceSha256": {},
                    "commands": [{"exitCode": 0} for _ in range(4)],
                    "dll": {"path": str(self.dll), "sha256": self.sha, "bytes": len(self.binary)}}
        (self.directory / "manifest.json").write_bytes(draft.encoded(manifest))
        skse_licence = self.directory / "skse/common/common_license.txt"
        notices = {"LICENSE.txt": self.license,
                   "licenses/SKSE-common.txt": b"SKSE licence fixture\n"}
        notices.update({f"licenses/{name.upper()}-{'MIT' if name == 'ppb' else 'GPL-3.0'}.txt":
                        f"{name} provider licence fixture\n".encode() for name in providers})
        notices.update({f"licenses/{name}.txt": f"{name} dependency licence fixture\n".encode()
                        for name in candidate.DEPENDENCIES})

        def git(root, *args):
            if args == ("rev-parse", "HEAD"):
                name = "bodySlots" if root == ROOT else next(name for name, path in providers.items() if root == path)
                return (revisions[name] + "\n").encode()
            if args == ("status", "--porcelain"):
                return b" M README.md\n" if dirty and root == ROOT else b""
            if args[:2] == ("ls-tree", "-r"):
                return "\n".join(path.relative_to(ROOT).as_posix() for path in sorted((ROOT / "include").rglob("*.h"))).encode()
            raise AssertionError(args)

        def blob(root, revision, path):
            if root == ROOT:
                self.assertEqual(revision, revisions["bodySlots"])
                return (ROOT / path).read_bytes()
            if path == "LICENSE":
                if root in providers.values():
                    name = next(name for name, folder in providers.items() if folder == root)
                    return f"{name} provider licence fixture\n".encode()
                return f"{root.name.removesuffix('-src')} dependency licence fixture\n".encode()
            return f"// {root.name} fixture header {path}\n".encode()

        checked = candidate.checked

        def fixture_checked(path, expected):
            if path == skse_licence:
                return notices["licenses/SKSE-common.txt"]
            return checked(path, expected)

        arguments = ["package_candidate.py", "--native-build", str(self.directory),
                     "--dependency-sources", str(self.directory / "dependencies"),
                     "--skse-root", str(self.directory / "skse"), "--output", str(output)]
        for name, root in providers.items():
            arguments.extend([f"--{name}-root", str(root)])
        with patch.object(candidate, "git", git), patch.object(candidate, "blob", blob), \
                patch.object(candidate, "checked", fixture_checked), patch.object(sys, "argv", arguments), \
                contextlib.redirect_stdout(io.StringIO()):
            candidate.main()
        return notices, revisions

    def test_candidate_archive_labels_notices_legacy_paths_and_checksums(self):
        output = self.directory / "candidate-one"
        notices, revisions = self.candidate_fixture(output)
        repeat = self.directory / "candidate-two"
        self.candidate_fixture(repeat)
        expected_names = {f"VRInteractionFramework-{kind}-0.2.0-rc.4.zip" for kind in ("runtime", "sdk")}
        self.assertEqual({path.name for path in output.glob("*.zip")}, expected_names)
        for name in sorted(expected_names):
            content = (output / name).read_bytes()
            self.assertEqual(content, (repeat / name).read_bytes())
            self.assertIn(f"{draft.digest(content)}  {name}\n", (output / "SHA256SUMS.txt").read_text())
            entries = self.read_archive(content)
            location = "SKSE/Plugins/BodySlots/" if "-runtime-" in name else ""
            manifest = self.verify_hashes(entries, location + "PROVENANCE.json")
            self.assertEqual(manifest["sourceRevisions"], revisions)
            self.assertEqual(manifest["runtimeSha256"], self.sha)
            for notice, expected in notices.items():
                self.assertEqual(entries[location + notice], expected)
            if location:
                self.assertEqual(entries["SKSE/Plugins/BodySlots.dll"], self.binary)
                self.assertTrue(entries[location + "README.txt"].startswith(b"VR Interaction Framework 0.2.0-rc.4"))
                self.assertEqual(len(entries), len(notices) + 3)
            else:
                self.assertTrue(entries["README.md"].startswith(b"# VR Interaction Framework 0.2.0-rc.4"))
                self.assertIn(b"](docs/naming.md)", entries["README.md"])
                self.assertIn(b"](docs/release-readiness.md)", entries["README.md"])
                self.assertIn("include/body_slots_region_dwell_api.h", entries)
                self.assertIn("examples/npc_pocket_binding.cpp", entries)

    def test_candidate_rejects_dirty_source_and_changed_dll(self):
        with self.assertRaisesRegex(ValueError, "differs from the verified committed source"):
            self.candidate_fixture(self.directory / "dirty", dirty=True)
        self.dll.write_bytes(self.binary + b"changed")
        with self.assertRaisesRegex(ValueError, "Unexpected input hash"):
            self.candidate_fixture(self.directory / "altered")


if __name__ == "__main__":
    unittest.main()
