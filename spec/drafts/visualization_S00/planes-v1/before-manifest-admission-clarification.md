# Finite masked inflow planes layout1

Accepted wire contract, 20 November 2026. Main, Core, viewer and (for planes)
Inflow approved the layout and clarified scope. The common contract in
POSE_OPTIONAL_OVERLAYS.md remains unchanged. The source review draft is retained
verbatim under spec/drafts/visualization_S00/planes-v1/DECISION.md.
Implementation and numerical/renderer qualification remain separate gates;
review questions below are retained as owner integration checks, not unresolved
wire choices. No new commit, push or qualification is implied by this freeze.

## Scope

Use the reserved `inflow_planes` kind8/stream6/layout1, raw encoding0, optional
flags0. A frame stores absolute unperturbed world velocity in m/s, including
the provider's mean/shear and turbulence. It excludes induction, blade-relative
velocity, body velocity, and any display displacement. Geometry and source
provenance are immutable catalog data; each frame carries the common80-byte
stamp/source prefix and a packed mask plus velocity triples for every plane.

The first producer binds every plane to the selected run's one actually prepared
inflow provider. It does not prepare a second field, require an active aero model,
or use a scientific wind channel to activate capture. A source table leaves
identity explicit without requiring a multiple-provider Core implementation.
Unsupported source combinations are rejected during preparation.

## Catalog

`inflow_planes` is an object with these required fields:

| Field | Exact meaning |
| --- | --- |
| `kind`, `stream_id`, `layout_version` | Exact integers8,6,1 |
| `encoding` | `raw_f64_le` |
| `source_preparation_generation` | Positive Core generation, <=INT64_MAX, identical to the other understood descriptors |
| `quantity` | `absolute_unperturbed_world_velocity` |
| `units` | `m/s` |
| `sources` | Nonempty dense source table described below |
| `planes` | Nonempty dense plane table described below |

Each source has dense integer `id`, nonempty `inflow_id` and `run_id`, and an
`initialization_manifest` object copied from the actual prepared provider's
`serene-inflow.initialization.v1` manifest. The first Core route admits exactly
one source, id0. Each plane references it by integer `source_id`; every source
must be used. No source ordinal is a runtime pointer or a diagnostic-provider
address. `inflow_id`/`run_id` must agree with the copied manifest identity and
the actual selected run. The manifest preserves provider/adapter source IDs,
schema identity, resolved mean, proper basis, native placement and transport,
amplitudes, native first/last knots, and original asset identity when present.
Preserve the provider manifest verbatim: in particular, its ordinary transverse
sampling policy remains `turbulence_only_clamp`. That describes the provider's
general API, whereas plane capture additionally filters original-point coverage
and rejects any clamping evidence. Do not rewrite the manifest to describe the
plane-specific policy or sanitize a source status into a successful binding.

Preserve native `source_bytes` and the supplied algorithm-labelled
`fnv1a64` fingerprint as noncryptographic provenance. Do not relabel it SHA256,
substitute an asset path for a content fingerprint, or invent native provenance
for a mean-only field. An optional stronger digest may be added separately
during setup with its algorithm and exact byte subject stated, distinguishing
source-file bytes from manifest bytes or another identity. It supplements the
original FNV provenance and never replaces or changes it. The copied manifest's
`validation.core_binding="not_performed"` describes its adapter stage and must
remain unchanged; Core's successful generation/source binding is a separate
fact, not an edit to that historical manifest. Readers use recorded velocities
and never reopen the source asset or reconstruct the provider.

Each plane has:

| Field | Exact meaning |
| --- | --- |
| `id` | Dense zero-based integer plane ordinal; determines wire order |
| `plane_id` | Unique nonempty user/request identity, preserved without normalization |
| `source_id` | Valid source-table ordinal |
| `lower_corner_world_m` | Three finite binary64 values defining sample(u=0,v=0) |
| `e1_world`, `e2_world` | Finite unit orthogonal world directions; e1 is the fast axis |
| `du_m`, `dv_m` | Finite strictly positive realized spacings in metres |
| `nu`, `nv` | Exact integers, each >=2, including both endpoints |
| `sample_order` | `u_fastest` |

