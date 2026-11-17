"""Author independent approved J07 wire fixtures without using any production writer.

Writes a new explicitly selected directory (default fixtures_pose_winch). Refuses
replacement of prior fixture evidence. Literal geometry is a byte-layout oracle,
not a numerical solution of the winch force law.
"""
from pathlib import Path
import copy
import hashlib
import json
import math
import struct
import sys
import zlib

# Default new corpus destination; callers may explicitly choose another unused path.
ROOT = Path(__file__).resolve().parents[2] / "fixtures_pose_winch"
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
    """Write fresh fixtures/expectations at the optional command-line path; return None."""
    # Author literal geometry, accepted-state and fixed representation-bound cases.
    # Add isolated semantic/header/CRC/truncation negatives with repaired unrelated fields.
    # Write exact reference rows and a separate old-reader-compatible bounded harness.
    root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else ROOT  # Explicit destination.
    if len(sys.argv) > 2 or root.exists():
        raise SystemExit("usage: author_winch_fixtures.py [NEW_DIRECTORY]; destination must not exist")
    root.mkdir(parents=True)
    harness = root / "legacy-viewer-harness"  # Separate expectations for unmodified old readers.
    harness.mkdir()
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
    references = {"transient_frames": reference, "static_frames": static, "prepared_frames": prepared}
    # With initial emission disabled, the first captured phase may already be accepted.
    no_initial = copy.deepcopy(reference[1:])
    for ordinal, row in enumerate(no_initial):
        row['stamp']['ordinal'] = ordinal
    files['initial_disabled.spose'] = make_file(no_initial)[0]
    references['initial_disabled_frames'] = no_initial
    cases['initial_disabled.spose'] = dict(legacy_valid=True, feature_valid_expected=True,
        complete=True, frames=2, frames_key='initial_disabled_frames')
    # S=1m makes the 64-epsilon boundary exact; signed zeros remain independently stored.
    boundary = copy.deepcopy(reference[:1])
    boundary[0]['poses'][2][:3] = [-0.0, 0.0, 0.0]
    boundary[0]['features'][0]['world_xyz_m'][0] = [0.0, 2.0 ** -46, 0.0]
    files['representation_boundary.spose'] = make_file(boundary)[0]
    references['representation_boundary_frames'] = boundary
    cases['representation_boundary.spose'] = dict(legacy_valid=True, feature_valid_expected=True,
        complete=True, frames=1, frames_key='representation_boundary_frames')
    beyond = copy.deepcopy(boundary)
    beyond[0]['features'][0]['world_xyz_m'][0][1] = math.nextafter(2.0 ** -46, math.inf)
    files['representation_exceeded.spose'] = make_file(beyond)[0]
    cases['representation_exceeded.spose'] = dict(legacy_valid=True, feature_valid_expected=False,
        error_contains='coordinate consistency')
    # Two force arrangements may share physical ports; each compares to the same canonical sample.
    shared_cat = copy.deepcopy(CATALOG)
    shared_cat['winch_geometry']['arrangements'][1] = arrangement(1, 20, '/bench/left', 2, 4, 3)
    shared_cat['winch_geometry']['arrangements'][1]['force_id'] = 'pull_shared'
    shared_cat['winch_geometry']['arrangements'][1]['key'] = key('/bench/left', 'pull_shared')
    shared = copy.deepcopy(reference[:1])
    for local in (0, 1, 3):
        shared[0]['features'][1]['world_xyz_m'][local] = shared[0]['features'][0]['world_xyz_m'][local][:]
    files['shared_ports.spose'] = make_file(shared, catalog=shared_cat)[0]
    references['shared_ports_frames'] = shared
    cases['shared_ports.spose'] = dict(legacy_valid=True, feature_valid_expected=True,
        complete=True, frames=1, frames_key='shared_ports_frames', catalog_key='shared_ports_catalog')
    shared_bad = copy.deepcopy(shared)
    shared_bad[0]['features'][1]['world_xyz_m'][0][0] += 1e-9
    files['shared_ports_mismatch.spose'] = make_file(shared_bad, catalog=shared_cat)[0]
    cases['shared_ports_mismatch.spose'] = dict(legacy_valid=True, feature_valid_expected=False,
        error_contains='coordinate consistency')
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
                       "complete": True, "frames": 3, "rejection_reason": reason}
    files["missing_features.spose"] = make_file(reference, paired=False)[0]
    cases["missing_features.spose"] = {"legacy_valid": True, "feature_valid_expected": False,
                                       "complete": True, "frames": 3,
                                       "rejection_reason": "declared feature population absent"}
    # Known-family dispatch must reject malformed optional semantics before generic skipping.
    for name, rel, fmt, value in (
        ('wrong_kind.spose', 4, '<H', 99), ('wrong_stream.spose', 16, '<I', 99),
        ('wrong_layout.spose', 20, '<I', 2), ('wrong_encoding.spose', 12, '<I', 1),
        ('count_mismatch.spose', 32, '<Q', 3), ('count_overflow.spose', 32, '<Q', U64),
        ('first_ordinal_mismatch.spose', 40, '<Q', 1), ('prior_link_nonzero.spose', 72, '<Q', 1)):
        mutated = bytearray(good)
        struct.pack_into(fmt, mutated, at + rel, value)
        mutated[at:at + 80] = repair(mutated[at:at + 80], 68)
        files[name] = bytes(mutated)
        cases[name] = dict(feature_valid_expected=False, rejection_reason='known-family header contract')
    for name, rel, fmt, value in (
        ('source_phase_mismatch.spose', 72, '<I', 3),
        ('source_reserved_nonzero.spose', 76, '<I', 1),
        ('entry_reserved_nonzero.spose', 84, '<I', 1),
        ('sample_stamp_mismatch.spose', 40, '<Q', 7)):
        files[name] = change_feature(good, at, rel, fmt, value)
        cases[name] = dict(feature_valid_expected=False, rejection_reason='feature record contract')
    # Metadata negatives are full files so CRC and length are valid before semantic rejection.
    for name, mutation in (
        ('missing_descriptor.spose', lambda c: c.pop('winch_geometry')),
        ('duplicate_role.spose', lambda c: c['winch_geometry']['arrangements'][0]['points'][1].update(role='A')),
        ('bad_reference.spose', lambda c: c['winch_geometry']['arrangements'][0]['points'][0].update(sample_id=999)),
        ('fake_C_sample.spose', lambda c: c['winch_geometry']['arrangements'][0]['points'][2].update(sample_id=2)),
        ('bad_edge.spose', lambda c: c['winch_geometry']['arrangements'][0]['edges'][0].update(points=[0, 3])),
        ('bad_key.spose', lambda c: c['winch_geometry']['arrangements'][0].update(key='ambiguous')),
        ('zero_generation.spose', lambda c: c['winch_geometry'].update(source_preparation_generation=0)),
        ('empty_population.spose', lambda c: c['winch_geometry'].update(arrangements=[]))):
        changed_cat = copy.deepcopy(CATALOG)
        mutation(changed_cat)
        files[name] = make_file(reference, catalog=changed_cat)[0]
        cases[name] = dict(feature_valid_expected=False, rejection_reason='feature catalog contract')
    # Positive files define only exact per-case expected frames; known semantic negatives need no dump.
    for name, case in cases.items():
        case['valid'] = case['feature_valid_expected']
        if case['valid']:
            case.setdefault('frames_key', 'static_frames' if name == 'static.spose' else
                            ('prepared_frames' if name == 'prepared.spose' else 'transient_frames'))
    for name, raw in files.items():
        (root / name).write_bytes(raw)
        cases[name].update(bytes=len(raw), sha256=digest(raw))
    # The unchanged old test executable has a hardcoded feature-free focused mutation.
    # Supply a clearly labelled harness control, never claim it is feature-conformant.
    (harness / "transient.spose").write_bytes(files["missing_features.spose"])
    (root / "catalog.json").write_text(json.dumps(CATALOG, indent=2) + "\n", encoding="utf-8")
    (harness / "catalog.json").write_bytes((root / "catalog.json").read_bytes())
    expected = {"status": "Independent approved wire expectations; execution evidence is separate",
                "geometry_scope": "Literal asymmetric byte oracles, not force-law solutions",
                "sample_count": 8, "arrangements": 2, "pose_frame_bytes": 512,
                "feature_frame_bytes": 288, **references,
                "shared_ports_catalog": shared_cat, "cases": cases}
    (root / "expected.json").write_text(json.dumps(expected, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    # The old driver assumes shared catalog and name-selected reference arrays, so include only
    # baseline-shaped cases here. New phase/representation/shared-port tests use the aware checker.
    legacy_cases = {name: case for name, case in cases.items() if 'legacy_valid' in case and
                    name not in ('initial_disabled.spose', 'representation_boundary.spose',
                                 'representation_exceeded.spose', 'shared_ports.spose', 'shared_ports_mismatch.spose')}
    for name in legacy_cases:
        (harness / name).write_bytes(files[name])
    legacy = {"transient_frames": reference, "static_frames": static, "prepared_frames": prepared,
              "cases": {name: {**{k: v for k, v in case.items() if k in
                         ("complete", "frames", "bytes", "sha256", "error_contains", "run_outcome", "capture_outcome")},
                         "valid": case["legacy_valid"]} for name, case in legacy_cases.items()}}
    (harness / "expected.json").write_text(json.dumps(legacy, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"root": str(root), "cases": len(files), "bytes": sum(map(len, files.values()))}))


if __name__ == "__main__":
    main()
