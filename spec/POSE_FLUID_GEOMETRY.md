# Fluid F geometry layout1

Accepted on 20 November 2026 after Core producer and viewer consumer review, with Serene Main approval. Bootstrap/catalog versions remain1.0/1; package versions are independent. Scientific archives are unchanged.

Use together with POSE_SIDECAR.md, POSE_WINCH_GEOMETRY.md and POSE_OPTIONAL_OVERLAYS.md.


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
require all rows valid. This explicit unavailable policy also supports Stage04 predeclared populations.

The public API keeps the two existing overloads and adds:

```cpp
using SourcePhase = WinchSourcePhase; // same four accepted lifecycle meanings
struct FluidPose { std::array<double,7> world_pose{0,0,0,1,0,0,0}; bool valid{}; };
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
First package0.2.5, with matching combined archive/pose headers and
Release/Debug libraries in a fresh Stage00-F prefix. No old install is rewritten.


The packed validity mask does not align the following binary64 values: K=7 or8 starts them at byte81, K=9 at byte82. Encode/decode bytewise or through memcpy; do not dereference unaligned double pointers.