For this layout, validate each basis squared norm within1e-12 of1 and
`abs(dot(e1,e2))<=1e-12`; reject outside the tolerance without normalizing.
The right-handed displayed normal is `n=e1 cross e2`. Do not store a second
independently mutable normal. Core's schema planner obtains e1 by projecting
the positive world axis least aligned with the normalized user normal, with
ties X,Y,Z, and takes e2=n cross e1. The wire accepts any admitted proper plane
basis; a reader need not rerun that planner. Core and viewer accepted this1e-12 representation tolerance; it is not a normalization instruction.

For `0<=u<nu`, `0<=v<nv`:

```text
j = u + nu*v
p(u,v) = lower_corner_world_m + (u*du_m)*e1_world + (v*dv_m)*e2_world
width_m  = (nu-1)*du_m
height_m = (nv-1)*dv_m
```

Core constructs and retains these original fixed world points once during
setup using the recorded realized values. Every multiplication, extent and
resulting coordinate must be finite/representable. Do not recompute points from
the requested rather than realized spacing. Wire geometry defines the affine
grid; actual boundary classification uses those original prepared point values,
never a viewer reconstruction or a rounded inverse-transform proof.

The descriptor contains no display offset, warp gain or color normalization.
Those are viewer controls. Descriptive request/default/coarsening provenance may
be added but cannot change the realized geometry or sample order. Normative
identity strings are nonempty, NUL-free valid UTF8. Additional manifest strings
keep their source schema's semantics, are valid UTF8/NUL-free, and cannot
override normative fields. All numerical metadata is finite; indices/counts
are exact integers excluding booleans/floats, bounded by INT64_MAX and host
extents. No duplicate plane ID, unknown source, orphan source or empty population
is admitted. An empty selection omits the descriptor.

## Exact record and offsets

For plane p let `N[p]=nu[p]*nv[p]`, `M[p]=ceil(N[p]/8)`. Define:

```text
A[p] = 80 + sum(q<p, M[q] + 24*N[q])
S8   = 80 + sum(p, M[p] + 24*N[p])
mask_byte(p,j)      = A[p] + floor(j/8)
mask_bit(p,j)       = j mod 8
component(p,j,c)    = A[p] + M[p] + 24*j + 8*c, c=0,1,2
record(r)          = payload_start + r*S8
```

The common80-byte prefix occurs once per frame, not once per plane. It uses
the frozen copied64-byte structural stamp, Core generation, SourcePhase and
zero reserved word. The stamp's sample count remains the physical pose count,
not a plane point count. Plane blocks follow dense catalog order. Each contains
its own LSB-first packed validity mask, then point-major little-endian
binary64 `(ux,uy,uz)` world velocity rows. There is no padding or alignment;
implementations use bytewise operations/memcpy, never an unaligned double pointer.

Every velocity component is finite. A clear bit requires the exact three
positive binary64 zero bit patterns. A set bit permits a true zero velocity and
preserves caller bits, including signed zeros; vector magnitude is never a
validity test. Unused high bits in each plane's last mask byte are zero. An
all-unavailable plane or complete frame is valid and retains every fixed slot.

Chunk `previous=0`; positive R and first ordinal match the other understood
group chunks; stored=decoded=checked `R*S8`. Kind8 OR stream6 engages strict
validation in a reader implementing this family. A wrong pair, flags, layout,
encoding or missing descriptor cannot be skipped. Earlier F-only readers keep
kind8 opaque optional and state that its semantics were ignored.

Kind8 follows enabled5/6/7 and precedes optional winch4 and required pose1.
Winch4 remains immediately adjacent to pose1. All declared implemented streams
occur exactly once per group with common R, ordinal, stamps, generation and
phase. Validate independently detectable complete-plane defects before retaining
a batch; a subsequently truncated pose cannot conceal malformed masks, values,
source facts or internal clocks. Cross-stream stamp equality is checked once
both records exist. Publish/recover only a completely validated understood
group. Empty failed files may declare the stream and contain zero groups.

## Typed borrowed Outputs boundary

Use the following extension of the already accepted grouped API:

