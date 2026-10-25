"""Audit draft ZIPs before clean extraction; no native DLL rebuild or game access."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import stat
import sys
import unittest
import zipfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("draft_packager", ROOT / "tools/package_draft.py")
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)
MAX_FILES, MAX_FILE_BYTES, MAX_TOTAL_BYTES = 64, 2 * 1024 * 1024, 4 * 1024 * 1024


def audit_zip(content, expected):
    with zipfile.ZipFile(io.BytesIO(content)) as archive:
        entries = archive.infolist()
        if not 0 < len(entries) <= MAX_FILES:
            raise ValueError("entry count")
        seen, total = set(), 0
        for info in entries:
            name = info.filename
            parts = name.split("/")
            if not name or "\\" in name or ":" in name or name.startswith("/") or any(ord(c) < 32 for c in name):
                raise ValueError("absolute/drive/UNC/ADS/control path")
            if any(not part or part in (".", "..") or part.rstrip(" .") != part for part in parts):
                raise ValueError("traversal/Windows-normalized path")
            for part in parts:
                stem = part.split(".")[0].casefold()
                if stem in ("con", "prn", "aux", "nul") or re.fullmatch(r"(?:com|lpt)[1-9]", stem):
                    raise ValueError("reserved Windows name")
            normalized = "/".join(part.casefold() for part in parts)
            if normalized in seen:
                raise ValueError("duplicate/case collision")
            seen.add(normalized)
            kind = stat.S_IFMT(info.external_attr >> 16)
            if info.is_dir() or kind not in (0, stat.S_IFREG) or info.flag_bits & 1:
                raise ValueError("symlink/special/directory/encrypted entry")
            total += info.file_size
            if info.file_size > MAX_FILE_BYTES or total > MAX_TOTAL_BYTES:
                raise ValueError("uncompressed size limit")
        if {entry.filename for entry in entries} != set(expected):
            raise ValueError("allowlist mismatch")
        if archive.testzip() is not None:
            raise ValueError("CRC failure")
        return {entry.filename: archive.read(entry) for entry in entries}


def fixture_zip(names, mode=stat.S_IFREG | 0o644, data=b"x"):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name in names:
            info = zipfile.ZipInfo(name)
            info.create_system = 3
            info.external_attr = mode << 16
            archive.writestr(info, data)
    return stream.getvalue()


class UnsafeArchiveTests(unittest.TestCase):
    def test_paths(self):
        for name in ("../x", "/x", "C:/x", "//server/x", "x:stream", "a\\b", "a/./b", "a//b", "x ", "x.", "CON.txt", "a/NUL", "COM1/x"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                audit_zip(fixture_zip([name]), {name})

    def test_collisions_and_duplicates(self):
        for names in (("A.txt", "a.txt"), ("same", "same")):
            with self.subTest(names=names), self.assertRaises(ValueError):
                audit_zip(fixture_zip(names), set(names))

    def test_special_entries(self):
        for kind in (stat.S_IFLNK, stat.S_IFIFO, stat.S_IFDIR, stat.S_IFSOCK):
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                audit_zip(fixture_zip(["entry"], kind | 0o777), {"entry"})

    def test_bounds_allowlist_crc(self):
        with self.assertRaises(ValueError):
            audit_zip(fixture_zip([str(i) for i in range(65)]), {str(i) for i in range(65)})
        with self.assertRaises(ValueError):
            audit_zip(fixture_zip(["large"], data=b"x" * (MAX_FILE_BYTES + 1)), {"large"})
        with self.assertRaises(ValueError):
            audit_zip(fixture_zip(["unexpected"]), {"expected"})
        corrupt = bytearray(fixture_zip(["entry"], data=b"payload"))
        corrupt[30 + len("entry")] ^= 1
        with self.assertRaises((ValueError, zipfile.BadZipFile)):
            audit_zip(bytes(corrupt), {"entry"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--expected-dll-sha256", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--extract-sdk", type=Path, required=True)
    args = parser.parse_args()
    extraction = args.extract_sdk.resolve()
    if extraction == ROOT or ROOT in extraction.parents or extraction.exists() or any((p / ".git").exists() for p in extraction.parents):
        parser.error("SDK extraction must be a new path outside Git")
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(UnsafeArchiveTests))
    if not result.wasSuccessful():
        raise SystemExit(1)
    frozen = args.dll.read_bytes()
    if packager.digest(frozen) != args.expected_dll_sha256:
        raise ValueError("input DLL hash")
    expected = packager.prepare(ROOT, args.dll, args.expected_dll_sha256, args.source_commit, args.label)
    sidecar = json.loads((args.packages / "draft-artifacts.json").read_text(encoding="utf-8"))
    checksums = (args.packages / "SHA256SUMS.txt").read_text(encoding="utf-8")
    extracted = None
    report = {"unsafeArchiveTestMethods": result.testsRun, "sourceCommit": args.source_commit, "draftLabel": args.label,
        "dllSha256": args.expected_dll_sha256, "archives": {}, "gameStarts": 0, "nativeRebuilds": 0}
    for name, artifact in expected.items():
        content = (args.packages / name).read_bytes()
        if content != artifact["bytes"] or sidecar["artifacts"][name]["sha256"] != packager.digest(content):
            raise ValueError("deterministic bytes/hash mismatch")
        if packager.digest(content) + "  " + name not in checksums.splitlines():
            raise ValueError("checksum sidecar")
        files = audit_zip(content, artifact["contents"])
        manifest_name = "PROVENANCE.json" if "-sdk-" in name else "SKSE/Plugins/BodySlots/PROVENANCE.json"
        provenance = json.loads(files[manifest_name])
        if provenance["productionSourceCommit"] != args.source_commit or provenance["productionDll"]["sha256"] != args.expected_dll_sha256 or provenance["productionDll"]["rebuiltForPackaging"]:
            raise ValueError("provenance")
        declarations = {entry["path"]: entry for entry in provenance["contents"]}
        if set(declarations) != set(files) - {manifest_name}:
            raise ValueError("manifest allowlist")
        for path, declaration in declarations.items():
            if packager.digest(files[path]) != declaration["sha256"] or len(files[path]) != declaration["bytes"]:
                raise ValueError("payload checksum")
        for path, data in files.items():
            if path.endswith((".dll", ".bslots")):
                continue
            text = data.decode("utf-8")
            if re.search(r"(?<![A-Za-z0-9])[A-Za-z]:[\\/]|/(?:home|Users|mnt)/|\\\\[^\\\s]+\\", text):
                raise ValueError("machine path in " + path)
            if re.search(r"(?:github_pat_|gh[pousr]_)[A-Za-z0-9_]{12,}", text):
                raise ValueError("credential pattern")
        if "-runtime-" in name:
            if files["SKSE/Plugins/BodySlots.dll"] != frozen or any(path.endswith(".bslots") for path in files):
                raise ValueError("runtime DLL/demo isolation")
        else:
            if {path for path in files if path.startswith("include/")} != {"include/" + header for header in packager.HEADERS}:
                raise ValueError("seven own headers")
            for header in packager.HEADERS:
                if files["include/" + header] != (ROOT / "include" / header).read_bytes():
                    raise ValueError("header bytes")
            extracted = files
        report["archives"][name] = {"sha256": packager.digest(content), "entries": sorted(files), "entryCount": len(files), "uncompressedBytes": sum(map(len, files.values())), "CRC": "passed", "allowlist": "exact", "machinePathsOrCredentials": "none detected"}
    if args.dll.read_bytes() != frozen or extracted is None:
        raise ValueError("input DLL changed or SDK missing")
    # All archives/paths/limits/CRCs/hashes were audited BEFORE any extraction.
    extraction.mkdir(parents=True)
    for name, data in extracted.items():
        target = extraction.joinpath(*name.split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (args.packages / "package-check.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("PASS exact runtime/SDK ZIP allowlists, provenance/CRC/bounds/path/type/collision audit, frozen DLL before/inside/after; clean SDK extracted")


if __name__ == "__main__":
    main()
