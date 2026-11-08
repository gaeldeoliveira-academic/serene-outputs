# Building and consuming serene-outputs

CMake 3.28+ and C++23 are required. MSVC is the primary qualification compiler. On Windows, load the existing Serene compiler environment before configuring. Package version 0.2.2 is independent of native container versions 1.0/1.1. The production targets have no container-library, Chrono, rendering or logging dependency. Use `tools/build_document_preflight.cmd` and fresh `build/document-preflight-msvc` / `build/install-document-preflight` paths. Preserve the qualified0.2.0 and0.2.1 prefixes (`build/install-extensions`, `build/install-capacity-planner`) and their build evidence.

## Model only

```
cmake -S . -B build/model -DSERENE_OUTPUTS_BUILD_CODEC=OFF -DSERENE_OUTPUTS_BUILD_ARCHIVE=OFF
cmake --build build/model --config Release
ctest --test-dir build/model -C Release --output-on-failure
cmake --install build/model --config Release --prefix <install-prefix>
```

This mode neither configures nor links ZFP. The private nlohmann JSON header and its license are pinned in `third_party/dependencies.json`; no JSON type is exposed in public headers.

## Codec and archive writer

The supported dependency mode is `PINNED`. Supply the original upstream ZFP source tarball through `SERENE_OUTPUTS_ZFP_ARCHIVE`. Configuration verifies its exact SHA-256 value. No network fetch, installed-package fallback or moving branch is selected. Set `SERENE_OUTPUTS_BUILD_ARCHIVE=OFF` for a codec-only build. The archive writer requires the model and codec, and writes the accepted `.serene` container with ordinary file I/O.

```
cmake -S . -B build/document-preflight-msvc -DSERENE_OUTPUTS_ZFP_ARCHIVE=<zfp-tar.gz>
cmake --build build/document-preflight-msvc --config Release
cmake --install build/document-preflight-msvc --config Release --prefix build/install-document-preflight
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

Components are `model`, `codec` and `archive`, exporting `serene::outputs_model`, `serene::outputs_codec` and `serene::outputs_archive`. Request the narrowest component. Model import does not load native codec or archive targets. Codec import does not load the archive backend. Archive import includes model and codec. With no explicit component, the package selects the most capable installed component.

The private ZFP imported archive resolves relative to the relocated install prefix. Its headers are not exposed to clients. Tests under `tests/install_consumer` can be copied outside the checkout and built against a relocated package through `CMAKE_PREFIX_PATH`. They call actual exported functions and check component isolation. No compatibility alias or package component exists for the historical HDF5 backend. Use a fresh build/install prefix; the historical artifacts remain under `history/hdf5` and their existing build directories.

## Save-memory measurement

`measure_save <record-count> <new-archive.serene>` is an optional test executable outside routine CTest. Run each record count in a fresh process with a new output path; no-clobber publication remains active. Caller histories, prepared workspace and codec are allocated before tracking. Its JSON result separates retained histories, requested application workspace, Debug CRT allocation/reallocation request totals, and observed process private-usage peaks sampled at save operations. Cumulative allocation bytes are churn, not simultaneously live memory. Sampled private usage is not an exact OS peak, and instrumented save time is not a performance benchmark. Release explicitly reports that the Debug allocation hook is unavailable.

## Qualification scope

The owning stage records exact source/artifact hashes, commands, configuration, installation and external-consumer results. Historical container evidence does not qualify this backend. Windows testing does not establish Linux/GCC/Clang support; unavailable platforms remain reported as not run. No remote package publication is performed by these commands.