```cpp
struct InflowVelocity {
    std::array<double,3> world_velocity_mps{0,0,0};
    bool valid{};
};
struct InflowPlaneView {
    std::uint64_t plane_id{}; // dense catalog id, not the request string
    std::span<const InflowVelocity> samples;
};
struct InflowPlanesFrameView {
    Stamp stamp;
    std::uint64_t preparation_generation{};
    SourcePhase phase{};
    std::span<const InflowPlaneView> planes;
};
// Additional member of FrameGroupView:
const InflowPlanesFrameView* inflow_planes{};
```

`planes.size()` equals the admitted catalog count; plane views are in dense
order and have matching IDs; each sample span has exactly nu*nv rows. The
recorder validates all spans, values, validity, identities and the common
packet before accepting/copying any population. Every pointer/span borrows only
for synchronous `append_frame(group)`. No geometry ownership, provider pointer,
source callback or mask-packing obligation crosses this boundary. The recorder
packs bool validity; no C++ object layout is a wire representation. Legacy
append overloads continue rejecting any catalog requiring a new population.

## Provider coverage and Core delivery

The needed Inflow prerequisite already exists in the inspected20November
worktree: `classify_coverage(time, original_world_point)` and
`native_to_global(time, native_coordinate)`. These share the sampling geometry
through `locate_point`; Core must use them rather than implement another
world-to-box transform. Read-only classification performs no allocation,
logging, interpolation or velocity reconstruction.

For native data let R be `basis()` columns, a the native anchor, and
`d(t)=phase+advection_speed*(t-time_origin)`. The shared mapping is
`xB=R^T*(p-a)`, native `(s,y,z)=(d(t)-xB.x,xB.y,xB.z)`; inverse planning is
`p=a+R*(d(t)-s,y,z)`. Increasing s points opposite R[:,0]. Use configured
advection, not mean speed or the file's generation speed. Tilt can therefore
translate the finite footprint vertically. No wrap or time remapping is allowed.

Use actual stored bounds `[0,s_max]`, `[y_min,y_max]`, `[z_min,z_max]`, not
count*spacing or a periodic seam. Closed native endpoints are included by the
provider's exact comparisons. No epsilon halo, coordinate snapping or clamped
sample can create validity. A transformed corner can classify just outside
after floating-point roundtrip; the forward classification of the actual
prepared world point remains authoritative.

At every enabled capture and its exact stamp time:

1. Check the live prepared generation, selected source, world frame and logger
   readiness. Initialize all unpublished rows to positive zero and clear validity.
2. Classify every original fixed world point. `code==ok && exterior_axes==0`
   is covered. `code==ok && exterior_axes!=0` is legitimately unavailable.
   `invalid_height` is also legitimately unavailable: physical height is exactly
   original world z minus configured ground_z, and must be strictly positive.
   Invalid time/point, frame mismatch, unrepresentable arithmetic, not_ready or
   other unexpected classifications fail the capture group; they are not masks.
3. Compact covered points into fixed scratch and retain an index back to each
   plane/row. Call the same provider's velocity-only `get_unperturbed_inflow`
   at the actual accepted time. No covered points means a valid all-unavailable
   frame without a provider call.
4. Require ordinary numerical/diagnostic success, `completed` equal to the
   compacted requested count, `clamp_mask==0`, `clamped_count==0`, no unexpected
   diagnostic attempt (`diagnostic_attempted==false` for this velocity-only
   covered batch), and finite values. Any contrary evidence fails the whole
   capture group even if the provider reports a successful numerical prefix or
   finite clamped values. Only after the entire covered batch succeeds scatter
   and set bits. Logger readiness remains an independent Core gate; successful
   coverage or the absence of a warning is not a readiness proof.
   A successful prefix followed by failure, exception or logger failure rejects
   the entire capture group. Keep exact failure evidence and solver/scientific
   outcomes distinct; do not republish a previous plane as current data.

Mean-only fields have no finite native box but still require finite inputs,
finite time and strictly positive physical height. Calm mean speed is valid.
Native fields retain finite coverage even with zero mean or zero sigma. A
configured native provider lacking residency is not a mean-only fallback.

