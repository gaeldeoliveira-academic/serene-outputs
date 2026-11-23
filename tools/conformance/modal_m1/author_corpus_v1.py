"""Author independent tiny native 1.2 modal archives and expected values.

Only the preserved independent 1.0 fixture helper, public NumPy/ZFP bindings and
Python's binary/JSON/hash tools define wire bytes. No production Serene module
is imported. Every generated artifact is written with exclusive creation.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import struct
from pathlib import Path

import numpy as np

from oracle import packet


# Source root is inferred from this maintained conformance author's fixed location.
ROOT = Path(__file__).resolve().parents[3]
# Historical independent helper is read-only input, never a production serializer.
HELPER = ROOT / "tools/conformance/custom_author.py"
# Modal token is required in addition to the unchanged five 1.0 container tokens.
MODAL_FEATURE = "modal-results-1"


def load_helper():
    """Load and return the preserved independent wire helper without changing it.

The helper imports public NumPy and zfpy. Import errors propagate; this author
does not install dependencies or substitute a production codec implementation.
"""
    # Load the explicitly identified historical fixture source into an isolated module name.
    specification = importlib.util.spec_from_file_location("independent_modal_wire_helper", HELPER)
    if specification is None or specification.loader is None:
        raise RuntimeError("Cannot load the independently authored native fixture helper")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def encode(helper, container) -> bytes:
    """Return exact container bytes with the descriptor's explicit format version.

The tiny owned container is sealed in memory. All section and directory hashes
are independently recomputed; no production writer or canonical JSON assumption
is used. The fixture-only implementation intentionally materializes whole files.
"""
    # Serialize metadata, pack contiguous section records, and hash the directory and bootstrap.
    container.seal()
    body = bytearray()
    directory = bytearray()
    offset = 128
    for section_id, (kind, data) in enumerate(container.sections):
        # Each dense section starts exactly where its predecessor ends, including empty sections.
        directory.extend(helper.DIRECTORY.pack(section_id, kind, 0, offset, len(data), helper.sha(data)))
        body.extend(data)
        offset += len(data)
    version = container.root["format"]
    header = helper.HEADER96.pack(
        b"SERENE\0\0", version["version_major"], version["version_minor"],
        0x01020304, 128, 64, offset + len(directory), offset,
        len(container.sections), 0, 1, helper.sha(directory))
    return header + helper.sha(header) + bytes(body) + bytes(directory)


def assert_envelope(helper, blob: bytes) -> None:
    """Verify the independent envelope of a trusted newly encoded fixture.

This check validates bounds and hashes, not modal descriptor semantics. It is
used before deliberate corruptions and cannot establish production-readability.
"""
    # Verify fixed fields and then walk every dense section and its exact digest.
    fields = helper.HEADER96.unpack(blob[:96])
    magic, major, minor, endian, header_bytes, row_bytes, extent, offset, count, root, flags, digest = fields
    assert magic == b"SERENE\0\0" and major == 1 and minor in (2, 3)
    assert endian == 0x01020304 and header_bytes == 128 and row_bytes == 64
    assert root == 0 and flags == 1 and extent == len(blob)
    assert helper.sha(blob[:96]) == blob[96:128]
    assert offset + count * 64 == len(blob) and helper.sha(blob[offset:]) == digest
    next_offset = 128
    for ordinal in range(count):
        # The record's digest covers exactly the checked extent, without padding or hidden bytes.
        record = helper.DIRECTORY.unpack_from(blob, offset + ordinal * 64)
        section_id, kind, reserved, start, size, section_digest = record
        assert section_id == ordinal and 1 <= kind <= 8 and reserved == 0
        assert start == next_offset and start + size <= offset
        assert helper.sha(blob[start:start + size]) == section_digest
        next_offset += size
    assert next_offset == offset


def write_new(path: Path, data: bytes) -> None:
    """Write owned bytes once at path, rejecting existing artifacts.

