# Stage00 fixed inflow planes handoff

20 November 2026. Package0.2.8 adds optional fixed inflow planes to the raw pose
sidecar. This is the fourth sequential Stage00 recorder slice. Provider numerics,
Core source preparation/capture and viewer rendering retain separate owners and
qualification gates. Scientific `.serene` archive semantics are unchanged.

## Contract and package

The accepted [plane catalog and record](../spec/POSE_INFLOW_PLANES.md) has SHA-256
`a1ed160b1dcc74ebb63efa83bc14d6e0ec3eb5933bbf6c669d784337b3b5bc9e`.
Main approved its exact copied-manifest admission predicate after inspection of
the actual Inflow emitter. The previous specification bytes and separate approval
receipt remain under `spec/drafts/visualization_S00/planes-v1`; the original freeze
and draft records are unchanged. Kind8/stream6/layout1/raw encoding0 and all wire
fields remain as frozen. Bootstrap1.0, catalog1 and required mask0 are unchanged.

Fresh matching Release/Debug installation:

`C:/Users/12650029/serene-academic/serene-outputs/build/visualization_S00/planes/combined-install`

Consumers request `find_package(SereneOutputs 0.2.8 EXACT CONFIG REQUIRED COMPONENTS
pose_recorder)` and add `archive` only when needed. Recompile against this complete
header/library set with `/MD` Release and `/MDd` Debug. No cross-package ABI promise
is made. Earlier F0.2.5, loads0.2.6 and moments0.2.7 prefixes, manifests and snapshots
remain immutable. The owning repository stays on `main`, baseline HEAD
`fec96a4b01802e6ed78808bdf4cd39fe25bc6160`; Stage00 changes remain uncommitted.

## Physical and typed boundary

Each sample stores absolute unperturbed world velocity in m/s, including the
provider's mean/shear and turbulence. It contains neither induction nor body
relative velocity. Fixed realized geometry and provider metadata belong to the
catalog; display offset, warp and coloring belong to the viewer.

`InflowVelocity` contains `world_velocity_mps[3]` and validity. `InflowPlaneView`
borrows a complete sample span and its dense numeric plane ID.
`InflowPlanesFrameView` borrows all planes with the exact structural stamp,
preparation generation and common source phase. `FrameGroupView` gains a trailing
default-null `inflow_planes` pointer. These views live only through synchronous
append; the recorder retains no caller pointer. All enabled populations must
validate before any population is copied. Legacy overloads reject enabled new
streams rather than silently losing them.

Planes are dense and request identities unique. Every source is used, with source
and selected-run identities matching the copied manifest. The wire supports a
generic source table; Core's first implementation binds one qualified selected
provider. The minimum manifest checks preserve exact JSON Boolean preparation
facts, `core_binding=not_performed`, and applicable native path, nonnegative byte
count, labelled FNV fingerprint and `turbulence_only_clamp` policy. Mean-only kind
`none` requires `source` to be absent. Unknown future nonempty kinds remain
descriptive metadata and imply no executable capability. Empty descriptive
strings, negative header integers and extra finite UTF8/NUL-free metadata survive.

Preservation means semantic equality with the actual emitted JSON object,
including extras; it does not mean identical whitespace or key order. No reader
reopens an asset, recomputes a fingerprint, rewrites provider stage facts or
repeats provider scientific validation. An optional supplemental digest must
name its algorithm and precise byte subject. The isolated recorder validates
recorded syntax and bindings; Core proves actual-source correspondence.

The fixed grid uses `j=u+nu*v` and
`p=lower_corner_world_m+(u*du_m)*e1_world+(v*dv_m)*e2_world`. Endpoint-inclusive
counts are at least2; realized spacings are positive and finite. Squared basis
norm errors and absolute dot product are bounded by1e-12, without normalization.
Checked dimensions, extents, multiplication and sequential addition must remain
representable. Four affine corner extrema bound the interior during admission;
the recorder allocates no point grid or provider state.

