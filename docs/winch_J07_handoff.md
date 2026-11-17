# J07 optional winch recording handoff

17 November 2026. Owner: serene-outputs. This handoff concerns Batch J07,
released by Serene Main after joint Core/viewer review and decision J-D11.
It does not restart the historical output-selection/native-format program.
J08 Core integration and J09 rendering remain separate owner stages, released
by Main after acceptance of this package. No commit, push or branch switch.

## Delivered contract and interface

The normative contract is [POSE_WINCH_GEOMETRY.md](../spec/POSE_WINCH_GEOMETRY.md),
supplementing [POSE_SIDECAR.md](../spec/POSE_SIDECAR.md). Package version is 0.2.4;
bootstrap remains 1.0, catalog version 1 and required-feature mask zero.
The optional feature family is kind4/stream2/layout1/raw encoding0/flags0.
It immediately precedes its matching required pose chunk. The structural sample
catalog, linked seek index and completion counts retain their prior meanings.
Aware recovery exposes only whole validated pairs. Known kind4 OR stream2 is
validated before generic optional skipping, including malformed combinations.

Each admitted selected prepared winch has an exact owning assembly occurrence,
force ID and UTF-8 byte-length-prefixed key; repeated local force IDs in distinct
occurrences are supported. Population includes effective-zero arrangements and
is independent of scientific channel selection. It does not include unselected
authored forces. A/B are distinct base `body_port` samples of one owning
occurrence, D is an `anchor_port`, and C is algebraic `pulley`, with exactly
role/kind/feature metadata and no structural identity or orientation. Edges are
A-C, C-B and C-D. Core remains responsible for native physical admission.

The public declarations are in
[pose_recorder.hpp](../include/serene/outputs/pose_recorder.hpp). In addition to
the unchanged feature-free overload, the recorder provides:

```cpp
Status append_frame(const Stamp& structural_stamp,
                    std::span<const double> physical_poses,
                    const WinchFrameView& winches) noexcept;
```

`WinchFrameView` borrows one exact translated source stamp, the owning Core
preparation generation, `WinchSourcePhase`, and all catalog-ordered
`WinchGeometry` rows. Each row carries twelve unchanged binary64 world-metre
coordinates Axyz/Bxyz/Cxyz/Dxyz and positive-tension or zero-tension-limit status.
Borrowed storage is consumed synchronously. Core checks row ownership/binding
and common accepted-packet identity before constructing the view; this API has
no Core provider pointers and does not solve geometry. Capture-plan identity
and Core generation are separate. Feature-enabled catalogs require this overload;
supplying or omitting a feature view against the wrong catalog fails explicitly.

All structural and feature values, shapes, source stamps, phases, generation
and endpoint checks pass before either population is copied or accepted counts
advance. Structural physical p and accepted feature f remain unchanged. For each
A/B/D, S=max(1 metre, norm_inf(f), norm_inf(p)), and each component must satisfy
abs(f_i-p_i)<=64*2^-52*S. Signed zeros compare equal but retain their original
stored signs. Shared references each compare to the same canonical physical
sample. This fixed representation-consistency condition is not a solver
tolerance or an accuracy guarantee; nothing is averaged or overwritten.

Initialized geometry translates to prepared/id0/event1/factor0. Certified static
uses the exact certified ID/time/factor and event8. Transient initial uses accepted
step0 and its initial event; transient accepted uses positive accepted IDs and
may be the first captured phase when initial emission is disabled. Transient
wire factor is zero even though Core's WinchReport factor is one. Actual time
is preserved, including nonzero starts and off-stride target captures. Scientific
C and feature C can be compared exactly when they share lossless source states.

## Resource and failure behavior

For P physical samples and F arrangements, pose frame S=64+56P bytes and feature
frame W=80+104F bytes. `open` checks count/product/sum/host bounds and admits
min(max_batch_frames, floor(max_batch_bytes/(S+W))) complete paired frames.
Zero capacity fails before creating a recording. The two raw buffers share that
byte budget. The setup-derived reference table requests 24F additional bytes;
catalog parse/setup storage, vector/allocator overhead and general state are
outside the raw-batch counter. They remain bounded by admitted catalog/population
limits, not by duration. Feature-free catalog behavior is retained.

`append_frame` retains no caller pointer. A batch writes feature then pose, each
with CRC and checked stdio flush. Persisted frame/pose-chunk counts advance only
after the complete pair succeeds. Accepted counts can include a copied batch
whose later I/O fails. Validation and I/O failures latch; no later append or
finalize upgrades them. Existing no-clobber publication, checked close, independent
numerical/capture outcomes and retained `.partial` evidence remain in place.
An orphan valid feature batch or truncated next pose contributes no paired
frames to aware recovery. Corrupt complete feature semantics are errors.
Flush/close and rename/link publication do not promise power-loss durability.