The parent must already exist. This protects existing generated corpus evidence
from accidental regeneration; filesystem exceptions propagate to the caller.
"""
    # Open exclusively and write the complete tiny artifact before the context closes it.
    with path.open("xb") as stream:
        stream.write(data)


def json_bytes(value) -> bytes:
    """Return deterministic readable UTF-8 JSON bytes for an owned fixture value.

Nonfinite values are forbidden, Unicode is retained and one terminal newline is
included. Binary floating-point oracles are stored separately when exact bits matter.
"""
    # Serialize without NaN extensions and preserve intentional human-readable structure.
    return (json.dumps(value, ensure_ascii=False, allow_nan=False, indent=2) + "\n").encode("utf-8")


def identity(path: Path) -> dict:
    """Return the exact byte length and SHA-256 of a small source/artifact file.

The file is read only. These identities describe this invocation's bytes and do
not rewrite older qualification records.
"""
    # Read and hash the complete bounded fixture/source file.
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def optional(value=None, floating: bool = False) -> dict:
    """Return a present value or canonical absent scalar evidence object.

None denotes unavailable evidence. Floating absence uses positive binary64 zero;
integer absence uses integer zero. Inputs are copied into the returned dictionary.
"""
    # Preserve the supplied value and distinguish it explicitly from unavailable evidence.
    return {"present": int(value is not None), "value": (0.0 if floating else 0) if value is None else value}


def build(helper, name: str, variant: str = "mixed"):
    """Return a tiny independent container and its logical readback oracle.