Each plane has its own LSB-first mask, followed by point-major little-endian
binary64 triples, with no alignment padding. Clear bits require three exact
positive zeros; set bits allow true zero and preserve signed-zero bits. Unused
mask bits are zero. Entirely unavailable planes remain valid with all slots.
The single common80-byte prefix precedes all plane blocks. For N points per
plane, its contribution is `ceil(N/8)+24*N`. Two3-by2 planes occupy370 bytes;
their masks begin at80/225 and first doubles at81/226. One such plane is225 bytes.

## Grouping, resources and failure

Enabled order is F5, loads6, moments7, planes8, winch4, pose1. Winch and pose remain
adjacent. Known families validate on kind OR stream, so malformed known pairs
cannot become opaque skipped chunks. Unknown optional chunks follow the common
gap rules. All understood chunks agree on frame count, ordinal, stamp, generation
and phase; only the final fully validated pose commits the group. Independently
detectable complete pending defects reject before a later torn partner can hide
them. Structural backlinks, index and completion retain their original meanings.

Setup retains only each plane's point count and block offset. Their actual vector
capacity is charged additively with applied-load tables in
`overlay_reference_bytes`, alongside existing winch references, raw buffers and
2048-byte active scratch. Minimum whole-group fit is checked before retained
reserves, raw allocation and file creation. Append uses admitted storage and
does not parse JSON, resolve assets or retain duration-sized history.

The existing16MiB catalog,64MiB chunk/known raw-group,128MiB recorder workspace
and1000000 aggregate plane-point ceilings remain. The Python reader pre-admits
raw groups; logical decoded plane retention is80+25 times aggregate point count.
Retained history is capped at256MiB/100000 frames. One skipped raw chunk may add
64MiB temporarily. These explicit payload/workspace measures exclude allocator,
I/O, general/Python object and GPU overhead; they are not process-memory limits.

Accepted, persisted, recoverable and published states remain distinct. Failure
latches and retains `.partial` evidence. A failed flush may leave readable complete
bytes without advancing persisted counters, and failed publication may leave a
complete unpublished file. No-clobber preserves a competing destination; no
power-loss durability is promised.

## Observed qualification

Release and Debug each passed all13 selected CTests: base, winch, F, loads,
moments and planes recorder/checker pairs plus the small scientific archive test.
Each configuration recorded3986 plane assertions,2345 moment assertions,
1852 applied assertions,1435 F assertions and1120 winch assertions. The six
Python suites passed23+12+10+10+11+12=78 tests per configuration.

Actual producer/readback checks cover all38 valid independent catalogs, exact
phase/subset populations, unchanged metadata, per-plane masks,8/9-point boundaries,
valid signed zeros, legitimate unavailability and all43 malformed borrowed-view
scenarios. They check exact whole-group raw/workspace boundaries, reference
accounting, borrowed-source lifetimes,1001 frames with constant retained capacity,
17 full/short-write positions,9 flush positions, close/publication failure,
abandonment and no-clobber behavior. Successful append calls, including automatic
flush, were observed to perform zero C++ new-family allocations, with an active
counter self-check. This is not a libc/OS/RSS or numerical-Core allocation audit.

Supplementary live tests admit two used source rows with repeated provider
identity and exact manifest copies, without changing the frozen corpus. Another
test retains four literal JSON integer `-0` spellings in new-plane metadata while
decoding their exact zero values. A plane-only integer helper corrects the
inherited unsigned-token limitation; shared helpers and previous-family rules are
unchanged. The first Release CTest attempt reached3986 successful native checks
but its readback assertion also counted ordinary negative decimals beginning
with `-0`. The corrected test counts complete integer tokens. The original failed
attempt and artifacts remain; the final Release and Debug attempts pass.