The independent checker keeps at most one pending raw feature chunk and one
current pose chunk (at most twice the declared chunk limit), catalog/reference
maps and constant-size counters. It retains no default decoded history.
Explicit frame dumps have a checked frame cap and cost proportional to admitted
P/F and that cap. Reader tests compare short/long scans with the same chunk size;
recorder tests check stable requested-workspace counters over 1001 frames. These
are scoped bounds/evidence, not total-process-memory or zero-allocation claims.
The output allocation relaxation does not waive numerical-Core restrictions.

## Independent corpus and verification

[fixtures_pose_winch/expected.json](../fixtures_pose_winch/expected.json) pins
40 independently authored byte cases and literal asymmetric expected coordinates,
including P8/F2, shared local force IDs, rotated structural orientation, zero
tension, prepared/static/transient states, actual times1.25/1.29/1.32 and IDs0/4/7.
[author_winch_fixtures.py](../tools/pose_sidecar/author_winch_fixtures.py) authors
these fixtures independently of the production writer. Test execution never
rewrites the corpus. The normative sources, API, fixture manifest, all corpus
files and build-relevant sources are pinned by the fresh J07 source manifest.

Bounded Release and Debug CTest selection passes **5/5 each**:

- Existing raw pose producer/readback and 23 baseline checker tests.
- Feature producer with 1120 assertions (including loop assertions), followed
  by independent exact-value/stamp readback of actual C++ recordings.
- 12 new checker tests covering all40 corpus outcomes, exact roles/references,
  Unicode byte lengths, source identity, malformed known-family headers,
  reserved fields, nonfinite values/JSON overflow, whole-pair commits, truncation,
  inclusive/scaled/over-limit consistency, signed zeros, shared samples, dump
  limits and short8/long1024 same-chunk memory behavior.
- Existing small archive failure/retry/no-clobber test.

The producer checks borrowed-buffer copying, complete pre-copy rejection,
configured byte capacity, initialized/static/transient phase mapping, initial
capture omission, short feature/pose writes, failure between paired chunks,
write/flush/close/publication faults, empty failed/stopped recording and
publication collision. The long recording is1001 frames/501 paired chunks with
constant requested raw-batch/index workspace. No solver or convergence campaign.

Independent review corrected two checker discrepancies before final qualification:
C now rejects every extra metadata key, and JSON numeric overflow rejects even
inside descriptive metadata. Existing LF checker source endings were restored;
that line-ending-only correction followed Release checks and preceded Debug
checks. Executable Python logic was unchanged by that correction.

The preserved old Python checker passed all20 explicitly classified legacy
cases. The unchanged viewer reader executable passed15 independent fixture
cases and its five focused checks. These establish structural compatibility,
not feature validation or rendering. The special legacy harness `transient.spose`
is deliberately feature-free for its historical hardcoded mutations; positive
feature-stream compatibility uses `paired_transient.spose`. Reader identities
and exact outcomes are in `build/batch-j/J07/legacy-compatibility.json`.

Both installed consumer configurations use the relocated combined package.
The new mixed consumer requests archive+pose_recorder0.2.4 EXACT, produces
`installed.spose` and `installed.serene`, and is checked independently. The
unchanged isolated consumer imports pose_recorder without model/codec/archive/ZFP
targets. The unchanged Python0.2.0 companion opens both scientific files,
verifies every section and reads exact Cx[-0.25,-0.125,0], time[1.25,1.29,1.32]
and accepted IDs[0,4,7]. This is synthetic installed interoperability evidence,
not actual J08 Core capture or J09 rendering. The first Release mixed-consumer
attempt returned12 after publishing its sidecar: the test marked elapsed time
absent but retained the default nonempty measurement scope. Clearing that absent
scope fixed the consumer input. Its original log/output remains preserved and
the retry uses a fresh directory. No production library change was needed.

## Usable package and exact evidence

Repository root: `C:/Users/12650029/serene-academic/serene-outputs`.
All paths below are relative to it. Fresh combined installation:
`build/batch-j/J07/combined-install`; relocated test copy:
`build/batch-j/J07/relocated-install`. Both contain the same44 files and both
Release `/MD` and Debug `/MDd` runtime families. Targets are
`serene::outputs_model`, `outputs_codec`, `outputs_archive` and
`outputs_pose_recorder`. Scientific archive semantics and codec sources remain
unchanged. The sidecar continues to use raw bytes without ZFP.