Core owns plane clipping/planning. For automatic extents, map the eight native
corners at both capture-window endpoints through `native_to_global` and intersect
the plane with their swept convex hull; Main/Inflow accepted this bound for the
current fixed basis and affine constant-speed advection. Ground filtering and per-time
classification still apply. Degenerate inferred cuts fail setup. Mean-only
fields require explicit finite positive width and height: they have neither
finite native extents nor native knot spacing from which to infer a grid. For a
native-source direction e, use native components
`(-R[:,0] dot e, R[:,1] dot e, R[:,2] dot e)` and target
`h=1/sqrt(sum((e_native[i]/delta_native[i])^2))`. Record realized endpoint-inclusive
spacing after count admission/coarsening.

**Main's mean-only planning decision:** each omitted count independently defaults
to 17 endpoint-inclusive samples, giving 16 intervals and including the centre
at the nominal resolution. For width W and height H with both counts omitted,
the requested grid is 17 by 17, with du=W/16 and dv=H/16. An explicitly supplied
count is preserved. If the aggregate budget requires coarsening, only inferred
counts may decrease, down to 2 per inferred axis, while the requested extents
stay fixed and realized spacings are recomputed. Report each requested and
realized count, spacing, cost and coarsening decision. Reject a fully explicit
grid that exceeds the budget; for a partially inferred grid, retain its explicit
counts and reject if no admitted inferred counts can make it fit. Reject even
an entirely inferred grid if it still cannot fit at the minimum resolution.
This is a Serene visualization default, not a native-provider spacing policy.
The wire stores only the realized grid under the existing fields and meanings.

The stream contract supports all common phases and demands a plane batch at
every enabled structural capture, including initialized, certified static,
transient initial, scheduled and off-stride terminal, and first positive-step
capture when initial capture is disabled. Wire support is not a claim that Core
currently admits every analysis. Main selected the selected-source fixed-step
HHT time-domain plane-only route for initial qualification, including transient
initial, accepted and terminal captures and initial-capture-disabled runs.
Prepared-only, static, other integrators or stepping modes, and other unsupported
requested analyses receive a diagnosed setup refusal before file
creation until their source/logger lifecycle is separately qualified. Today
Core's scientific `output_wind` callback requires prepared+ran+not finished;
the accepted capture adapter must use its explicitly qualified source lifecycle.
Do not silently omit the stream or fill it unavailable because the source or
lifecycle was never prepared.

Use the existing synchronous accepted-state pose delivery and Core-owned fixed
scratch; add no second hook. Factor the duplicated selected-run wind-demand
tests in application/context so enabled valid planes alone activate the actual
provider. Preserve world-frame, generation, source, lifetime, producer-registration
and logger gates. Rebuild the typed Inflow adapter against the exact changed
schema/DOM installation even if the input wire version is unchanged. The
existing AbsoluteWindService is a useful allocation-free, unchanged-on-failure
sampling boundary, but currently has no coverage/planning callback and uses
fluid-model evaluation identities. Core must choose a prepared capture-specific
view or a checked internal adapter; Outputs/viewer receive only immutable source
metadata and stamped values, never that live service.

## Bounded storage and admission

Keep the frozen16MiB catalog,64MiB chunk and64MiB aggregate understood raw-group
ceilings. Require nonempty source/plane arrays; bound plane count and aggregate
N by1000000, then by the stricter catalog/frame/group/workspace budgets. Use
checked multiplication/addition for nu*nv,24*N, summed populations and R*S8;
compute mask width as N/8+(N%8!=0). Admit all corners and extents before any
point allocation. A per-plane cap never substitutes for the aggregate cap.

For enabled widths B=S_pose+S_winch+S5+S6+S7+S8, derive the raw batch count with
the existing common formula, including workspace subtraction before admission.
At least one full group must fit before opening a file. Extend the recorder's
128MiB workspace accounting to actual-capacity plane offset/reference storage
if retained, alongside existing winch references and2048-byte active scratch;
do not hide a new payload table in an uncharged category. Initialization JSON,
allocator/I/O overhead and caller-owned spans retain the frozen exclusions.

Core preallocates original positions, compaction positions, row-address indices,
provider response scratch and complete plane output storage. All population and
byte charges are reported at setup. Capture does not resize, parse JSON, resolve
assets or construct duration-sized history. The provider keeps the resident
field; capture does not copy it. Numerical-Core allocation rules remain in force.

