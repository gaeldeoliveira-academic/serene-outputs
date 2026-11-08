# Serene outputs

Serene's native `.serene` output library stores explicit little-endian sections and direct raw/ZFP frames in a small versioned container. HDF5 is not a production dependency.

CMake components and targets:

- `model`: `serene::outputs_model` — owned descriptions, prepared budgets and borrowed result/provenance views.
- `codec`: `serene::outputs_codec` — pinned reversible/fixed-accuracy ZFP framing, exact-real error verification and SHA-256.
- `archive`: `serene::outputs_archive` — typed diagnostics, indexed provenance, bounded section writing, verified spool assembly and checked no-clobber publication.

Package 0.2.2 adds [bounded document preflight](docs/native_document_preflight_handoff.md) and retains [count-only capacity planning](docs/native_capacity_planner_handoff.md) over qualified0.2.0. It implements the unchanged [1.0 contract](spec/FORMAT.md) and accepted [1.1 extension contract](spec/FORMAT-1.1.md). Files without extensions remain 1.0. The separate joint acceptance is `serene-theory/validation/native_output_extensions_contract_acceptance.json`, SHA-256 `84147366e8534197fa1063db769b0060e3966aebfdc4f35757906bf342248c6b`; its frozen candidate and independent corpus remain unchanged. [Publication mapping](docs/native_extensions_spec_publication.json) records the public specification's status-only change.

See [cmake/BUILDING.md](cmake/BUILDING.md) for pinned offline builds and [docs/native_extensions_handoff.md](docs/native_extensions_handoff.md) for extension API, budgets and implementation qualification. The shared Python codec identity is fixed in `docs/codec_extensions_snapshot.json`. Historical 0.1/G1/G2 evidence remains in [docs/native_outputs_handoff.md](docs/native_outputs_handoff.md) and the immutable `history/before_extensions/accepted_0_1_20261108` checkpoint. Linux/macOS qualification remains unrun.

Historical HDF5 work is retained for provenance only. No compatibility reader or HDF5 deployment dependency is implied.