Install manifest: `build/batch-j/J07/install-manifest.json`.
SHA-256: `493bfc5fee1e6b04679103eab787f2270c807d254a5cac01171b1fbd20c7a55b`.
It names every installed header, configuration/export, library, license and spec.
`source-manifest.json` and `qualification.json` in the same directory record
exact maintained source/fixture/helper identities, preserved-baseline checks,
test logs, produced-file readback receipts and the dirty source base.

Commands actually used, from the repository root:

```text
tools\build_winch_recorder.cmd configure
tools\build_winch_recorder.cmd build Release
tools\build_winch_recorder.cmd test Release
tools\build_winch_recorder.cmd build Debug
tools\build_winch_recorder.cmd test Debug
tools\build_winch_recorder.cmd install Release
tools\build_winch_recorder.cmd install Debug
```

The driver loads the existing serene-math compiler loader, selects Visual
Studio18 2026 x64/MSVC19.50.35725.0, C++23, at most two build jobs, eic12 Python,
and the existing locally pinned ZFP1.0.1 archive. No download or dependency
installation occurred. Configure emitted only the upstream old-minimum-CMake
deprecation warning. All attempt logs are retained under `build/batch-j/J07/logs`.
Installed-consumer orchestration and the preserved-reader probe are recorded
there with exact argv; their scratch helper sources are pinned by qualification.
Only Windows x64 and the tested toolchain are qualified here.

## J08 consumer requirements

Core must consume one physical include/library view from this combined prefix,
with the new manifest and exact hash; do not combine archive headers from an
old prefix with a new recorder-only package. Its current package guard accepts
0.2.0-0.2.3 and pins the old combined0.2.3 manifest. J08 needs a narrow explicit
0.2.4 route and matched header/library identities for both configurations.
Preserve all old prefixes, receipts and old-version routes as historical evidence.

Suggested J08 configuration after its own reviewed guard change:

```text
SereneOutputs_DIR=<combined-install>/lib/cmake/SereneOutputs
SERENE_CORE_OUTPUTS_INSTALL_MANIFEST=<J07>/install-manifest.json
SERENE_CORE_OUTPUTS_INSTALL_MANIFEST_SHA256=493bfc5fee1e6b04679103eab787f2270c807d254a5cac01171b1fbd20c7a55b
SERENE_CORE_ENABLE_POSE_RECORDER=ON
SERENE_CORE_POSE_RECORDER_PREFIX=
SERENE_CORE_BUILD_POSE_RECORDER_TESTS=OFF
```

The package route is separate from Core's pose source-selection gate. Empty
`SERENE_CORE_POSE_SOURCE_SELECTION` invokes the historical Stage07 verifier;
nonempty selection invokes the I06a verifier with hardcoded old path/hash/stage.
A new JSON file alone cannot satisfy either route. J08 must add a narrowly
reviewed current-source gate or dispatch extension and publish its exact source
receipt. Keep old default pose-recorder tests off until their wip9 authoring
assumptions are replaced with J08's matched current-input tests. Current J06
Release and Debug recorder flags were both off; J07 does not change them.

At initialization, Core should reserve complete arrangement rows and physical
references for the admitted selected population. At each selected accepted
capture, validate exact per-row owner/force/binding/generation/phase/state/time
before constructing the common borrowed packet; retain the fixed endpoint
comparison without modifying either population. Use accepted geometry including
zero-tension limits, never recompute C from solver trial state. Preserve scientific
and capture cadence independently, including nonzero starts/off-stride targets.
Actual Core-produced files and common-state scientific-C comparisons belong to
J08. The aware viewer's decoding/rendering and feature toggle belong to J09.

## Preserved scope

The starting Outputs branch was main at
`2efc8cf9f541dea2a6cb10686e4545aff3b0a056`, with clean tracked files and unrelated
untracked history directories. J07 leaves tracked/untracked changes uncommitted.
Candidate-v1 remains unchanged at
`spec/drafts/winch_J07/candidate-v1/manifest.json`, SHA-256
`ee90cb0e0cd44d8d5fbbc6d912f1fa5eb198f7f86b44fcf455705e1d08b8fcd5`.
Its proposed bitwise coordinate rewrite was superseded by J-D11; preserve it as
review history rather than editing it into apparent final approval.

No Core, viewer, Python companion, schema, scientific-container or solver source
was changed by J07. Existing history/frozen corpora and installs were preserved.
No LyX reports were edited by this owner; Main coordinates their next milestone
alignment with the report owner. This receipt qualifies recording and installed
interoperability only, not numerical accuracy, active-Core allocation behavior,
rendering, additional platforms or power-loss durability.