variant selects mixed, frequencies_only, empty_shapes, unavailable_shapes,
failure, signed_zero or indeterminate. Owned sections follow FORMAT-1.2.md;
all numerical values originate in the separately checked manufactured packet.
"""
    # Build the existing provenance envelope, replace its empty index, and append modal branches.
    source = packet()
    container = helper.make(name, [], optional=0)
    root = container.root
    root["format"].update(version_minor=2, writer_name="independent-modal-m1-author",
                          writer_version="1", required_features=helper.FEATURES + [MODAL_FEATURE])
    index = root["results"]["index"]
    run_id = "manufactured-modal/α"
    helper_run_id = container.add(1, b"")
    container.run_ids.append(helper_run_id)
    for column, values in [("run_index", [0]), ("kind", [2]), ("run_metadata_section", [helper_run_id])]:
        # Preserve existing section ownership while growing the original empty run index.
        container.replace(index[column], values, shape=[1])
    container.replace_pool(index["run_id"], [run_id])
    container.replace_pool(index["run_name"], [""])
    container.replace(index["run_name"]["present"], [0], shape=[1])

    # Failure evidence has no accepted modal rows; optional quantity states remain independent.
    failed = variant == "failure"
    shape_requested = variant != "frequencies_only"
    diagnostic_requested = variant != "frequencies_only"
    shape_state = 1 if not shape_requested else (2 if failed or variant == "unavailable_shapes" else 0)
    mode_count = 0 if failed else 4
    observation_count = 0 if variant in ("frequencies_only", "empty_shapes") else 4
    frequency_state = 2 if failed else 0
    diagnostic_state = 1 if not diagnostic_requested else (2 if failed else 0)
    eigenvalues = list(source["eigenvalues_per_s2"][:mode_count])
    classes = [2, 0, 3, 3][:mode_count]
    if variant == "signed_zero":
        eigenvalues[1] = -0.0
    if variant == "indeterminate":
        eigenvalues[0] = -0.25
        classes[0] = 1
    run = {
        "info": {
            "run_id": run_id, "assembly_ref": "manufactured_blade_assembly", "kind": 2,
            "run_name_present": 0, "run_name": "", "result_kind": int(failed),
            "search_status": int(failed), "certification_status": 2 if failed else 0,
            "solve_code": "incomplete_prefix" if failed else "success",
            "diagnostic": "Manufactured retained-vector capacity excludes the complete cutoff cluster" if failed else "",
            "reference_state": 0, "physical_id": "manufactured-physical-reference/1",
            "binding_id": "manufactured-six-coordinate-binding/1",
            "requested": {"eigenvalues": int(variant != "frequencies_only"), "frequencies_hz": 1,
                          "mode_shapes": int(shape_requested), "mode_diagnostics": int(diagnostic_requested)},
            "frequencies_state": frequency_state, "mode_diagnostics_state": diagnostic_state,
            "solver_configuration": container.document({"manufactured": True, "variant": variant}),
            "execution": container.document({"scope": "independently manufactured archive, not production solve"}),
        },
        "criteria": {
            "minimum_eigenvalue_per_s2": -8.0, "maximum_eigenvalue_per_s2": 64.0,
            "zero_eigenvalue_tolerance_per_s2": 0.125, "negative_eigenvalue_tolerance_per_s2": 0.5,
            "relative_residual_tolerance": 1e-12, "constraint_tolerance": 1e-12,
            "mass_orthogonality_tolerance": 1e-12, "cluster_relative_tolerance": 0.0,
            "cluster_absolute_tolerance_per_s2": 0.0,
        },
    }
    # Explicit producer-asserted evidence distinguishes known zero from unavailable measurements.
    report = {"requested_elastic_modes": 1, "searched_candidates": 4,
              "retained_elastic_modes": 0 if failed else 2,
              "exhausted_complete_spectrum": 0, "used_sparse_path": 0,
              "used_dense_fallback": 0, "dense_fallback_reason": ""}
    known_counts = {"physical_coordinate_count": 6, "fixed_coordinate_count": 0,
                    "constraint_rows": 0, "constraint_rank": 0, "reduced_dimension": 6,
                    "reduced_mass_nonzeros": 6, "reduced_stiffness_nonzeros": 5,
                    "dense_counted_eigenvalues": 6, "cutoff_cluster_size": None if failed else 2}
    count_names = "physical_coordinate_count fixed_coordinate_count constraint_rows constraint_rank reduced_dimension reduced_mass_nonzeros reduced_stiffness_nonzeros dense_counted_eigenvalues cutoff_cluster_size sparse_requested_candidates sparse_converged_candidates sparse_iterations krylov_basis_size constraint_workspace_bytes operator_workspace_bytes dense_workspace_bytes dense_work_bound factor_workspace_bytes basis_workspace_bytes result_payload_bytes".split()
    for column in count_names:
        # Missing runtime measurements are deliberately absent rather than invented zero costs.
        report[column] = optional(known_counts.get(column))
    known_floats = {"lift_constraint_error": 0.0, "mass_symmetry_error": 0.0,
                    "stiffness_symmetry_error": 0.0, "maximum_mass_orthogonality_error": None if failed else 0.0,
                    "certified_lower_eigenvalue_per_s2": None if failed else -8.0,
                    "certified_upper_eigenvalue_per_s2": None if failed else 4.0,
                    "upper_separator_eigenvalue_per_s2": None if failed else 16.0,
                    "dense_absolute_uncertainty_per_s2": 0.0}
    float_names = "lift_constraint_error mass_symmetry_error stiffness_symmetry_error maximum_mass_orthogonality_error certified_lower_eigenvalue_per_s2 certified_upper_eigenvalue_per_s2 upper_separator_eigenvalue_per_s2 dense_absolute_uncertainty_per_s2 reduction_seconds dense_count_seconds sparse_seconds dense_fallback_seconds".split()
    for column in float_names:
        report[column] = optional(known_floats.get(column), floating=True)
    report["dependent_constraint_rows"] = {"state": 0, "values": container.arr("u64", [])}
    report["effective_shifts_per_s2"] = {"state": 2}
    run["report"] = report

    # Mode/cluster identifiers deliberately exceed binary64's exact integer range.
    mode_ids = [2**53 + 1 + ordinal for ordinal in range(mode_count)]
    cluster_count = 0 if failed else 3
    cluster_ids = [2**63 + ordinal for ordinal in range(cluster_count)]
    modes = {"count": mode_count}
    mode_columns = [("mode_index", "u64", list(range(mode_count))),
                    ("mode_id", "u64", mode_ids), ("cluster_index", "u64", [0, 1, 2, 2][:mode_count]),
                    ("eigenvalue_per_s2", "f64", eigenvalues), ("classification", "u8", classes),
                    ("display_scale", "f64", [0.0, 0.0, 2.0, 0.0][:mode_count]),
                    ("display_scale_present", "u8", [0, 0, 1, 0][:mode_count])]
    if frequency_state == 0:
        mode_columns += [("frequency_hz", "f64", [0.0, 0.0] + source["frequency_hz"][2:]),
                         ("frequency_hz_present", "u8", [0, 0, 1, 1])]
    if diagnostic_state == 0:
        for column, values in [("projected_relative_residual", [0.0] * mode_count),
                               ("constraint_relative_error", [0.0] * mode_count),
                               ("modal_mass", [1.0] * mode_count)]:
            mode_columns += [(column, "f64", values), (column + "_present", "u8", [1] * mode_count)]
    for column, dtype, values in mode_columns:
        modes[column] = container.arr(dtype, values)
    run["modes"] = modes
    clusters = {"count": cluster_count}
    for column, values in [("cluster_index", list(range(cluster_count))), ("cluster_id", cluster_ids),
                           ("first_mode_index", [0, 1, 2][:cluster_count]), ("mode_count", [1, 1, 2][:cluster_count])]:
        clusters[column] = container.arr("u64", values)
    run["clusters"] = clusters

    # Keep physical selectors in authored order; reference matrices retain their explicit ranks.
    selected_observations = source["observations"][:observation_count]
    observations = {"count": observation_count,
                    "observation_index": container.arr("u64", list(range(observation_count)))}
    for column in ("request_id", "occurrence_id", "port_id"):
        observations[column] = container.pool([row[column] for row in selected_observations])
    observations["reference_triad"] = container.pool(["port_local" if row["kind"] == "port" else "beam_material" for row in selected_observations])
    observations["kind"] = container.arr("u8", [int(row["kind"] == "material") for row in selected_observations])
    observations["material_s_m"] = container.arr("f64", [row["material_s_m"] or 0.0 for row in selected_observations])
    observations["side"] = container.arr("u8", [{"unspecified": 0, "left": 1, "right": 2}[row["side"]] for row in selected_observations])
    for column, width in [("position_world_m", 3), ("orientation_world_wxyz", 4)]:
        observations[column] = container.arr("f64", [row[column] for row in selected_observations], [observation_count, width])
    run["observations"] = observations
    shapes = {"state": shape_state}
    flattened = np.asarray(source["shape_values"], dtype="<f8")[:mode_count, :observation_count, :]
    if shape_state == 0:
        # Reuse only the frozen independent raw/public-ZFP frame author, never a production writer.
        matrix = flattened.reshape(mode_count, observation_count * 6)
        values, compression = helper.values(container, matrix, 0, name + ".shapes")
        values["axis_names"] = ["mode", "observation_component"]
        shapes.update(logical_shape=[mode_count, observation_count, 6],
                      component_names=["translation_x", "translation_y", "translation_z", "rotation_x", "rotation_y", "rotation_z"],
                      component_units=["m", "m", "m", "rad", "rad", "rad"], frame="world",
                      normalization="unit_generalized_mass", coefficient_convention="per_unit_modal_coordinate", values=values)
    else:
        compression = []
    run["shapes"] = shapes
    container.runs.append(run)
    expected = {"run_id": run_id, "result_kind": int(failed), "search_status": int(failed),
                "mode_count": mode_count, "observation_count": observation_count,
                "mode_ids": mode_ids, "cluster_ids": cluster_ids, "eigenvalues_per_s2": eigenvalues,
                "eigenvalue_bits_hex": [struct.pack("<d", value).hex() for value in eigenvalues],
                "classifications": classes, "frequency_state": frequency_state,
                "diagnostic_state": diagnostic_state, "shape_state": shape_state,
                "logical_shape": [mode_count, observation_count, 6] if shape_state == 0 else None,
                "shape_bits_hex": [struct.pack("<d", value).hex() for value in flattened.flat] if shape_state == 0 else [],
                "frames": compression}
    return container, expected


def mutate_case(helper, name: str, container) -> None:
    """Apply one deliberately malformed semantic change to an owned container.