Readers pre-admit R*B for their understood subset before reading its first known
payload; one skipped chunk can additionally occupy up to64MiB and is released
before the next read. Retained decoded history separately obeys256MiB logical
payload and100000frames before allocation/append. A checker can conservatively
charge plane data as80+sum(25*N), including one logical validity byte per point;
state this implementation-specific accounting and Python/container overhead
separately. Viewer GPU upload/storage must have its own cap. Preserve legacy
base/J07-only admission when no new descriptor is understood.

## Independent fixture and qualification plan

Start with a3-by2 plane having lower corner(10,-4,5), e1=(0,1,0),
e2=(0,0,1), du=2,dv=3. Its normal is+X and its e1-fast world points are
(10,-4,5),(10,-2,5),(10,0,5),(10,-4,8),(10,-2,8),(10,0,8).
Use this exact authored row oracle:

| j=(u+3v) | Valid | World velocity m/s |
| ---: | --- | --- |
| 0 | true | (1,2,3) |
| 1 | false | (+0,+0,+0) |
| 2 | true | (-4,5,-6) |
| 3 | true | (0,0,0) |
| 4 | false | (+0,+0,+0) |
| 5 | true | (7,-8,9) |

Its mask is 0x2d and frame width 225 bytes; vector 0 starts at byte 81. Use a
second different 3-by-2 plane in one frame to prove per-plane rather than
concatenated masking: its mask begins at byte 225, vectors at 226, total width
370. Exercise legal 2-by-4 and 3-by-3 grids across the 8/9-point mask boundary;
a 7-point rectangle must be rejected because both dimensions must be >=2.
Include a mask with only the final valid point, dirty high bits,
negative-zero invalid components, valid signed zero, all-unavailable frames,
nonfinite vectors, mismatched dimensions/source IDs/generation/phases/stamps,
wrong family discriminators and count/overflow failures. Do not infer expected
vectors by decoding a production recording.

Add prepared/static/transient/initial-disabled wire fixtures, mixed
F+planes+winch+pose and no-winch groups, unknown chunks in allowed gaps, forbidden
winch/pose gaps, each truncation boundary, complete-invalid pending planes and
cross-stamp disagreement once both chunks exist. Assert pre-read raw admission,
pre-allocation retained admission and constant default retention with duration.

Provider/Core qualification uses an asymmetric manufactured native field and
the existing coverage tests: exact endpoints and one representable step outside,
simultaneous exterior axes, oblique yaw/upflow/anchor, actual-time advection,
strict ground equality/below-ground rejection, native exhaustion without wrap,
zero amplitudes with finite support, a true zero velocity, all-unavailable grids,
and unexpected provider/diagnostic failure after a successful prefix. Inject
successful-status responses with nonzero clamp_mask, nonzero clamped_count or
an unexpected diagnostic_attempted flag and require whole-group failure; retain
an independent not-ready-logger rejection. Compare
sampled values to the same prepared provider at the original points and exact
accepted times. A display offset or warp must change neither those points nor
the values. Exercise the selected-source fixed-step HHT plane-only transient
with no aero or scientific wind channels, and every
additional analysis route that Core explicitly qualifies; compare disabled and
enabled numerical results and perform scoped allocation/cost checks.

Include a width/height-only mean-field request with both counts omitted: centre
(10,-4,20), normal +X, width 32 m and height 16 m. The admitted nominal basis is
e1=+Y, e2=+Z, lower corner (10,-20,12), nu=nv=17, du=2 m and dv=1 m. It contains
289 points, includes the centre at (u,v)=(8,8), and needs 37 mask bytes plus
6936 velocity bytes and the common 80-byte prefix: S8=7053 bytes. With a constant
10 m/s mean along +X and ground_z=0, every point is covered and every row is
(10,0,0); also qualify a calm field as valid zero. No native metadata may be
invented to obtain these counts. Verify independent omission of either count,
explicit-count preservation, reported inferred-only coarsening with unchanged
extents, rejection of an oversized explicit grid, and rejection when even the
minimum inferred grid cannot fit. The default 17-count assertion applies when
the configured aggregate budget admits that nominal grid.

