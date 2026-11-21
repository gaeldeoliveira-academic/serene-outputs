# Stage 00 joint wire decision candidate v1

20 November 2026. Prepared by Outputs from the current Core, Inflow and viewer
sources. **Candidate for Main/Core/viewer review; IDs and F layout are not yet
frozen. No recorder implementation has started.** The first handoff qualifies
F poses and common grouping. Later slices freeze their detailed records under
the reserved identities below before their respective implementations.

The current Outputs base is main/fec96a4b01802e6ed78808bdf4cd39fe25bc6160,
package0.2.4, clean worktree. Preserve all J07 specifications, fixtures and
installed prefixes. Scientific .serene and the Python scientific companion
are unchanged. This decision applies only to the separate raw .spose file.

## Shared identities, ordering and visibility

Retain bootstrap1.0/catalog1/required mask0, base structural sample meanings,
kind1/stream1 pose, kinds2/3 stream0 index/completion, and winch4/stream2/layout1.
Propose this permanent registry, each optional flags0/raw encoding0/layout1:

| Descriptor | Kind | Stream | Delivery |
| --- | ---: | ---: | --- |
| fluid_poses | 5 | 3 | F slice first |
| applied_loads | 6 | 4 | Completed held physical density slice next |
| elastic_moments | 7 | 5 | Sided elastic N-cut components |
| inflow_planes | 8 | 6 | Finite masked world velocity grids |

Synthetic unknown100/stream42 remains an unknown test family. New kind OR new
stream engages strict validation in a reader implementing that family. Wrong
kind/stream pairing, layout, encoding, flags or missing descriptor cannot fall
through to optional skipping. A binary's implemented family set is explicit.
Families not implemented by that binary remain opaque optional data; report
understood and ignored families instead of claiming their semantics were checked.

An enabled descriptor contributes exactly one batch per structural batch:
enabled5,6,7,8 in that order, then optional winch4 immediately followed by pose1.
Omitted descriptors mean absent features. Declared but unavailable samples or
an absent completed applied interval still produce their full fixed-width rows.
Unknown optional chunks may precede/between overlays and precede the final
winch/pose suffix. Nothing may occur between winch4 and pose1. Unknown chunks
after the linked index remain invalid. Duplicates, known order reversals and
known chunks without descriptors fail. With no winches, pose terminates the group.

All participating chunks have identical positive record count R and first
recording ordinal. Every feature record begins with the byte-identical64-byte
structural stamp, including physical sample count P, followed by:

| Offset | Type | Meaning |
| ---: | --- | --- |
| 64 | u64 | Positive source Core preparation generation, <=INT64_MAX |
| 72 | u32 | Source phase0 initialized,1 static_certified,2 transient_initial,3 transient_accepted |
| 76 | u32 | Reserved zero |

Descriptors and every enabled stream, including winches, must agree on source
generation. Core validates owner/binding/effective-field identities before
submitting a common packet. Never persist process addresses or use the capture
plan counter as the Core generation. Phases map exactly as J07: prepared/id0/
event1/factor0; certified static/event8 and exact certified time/id/factor;
transient initial/id0/initialbit or positive transient accepted/no initialbit.
Prepared/transient wire factor0 is not a model's evaluation factor. Initial
capture may be omitted; actual times and off-stride target bits remain exact.

A fully aware reader validates the entire group and commits only at its final
pose, after every declared stream passed. A partial reader commits only the
group it understands and checks outer bounds/CRC on skipped streams. Neither
kind of reader may claim validation of opaque semantics. Truncated headers or
payloads/EOF while pending expose only earlier complete understood groups.
Complete malformed payloads, CRC failures, duplicated/omitted known batches,
wrong source facts, an intervening forbidden chunk or early index are errors.
Empty failed files may declare any supported streams and contain zero groups.
Structural previous pointers and index/completion still count only pose batches.

Recorder accepted counters advance after complete borrowed-frame validation and
copying. Persisted counters/first-last structural links advance after every group
chunk and its existing checked flush succeed. A failed later chunk leaves the
prior complete group recoverable and latches failure. Keep checked close,
no-clobber publication and wire-complete-but-unpublished .partial outcomes.

## Memory contract

Preserve16MiB catalog and64MiB per-chunk ceilings. For a new-overlay group, add
a64MiB aggregate raw known-group ceiling. With per-frame widths S_pose, S_winch
and S_5..8, B=sum(enabled widths), the recorder admits
R=min(max_batch_frames, floor(effective_raw_budget/B)). At least one whole frame
group must fit or setup fails before creating a file. Default max_batch_bytes
remains4MiB and hard maximum64MiB, now explicitly shared by every active stream.
No duration-sized buffer or index is retained.

