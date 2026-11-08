# Serene outputs — custom-container transition

The active design review is `spec/CUSTOM_CONTAINER_DRAFT.md` (G0C), with independent fixtures in `fixtures_custom`. The user selected a small `.serene` container with explicit little-endian sections and direct raw/ZFP payloads. The existing HDF5 implementation and fixtures are retained as historical work; they are not the intended deployment backend. Production migration awaits coordinator G0C acceptance. See `docs/native_outputs_handoff.md` for exact evidence and current limits.

The model/view and codec components remain reusable; the shared Python codec snapshot is unchanged. No custom-container installed library or application integration is claimed yet.

## Historical implementation README

# serene-outputs

C++23 native Serene output components. The file format is ordinary unfiltered HDF5 containing explicit raw or full-header ZFP binary64 frames. See [spec/FORMAT.md](spec/FORMAT.md) for the normative contract and [docs/native_outputs_handoff.md](docs/native_outputs_handoff.md) for actual qualification state.

CMake targets are `serene::outputs_model`, `serene::outputs_codec`, and `serene::outputs_hdf5`. Model-only builds set `SERENE_OUTPUTS_BUILD_CODEC=OFF` and `SERENE_OUTPUTS_BUILD_HDF5=OFF`. Codec builds require an explicit pinned `SERENE_OUTPUTS_ZFP_ARCHIVE`; writer builds also require `SERENE_OUTPUTS_HDF5_ARCHIVE`. Archives are hash-checked; there is no moving network fetch or implicit system-library fallback. Installed packages support component-specific `find_package(SereneOutputs CONFIG REQUIRED COMPONENTS model)` (or codec/hdf5), with private native dependency libraries exported relocatably.

Prepare metadata, capacities and the codec before simulation. Run no HDF5 or compression during active simulation. After the gate ends and log files close, pass immutable committed views to `ArchiveSession`, append one run, and finalize with no-clobber publication. Keep all source owners alive through save/retry. Exact codec is the current implementation; fixed accuracy and bundles await their later gates.

N02/N03 Windows Release checks have passed. N04 paired-reader/publication/memory checks are in progress; no release or G1 claim yet. See the handoff for explicit platform, ownership, provenance-validity and memory-accounting limits.
