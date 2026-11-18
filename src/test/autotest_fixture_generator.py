"""Compile one disposable vanilla-asset NPC into QASmoke for the API autotest.

Only owner-supplied Skyrim.esm is read; no game installation is written. The
source recipe and compiler are maintained here; the generated ESP stays outside
Git. This is not a replacement for Skyrim/xEdit or actual scene qualification.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

PLUGIN = "test_VRInteractionFramework.esp"
NPC = 0x01000800
REFERENCE = 0x01000801
CELL = 0x00032AE7
ACTOR_TEMPLATE = 0x000C3A3F  # Vanilla Sylgja, not a live reference or quest override.
POSITION = (-554.0, 1893.0, 6650.0, 0.0, 0.0, 0.0)


def records(blob: bytes, start: int = 0, end: int | None = None):
    end = len(blob) if end is None else end
    while start < end:
        if start + 24 > end:
            raise ValueError("Truncated record header")
        kind = blob[start:start + 4]
        size, flags, form_id = struct.unpack_from("<III", blob, start + 4)
        stop = start + size if kind == b"GRUP" else start + 24 + size
        if stop > end or stop <= start or (kind == b"GRUP" and size < 24):
            raise ValueError("Invalid record/container length")
        if kind == b"GRUP":
            yield from records(blob, start + 24, stop)
        else:
            data = blob[start + 24:stop]
            if flags & 0x40000:
                if len(data) < 4:
                    raise ValueError("Truncated compressed record")
                expected = struct.unpack_from("<I", data)[0]
                data = zlib.decompress(data[4:])
                if len(data) != expected:
                    raise ValueError("Compressed record length mismatch")
                flags &= ~0x40000
            yield kind, form_id, flags, data
        start = stop


def subrecords(data: bytes):
    pos, extended = 0, None
    while pos < len(data):
        if pos + 6 > len(data):
            raise ValueError("Truncated subrecord header")
        kind, size = data[pos:pos + 4], struct.unpack_from("<H", data, pos + 4)[0]
        pos += 6
        if kind == b"XXXX":
            if size != 4 or extended is not None or pos + 4 > len(data):
                raise ValueError("Invalid extended size")
            extended = struct.unpack_from("<I", data, pos)[0]
            pos += 4
            continue
        if extended is not None:
            size, extended = extended, None
        if pos + size > len(data):
            raise ValueError("Truncated subrecord payload")
        yield kind, data[pos:pos + size]
        pos += size
    if extended is not None:
        raise ValueError("Unconsumed extended size")


def sub(kind: bytes, payload: bytes):
    if len(payload) > 65535:
        return sub(b"XXXX", struct.pack("<I", len(payload))) + kind + b"\0\0" + payload
    return kind + struct.pack("<H", len(payload)) + payload


def text(value: str):
    return value.encode("ascii") + b"\0"


def form(value: int):
    return struct.pack("<I", value)


def record(kind: bytes, form_id: int, data: bytes, flags: int = 0):
    return kind + struct.pack("<IIIIHH", len(data), flags, form_id, 0, 44, 0) + data


def group(label: bytes, kind: int, data: bytes):
    return b"GRUP" + struct.pack("<I", len(data) + 24) + label + struct.pack("<iHHHH", kind, 0, 0, 0, 0) + data


def compile_fixture(esm: bytes):
    wanted = {(b"NPC_", ACTOR_TEMPLATE), (b"CELL", CELL)}
    templates = {(kind, fid): (flags, data) for kind, fid, flags, data in records(esm)
                 if (kind, fid) in wanted}
    if set(templates) != wanted:
        raise ValueError("Required vanilla templates missing")
    actor = templates[b"NPC_", ACTOR_TEMPLATE][1]
    if dict(subrecords(actor)).get(b"EDID") != b"Sylgja\0":
        raise ValueError("Unexpected actor source identity")
    cell_flags, cell_data = templates[b"CELL", CELL]
    if dict(subrecords(cell_data)).get(b"EDID") != b"QASmoke\0":
        raise ValueError("Unexpected cell source identity")
    # No original NPC override, quest scripts, AI packages, factions, template,
    # inventory or gameplay spells. Keep stock human appearance/race/skin/outfit.
    omitted = {b"VMAD", b"SNAM", b"PKID", b"TPLT", b"CNTO", b"COED", b"SPCT", b"SPLO", b"CRIF"}
    actor_rows = []
    for kind, value in subrecords(actor):
        if kind in omitted:
            continue
        if kind == b"EDID":
            value = text("VRInteractionFrameworkTestNpc")
        elif kind == b"FULL":
            value = text("VR Interaction Framework test NPC")
        elif kind == b"ACBS":
            # Female, essential; remove uniqueness/template/player-level flags.
            if len(value) < 4:
                raise ValueError("Truncated NPC configuration")
            value = form(3) + value[4:]
        elif kind == b"AIDT":
            # Unaggressive. Other vanilla AI values do not authorize fixture motion.
            if not value:
                raise ValueError("Truncated AI configuration")
            value = b"\0" + value[1:]
        elif kind == b"COCT":
            value = form(0)
        actor_rows.append(sub(kind, value))
    actor_data = b"".join(actor_rows)
    placed_data = (sub(b"EDID", text("VRInteractionFrameworkTestNpcRef")) + sub(b"NAME", form(NPC))
                   + sub(b"DATA", struct.pack("<6f", *POSITION)))
    children = group(form(CELL), 6, group(form(CELL), 8, record(b"ACHR", REFERENCE, placed_data, 0x400)))
    cell_tree = group(form(1), 2, group(form(9), 3, record(b"CELL", CELL, cell_data, cell_flags) + children))
    header = (sub(b"HEDR", struct.pack("<fII", 1.7, 3, 0x802))
              + sub(b"CNAM", text("VR Interaction Framework API test fixture"))
              + sub(b"SNAM", text("Disposable test NPC; never use in a normal save."))
              + sub(b"MAST", text("Skyrim.esm")) + sub(b"DATA", b"\0" * 8))
    result = (record(b"TES4", 0, header) + group(b"NPC_", 0, record(b"NPC_", NPC, actor_data))
              + group(b"CELL", 0, cell_tree))
    rows = list(records(result))
    if [(k, f) for k, f, _, _ in rows] != [(b"TES4", 0), (b"NPC_", NPC), (b"CELL", CELL), (b"ACHR", REFERENCE)]:
        raise ValueError("Unexpected fixture record set")
    if rows[2][3] != cell_data or rows[2][2] != cell_flags:
        raise ValueError("Vanilla cell was changed")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skyrim-esm", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    destination = args.output / PLUGIN
    if destination.exists():
        raise SystemExit("Refusing an existing output")
    source = args.skyrim_esm.read_bytes()
    blob = compile_fixture(source)
    destination.write_bytes(blob)
    receipt = {"schemaVersion": 1, "source": {"path": str(args.skyrim_esm.resolve()),
        "sha256": hashlib.sha256(source).hexdigest(), "bytes": len(source)},
        "compiler": {"path": str(Path(__file__).resolve()), "sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},
        "output": {"path": str(destination.resolve()), "sha256": hashlib.sha256(blob).hexdigest(), "bytes": len(blob)},
        "masters": ["Skyrim.esm"], "cell": "QASmoke", "cellFormId": f"{CELL:08X}",
        "actorBaseLocalId": "000800", "placedReferenceLocalId": "000801",
        "positionRotation": POSITION, "gameQualification": "pending", "gameStarts": 0}
    (args.output / "fixture-esp-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt["output"]))


if __name__ == "__main__":
    main()
