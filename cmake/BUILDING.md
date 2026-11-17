# Building and consuming serene-outputs

CMake 3.28+ and C++23 are required. MSVC is the primary qualification compiler. On Windows, load the existing Serene compiler environment before configuring. Current package version 0.2.4 is independent of native container versions 1.0/1.1 and pose sidecar version1. The production targets have no container-library, Chrono, rendering or logging dependency. Load `C:/Users/12650029/serene-academic/serene-math/load_compilers.bat`, then explicitly select MSVC as the maintained Windows wrappers do. Choose new build/install paths for new work. The checked-in stage wrappers retain their qualification paths; do not rerun them over accepted artifacts. Preserve qualified prefixes including `build/install-custom`, `build/install-extensions`, `build/install-capacity-planner`, `build/install-document-preflight` and the pose-recorder prefixes recorded in its handoff.

## Model only

```
cmake -S . -B build/model -DSERENE_OUTPUTS_BUILD_CODEC=OFF -DSERENE_OUTPUTS_BUILD_ARCHIVE=OFF -DSERENE_OUTPUTS_BUILD_POSE_RECORDER=OFF
cmake --build build/model --config Release
ctest --test-dir build/model -C Release --output-on-failure
cmake --install build/model --config Release --prefix <install-prefix>
```

This mode neither configures nor links ZFP. The private nlohmann JSON header and its license are pinned in `third_party/dependencies.json`; no JSON type is exposed in public headers.

## Codec and archive writer

The supported dependency mode is `PINNED`. Supply the original upstream ZFP source tarball through `SERENE_OUTPUTS_ZFP_ARCHIVE`. Configuration verifies its exact SHA-256 value. No network fetch, installed-package fallback or moving branch is selected. Set `SERENE_OUTPUTS_BUILD_ARCHIVE=OFF` and `SERENE_OUTPUTS_BUILD_POSE_RECORDER=OFF` for model plus codec only. The archive writer requires the model and codec, and writes the accepted `.serene` container with ordinary file I/O.

```
cmake -S . -B build/current-native-local -DSERENE_OUTPUTS_ZFP_ARCHIVE=<zfp-tar.gz>
cmake --build build/current-native-local --config Release
cmake --install build/current-native-local --config Release --prefix build/install-current-native-local
```

Pinned ZFP is built from source with the matching runtime: Windows /MD for Release and /MDd for Debug. Research /MT binaries are not used in production. Serene Debug libraries use `_d` filenames; dependency archives are stored below `lib/serene-outputs/<configuration>`. Build/install both configurations for a multi-config consumer, or restrict its `CMAKE_CONFIGURATION_TYPES` to the installed configuration. Debug and Release imports have strict mappings, so a missing matching runtime fails configuration instead of falling back. RelWithDebInfo and MinSizeRel can use Release because their CRT family is identical.

Serial ZFP uses 64-bit words and ROUND_NEVER with OpenMP, CUDA, DAZ, tight-error and strided-bitstream options disabled. The public codec surface and binary profile are independent of the archive backend.

The preserved0.2.0 qualification used `tools/build_extensions.cmd Release` and `Debug`, loading `serene-math/load_compilers.bat` then explicitly selecting MSVC. The0.2.1 driver uses the same compiler setup and exact offline archive `build/dependencies-extensions/zfp-1.0.1.tar.gz`, SHA-256 `599802776f2427df2d3dbd655f4443e146ebeedcf50d06c6a8ce26784263884b`. This copy was recovered from the already pinned Python source dependency after the former CFD research-cache path disappeared. Source revision and dependency pin did not change. Existing `build/install-custom` and `build/install-extensions` are preserved.

Focused extension tests are `test_extensions`, `test_bundle`, `test_spool_reader` and `measure_extensions`. Writer/bundle/memory output paths must be new. The spool validator uses a separately authored single-run corpus under `build/extensions-spool-fixtures-final`; this is distinct from the accepted wire corpus. Final commands and artifacts are recorded in the extension qualification receipt. The codec owner's full exponent/oracle qualification is reused rather than repeated by every consumer.

