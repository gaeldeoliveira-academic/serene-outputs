"""Author independent J07 review bytes; never call the recorder or modify old fixtures.

This review helper writes only an explicitly selected new candidate directory. Identifiers
are proposed values, not a frozen wire registration or an implementation claim.
"""
from pathlib import Path
import copy
import hashlib
import json
import struct
import sys
import zlib

ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else None
if ROOT is None:
    raise SystemExit("usage: author_candidate.py NEW_CANDIDATE_DIRECTORY")
FIX = ROOT / "fixtures"
HARNESS = ROOT / "legacy-viewer-harness"
U64 = (1 << 64) - 1


def digest(data):
    """Return the SHA-256 hex identity of borrowed data bytes."""
    # Hash the unchanged byte sequence for the review manifest.
    return hashlib.sha256(data).hexdigest()


def crc(data):
    """Return the unsigned IEEE CRC-32 of borrowed bytes."""
    # Use the standard-library polynomial implementation, independent of C++.
    return zlib.crc32(data) & 0xffffffff


def repair(raw, offset):
    """Return owned header bytes with the checksum at offset recomputed."""
    # Copy the header, zero its CRC slot, compute and insert the new checksum.
    result = bytearray(raw)  # Mutable header owned by this helper.
    struct.pack_into("<I", result, offset, 0)
    struct.pack_into("<I", result, offset, crc(result))
    return bytes(result)


def key(path, force):
    """Return the proposed UTF-8 byte-length-prefixed occurrence/force key."""
    # Count encoded bytes, preserving exact input spelling and Unicode.
    return f"winch_with_saddle.v1.o{len(path.encode('utf-8'))}_{path}.f{len(force.encode('utf-8'))}_{force}"


def arrangement(index, owner, path, a, b, d):
    """Return literal immutable topology for one independently named arrangement."""
    # Retain the existing physical ports and describe C only as an algebraic point.
    return {"index": index, "key": key(path, "pull"), "owner_occurrence_id": owner,
            "force_id": "pull", "feature_type": "winch_with_saddle",
            "points": [{"role": "A", "kind": "physical_port", "sample_id": a},
                       {"role": "B", "kind": "physical_port", "sample_id": b},
                       {"role": "C", "kind": "algebraic", "feature": "pulley"},
                       {"role": "D", "kind": "physical_port", "sample_id": d}],
            "edges": [{"points": [0, 2], "kind": "bridle"},
                      {"points": [2, 1], "kind": "bridle"},
                      {"points": [2, 3], "kind": "main_cable"}]}


CATALOG = {
    "catalog_version": 1, "recording_id": "j07-independent-candidate-v1",
    "run_id": "two-occurrences-shared-force-id", "preparation_id": "capture-plan-17",
    "world": {"axes": "right_handed_xyz", "length_unit": "m", "orientation": "hamilton_wxyz_local_to_world"},
    "occurrences": [
        {"id": 10, "parent_id": None, "path": "/bench", "kind": "assembly"},
        {"id": 20, "parent_id": 10, "path": "/bench/left", "kind": "assembly"},
        {"id": 21, "parent_id": 20, "path": "/bench/left/blade", "kind": "flexible"},
        {"id": 22, "parent_id": 20, "path": "/bench/left/ground", "kind": "anchor"},
        {"id": 30, "parent_id": 10, "path": "/bench/right", "kind": "assembly"},
        {"id": 31, "parent_id": 30, "path": "/bench/right/blade", "kind": "flexible"},
        {"id": 32, "parent_id": 30, "path": "/bench/right/ground", "kind": "anchor"}],
    "samples": [
        {"id": 0, "kind": "structural_node", "occurrence_id": 21, "material_coordinate_m": 0.0, "side": "none"},
        {"id": 1, "kind": "structural_node", "occurrence_id": 21, "material_coordinate_m": 5.0, "side": "none"},
        {"id": 2, "kind": "body_port", "occurrence_id": 21, "port_id": "pin_A"},
        {"id": 3, "kind": "anchor_port", "occurrence_id": 22, "port_id": "winch_D"},
        {"id": 4, "kind": "body_port", "occurrence_id": 21, "port_id": "pin_B"},
        {"id": 5, "kind": "body_port", "occurrence_id": 31, "port_id": "pin_A"},
        {"id": 6, "kind": "body_port", "occurrence_id": 31, "port_id": "pin_B"},
        {"id": 7, "kind": "anchor_port", "occurrence_id": 32, "port_id": "winch_D"}],
    "segments": [{"id": 0, "occurrence_id": 21, "sample_ids": [0, 1]}],
    "port_aliases": [], "connections": [],
    "winch_geometry": {"layout_version": 1, "kind": 4, "stream_id": 2,
                       "encoding": "raw_f64_le", "source_preparation_generation": 2,
                       "arrangements": [arrangement(0, 20, "/bench/left", 2, 4, 3),
                                        arrangement(1, 30, "/bench/right", 5, 6, 7)]}}