name chooses the bounded negative case. Hashes are later recomputed, so rejection
must follow the named violated schema rule rather than incidental stale hashes.
"""
    # Locate the one run, mutate one contract property, and retain other valid envelope structure.
    run = container.runs[0]
    if name == "unsupported_minor":
        container.root["format"]["version_minor"] = 3
    elif name == "missing_modal_token":
        container.root["format"]["required_features"].remove(MODAL_FEATURE)
    elif name == "unknown_feature":
        container.root["format"]["required_features"].append("future-modal-feature-1")
    elif name == "wrong_logical_shape":
        run["shapes"]["logical_shape"] = [4, 3, 6]
    elif name == "oversized_mode_count":
        run["modes"]["count"] = 1000001
    elif name == "bad_quaternion":
        values = np.frombuffer(container.data(run["observations"]["orientation_world_wxyz"]), dtype="<f8").copy()
        values[0] = 2.0
        container.replace(run["observations"]["orientation_world_wxyz"], values)
    elif name == "wrong_shape_frame":
        run["shapes"]["frame"] = "owner_local"
    elif name == "wrong_reference_triad":
        container.replace_pool(run["observations"]["reference_triad"], ["future-triad", "beam_material", "beam_material", "port_local"])
    elif name == "invalid_classification":
        container.replace(run["modes"]["classification"], [2, 0, 2, 3])
    elif name == "invalid_frequency_presence":
        container.replace(run["modes"]["frequency_hz_present"], [1, 0, 1, 1])
        container.replace(run["modes"]["frequency_hz"], [1.0, 0.0, 1.0 / np.pi, 1.0 / np.pi])
    elif name == "incomplete_as_success":
        run["info"]["search_status"] = 1
    elif name == "bad_frame_extent":
        descriptor = run["shapes"]["values"]["frame_index"]
        raw = bytearray(container.data(descriptor))
        fields = list(helper.FRAME.unpack_from(raw))
        fields[2] = 0
        helper.FRAME.pack_into(raw, 0, *fields)
        container.replace(descriptor, raw)
    elif name == "bad_cluster_partition":
        container.replace(run["clusters"]["mode_count"], [1, 1, 1])
    elif name == "bad_separator":
        run["report"]["upper_separator_eigenvalue_per_s2"] = optional(4.0, floating=True)
    else:
        raise ValueError("Unknown malformed modal fixture: " + name)


def damage_section(container, blob: bytes, descriptor: dict) -> bytes:
    """Flip the first byte of one nonempty section without repairing its hashes.

