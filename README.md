# Serene outputs

Serene's native `.serene` output library stores explicit little-endian sections and direct raw/ZFP frames in a small versioned container. HDF5 is not a production dependency.

CMake components and targets:

- `model`: `serene::outputs_model` â€” owned descriptions, prepared budgets and borrowed result/provenance views.
- `codec`: `serene::outputs_codec` â€” pinned reversible/fixed-accuracy ZFP framing, exact-real error verification and SHA-256.
- `pose_recorder`: `serene::outputs_pose_recorder` — independent headless `.spose` visualization batches; raw poses and bounded linked-index publication, no codec/core/Chrono/Qt dependency.
- `archive`: `serene::outputs_archive` â€” typed diagnostics, indexed provenance, bounded section writing, verified spool assembly and checked no-clobber publication.

Package 0.2.4 adds optional four-point winch geometry to the raw sidecar: [wire amendment](spec/POSE_WINCH_GEOMETRY.md) and [J07 implementation handoff](docs/winch_J07_handoff.md). Physical poses and accepted feature coordinates are retained unchanged, with explicit representation-consistency checks and complete-pair publication. Scientific archive semantics remain unchanged.

Package 0.2.3 adds the separate [pose sidecar recorder](docs/pose_recorder_handoff.md), specified in [POSE_SIDECAR.md](spec/POSE_SIDECAR.md). This does not change scientific `.serene` semantics or ArchiveSession.

Package 0.2.2 adds [bounded document preflight](docs/native_document_preflight_handoff.md) and retains [count-only capacity planning](docs/native_capacity_planner_handoff.md) over qualified0.2.0. It implements the unchanged [1.0 contract](spec/FORMAT.md) and accepted [1.1 extension contract](spec/FORMAT-1.1.md). Files without extensions remain 1.0. The separate joint acceptance is `serene-theory/validation/native_output_extensions_contract_acceptance.json`, SHA-256 `84147366e8534197fa1063db769b0060e3966aebfdc4f35757906bf342248c6b`; its frozen candidate and independent corpus remain unchanged. [Publication mapping](docs/native_extensions_spec_publication.json) records the public specification's status-only change.

See [cmake/BUILDING.md](cmake/BUILDING.md) for pinned offline builds and [docs/native_extensions_handoff.md](docs/native_extensions_handoff.md) for extension API, budgets and implementation qualification. The qualified shared Python codec snapshot is recorded in `docs/codec_extensions_snapshot.json`; later documentation-only source edits do not rewrite that historical identity. Historical 0.1/G1/G2 evidence remains in [docs/native_outputs_handoff.md](docs/native_outputs_handoff.md) and the immutable `history/before_extensions/accepted_0_1_20261108` checkpoint. Linux/macOS qualification remains unrun.

Historical HDF5 work is retained for provenance only. No compatibility reader or HDF5 deployment dependency is implied.

Maintainer ownership, documentation and verification conventions are in [AGENTS.md](AGENTS.md). The [16 November documentation handoff](docs/documentation_coverage_20261116.md) records comment-only coverage relative to the existing pose-recorder working tree; it is not a new binary qualification.

Package 0.2.5 adds optional sided F geometry and bounded complete frame groups to
the raw sidecar. The [common group contract](spec/POSE_OPTIONAL_OVERLAYS.md) and
[F layout](spec/POSE_FLUID_GEOMETRY.md) preserve physical N/port and winch meanings.
F is a separate station population with explicit validity, topology and source
identity. The [Stage00 F handoff](docs/visualization_S00_F_handoff.md) records the
actual tested package and compatibility limits. Scientific archive semantics
and the scientific Python companion remain unchanged.

Package 0.2.6 adds optional applied physical load density, with exact completed
interval provenance and original source points. See the [load contract](spec/POSE_APPLIED_LOADS.md)
and [Stage00 load handoff](docs/visualization_S00_loads_handoff.md). It can be
recorded without the F display population. Scientific archives are unchanged.

Package 0.2.7 adds signed elastic moments at independently addressed sided cuts.
The [moment contract](spec/POSE_ELASTIC_MOMENTS.md) preserves all three current-N
components about N, explicit display intervals and sided discontinuities.
See the [moment handoff](docs/visualization_S00_moments_handoff.md) for the actual
qualification and separate Core/viewer responsibilities. The earlier qualified
F and applied-load installations remain immutable.

Package 0.2.8 adds optional fixed inflow planes with per-plane validity masks,
absolute unperturbed world velocities and preserved provider initialization
metadata. The [plane contract](spec/POSE_INFLOW_PLANES.md) and
[plane handoff](docs/visualization_S00_planes_handoff.md) define the typed boundary,
resource accounting and separate provider/Core/viewer integration gates.
The previous three Stage00 installations and their qualification evidence remain
immutable. Scientific archive semantics and the scientific Python reader are unchanged.