Viewer tests use the3-by2 asymmetry to catch u/v transposition, source/plane
swaps, reflected normal and masking errors. Omit each cell with any unavailable
corner. Speed coloring uses original world velocity; offset and tau-seconds
warp apply only to drawing. Optional normal-only warp projects onto e1 cross e2.
Default model fitting excludes displaced planes unless the user opts in.

## Owner integration and qualification gates

1. **Inflow:** coverage and sampling semantics are accepted: exact closed native
   endpoints, original-world positive height, no halo/snap, inverse-roundtrip
   caveat, eligible compacted batches with no clamping/diagnostic evidence, and
   verbatim manifest provenance. Name the supported installed coverage package
   for the producer handoff. No provider behavior change is requested here.
2. **Core/Main:** initial analysis scope is decided: qualify the selected-source
   fixed-step HHT time-domain plane-only route with initial/accepted/terminal and
   initial-disabled capture; diagnose unsupported analyses, integrators and
   stepping modes during setup. The wire remains fully phase-capable.
   Core must identify and qualify the actual capture adapter's source and logger
   readiness gates; prepared/static qualification is deferred.
3. **Core:** choose the internal prepared capture view and exact source identity
   check. The public context currently exposes manifest/path/status, not a
   prepared provider coverage view; AbsoluteWindService supplies sampling but no
   coverage callback. Confirm one selected-run provider for the first producer,
   no induced-field broker access, and shared wind-demand factoring.
4. **Core/viewer:** accept the proposed geometry field names, source table,
   e1-fast order, per-plane masks, exact offset formula and1e-12 basis tolerance;
   preserve realized values and define the setup point-construction arithmetic
   used by provider boundary tests. Apply Main's mean-only defaults above; they
   are a selected producer-planning decision, not an open wire question. Confirm
   inferred-only coarsening and automatic swept-box clipping responsibilities.
5. **Outputs/viewer:** accept the typed `InflowPlanesFrameView` addition and
   conservative logical retention accounting; enumerate all Core/recorder/viewer
   CPU/GPU storage charges and show requested duration cost. Package identity and
   installed prefix are chosen only after this layout is accepted.

## Source evidence inspected, not rebuilt

Paths are relative to `serene-academic`; references identify the live inspected
worktree, not an assertion that it is committed or that this proposal ran tests.

- `serene-inflow/include/serene/inflow/inflow.hpp:245` CoverageResult and its
  allocation-free, geometric-only semantics;335/359 coverage and inverse APIs;
  369 velocity evaluation.
- `serene-inflow/src/inflow.cpp:187` shared locate_point arithmetic;208 exact
  coverage;232 inverse transform;251 original sampling;269 streamwise rejection,
  274 transverse clamping,305 mean addition and world rotation.
- `serene-inflow/include/serene/inflow/native_bladed.hpp:29` actual knot counts,
  spacings, first/last bounds and algorithm-specific source fingerprint.
- `serene-inflow/include/serene/inflow/schema_adapter.hpp:44` PreparedInflow
  ownership and source/run identity; `src/schema_adapter.cpp:224` manifest
  identity,262 standalone binding provenance,291 native source identity.
- `serene-inflow/docs/inflow_plane_coverage_handoff.md:49` authoritative coverage
  contract;106 rounded endpoint caveat;176 swept-box planning;201 provider-only
  qualification record. Its reported tests are prior Inflow evidence, not tests
  rerun for this layout.
- `serene-core/includes/serene/core/simulation/fluid_environment.hpp:33`
  AbsoluteWindService readiness/revision/bounded scratch; matching
  `src/simulation/fluid_environment.cpp:65` transactional complete sampling.
- `serene-core/includes/serene/core/simulation/detail/preparation.hpp:382`
  owned PreparedInflow and source/manifest/status fields;
  `src/simulation/chrono_simulation_context.cpp:318` time-domain binding,
  391 actual provider/logger preparation,624 public manifest/path/status views.
- `serene-core/src/simulation/wind.cpp:29` current scientific callback and37
  active lifecycle gate; `src/application.cpp:410` and
  `src/simulation/chrono_simulation_context.cpp:72` duplicated wind-demand tests.
- `serene-theory/implementation_prompts/serene_3d_visualization/08_core_inflow_plane_capture_prompt.md`
  and `serene-theory/serene_3d_visualization.tex:460` planning/capture/display scope.
