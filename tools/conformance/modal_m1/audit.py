"""Independently inspect trusted manufactured modal fixtures without Serene imports.

This small audit checks positive envelopes, descriptor ownership, exact numeric
oracles, frame decoding and the manufactured reference mapping. It is not a
hostile-input production parser or a numerical certification of an actual blade.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

import numpy as np
import zfpy


# Independent explicit little-endian wire layouts; native struct padding is never used.
HEADER = struct.Struct("<8sHHIIIQQQQQ32s")
DIRECTORY = struct.Struct("<QIIQQ32s")
FRAME = struct.Struct("<QQQQQBI32s")
# Descriptor scalar layouts and fixed width are separately stated for this tiny audit.
DTYPES = {"u8": (2, "B", 1), "u32": (3, "I", 4), "u64": (4, "Q", 8),
          "f64": (5, "d", 8), "frames77": (6, None, 77), "i8": (7, "b", 1), "i32": (8, "i", 4)}


def sha(data: bytes) -> bytes:
    """Return SHA-256 of exact borrowed fixture bytes without text normalization."""
    # Hash the exact byte sequence.
    return hashlib.sha256(data).digest()


def sections(blob: bytes) -> list:
    """Verify and return owned section tuples from a trusted positive 1.2 file.

Each tuple contains its wire kind and exact body bytes. Assertions identify
fixture-authoring mistakes; they are not a public reader error interface.
"""
    # Check the bootstrap and directory, then verify every contiguous section and its digest.
    fields = HEADER.unpack(blob[:96])
    assert fields[:6] == (b"SERENE\0\0", 1, 2, 0x01020304, 128, 64)
    _, _, _, _, _, _, extent, offset, count, root_id, flags, digest = fields
    assert extent == len(blob) and offset + count * 64 == extent and root_id == 0 and flags == 1
    assert sha(blob[:96]) == blob[96:128] and sha(blob[offset:]) == digest
    result = []
    next_offset = 128
    for ordinal in range(count):
        # Read one canonical record and preserve exact section bytes for subsequent comparisons.
        section_id, kind, reserved, start, size, expected = DIRECTORY.unpack_from(blob, offset + ordinal * 64)
        assert section_id == ordinal and reserved == 0 and start == next_offset and start + size <= offset
        data = blob[start:start + size]
        assert sha(data) == expected
        result.append((kind, data))
        next_offset += size
    assert next_offset == offset
    return result


def array_bytes(table: list, descriptor: dict) -> bytes:
    """Return exact typed bytes after independently checking kind, rank and dimensions."""
    # Resolve the declared section and check the descriptor's product against its byte extent.
    kind, _, width = DTYPES[descriptor["dtype"]]
    stored_kind, data = table[descriptor["section"]]
    assert stored_kind == kind and 1 <= len(descriptor["shape"]) <= 2
    assert all(isinstance(size, int) and size >= 0 for size in descriptor["shape"])
    assert math.prod(descriptor["shape"]) * width == len(data)
    return data


def numbers(table: list, descriptor: dict) -> list:
    """Return independently unpacked scalar values from one trusted typed descriptor."""
    # Validate its bytes and unpack each scalar through an explicit little-endian format.
    data = array_bytes(table, descriptor)
    scalar = DTYPES[descriptor["dtype"]][1]
    assert scalar is not None
    return [item[0] for item in struct.iter_unpack("<" + scalar, data)]


def pool(table: list, descriptor: dict) -> list:
    """Return exact UTF-8 strings from a trusted pool after checking all byte offsets."""
    # Verify offset coverage and decode each independently delimited string.
    data = array_bytes(table, descriptor["bytes"])
    offsets = numbers(table, descriptor["offsets"])
    assert len(offsets) == descriptor["count"] + 1 and offsets[0] == 0 and offsets[-1] == len(data)
    assert all(left <= right for left, right in zip(offsets[:-1], offsets[1:]))
    return [data[left:right].decode("utf-8") for left, right in zip(offsets[:-1], offsets[1:])]


def owned_sections(value, table: list, used: set) -> None:
    """Check descriptor ownership recursively for a trusted root/run metadata object.

used is an in-out set seeded with metadata section IDs. Every binary descriptor
must add one distinct section; callers finally check that the graph has no orphans.
"""
    # Traverse dictionaries/lists, recognizing only the explicitly typed array descriptors.
    if isinstance(value, dict):
        if "section" in value:
            section_id = value["section"]
            assert section_id not in used
            array_bytes(table, value)
            used.add(section_id)
        else:
            for child in value.values():
                owned_sections(child, table, used)
    elif isinstance(value, list):
        for child in value:
            owned_sections(child, table, used)


def decode_shapes(table: list, values: dict) -> tuple:
    """Return exact row-major bytes and raw/ZFP counts from trusted framed values.

