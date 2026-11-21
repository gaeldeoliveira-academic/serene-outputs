# Stage00 F-pose recorder handoff

20 November 2026. Package **0.2.5** qualifies the first optional visualization
slice: independent fluid F poses, grouped with unchanged structural poses and
optional J07 winch geometry. This is a recorder/wire/API qualification, not an
implementation or numerical qualification of Core capture or viewer rendering.
Applied loads, elastic moments and inflow planes have reserved identities but
are not implemented by this package. Their review candidates remain separate.

## Package and contract identities

The starting Outputs worktree was clean on `main`, HEAD
`fec96a4b01802e6ed78808bdf4cd39fe25bc6160`, package0.2.4. This work stays on that
branch and is uncommitted; no publication commit or push is implied.

* [Common groups](../spec/POSE_OPTIONAL_OVERLAYS.md), SHA-256
  `689ca870aa260cc13972eac2e981e24ae166e61e0eb4c3421eedc65182bdf12f`.
* [F catalog and record](../spec/POSE_FLUID_GEOMETRY.md), SHA-256
  `e516e87ac56aea6ad779682c39034f993f50d6caf456addc5bce89513a4a51f9`.
* [Review/freeze record](../spec/drafts/visualization_S00/candidate-v1/F_FREEZE.json).
  Core and viewer reviewed the corrected contract; Serene Main approved it.

Bootstrap remains1.0, catalog1, required feature mask0. F is optional
kind5/stream3/layout1/raw encoding0. The existing winch tuple remains4/2/1.
F precedes the adjacent winch/pose pair, or pose alone. Structural previous
links, index and completion retain their original meanings. A new-aware reader
commits only a complete understood group; an old reader cannot validate opaque
F semantics. The generic count ceiling is one million, including a line's
native-element count. Numeric identities otherwise retain exact integer bounds.

Fresh matching Release and Debug installation:

`C:/Users/12650029/serene-academic/serene-outputs/build/visualization_S00/F/combined-install`

The immutable local qualification manifest and source snapshot are under
`build/visualization_S00/F/qualification-v1/`. The manifest records exact source,
installed header/library, fixture, command and readback identities. Later slices
must use a new prefix and preserve this one and all historical J07 prefixes.

## Public API and Core boundary

Use the single matching header/library view and explicitly request the package:

```cmake
find_package(SereneOutputs 0.2.5 EXACT CONFIG REQUIRED COMPONENTS pose_recorder)
target_link_libraries(my_capture PRIVATE serene::outputs_pose_recorder)
```

Add `archive` to COMPONENTS when both are needed. Pose-only import does not load
model, codec, archive or ZFP targets. The recorder itself has no Core, Chrono, Qt,
HDF5 or scientific-archive dependency. Recompile consumers against the matching
public structs and libraries; this is not a binary ABI compatibility promise.
Use `/MD` Release and `/MDd` Debug.

The added types are `SourcePhase` (alias of the existing lifecycle enum),
`FluidPose`, `FluidFrameView`, `FrameGroupView`, and the overload
`Recorder::append_frame(const FrameGroupView&)`. Spans and optional pointers are
borrowed only during the synchronous call. Missing or extra enabled populations,
wrong identities and malformed values latch failure before copying any part of
the group. Legacy overloads explicitly reject a catalog requiring F.

Core supplies the exact preparation generation, common source phase and source
stamp. It must validate owner/binding/effective-field identities before creating
the borrowed view. The capture-plan ID is not the Core generation. Preserve the
actual authored/defaulted spanline ID, separate source/effective fingerprints,
resolved left/right native addresses and explicit continuity segments. Preserve
the admitted `preserve_native_boundaries` policy; an occurrence kind alone does
not prove blade/backend capability. F sampling is independent of active aero.

The first recommended sampling is right-start/left-end for each effective
interval. A genuine fluid cut inside one native element remains a sided break.
Distinct left/right samples at the same coordinate retain separate orientations.
Prepared, certified-static, transient-initial, scheduled and terminal captures
use their actual source facts; a first positive-step capture is valid when
initial capture is omitted. Do not weaken the existing public running-context
guard: Core's accepted-state adapter owns its separate prepared internal route.

Valid F rows retain finite world xyz metres and Hamilton local-F-to-world wxyz
bits, without normalization or sign adjustment. Invalid rows require exactly
`(+0,+0,+0,1,+0,+0,+0)` and a clear validity bit. Valid zero positions remain
distinct. Unexpected provider/binding failure fails the group; masks are not an
error-hiding mechanism. The first Core route may require all admitted rows valid.

## Memory and publication

The default raw batch budget remains4MiB, hard64MiB, shared by every enabled
population. `Options::max_workspace_bytes` defaults to and is capped at128MiB.
Admission subtracts actual retained winch-reference capacity and2048 bytes of
encoding/index scratch before choosing the whole-group frame count. At least
one group must fit before file creation. F topology is setup-only storage.

