#!/usr/bin/env python3
"""Strict offline Body Slots v1 JSON authoring validator and package compiler."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import struct
import tempfile

VERSION = 1
HEADER_SIZE = 112
DEFINITION_SIZE = 408
MAX_DEFINITIONS = 256
MAX_JSON_BYTES = 1024 * 1024
RIG = "skyrimvr-humanoid-v1"
UNITS = "skyrim-engine-unit"
AXES = "right-handed-z-up"
HANDS = {"left": 1, "right": 2}
CONTENTS = {"weapon": 1, "shield": 2, "torch": 4, "potion": 8, "food": 16, "tool": 32, "other": 64}
RECORD = struct.Struct("<II64s64s96s64s32s3f9ff3fIIII")
HEADER = struct.Struct("<8s6I64s16s")
assert RECORD.size == DEFINITION_SIZE and HEADER.size == HEADER_SIZE


class ValidationError(ValueError):
    pass


def fail(message):
    raise ValidationError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def fields(value, names, label):
    if type(value) is not dict:
        fail(f"{label} must be an object")
    missing = set(names) - value.keys()
    extra = value.keys() - set(names)
    if missing or extra:
        fail(f"{label}: missing fields {sorted(missing)}, unknown fields {sorted(extra)}")


def text(value, size, label, identity=False):
    if type(value) is not str or not 0 < len(value) < size:
        fail(f"{label}: expected 1..{size - 1} ASCII characters")
    if any(ord(c) < 32 or ord(c) > 126 for c in value):
        fail(f"{label}: printable ASCII required")
    if identity and not re.fullmatch(r"[A-Za-z0-9_./-]+", value):
        fail(f"{label}: identifier uses A-Z a-z 0-9 _ . / - only")
    return value.encode("ascii").ljust(size, b"\0")


def f32(value, label, bounded=False):
    if type(value) not in (int, float):
        fail(f"{label}: number required")
    try:
        converted = struct.unpack("<f", struct.pack("<f", value))[0]
    except (OverflowError, struct.error):
        fail(f"{label}: outside binary32 range")
    if not math.isfinite(converted) or (bounded and abs(converted) > 1000000):
        fail(f"{label}: nonfinite or outside supported bounds")
    return converted


def vector(value, size, label, bounded=False):
    if type(value) is not list or len(value) != size:
        fail(f"{label}: exactly {size} numbers required")
    return [f32(v, label, bounded) for v in value]


def mask(value, choices, label):
    if type(value) is not list or not value:
        fail(f"{label}: nonempty category list required")
    result = 0
    for name in value:
        if type(name) is not str or name not in choices or result & choices[name]:
            fail(f"{label}: unknown or duplicate category")
        result |= choices[name]
    return result


def rotation(value):
    if type(value) is not list or len(value) != 3:
        fail("rotation: three matrix rows required")
    rows = [vector(row, 3, "rotation") for row in value]
    for i in range(3):
        for j in range(i, 3):
            dot = sum(rows[i][k] * rows[j][k] for k in range(3))
            if abs(dot - (1 if i == j else 0)) > 0.0001:
                fail("rotation: orthonormal rigid matrix required")
    r = sum(rows, [])
    determinant = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) + r[2] * (r[3] * r[7] - r[4] * r[6])
    if abs(determinant - 1) > 0.0001:
        fail("rotation: determinant +1 required")
    return r


def compile_project(project):
    fields(project, ("schemaVersion", "provider", "referenceRig", "units", "axes", "slots"), "project")
    if type(project["schemaVersion"]) is not int or project["schemaVersion"] != VERSION:
        fail("unsupported schemaVersion")
    if project["referenceRig"] != RIG or project["units"] != UNITS or project["axes"] != AXES:
        fail("unsupported rig, units or axes")
    provider = text(project["provider"], 64, "provider", True)
    slots = project["slots"]
    if type(slots) is not list or len(slots) > MAX_DEFINITIONS:
        fail("slots: array of at most 256 definitions required")
    records, keys = [], set()
    for slot in slots:
        fields(slot, ("key", "anchor", "kind", "enabled", "position", "rotation", "accessRegion", "hands", "contents", "scaleMode"), "slot")
        key = text(slot["key"], 64, "key", True)
        if key in keys:
            fail("duplicate slot key")
        keys.add(key)
        anchor = text(slot["anchor"], 96, "anchor")
        kind = text(slot["kind"], 32, "kind", True)
        if type(slot["enabled"]) is not bool:
            fail("enabled: boolean required")
        position = vector(slot["position"], 3, "position", True)
        matrix = rotation(slot["rotation"])
        region = slot["accessRegion"]
        fields(region, ("shape", "center", "radius"), "accessRegion")
        if region["shape"] != "sphere":
            fail("accessRegion: only sphere supported")
        radius = f32(region["radius"], "radius", True)
        if radius <= 0:
            fail("radius must be positive")
        center = vector(region["center"], 3, "region center", True)
        hands = mask(slot["hands"], HANDS, "hands")
        content = mask(slot["contents"], CONTENTS, "contents")
        if slot["scaleMode"] != "anchor":
            fail("unsupported scaleMode")
        records.append(RECORD.pack(DEFINITION_SIZE, int(slot["enabled"]), provider, key, anchor, text(RIG, 64, "rig", True),
                                   kind, *position, *matrix, radius, *center, hands, content, 0, 0))
    # Stable authoring output independent of input ordering.
    records.sort(key=lambda r: r[72:136])
    payload = b"".join(records)
    return HEADER.pack(b"BSLOTS\r\n", VERSION, HEADER_SIZE, DEFINITION_SIZE, len(records), len(payload), 0, provider, b"\0" * 16) + payload


def load_project(path):
    data = Path(path).read_bytes()
    if len(data) > MAX_JSON_BYTES:
        fail("JSON exceeds 1 MiB authoring limit")
    try:
        return json.loads(data.decode("utf-8"), object_pairs_hook=unique_object,
                          parse_constant=lambda value: fail(f"nonfinite JSON number: {value}"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        fail(str(exc))


def write_package(path, content):
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=".bslots-", delete=False) as stream:
            temporary = stream.name
            stream.write(content)
        os.replace(temporary, destination)
    finally:
        if temporary and os.path.exists(temporary):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, help="write a .bslots package; omit to validate only")
    args = parser.parse_args()
    try:
        package = compile_project(load_project(args.source))
        if args.output:
            write_package(args.output, package)
        print(f"VALID: {(len(package) - HEADER_SIZE) // DEFINITION_SIZE} definitions, {len(package)} bytes")
        return 0
    except (ValidationError, OSError) as exc:
        print(f"INVALID: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