The public zfpy decoder is independent of the installed Serene codec. Complete
coverage, per-frame digests, extent limits and verification columns are checked.
"""
    # Verify profile and frame coverage, decode each frame, then concatenate rows in stored order.
    outer, inner = values["shape"]
    assert values["axis_names"] == ["mode", "observation_component"]
    assert numbers(table, values["row_profile_index"]) == [0] * outer
    assert numbers(table, values["profiles"]["mode"]) == [0]
    assert numbers(table, values["profiles"]["mode_code"]) == [2176]
    payload = array_bytes(table, values["payload"])
    records = list(FRAME.iter_unpack(array_bytes(table, values["frame_index"])))
    expected_offset = 0
    expected_row = 0
    expected_begin = 0
    decoded = bytearray()
    counts = [0, 0]
    for row, begin, count, offset, size, encoding, profile, digest in records:
        # Each frame stays inside one row and appends at the immediately next payload byte.
        assert row == expected_row and begin == expected_begin and count > 0 and begin + count <= inner
        assert count <= values["nominal_frame_values"] and offset == expected_offset and profile == 0
        encoded = payload[offset:offset + size]
        assert len(encoded) == size and sha(encoded) == digest and encoding in (0, 1)
        frame = encoded if encoding == 0 else zfpy.decompress_numpy(encoded).astype("<f8", copy=False).tobytes()
        assert len(frame) == count * 8 and all(math.isfinite(value[0]) for value in struct.iter_unpack("<d", frame))
        decoded.extend(frame)
        counts[encoding] += 1
        expected_offset += size
        expected_begin += count
        if expected_begin == inner:
            expected_row += 1
            expected_begin = 0
    assert expected_offset == len(payload) and len(decoded) == outer * inner * 8
    assert inner == 0 or (expected_row == outer and expected_begin == 0)
    verification = values["verification"]
    assert numbers(table, verification["bitwise_equal"]) == [1] * len(records)
    assert numbers(table, verification["verified_count"]) == [row[2] for row in records]
    assert array_bytes(table, verification["max_abs_error"]) == bytes(8 * len(records))
    assert array_bytes(table, verification["rms_error"]) == bytes(8 * len(records))
    return bytes(decoded), counts


def audit_positive(directory: Path, item: dict) -> dict:
    """Audit one positive fixture against its exact generated numerical oracle.