`Statistics` retains raw `batch_capacity_bytes`, effective frames and the
existing256-byte `index_workspace_bytes` subset. New counters report
`physical_reference_bytes`, `encoding_workspace_bytes`,
`workspace_capacity_bytes` and `effective_raw_budget_bytes`. Setup JSON/maps,
caller-owned arrays, general object/path storage and allocator/I/O overhead are
outside this payload accounting. It is neither total RSS nor a waiver of
numerical-Core allocation restrictions.

The checker pre-admits the known aggregate raw group under64MiB before reading
its first payload. One skipped raw chunk may additionally occupy up to64MiB;
it is released before another read. Base/J07-only legacy admission is retained.
Default checking keeps no decoded history. Explicit dumps are capped at256MiB
logical payload and100000 frames (default CLI frame cap1000), before allocation.
The conservative F charge is `80+57*K`, plus pose/winch wire extents; Python
objects and GPU storage are separate. Viewer owners must budget retained CPU
and GPU data independently.

Accepted frames advance after complete validation/copy. Persisted counters and
structural links advance only after all group chunks and checked flushes succeed.
A failed final pose flush can leave a complete recoverable wire group while
persisted counters still describe the preceding group. Truncated groups expose
only earlier complete groups. Complete independently detectable defects remain
errors even if a later partner is torn. A missing pose alone cannot prove a
cross-stream mismatch. Close/publication failure can leave a wire-complete
`.partial`; no-clobber publication and numerical outcomes remain separate.

## Qualification performed

Windows x64/MSVC, existing compiler loader, CMake and eic12 Python were used.
The offline ZFP1.0.1 archive remains pinned to SHA-256
`599802776f2427df2d3dbd655f4443e146ebeedcf50d06c6a8ce26784263884b`.
No dependency was installed or upgraded.

| Check | Observed result |
| --- | --- |
| Fresh combined Release and Debug builds | Passed both configurations |
| Selected CTest entries per configuration | 7/7 passed: base recorder/checker, winch recorder/checker, F recorder/checker and small archive check |
| C++ F producer | 1435 assertions per configuration, followed by independent byte/readback inspection |
| Final independent Python suites | 45 tests passed after aligning the native count boundary; all71 F fixtures checked against literal oracles |
| Pinned structural and winch-aware Qt generic readers | Each passed71 per-case subset checks; F semantics remain opaque to them |
| Original pinned reader corpora | Structural8 cases plus5 driver regressions for each generic executable; original winch40 cases passed |
| Relocated installed consumers | Combined archive+pose and pose-only, Release and Debug, all built and ran |
| Installed scientific archive readback | Existing unchanged Python companion read exact values[-2.5,-2.375,-2.25], times[1.25,1.29,1.32], uint64 accepted IDs[0,4,7], and verified both archives |
| Native-count inclusive/exclusive boundary | Installed public API and checker agree at1000000 and reject1000001 before creating a recorder file |

The producer tests include29 isolated packet negatives,28 setup/catalog
negatives, prepared/static/transient states, omitted initial capture, F-only and
mixed groups, borrowed-buffer independence, signed-zero fidelity, masks for
K7/K8/K9, exact workspace boundaries and1001 frames with bounded batch storage.
They inject11 outright writes,11 short writes, six flush boundaries, close,
publication and destination-collision failures. Failure evidence distinguishes
accepted, persisted, recoverable, wire-complete and published states.

The Python count correction did not change the C++ library or installed package.
Final45-test validation follows that correction; the earlier CTest checker
invocations had44 tests. The subsequent installed boundary check exercises the
same unchanged Release/Debug public libraries and corrected checker.

## Independent artifacts and compatibility limits

[fixtures_pose_fluid](../fixtures_pose_fluid/README.md) contains71 independently
authored literal cases. Main expected manifest SHA-256:
`9171b49041e960ed6639f7b955450f358208ae4e177e5c3ed4b3c4b938e0e84a`.
Author SHA-256:
`5cec7df530e26273f6faeb481202316f37ce96b571f1280ab88607b0f37d623e`.
The author imports only earlier independent fixture definitions, never recorder
or checker implementation. Original66 main binaries and130 earlier harness
copies remain byte-identical after the five mask-boundary additions.

F payload widths forK7/K8/K9 are473/529/586 bytes; their first double is at
offset81/81/82. The unaligned wire is encoded/decoded bytewise. Main corpus
binary size is559962 bytes. The frozen fixture manifest binds every case.

The first aggregate Qt-harness attempt failed its historical single-catalog
assumption. It was retained as failed evidence. Qualification instead used
per-case adapters with exact unchanged binary bytes and pinned executable
hashes. The historical driver also requires a separate feature-free control
for hardcoded mutations. Neither the readers nor the fixtures were changed to
turn that harness failure into a pass.

Unaware readers can accept semantically malformed F bytes as opaque optional
data; the per-case matrix records those narrower outcomes deliberately. The
new checker validates F and winches, reports understood/ignored families, and
treats reserved later families6–8 as opaque. This F-only writer rejects their
descriptors because it cannot emit their declared populations.

No Linux/macOS qualification, Core numerical capture, Qt rendering, applied-load
rollback, moment recovery or inflow-plane sampling qualification is claimed by
this handoff. Those belong to their respective subsequent owner stages.