Add a separate Options.max_workspace_bytes, default/hard maximum128MiB. Its
payload accounting charges all raw batches, retained physical-reference tables
(actual capacity; currently three u64 per winch), plus a fixed2048-byte allowance
for active encoding/index scratch. Subtract reference/scratch charges before
deriving effective_raw_budget, so a tighter workspace can reduce batch count.
Expose all charges and admitted frames in Statistics. Initialization JSON/maps,
caller-owned borrowed arrays, path/general object storage, allocator and I/O
implementation overhead are outside this payload budget; it is not process RSS
or a global allocation guarantee. Do not silently redefine the existing raw
batch counter or numerical-Core restrictions.

A fully aware reader pre-admits R*B before retaining its first known payload.
Known pending plus current payload stay within64MiB; one unknown raw chunk up to
64MiB can coexist while checked/skipped, giving a128MiB combined ceiling. Release
unknown bytes before reading another chunk. A partial reader bounds its known
subset plus current skipped payload similarly. Preserve legacy base/J07-only
admission rules (which can allow two separate64MiB chunks) when no new descriptor
is understood. Default checker operation retains no decoded history. Explicit
dumps and the viewer must separately limit retained logical payload to256MiB and
100000 frames, counting all understood overlays and stamps. Python object/
container overhead and GPU allocations are separate and must be stated by their
owners; the viewer must bound GPU uploads independently and expose duration cost.

## Exact F slice candidate

`fluid_poses` contains exact integer kind5/stream_id3/layout_version1, encoding
`raw_f64_le`, positive source_preparation_generation, and nonempty `lines`,
`samples`, `segments`. Counts are <=1000000 and also limited by catalog and
complete-frame budgets. Integer tokens exclude booleans/floats and exceed neither
INT64_MAX nor host extents. All strings are valid UTF-8, nonempty and NUL-free;
all numeric metadata is finite. Additional descriptive metadata cannot change
these meanings. Empty F populations omit the descriptor.

Lines have dense `id`, existing flexible `occurrence_id`, nonempty `spanline_id`,
`source_geometry_fingerprint`, `effective_geometry_fingerprint`,
`approximation_policy`, and positive integer `native_element_count`. The pair
(occurrence_id, spanline_id) is unique. `spanline_id` preserves the exact selected
FluidSpanlineDefinition source ID; `F` is valid only if it is that source's actual
authored/defaulted value, never a substitute for another source name. Core admits supported
blades independently of active aero models; the generic occurrence kind alone
cannot prove blade qualification. Preserve source and effective fingerprints
separately. Wire can represent a policy string; Core must retain its current
preserve_native_boundaries admission and reject unqualified approximations.

Samples have dense `id`, `line_id`, nonnegative finite `material_coordinate_m`,
resolved `side`=`left`/`right`, and integer `native_element` strictly below
that line's native_element_count. Each line owns at least two samples. Exact
(line_id, material_coordinate_m, side) addresses cannot repeat, including with
contradictory native_element owners.
Distinct sides at the same coordinate remain independent samples. They never
enter the physical samples/ports or replace N. No global coordinate sorting,
snapping or quaternion sign continuity is assumed.
Layout1 does not admit `none`: the current prepared reference resolves every
unspecified request to an actual limiting side (including implied interval ends
and right for a strict interior). Serialize that resolved result. A later
unsided external source would need an explicit supported extension, not an
unresolved `none` substitution at a discontinuity.

Segments have dense `id`, `line_id`, and at least two `sample_ids`. IDs refer to
distinct samples of that line in explicitly increasing material-coordinate
order. Every sample belongs to at least one declared segment. Core must not
bridge a genuine field discontinuity; readers do not reconstruct topology from
coordinates. The recommended first Core sampling uses right-start and left-end
samples for each effective reference interval, with a separate segment per
interval. It may duplicate continuous native boundaries and preserves genuine
internal fluid cuts within one native element.

Every F chunk has previous=0, its first ordinal equals the next structural
ordinal, and stored=decoded=checked R*(80+ceil(K/8)+56K). For K F samples, let
M=ceil(K/8). An F record is exactly80+M+56K bytes:
common80-byte prefix; M validity bytes, LSB-first bit j names sample j, unused
high bits zero; then K seven-f64 rows px,py,pz,qw,qx,qy,qz. World metres and
Hamilton local-F-to-world convention match base poses. All scalars are finite.
Valid rows retain caller bits and squared quaternion norm within1e-6 of one.
Unavailable rows are exactly(+0,+0,+0,1,+0,+0,+0), mask clear; this is unavailable,
not an identity pose to draw. Valid zero position remains possible with mask set.
Do not normalize or sign-adjust. Both producer and checker validate masks.
Before retaining a complete F batch, validate every defect independently
detectable from its catalog, header and payload, including internal stamp order,
next ordinal, physical count, generation/phase, masks and poses. A subsequently
torn pose cannot make these defects recoverable. Cross-stream stamp disagreement
is rejected once both compared records exist; absence of the final pose alone
does not prove a discrepancy in an otherwise valid F stamp.