`SERENE_OUTPUTS_DEPENDENCIES_ONLY=ON` is a bootstrap mode for configuring/building the pinned codec dependency before application sources are ready. It does not build a usable Serene library or establish an acceptance gate.

## Installed consumers

```
find_package(SereneOutputs CONFIG REQUIRED COMPONENTS archive)
target_link_libraries(my_app PRIVATE serene::outputs_archive)
```

Components are `model`, `codec`, `archive` and `pose_recorder`, exporting the matching `serene::outputs_<component>` targets. Request the narrowest component. Model import does not load native codec or archive targets. Codec import does not load the archive backend. Archive import includes model and codec. A pose-only import does not load model, codec or archive. With no explicit component, the package keeps the existing archive, then codec, then model preference; it does not select pose_recorder implicitly.

The private ZFP imported archive resolves relative to the relocated install prefix. Its headers are not exposed to clients. Tests under `tests/install_consumer` can be copied outside the checkout and built against a relocated package through `CMAKE_PREFIX_PATH`. They call actual exported functions and check component isolation. No compatibility alias or package component exists for the historical HDF5 backend. Use a fresh build/install prefix; the historical artifacts remain under `history/hdf5` and their existing build directories.

## Save-memory measurement

`measure_save <record-count> <new-archive.serene>` is an optional test executable outside routine CTest. Run each record count in a fresh process with a new output path; no-clobber publication remains active. Caller histories, prepared workspace and codec are allocated before tracking. Its JSON result separates retained histories, requested application workspace, Debug CRT allocation/reallocation request totals, and observed process private-usage peaks sampled at save operations. Cumulative allocation bytes are churn, not simultaneously live memory. Sampled private usage is not an exact OS peak, and instrumented save time is not a performance benchmark. Release explicitly reports that the Debug allocation hook is unavailable.

## Qualification scope

The owning stage records exact source/artifact hashes, commands, configuration, installation and external-consumer results. Historical container evidence does not qualify this backend. Windows testing does not establish Linux/GCC/Clang support; unavailable platforms remain reported as not run. No remote package publication is performed by these commands.

## Independent pose sidecar recorder (0.2.3)

The preserved qualification used `tools/build_pose_recorder.cmd Release` and `Debug`. For new work, configure a fresh build/install prefix with codec and
archive OFF and `SERENE_OUTPUTS_BUILD_POSE_RECORDER=ON`. The raw recorder neither
configures nor links ZFP. Tests use the selected Python3 interpreter for independent
byte validation. Request `find_package(SereneOutputs 0.2.3 CONFIG REQUIRED COMPONENTS
pose_recorder)` and link `serene::outputs_pose_recorder`; this isolated import does
not load model, codec or archive targets. The ordinary default package component
selection remains unchanged. Public plain metadata/value types have no core/Qt/Chrono
dependency. The wire contract and qualification limits are in `spec/POSE_SIDECAR.md`
and `docs/pose_recorder_handoff.md`; capture scheduling itself belongs to core.

Documentation-only changes are checked against the starting working tree without rebuilding qualified binaries. Existing source hashes and installed artifacts continue to identify their historical snapshots; see the [documentation handoff](../docs/documentation_coverage_20261116.md).

## J07 combined winch-feature package (0.2.4)

Use `tools/build_winch_recorder.cmd configure`, then `build`, `test` and `install`
with Release and Debug. This driver owns fresh `build/batch-j/J07/combined-msvc`
and `combined-install` paths, uses the existing offline ZFP archive and preserves
all old installed prefixes. Do not rerun it over accepted J07 evidence for later
work; choose a new stage path instead. The package contains all four components
in one physical header view. See `docs/winch_J07_handoff.md` for exact qualified
commands, manifest identity and current Core consumption requirements. Core must
refresh its own explicit version/manifest and source-selection guards; merely
selecting a new prefix cannot bypass historical stage pins. No Core changes are
made by this repository.
