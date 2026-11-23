# M1.04 modal archive and reader — working handoff

Status: package implementation and manufactured interoperability qualified; actual Core archive integration pending. No commits or pushes.
Main collects this handoff; this chat will not send further coordination messages to Main.

Task work and evidence directory: `C:/Users/12650029/serene-academic/serene-theory/tmp/modal_m1_20261122`. Relative operational paths below refer to that directory.

Main confirmed the final dense-count contract on 22 November 2026. The final source, SDK and wheel are frozen on that contract.

## Ownership and current contract

This chat owns both `serene-outputs` and `serene-outputs-python` through scoped
workers. No separate owner chat or duplicate writer is needed. Core, inputs and
schema changes remain with Main and their owners.

- Native selector stays `native_archive`; disabled storage remains an application decision.
- Modal wire version **1.2**, required feature **modal-results-1**, run-kind **2**.
- C++ package and independent Python package **0.3.0**. Existing scalar writer
  stays on 1.0/1.1; its source and the shared codec remain unchanged.
- Canonical C++ interface:
  `C:/Users/12650029/serene-academic/serene-outputs/include/serene/outputs/modal.hpp`.
- Canonical wire specification:
  `C:/Users/12650029/serene-academic/serene-outputs/spec/FORMAT-1.2.md`.
- Borrowed `ModalRunResultView` is held by Core's inactive immutable lease through
  append/finalize/abandonment. `PreparedModalExport` owns copied physical metadata
  and bounded scratch. `ModalArchiveSession` uses `serene::outputs_archive`.
- Shapes are mode × observation × six world components, reversible and unscaled.
  Reference quaternion is wxyz local-to-world. Main chose `blade_chord` at the
  structural spanline origin for blade stations; actual ports use `port_local`.
  Material-backed named ports also retain optional material coordinate and side.
- Signed eigenvalue/class remain intrinsic; requested quantities remain separate.
  Four initial classes explicitly do not collapse proved rigid/mechanism classes.
- **Core alignment correction:** grouping uses source-reported membership on the
  same-run dense count, with absolute + relative*max magnitude + twice count
  uncertainty. The archive records this rule and basis. Returned Ritz estimates
  are preserved separately; their last value need not equal dense certified_upper.
  The archive verifies partitions/counts/declared outcomes, not the eigensolver.
- Failure evidence has zero published modes and failed/unavailable certification.
  Search completion is independent: counting may complete before certification
  fails. Failure evidence must use a separate application destination from the
  ordinary certified scientific file. Requested quota zero is retained on failure.

## Qualified package handback — 22 November 2026

**The modal 0.3.0 SDK is sealed and available for Core integration.**

```text
SereneOutputs_DIR=C:/Users/12650029/serene-academic/serene-outputs/build/modal_m1_20261122/sdk-install/lib/cmake/SereneOutputs
SERENE_CORE_OUTPUTS_INSTALL_MANIFEST=C:/Users/12650029/serene-academic/serene-outputs/build/modal_m1_20261122/qualification-v2/manifest.json
SERENE_CORE_OUTPUTS_INSTALL_MANIFEST_SHA256=5840da521674e79d25579a5c4f45e29f9f7119442e00f579b28d2d79e1a4c120
```

The manifest exhaustively identifies all **51 installed files**,
including Release `/MD` and Debug `/MDd` model/codec/archive/ZFP libraries,
modal.hpp and the format specification. Link `serene::outputs_archive`.
Its sibling source, dependency, corpus and execution-evidence snapshots describe
the sealed bytes. Do not rebuild into or reinstall over this prefix. Core intake
and actual Core readback are subsequent receipts referencing this immutable pin.

The independent CPython 3.12 Windows x64 wheel is:

`C:/Users/12650029/serene-academic/serene-outputs-python/build/modal-m1-20261122-v1/dist/serene_outputs-0.3.0-cp312-cp312-win_amd64.whl`

SHA256 `adda76abf657f699239096503c0f5193d5d9585032c4eaabbf2dd26d6beeb383`.
The adjacent sdist is `serene_outputs-0.3.0.tar.gz`, SHA256
`b75edffcaf6f441fd18a9292e3435f05f77065b1ab1dfe7d87452cb88b0e5c3e`.
Exact copies are also under the seal's `python/` directory. The wheel compiles
its own native bridge and pinned ZFP; no Core or C++ producer package import is
needed. Existing shared native codec/vendor source remains unchanged.

## Executed verification

- Final focused C++ tests: **9/9 Release and 9/9 Debug**. The unchanged pose
  component was built and installed; its test suite is outside this modal gate.
- Relocated SDK public consumer: both configurations configured, built and ran.
  CMake File API confirms only copied main.cpp compiles, with only relocated SDK
  includes and four SDK libraries, exact package location and matching CRT.
- Isolated installed wheel: **40 tests passed**, including existing 1.0/1.1
  archives and final modal positive/malformed cases. Runtime used NumPy 2.2.6,
  isolated site-packages, and no Core/producer/h5py/zfpy/pychrono import.
- Final source writer through installed reader: **12/12 archives passed**.
  Both installed consumer archives also passed (**2/2**), with exact signed-zero
  `[1,-0,0,0,0,0]` shape bits. Independent physical M/K/shape oracles cover the
  manufactured case, offset port, sided stations and reference triad.