Returns measured counts only. A passing result verifies the independent corpus,
not the production writer, reader or scientific solver.
"""
    # Verify container ownership and typed identities, then optional-state and decoded-value semantics.
    table = sections((directory / item["file"]).read_bytes())
    root = json.loads(table[0][1])
    assert root["format"]["version_minor"] == 2 and "modal-results-1" in root["format"]["required_features"]
    index = root["results"]["index"]
    assert numbers(table, index["kind"]) == [2] and numbers(table, index["run_index"]) == [0]
    run_id = numbers(table, index["run_metadata_section"])[0]
    run = json.loads(table[run_id][1])
    expected = json.loads((directory / item["expected"]).read_text(encoding="utf-8"))
    used = {0, run_id}
    owned_sections(root, table, used)
    owned_sections(run, table, used)
    assert used == set(range(len(table)))
    assert pool(table, index["run_id"]) == [expected["run_id"]]
    modes, clusters, observations = run["modes"], run["clusters"], run["observations"]
    assert modes["count"] == expected["mode_count"] and observations["count"] == expected["observation_count"]
    assert numbers(table, modes["mode_id"]) == expected["mode_ids"]
    assert numbers(table, clusters["cluster_id"]) == expected["cluster_ids"]
    assert numbers(table, modes["classification"]) == expected["classifications"]
    assert array_bytes(table, modes["eigenvalue_per_s2"]) == b"".join(bytes.fromhex(value) for value in expected["eigenvalue_bits_hex"])
    assert run["info"]["result_kind"] == expected["result_kind"]
    assert run["info"]["frequencies_state"] == expected["frequency_state"]
    assert run["info"]["mode_diagnostics_state"] == expected["diagnostic_state"]
    assert run["shapes"]["state"] == expected["shape_state"]
    assert ("frequency_hz" in modes) == (expected["frequency_state"] == 0)
    assert ("modal_mass" in modes) == (expected["diagnostic_state"] == 0)
    if expected["result_kind"] == 1:
        assert modes["count"] == clusters["count"] == 0 and run["info"]["certification_status"] != 0
        assert run["info"]["search_status"] == expected["search_status"]
    if "counted_reference_eigenvalues" in expected:
        # Current source-counted grouping preserves a partition independently of rounded Ritz gaps.
        assert clusters["grouping_rule"] == "adjacent_gap_absolute_plus_relative_plus_twice_count_uncertainty"
        assert clusters["grouping_basis"] == "same_run_dense_count"
        assert numbers(table, observations["material_s_present"]) == expected["material_s_present"]
        # Recompute raw physical residuals from the independently recorded diagonal pencil.
        mass = np.asarray(expected["oracle_mass_diagonal"], dtype=np.float64)
        stiffness = np.asarray(expected["oracle_stiffness_diagonal"], dtype=np.float64)
        for ordinal, vector in enumerate(expected["oracle_physical_mode_vectors"]):
            direction = np.asarray(vector, dtype=np.float64)
            eigenvalue = expected["eigenvalues_per_s2"][ordinal]
            mass_action, stiffness_action = mass * direction, stiffness * direction
            denominator = float(np.linalg.norm(stiffness_action) + abs(eigenvalue) * np.linalg.norm(mass_action))
            residual = 0.0 if denominator == 0.0 else float(np.linalg.norm(stiffness_action - eigenvalue * mass_action)) / denominator
            assert math.isclose(residual, expected["projected_relative_residual"][ordinal], rel_tol=1e-15, abs_tol=0.0)
        if expected["diagnostic_state"] == 0:
            assert numbers(table, modes["projected_relative_residual"]) == expected["projected_relative_residual"]
        if item["variant"] in ("dense_ritz_cutoff", "counted_cluster"):
            assert 0.0 < expected["projected_relative_residual"][-1] <= run["criteria"]["relative_residual_tolerance"]
            assert run["report"]["certified_upper_eigenvalue_per_s2"]["value"] != expected["eigenvalues_per_s2"][-1]
        if expected["ritz_gap_would_split_counted_cluster"]:
            # Dense repeated values group, although applying the same rule to Ritz estimates would split them.
            left, right = expected["eigenvalues_per_s2"][-2:]
            criteria = run["criteria"]
            uncertainty = run["report"]["dense_absolute_uncertainty_per_s2"]["value"]
            threshold = criteria["cluster_absolute_tolerance_per_s2"] + criteria["cluster_relative_tolerance"] * max(abs(left), abs(right)) + 2.0 * uncertainty
            assert right - left > threshold and expected["counted_reference_eigenvalues"][2] == expected["counted_reference_eigenvalues"][3]
    frame_counts = [0, 0]
    if expected["shape_state"] == 0:
        # Compare decoded bytes directly, retaining arbitrary signs and the negative-zero bit.
        decoded, frame_counts = decode_shapes(table, run["shapes"]["values"])
        assert run["shapes"]["logical_shape"] == expected["logical_shape"]
        assert decoded == b"".join(bytes.fromhex(value) for value in expected["shape_bits_hex"])
        assert decoded == (directory / (Path(item["file"]).stem + ".shapes.expected.bin")).read_bytes()
        if observations["count"]:
            # Cross-check the physical offset example independently of the fixture's expected matrix.
            shape_values = np.frombuffer(decoded, dtype="<f8").reshape(4, 4, 6)
            assert shape_values[3, 3].tolist() == ([0.0, 0.0, 0.0, 1.0, -0.0, 0.0] if item["variant"] == "material_port" else [0.0, 0.0, -2.0, 1.0, -0.0, 0.0])
            assert numbers(table, observations["orientation_world_wxyz"])[12:16] == [0.0, 0.0, 0.0, 1.0]
            assert numbers(table, observations["position_world_m"])[9:12] == ([1.0, 2.0, 3.0] if item["variant"] == "material_port" else [1.0, 0.0, 3.0])
            assert pool(table, observations["reference_triad"]) == ["port_local", "beam_material", "beam_material", "port_local"]
    return {"file": item["file"], "modes": modes["count"], "observations": observations["count"],
            "raw_frames": frame_counts[0], "zfp_frames": frame_counts[1]}


def main() -> None:
    """Audit a supplied corpus directory and print a compact result, without modifying it."""
    # Check manifest byte identities and audit each positive with an independent public decoder.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    arguments = parser.parse_args()
    manifest = json.loads((arguments.directory / "manifest.json").read_text(encoding="utf-8"))
    for filename, expected in manifest["artifacts"].items():
        # Manifest identities cover negative bytes too, without treating them as positive archives.
        data = (arguments.directory / filename).read_bytes()
        assert len(data) == expected["bytes"] and sha(data).hex() == expected["sha256"]
    results = [audit_positive(arguments.directory, item) for item in manifest["positive"]]
    print(json.dumps({"status": "independent_corpus_audit_passed", "positive": len(results),
                      "negative_identities": len(manifest["negative"]), "results": results}, indent=2))


if __name__ == "__main__":
    main()