def frames():
    """Return three literal review states with asymmetric coordinates and sparse stamps."""
    # Vary accepted geometry on selected steps while both fixed anchors stay unchanged.
    # Values are byte-contract oracles, not a solved force-law trajectory.
    result = []  # Small owned list of independent reference frames.
    for i, (step, time, event) in enumerate(((0, 1.25, 2), (4, 1.29, 4), (7, 1.32, 8))):
        delta = i * 0.125  # Exact dyadic change for clear bitwise expectations.
        first = [[-1.25 + delta, 2.5, 3.75], [.75 + delta, 2.5, 3.75],
                 [-.25 + delta, 5.0, 4.25], [8.5, 14.25, -2.0]]
        second = [[10.25, -3.5 + delta, 6.75], [10.25, -1.5 + delta, 6.75],
                  [12.5, -2.5 + delta, 7.125], [-7.5, 11.25, 9.0]]
        poses = [[0, 0, 0, 1, 0, 0, 0], [0, 0, 5, .5, .5, .5, .5],
                 first[0] + [1, 0, 0, 0], first[3] + [1, 0, 0, 0],
                 first[1] + [1, 0, 0, 0], second[0] + [1, 0, 0, 0],
                 second[1] + [1, 0, 0, 0], second[3] + [1, 0, 0, 0]]
        result.append({"stamp": {"ordinal": i, "analysis": 1, "events": event,
                                 "time": time, "state_id": step, "load_factor": 0.0},
                       "poses": poses, "source_generation": 2, "source_phase": 2 if i == 0 else 3,
                       "features": [{"geometry_status": 0, "world_xyz_m": first},
                                    {"geometry_status": 1, "world_xyz_m": second}]})
    return result


def stamp(frame):
    """Encode the unchanged 64-byte structural stamp for borrowed frame."""
    # Use the structural population even when copying this stamp to a feature record.
    s = frame["stamp"]  # Independent expected stamp dictionary.
    return struct.pack("<QIIdQdQQQ", s["ordinal"], s["analysis"], s["events"],
                       s["time"], s["state_id"], s["load_factor"], 8, 0, 0)


def pose_bytes(frame):
    """Return raw v1 structural stamp and pose bytes for one literal frame."""
    # Serialize exact expected values without normalization or production helpers.
    return stamp(frame) + b"".join(struct.pack("<7d", *p) for p in frame["poses"])


def feature_bytes(frame):
    """Return proposed copied stamp, provenance and two raw XYZ/status entries."""
    # Encode the complete structural stamp, local Core generation/phase, then catalog order.
    raw = stamp(frame) + struct.pack("<QII", frame["source_generation"], frame["source_phase"], 0)
    for feature in frame["features"]:
        xyz = [v for p in feature["world_xyz_m"] for v in p]  # Axyz,Bxyz,Cxyz,Dxyz.
        raw += struct.pack("<II12d", feature["geometry_status"], 0, *xyz)
    return raw


def chunk(kind, stream, seq, count, first, payload, previous=0, flags=1):
    """Return a checksummed raw chunk with explicit proposed or v1 identity."""
    # Construct the fixed header and bind the exact payload bytes through CRC.
    header = struct.pack("<4sHHIIIIQQQQQIIQ", b"PCHK", kind, flags, 80, 0, stream, 1,
                         seq, count, first, len(payload), len(payload), crc(payload), 0, previous)
    return repair(header, 68) + payload


