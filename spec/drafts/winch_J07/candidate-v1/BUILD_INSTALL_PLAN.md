# Minimal J07 build and J08 consumption plan

Prospective commands only: no configure, compilation, installation or Core guard
edit has been performed at this contract gate.

Recommend an honest package **0.2.4**, independent of bootstrap1.0, archive1.0/1.1
and proposed feature layout1. Use a single fresh combined package with model,
codec, archive and pose recorder. Rebuilding the small Outputs target set avoids
mixing old scientific and new recorder physical header trees. Preserve all old
prefixes, dependencies, profiles, source snapshots and accepted manifests.

The current Core guard accepts package0.2.0 through0.2.3 and, with recorder enabled
and no standalone prefix, additionally requires exactly0.2.3 and the old combined
manifest SHA `e46caba56d09261ca38b52e18549bc0f1a765d1917998a1c8f91770feffa584c`.
Thus retaining version0.2.3 for changed bytes would **still require a guard change**.
The standalone route also compares physical headers including `pose_recorder.hpp`;
unchanged scientific headers alone do not establish a safe two-prefix view.

Current J06 ordinary Release uses combined0.2.3; Debug uses document-preflight0.2.2.
Both have recorder OFF. Neither proves a matched new Debug recorder closure.
J08 must update its own selected package/manifest checks and recorder-on source
pins to the newly qualified package. This is a bounded dependency-identity refresh,
not permission to remove validation or rewrite historical evidence.

There is a second independent guard: an empty `SERENE_CORE_POSE_SOURCE_SELECTION`
chooses historical Stage07 source verification; the nonempty route currently
invokes `verify_i06a_pose_selection.py`, which pins the actual old I06a path,
SHA and stage. Pointing that variable at new JSON is **not sufficient today**.
J08 must add a narrow current source-selection verification route binding its
accepted J06/J07 inputs, or explicitly extend the live dispatch to that reviewed
route while preserving the old stage verifiers and evidence. The old default
recorder-test branch also authors wip.9 fixtures. Keep it OFF until J08 provides
fresh current-schema tests; do not update frozen fixtures merely to pass.

## Outputs-owned future commands

After contract approval and implementation release, use new paths below
`serene-outputs/build/batch-j/J07/`. Check that they are unused before configuring.
The existing `tools/build_pose_recorder.cmd` hardcodes its historical tree; do not
run it unchanged. Load the existing compiler environment and explicitly select MSVC.

```bat
call C:\Users\12650029\serene-academic\serene-math\load_compilers.bat
cmake -S C:/Users/12650029/serene-academic/serene-outputs ^
  -B C:/Users/12650029/serene-academic/serene-outputs/build/batch-j/J07/combined-msvc ^
  -G "Visual Studio 18 2026" -A x64 -T host=x64 ^
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
  -DSERENE_OUTPUTS_BUILD_CODEC=ON -DSERENE_OUTPUTS_BUILD_ARCHIVE=ON ^
  -DSERENE_OUTPUTS_BUILD_POSE_RECORDER=ON ^
  -DSERENE_OUTPUTS_DEPENDENCY_MODE=PINNED ^
  -DSERENE_OUTPUTS_ZFP_ARCHIVE=C:/Users/12650029/serene-academic/serene-outputs/build/dependencies-extensions/zfp-1.0.1.tar.gz ^
  -DPython3_EXECUTABLE=C:/Users/12650029/AppData/Local/anaconda3/envs/eic12/python.exe
```

The archive already exists and matches pinned SHA
`599802776f2427df2d3dbd655f4443e146ebeedcf50d06c6a8ce26784263884b`.
Retain serial64/ROUND_NEVER and the existing disabled OpenMP/CUDA/DAZ/tight-error/
strided-bitstream switches. No dependency upgrade or download is required.

For each configuration Release then Debug, build only the four library targets,
focused recorder test target and whichever small installed scientific regression
target is selected after the code diff. Use at most two parallel jobs. Install both
configurations into the same **new** combined prefix:

```text
cmake --build <combined-msvc> --config <Release-or-Debug> --parallel 2 --target serene_outputs_model serene_outputs_codec serene_outputs_archive serene_outputs_pose_recorder test_pose_recorder
ctest --test-dir <combined-msvc> -C <Release-or-Debug> -R "^serene_outputs_pose_" --output-on-failure
cmake --install <combined-msvc> --config <Release-or-Debug> --prefix <J07>/combined-install
```

New feature checks may extend the focused recorder target only after release.
Use new per-attempt generated outputs; failure fixtures never overwrite prior
qualified files. Preserve `/MD` Release, `/MDd` Debug and existing `_d`/dependency
configuration paths. Test relocated combined consumers requesting archive and
pose_recorder together, plus the existing isolated pose component import. Reuse
old scientific wire fixtures/readers for a small compatibility check, without
rerunning codec exponent sweeps, large-memory measurements or numerical studies.

Publish one new install manifest listing package0.2.4, both configurations and
all components, exact public headers, exports and binary hashes. Record compiler,
runtime/profile facts and source identities. Reuse the existing manifest schema
where possible; do not add a second provenance framework. This manifest and the
selected library outputs are the J08 input, not a relocated old receipt.

## Core-owned J08 dependency change

After J07 qualification/Main acceptance **and the J08-owned guard/dispatch
changes above**, use these package selections:

```text
SereneOutputs_DIR=<J07>/combined-install/lib/cmake/SereneOutputs
SERENE_CORE_OUTPUTS_INSTALL_MANIFEST=<new exact manifest path>
SERENE_CORE_OUTPUTS_INSTALL_MANIFEST_SHA256=<new qualified SHA256>
SERENE_CORE_ENABLE_POSE_RECORDER=ON
SERENE_CORE_POSE_RECORDER_PREFIX=
SERENE_CORE_BUILD_POSE_RECORDER_TESTS=OFF
```

These variable names match the current J06 caches and Core verifier. The new
manifest's path/hash are supplied only after J07 qualification. Select only this include/library
view, and configuration-matched Chrono and CRT. Do not combine a new recorder
header with an old recorder library or include the old Outputs prefix ahead of it.
Core owns the narrow allowed-version, exact-manifest and recorder-source-pin
refresh. Retain hash/size/binary/component checks and historical compatibility
paths; do not weaken the guard to arbitrary0.2.x or arbitrary paths.
The new J08 source-selection route supplies its own explicit accepted selection
path/hash through the reviewed live dispatch. Its exact option/API is a Core
decision, not a currently working command promised here. The options above alone
do not enable a current-schema recorder build. J08 registers and selects its
fresh matched tests separately from the disabled historical recorder-test branch.

No old Core build tree is implicitly cleared or reconfigured by this plan. Main
and Core choose the bounded J08 tree/refresh after preserving their current J06
evidence. Other repositories and the Python reader need no library implementation
change for this separate raw visualization stream.