The independent corpus predates recorder implementation:223 cases,38 valid and
185 invalid,5,069,458 binary bytes. Its manifest SHA-256 is
`79179baa6c597cc46d24b578ac6e8a5fe89a9bbcab05d3482f1c4561c4eb70f0`.
The primary two-plane record is370 bytes and its six-stream group2494 bytes.
All1115 pinned Python classifications and446 pinned Qt base/winch checks match
their original expectations. A valid control catalog initializes the old native
harness only for strict-JSON-invalid originals; actual submitted files and false
expectations remain unchanged. Earlier readers validate their understood subsets,
not plane semantics. The moment-only historical `opaque_future_planes.spose`
remains unchanged with its original oracle; the new fully aware checker correctly
rejects that deliberately malformed now-known family.

All four external consumers passed against a relocated installation: combined
archive/pose and isolated pose-only imports in Release and Debug. Headers match
maintained source, all eight component libraries match their build configuration,
and the installed plane specification matches the approved source/hash. Each
consumer emits the exact three-frame mixed oracle and two plane chunks. Pose-only
import excludes archive/model/codec/ZFP targets and emits no scientific archive.
The unchanged scientific Python companion opens both combined archives, verifies
all blocks, and reads `installed_plane` / `plane0.point0.vx` values[1,1] at
times[1.29,1.32] with exact uint64 accepted IDs[4,7]. This explicitly constructed
scientific channel stores the two positive accepted states; the sidecar also
retains the initial state.

Standalone consumer receipt:
`serene-theory/tmp/s00_20261120/plane-installed-consumers/20261120T190505218998Z/result.json`, SHA-256
`f15a7e4e6e5871f276dae473c8da9f49e712d86b799670119d336e2d3b127dd3`.
The immutable package manifest is
`build/visualization_S00/planes/qualification-v1/manifest.json`. Its source,
installed-package and evidence snapshots retain every observed build/test/install
attempt, literal fixtures/classifications, test executables and producer outputs,
consumer commands/inputs/outputs, and the unchanged scientific Python snapshot.
All three earlier Stage00 package prefixes and source/evidence snapshots are
verified unchanged when sealing. No staging, commit, push or branch change occurs.

Final independent C++ and Python source reviews found no remaining actionable
issues. Review is distinct from execution evidence. This package is qualified on
Windows x64/MSVC only; Linux/macOS, provider numerics, numerical-Core capture and
viewer rendering are not qualified by these recorder/reader tests.

## Core, Inflow and viewer integration

Core's first executable route is selected-source fixed-step HHT time domain,
including initial, accepted and terminal capture and initial capture disabled.
Unsupported requested analyses, integrators and stepping modes fail setup before
file creation. Generic wire phase support is not proof of another Core route.
Plane-only demand must activate the actual selected inflow provider even without
an aero model or scientific wind channels. Factor the existing duplicated demand
tests; use the existing synchronous pose delivery and preallocated Core scratch.

Use original fixed world points and exact accepted time with the qualified Inflow
coverage boundary. Legitimate exterior/invalid-height points retain unavailable
slots. Compact only eligible points, then require complete successful sampling,
zero clamp masks/counts and no unexpected diagnostic attempt. Provider residency,
overflow, numerical or partial-response failures reject the whole capture.
Logger readiness is a separate requirement. Preserve source/generation identity
and numerical-Core allocation rules; no second provider or induced-field broker
is part of this recorder.

Mean-only planes require explicit width/height. Omitted counts default to17 when
the aggregate budget admits them; coarsen only inferred counts, down to2, and
report realized counts/spacings. Preserve explicit counts and reject oversized
explicit grids or an unfittable minimum. The independent mean fixture uses the
actual copied manifest's12m/s mean,17-by17 points and7053-byte plane record. It is
a wire oracle, not a newly run provider numerical test.

Viewer uses the stored source/plane IDs, realized geometry and absolute velocities,
omits cells with any unavailable corner, and caps its CPU/GPU allocations.
Speed coloring uses original velocity. Offset and tau-seconds warp affect drawing
only; normal-only warp uses `e1 cross e2`. Default model fitting excludes displaced
planes unless selected. Core must rebuild its typed Inflow adapter against the
matching schema/DOM installation and separately qualify original-point boundary,
advection, no-clamping, logger, lifecycle and failure behavior.