def make_file(reference, *, paired=True, outcomes=(1, 1), catalog=CATALOG):
    """Return independent complete file bytes and named chunk offsets.

    reference is the small owned frame list. paired controls the intentional missing
    feature negative/control; outcomes are independent run/capture wire codes.
    """
    # Write bootstrap/catalog, adjacent feature/pose pairs, then unchanged terminal records.
    cat = json.dumps(catalog, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode("utf-8")
    header = struct.pack("<8sHHIQQQQIIQ", b"SRNPOSE\0", 1, 0, 64, 0, len(cat), 8, 4096, crc(cat), 0, 0)
    data = bytearray(repair(header, 52) + cat)  # Small owned byte-contract artifact.
    offsets = {"features": [], "poses": []}  # Exact locations for isolated mutations.
    seq = previous = first_pose = batches = 0  # Global sequence and structural index facts.
    for start in range(0, len(reference), 2):
        rows = reference[start:start + 2]  # One fixed fixture batch.
        if paired:
            offsets["features"].append(len(data))
            data += chunk(4, 2, seq, len(rows), start, b"".join(feature_bytes(f) for f in rows), flags=0)
            seq += 1
        offset = len(data)  # Structural chunk offset used by the unchanged linked index.
        offsets["poses"].append(offset)
        data += chunk(1, 1, seq, len(rows), start, b"".join(pose_bytes(f) for f in rows), previous)
        first_pose = first_pose or offset
        previous = offset
        seq += 1
        batches += 1
    index = len(data)  # Unchanged structural-only seek index offset.
    data += chunk(2, 0, seq, 1, 0, struct.pack("<4Q", batches, len(reference), first_pose, previous))
    completion = len(data)  # Unchanged terminal outcome record offset.
    data += chunk(3, 0, seq + 1, 1, 0, struct.pack("<4I6Q", *outcomes, 1, 0,
                  len(reference), len(reference), batches, len(reference) - 1 if reference else U64, index, 0))
    data += repair(struct.pack("<8sQQII", b"POSEEND\0", index, completion, 0, 0), 24)
    offsets.update(index=index, completion=completion)
    return bytes(data), offsets


def change_feature(raw, offset, relative, fmt, value):
    """Return an isolated feature-payload mutation with both CRCs repaired."""
    # Mutate one scalar, then repair checksums so future semantic validation is exercised.
    data = bytearray(raw)  # Owned mutation, leaving the positive control unchanged.
    struct.pack_into(fmt, data, offset + 80 + relative, value)
    size = struct.unpack_from("<Q", data, offset + 48)[0]  # Declared raw payload extent.
    struct.pack_into("<I", data, offset + 64, crc(data[offset + 80:offset + 80 + size]))
    data[offset:offset + 80] = repair(data[offset:offset + 80], 68)
    return bytes(data)


def main():
    """Write new candidate artifacts and two explicit expectation manifests; return None."""
    # Author small positive, truncation, integrity and prospective semantic cases.
    # Build the old-viewer harness separately; its legacy-only control is not a positive feature file.
    # Record hashes and exact values without importing any producer or decoder.
    if ROOT.exists():
        raise SystemExit("Refusing to overwrite an existing candidate directory")
    FIX.mkdir(parents=True)
    HARNESS.mkdir(parents=True, exist_ok=True)
    reference = frames()  # Literal sparse accepted transient states.
    static = copy.deepcopy(reference[:1])  # Independent certified target variant.
    static[0]["stamp"].update(analysis=2, events=8, time=2.0, state_id=9, load_factor=1.0)
    static[0]["source_phase"] = 1
    prepared = copy.deepcopy(reference[:1])  # Initialized diagnostic, not an accepted solve.
    prepared[0]["stamp"].update(analysis=0, events=1, time=100.0, state_id=0, load_factor=0.0)
    prepared[0]["source_phase"] = 0
    good, offsets = make_file(reference)
    files = {"paired_transient.spose": good, "static.spose": make_file(static)[0],
             "prepared.spose": make_file(prepared, outcomes=(0, 1))[0],
             "empty_failed.spose": make_file([], outcomes=(2, 3))[0]}
    cases = {name: {"legacy_valid": True, "feature_valid_expected": True, "complete": True,
                    "frames": 3 if name.startswith("paired") else (0 if name.startswith("empty") else 1)} for name in files}
    cases["prepared.spose"]["run_outcome"] = 0
    cases["empty_failed.spose"].update(run_outcome=2, capture_outcome=3)
    for name, at in (("truncated_feature.spose.partial", offsets["features"][1] + 80 + 17),
                     ("truncated_pose.spose.partial", offsets["poses"][1] + 80 + 17),
                     ("pending_feature.spose.partial", offsets["poses"][1])):
        files[name] = good[:at]
        cases[name] = {"legacy_valid": True, "feature_valid_expected": True,
                       "complete": False, "frames": 2, "allow_incomplete": True}
    minor = bytearray(good)  # Unsupported bootstrap minor, valid CRC.
    struct.pack_into("<H", minor, 10, 1)
    minor[:64] = repair(minor[:64], 52)
    files["unsupported_minor.spose"] = bytes(minor)
    cases["unsupported_minor.spose"] = {"legacy_valid": False, "feature_valid_expected": False,
                                        "error_contains": "unsupported"}
    required = bytearray(good)  # Unknown required feature chunk must reject in old reader.
    at = offsets["features"][0]  # First optional feature chunk header.
    struct.pack_into("<H", required, at + 6, 1)
    required[at:at + 80] = repair(required[at:at + 80], 68)
    files["required_feature.spose"] = bytes(required)
    cases["required_feature.spose"] = {"legacy_valid": False, "feature_valid_expected": False,
                                       "error_contains": "unsupported"}
    corrupt = bytearray(good)  # Payload corruption with deliberately stale CRC.
    corrupt[at + 80 + 90] ^= 1
    files["corrupt_feature.spose"] = bytes(corrupt)
    cases["corrupt_feature.spose"] = {"legacy_valid": False, "feature_valid_expected": False,
                                      "error_contains": "checksum"}
    # Existing readers cannot detect semantic errors inside an unknown optional stream.
    for name, relative, fmt, value, reason in (
        ("mismatched_stamp.spose", 24, "<Q", 17, "feature/pose state identity mismatch"),
        ("wrong_generation.spose", 64, "<Q", 17, "generation is not capture-plan identity"),
        ("nonfinite_feature.spose", 88, "<d", float("nan"), "nonfinite feature coordinate"),
        ("unknown_geometry_status.spose", 80, "<I", 7, "unsupported geometry status")):
        files[name] = change_feature(good, at, relative, fmt, value)
        cases[name] = {"legacy_valid": True, "feature_valid_expected": False,
                       "complete": True, "frames": 3, "prospective_rejection": reason}
    files["missing_features.spose"] = make_file(reference, paired=False)[0]
    cases["missing_features.spose"] = {"legacy_valid": True, "feature_valid_expected": False,
                                       "complete": True, "frames": 3,
                                       "prospective_rejection": "declared feature population absent"}
    for name, raw in files.items():
        (FIX / name).write_bytes(raw)
        (HARNESS / name).write_bytes(raw)
        cases[name].update(bytes=len(raw), sha256=digest(raw))
    # The unchanged old test executable has a hardcoded feature-free focused mutation.
    # Supply a clearly labelled harness control, never claim it is feature-conformant.
    (HARNESS / "transient.spose").write_bytes(files["missing_features.spose"])
    (FIX / "catalog.json").write_text(json.dumps(CATALOG, indent=2) + "\n", encoding="utf-8")
    (HARNESS / "catalog.json").write_bytes((FIX / "catalog.json").read_bytes())
    expected = {"status": "DRAFT candidate; feature-aware expectations are not implementation results",
                "geometry_scope": "Literal asymmetric byte oracles, not force-law solutions",
                "sample_count": 8, "arrangements": 2, "pose_frame_bytes": 512,
                "feature_frame_bytes": 288, "transient_frames": reference,
                "static_frames": static, "prepared_frames": prepared, "cases": cases}
    (FIX / "expected.json").write_text(json.dumps(expected, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    legacy = {"transient_frames": reference, "static_frames": static, "prepared_frames": prepared,
              "cases": {name: {**{k: v for k, v in case.items() if k in
                         ("complete", "frames", "bytes", "sha256", "error_contains", "run_outcome", "capture_outcome")},
                         "valid": case["legacy_valid"]} for name, case in cases.items()}}
    (HARNESS / "expected.json").write_text(json.dumps(legacy, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"candidate": str(ROOT), "cases": len(files), "bytes": sum(map(len, files.values()))}))


if __name__ == "__main__":
    main()
