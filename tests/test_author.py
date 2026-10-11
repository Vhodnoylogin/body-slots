import copy
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("body_slots_author", ROOT / "tools" / "author.py")
author = importlib.util.module_from_spec(spec)
spec.loader.exec_module(author)


class AuthorTests(unittest.TestCase):
    def setUp(self):
        self.project = json.loads((ROOT / "examples" / "potion-belt.json").read_text())

    def rejects(self, transform):
        transform(self.project)
        with self.assertRaises(author.ValidationError):
            author.compile_project(self.project)

    def test_example_layout(self):
        data = author.compile_project(self.project)
        self.assertEqual(len(data), 520)
        self.assertEqual(data[:8], b"BSLOTS\r\n")
        # Independent field reads, without the compiler's Struct definition.
        self.assertEqual(struct.unpack_from("<6I", data, 8), (1, 112, 408, 1, 408, 0))
        self.assertEqual(data[32:96].rstrip(b"\0"), b"example.potion-belt")
        self.assertEqual(struct.unpack_from("<II", data, 112), (408, 1))
        self.assertEqual(data[112 + 72:112 + 136].rstrip(b"\0"), b"potion/front-left")
        self.assertEqual(struct.unpack_from("<3f", data, 112 + 328), (6, 14, 0))
        self.assertEqual(struct.unpack_from("<f3f4I", data, 112 + 376), (10, 0, 0, 2, 1, 8, 0, 0))

    def test_duplicate_json_fields(self):
        for raw in ('{"provider":"a","provider":"b"}', '{"slots":[{"key":"a","key":"b"}]}'):
            with tempfile.TemporaryDirectory() as folder:
                path = Path(folder) / "bad.json"
                path.write_text(raw)
                with self.assertRaises(author.ValidationError):
                    author.load_project(path)

    def test_unknown_fields(self):
        self.rejects(lambda p: p.update(extra=1))

    def test_unknown_slot_fields(self):
        self.rejects(lambda p: p["slots"][0].update(extra=1))

    def test_unknown_region_fields(self):
        self.rejects(lambda p: p["slots"][0]["accessRegion"].update(extra=1))

    def test_missing_fields(self):
        self.rejects(lambda p: p.pop("axes"))

    def test_versions_and_conventions(self):
        for field, value in (("schemaVersion", 2), ("schemaVersion", True), ("referenceRig", "other"),
                             ("units", "meters"), ("axes", "left-handed-y-up")):
            project = copy.deepcopy(self.project)
            project[field] = value
            with self.assertRaises(author.ValidationError):
                author.compile_project(project)

    def test_keys(self):
        for value in ("", "x" * 64, "bad:key", "bad\nkey", "привет", 3):
            project = copy.deepcopy(self.project)
            project["slots"][0]["key"] = value
            with self.assertRaises(author.ValidationError):
                author.compile_project(project)

    def test_duplicate_keys(self):
        self.rejects(lambda p: p["slots"].append(copy.deepcopy(p["slots"][0])))

    def test_anchor(self):
        self.rejects(lambda p: p["slots"][0].update(anchor="x" * 96))

    def test_kind(self):
        self.rejects(lambda p: p["slots"][0].update(kind="x" * 32))

    def test_masks(self):
        for field, value in (("hands", []), ("hands", ["both"]), ("hands", ["left", "left"]),
                             ("contents", []), ("contents", ["magic"]), ("contents", ["potion", "potion"]),
                             ("scaleMode", "fixed"), ("enabled", 1)):
            project = copy.deepcopy(self.project)
            project["slots"][0][field] = value
            with self.assertRaises(author.ValidationError):
                author.compile_project(project)

    def test_nonfinite_and_number_bounds(self):
        for value in (float("nan"), float("inf"), float("-inf"), 1e100, 1000001, True, "4"):
            project = copy.deepcopy(self.project)
            project["slots"][0]["position"][0] = value
            with self.assertRaises(author.ValidationError):
                author.compile_project(project)
        for raw in ("NaN", "Infinity", "-Infinity"):
            with tempfile.TemporaryDirectory() as folder:
                path = Path(folder) / "bad.json"
                path.write_text('{"value":' + raw + '}')
                with self.assertRaises(author.ValidationError):
                    author.load_project(path)

    def test_matrix(self):
        for matrix in ([[1, 0, 0], [0, 1, 0], [0, 0, -1]], [[2, 0, 0], [0, 1, 0], [0, 0, 1]],
                       [[1, 0, 0], [0.1, 1, 0], [0, 0, 1]], [[1, 0, 0], [0, 1, 0]], [1, 0, 0]):
            project = copy.deepcopy(self.project)
            project["slots"][0]["rotation"] = matrix
            with self.assertRaises(author.ValidationError):
                author.compile_project(project)

    def test_region(self):
        for field, value in (("shape", "box"), ("radius", 0), ("radius", -1), ("center", [1, 2]), ("radius", float("nan"))):
            project = copy.deepcopy(self.project)
            project["slots"][0]["accessRegion"][field] = value
            with self.assertRaises(author.ValidationError):
                author.compile_project(project)

    def test_count_bounds(self):
        self.project["slots"] = []
        self.assertEqual(len(author.compile_project(self.project)), 112)
        sample = json.loads((ROOT / "examples" / "potion-belt.json").read_text())["slots"][0]
        self.project["slots"] = [dict(copy.deepcopy(sample), key=str(i)) for i in range(256)]
        self.assertEqual(len(author.compile_project(self.project)), 104560)
        self.rejects(lambda p: p["slots"].append(dict(copy.deepcopy(sample), key="256")))

    def test_deterministic_sort_and_atomic_output(self):
        slot = copy.deepcopy(self.project["slots"][0])
        slot["key"] = "aaa"
        self.project["slots"].append(slot)
        first = author.compile_project(self.project)
        self.project["slots"].reverse()
        self.assertEqual(first, author.compile_project(self.project))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "package.bslots"
            author.write_package(path, first)
            self.assertEqual(path.read_bytes(), first)
            self.assertEqual(len(list(Path(folder).iterdir())), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
