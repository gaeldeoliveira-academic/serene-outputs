# Pose recorder implementation handoff

13 November 2026. **recorder-qualified** for the bounded MSVC Release/Debug scope below.
Stage 04 implements an independent headless raw visualization recorder. Core capture,
run activation, ordinary application integration and serene-3d playback remain separate
owner stages; this receipt does not claim a simulation recording or viewer qualification.
No commits, branches or pushes were made. Existing unrelated work was preserved.

## Public contract and package

- Exact bytes: `spec/POSE_SIDECAR.md`, sidecar 1.0, extension `.spose`.
- Public API: `include/serene/outputs/pose_recorder.hpp`.
- Component/target: `pose_recorder` / `serene::outputs_pose_recorder`, package 0.2.3.
- Implementation: `src/pose_recorder.cpp`, private pinned nlohmann JSON only.
- No core, Chrono, Qt, model, codec or archive target dependency. The installed isolated
  component test rejects accidental imports of those scientific targets.
- Existing scientific `.serene` grammar, ArchiveSession, codec and archive sources are
  unchanged. Headless qualification does not requalify unrelated scientific algorithms.

The catalog includes structural native-node N and actual body/anchor port identities,
world conventions, occurrence hierarchy, explicit segments, physical port identities,
scoped aliases and resolved connection kind/scope/endpoint/junction information. Preserve
available component/display names, spanline identities, port material coordinates/sides
and chosen recording settings as documented metadata. No metadata recovers source geometry
or invents mechanical connections. Topology is fixed for a recording.

`Recorder::open(destination, catalog_json, options)` validates/writes the setup catalog
and allocates one fixed byte batch. Catalog is UTF-8 without BOM. `append_frame` validates
and copies the complete borrowed span of `sample_count * 7` doubles. Events are a bitmask;
combined initial/scheduled/target is one frame. Prepared snapshots remain separately
labelled. The recorder assigns consecutive frame ordinals and preserves actual times,
accepted-step/equilibrium IDs and static load factors without interpolation.

An illustrative external callback adapter (the core owner chooses its exact signature):

```cpp
using serene::outputs::Status;
namespace pose = serene::outputs::pose;
Status copy_frame(void* owner, const pose::Stamp& stamp,
                  std::span<const double> xyz_wxyz) noexcept {
    return static_cast<pose::Recorder*>(owner)->append_frame(stamp, xyz_wxyz);
}
```

The recorder and any adapter flattening buffer outlive sink registration/run. No callback
pointer is retained by the recorder. After the run, translate the independent run/capture
outcomes to `Completion`, call `finalize`, and check its result and `state()`. A failed
optional sink does not redefine the numerical outcome. Application path-alias checks
against inputs/scientific archives/logs and authored schedule validation belong to the
application owner; the recorder never overwrites its destination or `.partial` path.

## Bounded storage and publication

Raw little-endian f64 is the only implemented encoding. Header/payload CRC-32 detects
accidental corruption, not malicious modification. A future codec is independently framed;
no lossy precision, background queue, renderer or frame-history store is introduced.

For P samples one frame is `64 + 56*P` bytes. Actual batch capacity is
`min(max_batch_frames, floor(max_batch_bytes/frame_bytes))*frame_bytes`; one frame must fit.
Defaults are 32 frames and 4 MiB, with a 4 MiB default catalog cap. Hard wire caps are 16 MiB
catalog and 64 MiB chunk. Setup JSON/catalog validation storage is bounded by the catalog;
that parse tree is discarded after open. Runtime owns the fixed batch plus constant
state. Its backward-linked batch seek index has O(1) writer memory; seeking a frame is
O(number of batches), not logarithmic. `index_workspace_bytes=256` is a conservative
fixed upper bound for linked-index metadata/state scratch, excluding general statistics.
No vector grows with recording duration.

`accepted_frames` means validated/copied, potentially RAM-only, including a frame whose
ensuing flush fails. `persisted_frames` counts complete chunks after checked stdio flush;
it is not fsync/power-loss durability. Core delivery acknowledgement remains separate:
a callback returning failure is not acknowledged even if that attempted frame was copied.
A 1001-frame/501-batch test retains the same 576-byte batch and 256-byte index bound as the
small four-sample test. This is focused boundedness evidence, not a universal performance
or OS-memory benchmark.

