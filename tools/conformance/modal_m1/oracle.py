"""Author a tiny exact modal oracle independently of the Serene solver and writer.

This manufactured structural packet qualifies archive semantics only. It is not
a finite-element blade model or evidence for production blade frequencies.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def packet() -> dict:
    """Return an owned exact unit-mass packet and independently mapped observations.

The six coordinates contain world translation followed by owner-local rotation.
The returned JSON-compatible object borrows nothing and contains only finite
numbers. The first four directions form the certified prefix through a repeated
positive cutoff cluster; the full diagonal pencil supplies the exact count.
"""
    # Define the exact diagonal pencil, normalize its basis, and map four retained modes.
    # Physical mass/inertia coefficients corresponding to the declared coordinate order.
    mass = [1.0, 4.0, 16.0, 1.0, 4.0, 16.0]
    # Signed angular-frequency-squared spectrum of the independent diagonal pencil.
    spectrum = [-4.0, 0.0, 4.0, 4.0, 16.0, 36.0]
    # Restoring coefficients are the exact diagonal product M*lambda.
    stiffness = [m * value for m, value in zip(mass, spectrum)]
    # Each retained vector is a basis column with exact binary mass normalization.
    vectors = [[0.0] * 6 for _ in range(4)]
    for ordinal, scale in enumerate([1.0, 0.5, 0.25, -1.0]):
        # Preserve a deliberately negative fourth eigenvector to test sign freedom.
        vectors[ordinal][ordinal] = scale
    # Reference observations preserve separate sided identities at one physical point.
    observations = [
        {"request_id": "root", "occurrence_id": "blade/fixture", "kind": "port",
         "port_id": "root", "material_s_m": None, "side": "unspecified",
         "position_world_m": [1.0, 2.0, 0.0], "orientation_world_wxyz": [1.0, 0.0, 0.0, 0.0]},
        {"request_id": "station-left", "occurrence_id": "blade/fixture", "kind": "material",
         "port_id": "", "material_s_m": 1.0, "side": "left",
         "position_world_m": [1.0, 2.0, 3.0], "orientation_world_wxyz": [0.0, 0.0, 0.0, 1.0]},
        {"request_id": "station-right", "occurrence_id": "blade/fixture", "kind": "material",
         "port_id": "", "material_s_m": 1.0, "side": "right",
         "position_world_m": [1.0, 2.0, 3.0], "orientation_world_wxyz": [0.0, 0.0, 0.0, 1.0]},
        {"request_id": "offset-port", "occurrence_id": "blade/fixture", "kind": "port",
         "port_id": "tip-offset", "material_s_m": None, "side": "unspecified",
         "position_world_m": [1.0, 0.0, 3.0], "orientation_world_wxyz": [0.0, 0.0, 0.0, 1.0]},
    ]
    # Owned [mode, observation, component] coefficients, with six fixed world components.
    shapes = []
    for vector in vectors:
        # Exact half-turn around world Z maps the owner's local angular increment.
        translation = vector[:3]
        angular = [-vector[3], -vector[4], vector[5]]
        # The offset is r_world=(0,-2,0); use the analytic cross product theta x r.
        offset_translation = [translation[0] + 2.0 * angular[2],
                              translation[1], translation[2] - 2.0 * angular[0]]
        shapes.append([[0.0] * 6, translation + angular,
                       translation + angular, offset_translation + angular])
    # Prove normalization, orthogonality, and exact eigenpairs before emitting evidence.
    for left, vector in enumerate(vectors):
        for right, other in enumerate(vectors):
            assert sum(m * a * b for m, a, b in zip(mass, vector, other)) == float(left == right)
        assert all(k * value == spectrum[left] * m * value
                   for k, m, value in zip(stiffness, mass, vector))
    assert shapes[3][3] == [0.0, 0.0, -2.0, 1.0, -0.0, 0.0]
    # Return scientific and provenance details without claiming a real blade solution.
    return {
        "schema": "serene-modal-independent-oracle/1",
        "scope": "manufactured archive and structural mapping oracle; not blade qualification",
        "coordinate_order": ["world_tx", "world_ty", "world_tz", "owner_local_rx", "owner_local_ry", "owner_local_rz"],
        "mass_diagonal": mass, "mass_units": ["kg"] * 3 + ["kg*m^2"] * 3,
        "stiffness_diagonal": stiffness, "complete_eigenvalues_per_s2": spectrum,
        "physical_mode_vectors": vectors, "normalization": "unit_generalized_mass",
        "requested_elastic_modes": 1, "found_elastic_modes": 2,
        "eigenvalues_per_s2": spectrum[:4],
        "classifications": ["adverse_negative", "near_zero_unresolved", "elastic", "elastic"],
        "frequency_hz": [None, None, 1.0 / math.pi, 1.0 / math.pi],
        "cluster_ids": [0, 1, 2, 2], "cluster_sizes": [1, 1, 2],
        "search_interval_per_s2": [-8.0, 64.0],
        "certified_lower_per_s2": -8.0, "certified_upper_per_s2": 4.0,
        "upper_separator_per_s2": 16.0, "dense_counted_eigenvalues": 6,
        "zero_tolerance_per_s2": 0.125, "negative_tolerance_per_s2": 0.5,
        "cluster_absolute_tolerance_per_s2": 0.0, "cluster_relative_tolerance": 0.0,
        "owner_reference_position_world_m": [1.0, 2.0, 3.0],
        "owner_reference_orientation_world_wxyz": [0.0, 0.0, 0.0, 1.0],
        "offset_local_m": [0.0, 2.0, 0.0], "offset_world_m": [0.0, -2.0, 0.0],
        "observations": observations,
        "shape_component_order": ["tx", "ty", "tz", "rx", "ry", "rz"],
        "shape_component_units": ["m/unit_modal_coordinate"] * 3 + ["rad/unit_modal_coordinate"] * 3,
        "shape_components_frame": "world", "shape_values": shapes,
        "physical_amplitude": "independent modal coordinate; never encoded as a shape scale",
    }


def main() -> None:
    """Write the independently computed packet at the requested fresh output path.

The only command-line argument is the output JSON path. Existing files are
rejected, so an author rerun cannot silently replace a frozen oracle.
"""
    # Parse the destination, compute the verified oracle, then publish exact JSON bytes once.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    result = packet()
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    with arguments.output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(result, stream, ensure_ascii=False, allow_nan=False, indent=2)
        stream.write("\n")


if __name__ == "__main__":
    main()