container is the already sealed fixture assembly. The returned owned bytes have
unchanged envelope/root/index metadata, targeting the descriptor's read phase.
"""
    # Calculate the physical section start from the independent ordered section list and flip one bit.
    section_id = descriptor["section"]
    assert container.sections[section_id][1]
    offset = 128 + sum(len(data) for _, data in container.sections[:section_id])
    damaged = bytearray(blob)
    damaged[offset] ^= 1
    return bytes(damaged)


def main() -> None:
    """Author the bounded corpus in a fresh directory and write its exact manifest.

--output selects the destination; --spec identifies the reviewed 1.2 contract.
An existing identical manufactured_packet.json is allowed because it precedes
the binary author. Every other artifact must be absent before any write begins.
"""
    # Parse destinations, prepare all independent cases, verify envelopes, then publish fresh artifacts.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "fixtures_modal_m1")
    parser.add_argument("--spec", type=Path, default=ROOT / "spec/FORMAT-1.2.md")
    arguments = parser.parse_args()
    helper = load_helper()
    artifacts = {}
    manifest = {"schema": "serene-modal-independent-corpus/1", "format_version": "1.2",
                "scope": "independent manufactured byte/physical oracle; no production qualification claim",
                "sources": {"author": identity(Path(__file__)),
                            "oracle": identity(Path(__file__).with_name("oracle.py")),
                            "independent_base_author": identity(HELPER),
                            "format_contract": identity(arguments.spec)}, "positive": [], "negative": []}
    for variant in ("mixed", "frequencies_only", "empty_shapes", "unavailable_shapes", "failure", "signed_zero", "indeterminate"):
        # Each valid variant is built afresh so omitted branches never leave orphan sections.
        name = "modal_" + variant
        container, expected = build(helper, name, variant)
        blob = encode(helper, container)
        assert_envelope(helper, blob)
        artifacts[name + ".serene"] = blob
        artifacts[name + ".expected.json"] = json_bytes(expected)
        artifacts.update(container.sidecars)
        manifest["positive"].append({"file": name + ".serene", "expected": name + ".expected.json", "variant": variant})
    negative_cases = [
        ("unsupported_minor", "open", "unsupported_feature"),
        ("missing_modal_token", "open", "invalid_contract"),
        ("unknown_feature", "open", "unsupported_feature"),
        ("wrong_logical_shape", "run", "invalid_contract"),
        ("oversized_mode_count", "run", "resource_limit"),
        ("bad_quaternion", "run", "invalid_contract"),
        ("wrong_shape_frame", "run", "invalid_contract"),
        ("wrong_reference_triad", "run", "invalid_contract"),
        ("invalid_classification", "run", "invalid_contract"),
        ("invalid_frequency_presence", "run", "invalid_contract"),
        ("incomplete_as_success", "run", "invalid_contract"),
        ("bad_frame_extent", "run", "invalid_contract"),
        ("bad_cluster_partition", "run", "invalid_contract"),
        ("bad_separator", "run", "invalid_contract"),
    ]
    for name, phase, error in negative_cases:
        # Semantic failures retain a completely valid hash envelope to isolate schema admission.
        container, _ = build(helper, name)
        mutate_case(helper, name, container)
        blob = encode(helper, container)
        assert_envelope(helper, blob)
        artifacts[name + ".serene"] = blob
        manifest["negative"].append({"file": name + ".serene", "phase": phase, "error": error})
    container, _ = build(helper, "damage_source")
    clean = encode(helper, container)
    run = container.runs[0]
    for name, descriptor, phase in [("bad_eigenvalue_digest", run["modes"]["eigenvalue_per_s2"], "run"),
                                     ("bad_shape_payload_digest", run["shapes"]["values"]["payload"], "decode")]:
        # Only these cases intentionally retain stale section/frame hashes.
        artifacts[name + ".serene"] = damage_section(container, clean, descriptor)
        manifest["negative"].append({"file": name + ".serene", "phase": phase, "error": "verification_failure"})
    artifacts["truncated_directory.serene"] = clean[:-1]
    manifest["negative"].append({"file": "truncated_directory.serene", "phase": "open", "error": "invalid_contract"})
    artifacts["manufactured_packet.json"] = json_bytes(packet())
    manifest["artifacts"] = {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                             for name, data in sorted(artifacts.items())}
    artifacts["manifest.json"] = json_bytes(manifest)
    # Reject a populated destination before writing any new artifact.
    arguments.output.mkdir(parents=True, exist_ok=True)
    for name, data in artifacts.items():
        existing = arguments.output / name
        if existing.exists() and not (name == "manufactured_packet.json" and existing.read_bytes() == data):
            raise FileExistsError(str(existing))
    for name, data in artifacts.items():
        destination = arguments.output / name
        if not destination.exists():
            write_new(destination, data)
    print(json.dumps({"positive": len(manifest["positive"]), "negative": len(manifest["negative"]),
                      "artifacts": len(artifacts), "output": str(arguments.output)}))


if __name__ == "__main__":
    main()