- Independent final corpus: **12 positive and 25 negative cases**, 79 artifacts.
  Revision 4 manifest SHA256
  `ef0f16e69583bba5fd9216c5650a998ce01cc6df82f83337d8c577958a76d12e`.
- Old installed Python 0.2.0 rejects **12/12** valid 1.2 archives at unsupported
  version admission. Old scalar writer, numeric reader/codec/vendor paths and
  historical qualification records remain preserved, as audited separately.
- Final specification SHA256
  `f99df33d74251509c58ca9fb66e3b5ad98692a782b2e4471fc186764a912ddf2`.

Execution receipts under the task directory: `sdk-attempts/006-test-Debug.json`,
`sdk-attempts/008-test-Release.json`, `sdk-attempts/009-install-Release.json`,
`sdk-attempts/010-install-Debug.json`,
`external_consumer/run-v2/summary.json`, `new-wheel-qualification.json`,
`readback/final-source-v1.json`, `readback/installed-consumer-v1.json`,
`old-reader-final-rejection.json`, `final_readonly_compatibility_audit.json`.
The seal copies these records and associated logs without rewriting history.

The first external-consumer harness rejected Visual Studio utility targets; its
failed attempt and original runner are preserved. A corrected harness admitted
only utility targets without compile groups and retained the strict single
compiled-target check. Python's initial source extension test encountered a
transient Windows DLL import error; an unchanged narrow retry passed. Both
original and retry logs remain separate. The isolated wheel passed all 40 tests.

## Reader and reproduction commands

The installed readback executable is
`C:/Users/12650029/serene-academic/serene-theory/tmp/modal_m1_20261122/new-reader-v1/Scripts/python.exe`.
Use `-I -B`, clear PYTHONPATH/PYTHONHOME and build/library/plugin search paths,
and keep PATH to the system runtime. The existing Conda-based Python runtime
needed its own ffi.dll in Scripts; this runtime prerequisite and hash are recorded
in `new-reader-runtime.json`, separate from wheel dependencies.

```text
<isolated-python> -I -B <task>/readback/read_modal_archives.py --source-dir <six-archive-directory> --oracle <outputs>/fixtures_modal_m1/revision_4/manufactured_packet.json --receipt <new-receipt.json>
<isolated-python> -I -B <task>/readback/read_modal_archives.py --installed-consumer <consumer.serene> --receipt <new-receipt.json>
<isolated-python> -I -B <task>/readback/read_modal_archives.py --actual-core <core.serene> --receipt <new-receipt.json>
<isolated-python> -I -m serene_outputs.cli <archive.serene>
```

The generic actual-Core check fully verifies archive integrity and typed API
states and records summaries; independent numerical/physical comparison still
uses Core's known operator/reference evidence. Repeated `--source-dir`,
`--installed-consumer` and `--actual-core` arguments are supported. Receipts use
fresh paths and never overwrite earlier outcomes. Time/load accessors reject
modal runs. Shape reads preserve mode × observation × component axes and exact
unscaled bits; caller-owned arrays survive archive closure.

`sdk-attempts/001-configure-Release.json` and subsequent attempt records contain
exact CMake arguments and the maintained compiler-loader command. Reproduction
must select new build/install directories: configure pinned ZFP, build the listed
targets, `ctest -C Release -E pose --output-on-failure` and Debug analog, then
install both configurations. `external_consumer/run_external.py --sdk-prefix
<prefix> --output <fresh-child>` copies an installed SDK and consumer outside
the checkout and executes both configurations. Historical attempts and the
current seal are never reused as mutable build destinations.

## Remaining integration boundary

Actual Core blade native archive readback and native/disabled in-memory
equivalence remain pending Core's matched build. The available preflight JSON
signals are explicitly synthetic and do not count as actual archive readback.
Core owns the numerical certification, blade comparison and equivalence claim.
This package's manufactured evidence alone does not qualify a blade solution.

No Linux/other Python ABI qualification is claimed. No commits or pushes.

Core's actual intake guard passed all six controls: matching Release and Debug were accepted; crossed physical prefixes for both configurations, wrong manifest pin and wrong CRT were rejected for the intended reasons. Receipt: `intake_review/run-v1/summary.json`, SHA256 `380b1cd2b819368efe0e876c94cf3c5fa4acebef686649f4b879c05dc1a0af8f`. This configures the real guard without building or modifying Core; actual Core archive delivery remains the boundary above.

Independent post-seal artifact audit: `final_seal_audit.json`, SHA256 `40f5fcb933a18e447b39cf9f73d720835cb6e2ec53c47ffeb24f312a3f946197`. This post-seal record is separate from the immutable package manifest.

The initial audit verified all installed and sealed files but failed to read the two original Python distribution files under restricted access. Its static all-originals summary sentence was overbroad; the failed receipt remains unchanged. A scoped read-only retry verified both original files against the seal without changing ACLs or artifacts: `final_seal_original_artifacts_retry.json`, SHA256 `e9ab879ba8a67bfa8d49cb75df0379bf0e9d423f68f0938e85a9911504b647fa`. Combined evidence matches 51 installed files, 200 source files, 79 corpus files, 75 evidence files, both Python distributions and the dependency (358 sealed files including the manifest). No blocking artifact finding remains. The incomplete qualification-v1 snapshot remains preserved without a manifest.