The exclusively created destination `.partial` remains on failure/destruction. Finalize
flushes the last partial batch, index, completion and footer, checks flush/close, then
publishes by a same-directory no-clobber hard link. Filesystems lacking hard-link support
fail publication and preserve the partial file; there is no overwrite fallback. Removing
its own partial alias after successful publication is best effort. Failed numerical runs
can have complete recordings with explicit failed/stopped outcomes. Conversely, a
wire-complete `.partial` after close/publication failure does not prove successful recorder
publication. Readers report wire completeness separately from external publication status.

## Independent byte evidence and tests

`tools/pose_sidecar/check_sidecar.py` is a stdlib checker independently authored from the
C++ serializer. It validates CRC, header bounds, stamps, pose validity, topology references,
linked index, completion/footer and outcomes. Default memory is one bounded catalog/chunk
plus counters; optional fixture dumps are explicitly capped. Unknown optional streams are
skipped only after length/integrity checks; unknown required semantics and corrupt optional
chunks fail. `--allow-incomplete` exposes only a validated complete-chunk truncation prefix.

`fixtures_pose_sidecar/` contains literal independent raw fixtures and `expected.json`:
asymmetric structural positions, nonidentity orientations, actual offset body and anchor
ports, a deliberate quaternion sign reversal, nonzero start 1.25 s, steps 0/4/7 at
1.25/1.29/1.32s, an off-stride target, batches 2+1, static and prepared snapshots,
valid incomplete prefix, optional stream, failed-run completion and corrupt/unsupported
cases. `author_fixtures.py` regenerates those test bytes; it is not a production writer.

Actual qualification:

- MSVC 19.50.35725.0, Visual Studio 18 generator, x64, `/MD` Release and `/MDd` Debug.
- Release focused CTest 2/2 passed, 0.82 s; Debug 2/2 passed, 0.68 s. Each includes 23 independent
  checker checks and the C++ recorder cases with independent readback of emitted files.
- C++ cases cover byte+frame limits, final partial batch, no frames/zero samples, repeated
  identities/clock errors, invalid pose/quaternion/catalog, cyclic graph, no-clobber races,
  abandoned recording, short write, write/flush/close/publication faults and bounded long
  recording. The actual transient output exactly matches independent expected values.
- Isolated relocated installed Release and Debug public consumers compiled, executed and
  independently reopened: each2 frames Ã—4 samples, clocks 1.25/1.26 s. Their CMake test ensures
  requesting only pose_recorder imports no model/codec/archive/ZFP target.
- Earlier local negative-test failure exposed numeric catalog-version admission (`1.0`
  versus integer1), corrected before final qualification. A fixture comparison initially
  used different third-frame literals; the test was aligned to the independent expected
  pose/sign values. Final review also closed a BOM parser mismatch. Earlier files remain
  under the fresh test run directories; none is claimed as final acceptance.

Commands (from serene-outputs):

```text
tools\build_pose_recorder.cmd Release
tools\build_pose_recorder.cmd Debug
ctest --test-dir build/pose-recorder-msvc -C Release -R serene_outputs_pose_ --output-on-failure
ctest --test-dir build/pose-recorder-msvc -C Debug -R serene_outputs_pose_ --output-on-failure
python tools/pose_sidecar/check_sidecar.py fixtures_pose_sidecar/transient.spose --dump-frames
python tools/pose_sidecar/check_sidecar.py fixtures_pose_sidecar/incomplete.spose.partial --allow-incomplete
```

Use the configured bundled Python executable or a compatible Python3 interpreter. All C++
builds used CMake and at most 2 jobs. Compiler environment was loaded using
`serene-math/load_compilers.bat`; no missing GCC/MSYS DLL path was used.

Final source/artifact identities and logs: `build/pose-recorder-qualification/final-v1/receipt.json`.
The source snapshot is `final-v1/source`; build/test executables are below
`build/pose-recorder-msvc`. Fresh install and relocated install are below `final-v1`.
These installs intentionally have codec/archive OFF. Preserve the existing scientific
prefix. Ordinary core application integration requiring both archive and recorder should
build a fresh combined package with the existing pinned ZFP/archive settings; it must not
silently replace its scientific dependency with this headless-only prefix.

Linux/GCC/Clang execution, filesystems without hard links, power-loss recovery, crash
atomicity, async/IPC/compression, adaptive topology and live viewer playback are unqualified
or deferred. No heavy solver/convergence study was performed. Stage 07 must supply an actual
ordinary-run recording and its separate run/capture/recorder/readback outcomes.