F is present at every enabled structural capture: prepared, static, transient
initial/scheduled/terminal, and a first positive-step capture. Missing active
aero does not make F unavailable. Unexpected provider failure fails the whole
capture group. The mask reserves explicit legitimate unavailable samples, not a
mechanism to hide stale identities or errors; Core's first qualified route can
require all rows valid. Joint review should confirm this mask policy against
Stage04's predeclared-unavailable requirement.

Proposed public API keeps the two existing overloads and adds:

```cpp
using SourcePhase = WinchSourcePhase; // same four accepted lifecycle meanings
struct FluidPose { std::array<double,7> world_pose; bool valid; };
struct FluidFrameView {
    Stamp stamp;
    std::uint64_t preparation_generation;
    SourcePhase phase;
    std::span<const FluidPose> poses;
};
struct FrameGroupView {
    Stamp stamp;
    std::span<const double> structural_poses;
    const WinchFrameView* winches{};
    const FluidFrameView* fluid_poses{};
};
Status Recorder::append_frame(const FrameGroupView&) noexcept;
```

Pointers and spans borrow only for the synchronous call. Missing/extra enabled
populations fail before copying any population. Catalog-free old calls continue
unchanged; old overloads explicitly reject a new-overlay catalog. Later slices
add typed borrowed members and exact package versions; no opaque extension blob.
First package proposed0.2.5, with matching combined archive/pose headers and
Release/Debug libraries in a fresh Stage00-F prefix. No old install is rewritten.

## Remaining slice meanings and questions, before their detailed freeze

Applied loads bind owning assembly/model scope and attachment separately from
target F line/station and side. Each physical density is world N/m of current F,
pre-quadrature/pre-transfer, with inline actual source world XYZ. Per-attachment
records preserve source accepted ID/time, FluidModelEvaluationId numeric
publication/mechanical/state/geometry/environment revisions, held packet ID,
prepared transition sequence and completed accepted end ID/time. A catalog
binding replaces process-local coordinator addresses. End identity equals the
common structural stamp. Missing completed interval at prepared/initial/static
is explicit unavailable status, clear masks and canonical zero payload, not an
omitted batch. An accepted-end diagnostic needs a separate product/layout.
Core must snapshot physical source points/densities at publish_packet before
integrate_fluid_density, then expose only after accept_step; current held_Q and
applied_provenance alone do not retain this payload.

Elastic moments bind owner/N spanline/material cut/side and native provider
address without pretending an off-node cut is a base structural node. Store
three signed local-N components in N.m, elastic positive-span-on-negative-span
action at current N, with validity and canonical zero unavailable rows. Core
transports the same wrench's moment from P to N using its force, then rotates
into current N axes. Freeze an explicit mapping of sided cuts onto display
intervals; never infer or interpolate across jumps. A base-sample-ID-only cut
schema would incorrectly exclude existing off-node providers.

Planes are run-owned with stable ID/source, lower-corner world origin, right-
handed orthonormal basis, positive realized spacings and endpoint-inclusive
integer dimensions>=2. Order i+nu*j, e1 fastest. Each plane has packed LSB-first
validity and world absolute velocity triples in m/s. Invalid triples are +0;
valid zero and all-unavailable planes are legitimate. Core uses original points
and actual time; display offset/warp is viewer-only. The Inflow owner must expose
coverage sharing its rotated/advected sampling transform, excluding transverse
clamping and nonpositive height. A successful clamped query is not valid coverage.

## Review and first evidence

Main/Core/viewer review should settle the shared registry/order/subset guarantees,
F catalog (including resolved native address and sided intervals), mask policy,
workspace accounting and first API/package. Later source-packet IDs and exact
moment mapping may be reviewed at their slice without delaying F.
Independent fixtures must precede recorder implementation and include asymmetric
F orientations/positions, coincident sided samples, mixed F+winch, F without
winches, optional gaps, initial omission, unavailable and valid-zero samples,
empty failed, exact-integer/owner/side/reference/mask/stamp negatives, CRC and
group truncations after a complete prefix. Pin baseline and J07-aware readers
before edits, and test their explicit subset behavior. Curated fixtures and
the final handoff will identify exact bytes and commands; this draft itself is
not implementation or numerical qualification.
