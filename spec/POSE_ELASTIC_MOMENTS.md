# Signed elastic moments at sided N cuts

Accepted wire contract, 20 November 2026. Main, Core, viewer and (for planes)
Inflow approved the layout and clarified scope. The common contract in
POSE_OPTIONAL_OVERLAYS.md remains unchanged. The source review draft is retained
verbatim under spec/drafts/visualization_S00/moments-v1/DECISION.md.
Implementation and numerical/renderer qualification remain separate gates;
review questions below are retained as owner integration checks, not unresolved
wire choices. No new commit, push or qualification is implied by this freeze.

This layout uses reserved optional `elastic_moments`, kind7/stream5/layout1.
It stores three signed elastic moment components about the actual current
structural N point, expressed in that cut's actual current N axes. A cut owns
its material/side/native-provider address independently of the base pose sample
population. An off-node cut is neither a new mechanical node nor a renamed base
sample. The initial viewer offers signed component and full-resultant coloring
and material-coordinate diagrams, with explicit limited display interpolation. It does not
claim to reconstruct an off-node physical pose from the recorded base polyline.

## Existing Core evidence and its limits

Read-only source review used the working tree of `serene-core` on
`7102dbb69927750977224ed4257c33c46f1570a8`; this is an inspected repository HEAD,
not a claim that every working-tree byte or previously qualified executable has
that identity. No builds, numerical runs or changes to Core/viewer occurred for
this review. The following source paths are the review anchors.

| Existing authority | What the source establishes | Review link |
| --- | --- | --- |
| `includes/serene/core/simulation/detail/elastic_section_provider.hpp` | `ElasticSectionWrench` supplies world force, world moment and its actual native current point. The sign is positive-span material acting on negative-span material. Euler recovery borrows the actual prepared native law; it reads no damping, inertia or applied-load term. | [Euler provider and wrench](C:/Users/12650029/serene-academic/serene-core/includes/serene/core/simulation/detail/elastic_section_provider.hpp:12) |
| `includes/serene/core/simulation/detail/fpm_elastic_section_provider.hpp` | FPM uses cached full `K6(eta)*Bx(eta)` and native `Nx`; coupled stiffness entries remain present. It publishes a complete world wrench about the native current section point using fixed storage. | [FPM provider](C:/Users/12650029/serene-academic/serene-core/includes/serene/core/simulation/detail/fpm_elastic_section_provider.hpp:9) |
| `src/simulation/detail/chrono_fpm_beam.cpp` | Exact material lookup selects the native element and eta. Interior native knots require an authored side, even if reference triads agree; outward endpoint limits reject. Strictly off-node cuts are supported. | [FPM sided binding](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/chrono_fpm_beam.cpp:456) |
| `src/simulation/detail/chrono_rayleigh_beam.cpp` | Euler exposes the equivalent explicit native element/eta binding, including both interior sides and inward physical endpoints. | [Euler sided binding](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/chrono_rayleigh_beam.cpp:338) |
| `src/output/selection.cpp` | Structural material selection resolves actual reference position and `Q_N`; endpoints use inward sides. An unspecified interior reference triad is admitted only if its two limits agree, then resolves right. This reference resolution does not waive the elastic provider's stricter native-interface side requirement. | [N reference and side resolution](C:/Users/12650029/serene-academic/serene-core/src/output/selection.cpp:787) |
| `src/simulation/detail/observation_motion.cpp` | `prepare_material_observation` retains exact material/native ownership and N-reference residuals. `evaluate_pose` / `evaluate_motion` supply the current physical N frame, using the existing off-node attachment map rather than the native elastic section interpolation point. | [Prepared N address](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/observation_motion.cpp:114), [current pose](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/observation_motion.cpp:56) |
| `src/simulation/detail/output_binding.cpp` | Ordinary Euler/FPM elastic providers already bind actual sided cuts and publish their complete native wrench. FPM admission checks the actual prepared blade, native backend and current request; steady and admitted HHT are supported, with preserved native boundaries required for the transient route. | [Actual FPM carrier proof](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/output_binding.cpp:16), [provider binding](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/output_binding.cpp:485), [world wrench](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/output_binding.cpp:607) |
| `src/output/evaluation.cpp` | Moment evaluation projects the source moment and the same source force's origin shift into the requested current axes. Native point and selected N point are separate dependencies. | [Exact-once moment relocation](C:/Users/12650029/serene-academic/serene-core/src/output/evaluation.cpp:747) |
| `src/simulation/pose_capture.cpp`, `src/pose_recording.cpp` | Base capture retains actual structural node coordinates/sides, actual N reference frames, explicit segment breaks and native element metadata. It may retain only the right-sided base sample at a continuous N frame; that does not prove equal left/right elastic actions. | [N capture and breaks](C:/Users/12650029/serene-academic/serene-core/src/simulation/pose_capture.cpp:275), [catalog metadata](C:/Users/12650029/serene-academic/serene-core/src/pose_recording.cpp:132) |
| `tests/fpm_blade_output_tests.cpp` | Maintained test source contains off-node `s=.37`, left/right cuts at `s=1`, inward endpoints, a nonzero native-point-to-N transport comparison, current-N Mx/My checks, full world moment checks and rate-independence probes. | [Literal sided stations](C:/Users/12650029/serene-academic/serene-core/tests/fpm_blade_output_tests.cpp:131), [transport and current axes](C:/Users/12650029/serene-academic/serene-core/tests/fpm_blade_output_tests.cpp:281), [rate independence](C:/Users/12650029/serene-academic/serene-core/tests/fpm_blade_output_tests.cpp:318) |
| `docs/fpm_blade_outputs_handoff.md` | Historical owner handoff reports qualification for steady/HHT, independent off-node transport and inactive initialized/perturbed probes. Its original measurements, executable identity and scope remain its own evidence. | [Existing qualification handoff](C:/Users/12650029/serene-academic/serene-core/docs/fpm_blade_outputs_handoff.md:1) |

The present pose-capture public frame carries base poses and winches; it does
not yet expose an elastic-cut capture population. Provider capability is useful
implementation evidence, not evidence that this proposed stream already
exists. The new capture adapter needs its own prepared provider/basis bindings,
source identity checks and accepted-state publication. Do not route through an
unrelated user-selected scalar column or broaden FPM support from an occurrence
kind/string alone. Graph resultants, graph cuts, connection reactions and
generalized elastic torques are distinct products and are not admitted here.

## Physical quantity, sign, point and basis

For a prepared sided cut let P be the actual point returned by the elastic
provider and N the current structural material point obtained from the matching
prepared N observation. The provider's single coherent packet supplies
`F_world`, `M_P_world` and `p_P_world`. Evaluate the N observation from the same
source state to obtain `p_N_world` and `Q_N_world`, whose columns are current
local-N axes in world coordinates. Compute exactly once:

```
M_N_world = M_P_world + (p_P_world - p_N_world) cross F_world
m_N      = transpose(Q_N_world) * M_N_world
payload  = [m_N.x, m_N.y, m_N.z]
```

The sign convention is `positive_span_on_negative_span` for both left and right
limits. `left` and `right` select limiting material/native ownership; they do not
reverse the action sign. Units are N.m. Components are local-N Mx, My and Mz;
do not rename a component as flap/edge/torsion from a source-family assumption.

Use the full force from that same elastic wrench for transport. Do not use an
applied-load sum, a different accepted packet, a scalar force projection, a
current fluid frame, a native carrier basis or the reference N basis. Do not
transport an already relocated evaluator result a second time. No rate,
inertial, damping, hydrodynamic or aerodynamic contribution is added. Elastic
response can of course reflect the solved configuration caused by those loads.

The provider may have a different current point from N even when their reference
points agree. The off-node tests explicitly discriminate that case. Current N
orientation is available through the prepared observation map, but its use in a
new capture route still needs Core-owner qualification. The maintained selected
tests explicitly cover current-N Mx/My and three world components; a complete
nontrivial current-N Mz case is required for this stream before qualification.
This layout carries a complete three-component row and offers its full
resultant, so admission requires proved Mx/My/Mz capability for each requested
provider/phase combination. If only some components are proved, do not fabricate
the others as zero or call a partial norm the full resultant: reject this
capture request for that combination. This does not require every backend to
support every phase or broaden another provider's admitted domain.

## Catalog

The optional descriptor contains the following exact scalar fields and tables.
All integers are JSON integer tokens, excluding booleans/floats, in
`[0, INT64_MAX]` and host range. Dense IDs equal array positions; base segment
IDs are looked up by their existing stable ID. Strings are nonempty valid
UTF-8 without NUL. Numeric metadata is finite. Counts are at most1,000,000 and
also subject to the16MiB catalog and complete-group byte bounds. Empty cut
populations omit the descriptor. A minimal useful mapped population has two
cuts; every declared cut participates in at least one display interval.

```
elastic_moments:
  kind: 7
  stream_id: 5
  layout_version: 1
  encoding: "raw_f64_le"
  source_preparation_generation: <positive exact integer>
  quantity: "elastic_section_moment"
  components: ["Mx", "My", "Mz"]
  unit: "N.m"
  expression_frame: "current_structural_N"
  moment_point: "current_structural_N"
  action_convention: "positive_span_on_negative_span"
  display_interpolation: "linear_scalar_on_declared_base_edge"
  lines: [...]
  provider_addresses: [...]
  cuts: [...]
  sided_breaks: [...]
  display_intervals: [...]
```

Phase capability is a Core capture-setup admission rule, not a new required
wire/catalog field. Before opening the recording, Core checks every selected
cut/provider and all three required components for each phase actually requested
by its capture plan. A certified-static-only plan need prove phase1; an accepted
transient plan proves its requested initial2 and/or accepted3 captures. Add
prepared0 only when explicitly requested and qualified. The unchanged common
`SourcePhase` on each actual record and its exact group-stamp consistency remain
the reader's authority. No recording is required to include phase0. Optional
descriptive capability metadata may retain Core's proof provenance, but cannot
waive, replace or extend the common wire semantics or become a new required
reader membership rule.

`lines` rows contain:

```
{id, occurrence_id, spanline_id, native_element_count,
 source_geometry_fingerprint, effective_geometry_fingerprint,
 approximation_policy}
```

`occurrence_id` names an existing flexible occurrence; that wire check alone
does not establish Core capability. `(occurrence_id, spanline_id)` is unique.
`spanline_id` preserves the actual structural line local ID, currently `N` in
ordinary Core capture. `native_element_count` is a positive integer. Source and
effective geometry fingerprints remain distinct; their equality is not assumed.
Core supplies an actual realization policy instead of borrowing the F policy
unconditionally. If the existing optional `structural_lines` metadata is
present, its matching occurrence/local ID must agree with this line's identity;
the proposed descriptor does not require an old generic reader to understand
that additional table.

Main approved the precise optional-table interpretation on 20 November 2026,
after checking the maintained Core producer. Its structural rows contain
`{id, occurrence_id, local_id, sample_begin, sample_count}`. When the table is
absent, use the validated explicit structural segment/edge and occurrence
ownership below. When present, resolve exactly one structural row for each
moment line by `(occurrence_id, local_id == spanline_id)`. Validate that matched
row's integer fields and checked sample range against the base sample count;
both endpoints of every mapped display edge must lie in that range. Any
`line_id` supplied on the base segment or either endpoint sample must equal
this matched **structural row ID**. That ID is independent of the moment-line
ID; do not equate the two table ordinals. No literal `N` spelling, F recording,
fingerprint equality or native-element-count field is required by this table.
The producer authority is `src/pose_recording.cpp` and the `StructuralLine`
declaration in `includes/serene/core/simulation/pose_capture.hpp`, in Core.
This clarifies catalog validation and does not change kind7/layout1.

`provider_addresses` rows contain:

```
{id, line_id, native_element, provider_id,
 material_begin_m, material_end_m}
```

`line_id` refers to this descriptor's line table. `native_element` is below the
line's actual native-element count. `(line_id, native_element)` is unique.
`0 <= material_begin_m < material_end_m`, and their difference is finite.
The interval names the actual prepared native material element, not a nearest
display edge. A table may cover only selected native elements. Provider ID is
the actual prepared provider's stable string, initially one of:

* `chrono10_euler_diagonal_elastic_cubic_recovery_v1`
* `chrono10_fpm_full_K6_Bx_elastic_native_section_v1`

The wire reader validates a nonempty provenance string, not the underlying
constitutive law. Core admission is restricted to actually qualified providers;
a new string does not authorize a new provider. Recording-local table references
replace process pointers, native object addresses and scalar output indexes.

`cuts` rows contain:

```
{id, line_id, material_coordinate_m, side, native_element,
 provider_address_id, native_eta}
```

The cut's line/native element must exactly match its referenced provider
address. The coordinate lies inside that address's closed material interval.
`side` is resolved `left` or `right`, never `none`. At the provider interval's
lower endpoint the side is right and eta exactly-1; at its upper endpoint the
side is left and eta exactly+1. At a strict interior, eta is strictly between
-1 and+1 and the resolved physical N side remains explicit. An unspecified
strict-interior request may resolve right during preparation; it cannot erase
an authored side or bypass a reference discontinuity.

`(line_id, material_coordinate_m, side)` is unique, including across different
native owners/providers. Opposite sides at the same coordinate remain separate
cut IDs and may have different values. The material coordinate is authoritative
for display placement; native eta is actual provider provenance. The reader
checks bounds and exact endpoint relationships but does not recompute a
floating-point eta to pretend it has re-bound the native provider. Core must
cross-check both against the same actual prepared element before publication.

This is intentionally not `{base_sample_id, Mx, My, Mz}`. No base sample ID is
required at the cut coordinate. A base endpoint's retained side need not equal
an elastic cut's side: ordinary base capture can retain a single N pose at a
continuous interface while elastic recovery still requires two independent
native limits.

## Exact cut-to-display mapping, without inferred continuity

The required `sided_breaks` array may be empty. Each row contains only:

```
{left_cut_id, right_cut_id}
```

The two distinct referenced cuts must have the same line and exactly equal
material coordinate; the first has side left and the second side right. Their
provider/native element may be the same or different. A cut ID can appear in
only one pair. Every declared pair of opposite-sided cuts at one line/coordinate
must occur exactly once in this table. Thus there is no ambiguous unpaired
duplicate coordinate to join accidentally. The pair itself is a display
separation contract; it need not imply that the two moment values differ in
every frame. No redundant break ID, coordinate or owner field is needed because
those facts are derived from the cut IDs.

Core declares every genuine N-basis or elastic-field discontinuity using such a
pair, including a discontinuity strictly inside one native element. A continuous
interior station may instead have a single cut ID shared by adjacent intervals
and no break pair. The reader checks this explicit declaration and interval
rules; it cannot discover an undeclared physical discontinuity from coordinates.
Native-interface cuts with separate left/right IDs are paired even if their
sampled moments happen to agree. This preserves their explicit independence.

Each `display_intervals` row contains:

```
{id, line_id, base_segment_id, edge_index, cut_ids: [lower_cut_id, upper_cut_id]}
```

The base segment must exist and belong to the line's occurrence. `edge_index`
selects the consecutive pair at positions `edge_index` and `edge_index+1` in
that segment's existing `sample_ids`. Both samples must be structural nodes of
that occurrence, with finite strictly increasing material coordinates A and B
and finite B-A. The descriptor cannot reverse an existing base edge or connect
different base segments.

The two distinct cuts must belong to this line and the same provider address;
their coordinates a,b satisfy `A <= a < b <= B`. Cuts need not coincide with
either base node. An interval covers exactly[a,b], with no extrapolation to A/B.
Each interval is an explicit declaration of continuity of the sampled scalar
display over that range. For every `sided_breaks` pair on the same line at s_b:

* An interval must not have `a < s_b < b`.
* If `b == s_b`, its upper endpoint must be the pair's left cut ID.
* If `a == s_b`, its lower endpoint must be the pair's right cut ID.

Consequently, an interval ending with the right limit, starting with the left
limit, or crossing the break is invalid even if all cuts share one native
element/provider and one base edge. These are exact ID/coordinate checks, not
inferred physical continuity. Core must split intervals at every known elastic
or N-basis discontinuity. Even when two
adjacent elements are physically continuous, this first mapping does not join
them in one interval. Different native owners always have different intervals.

The viewer must offer Mx, My, Mz and the full resultant. At each **valid cut** j,
derive the scalar for the selected display first:

```
component_j = moment[j].Mx, moment[j].My, or moment[j].Mz
resultant_j = hypot(moment[j].Mx, moment[j].My, moment[j].Mz)
```

The resultant includes all three components, including the component that may
represent torsion in a particular N convention. Use a numerically scaled hypot
operation. No extra norm is stored. It is the norm of the sampled physical
moment, invariant under that cut's orthonormal basis; local N bases may differ
between cuts.

For selected scalar `v_j` (one signed component or the per-cut full resultant)
and display material coordinate s in[a,b], the declared display value and
location are:

```
t = (s-a)/(b-a)
display_value = lerp(v_lower_cut, v_upper_cut, t)
u = (s-A)/(B-A)
display_position = lerp(base_pose[edge_start].position,
                        base_pose[edge_end].position, u)
```

Do not interpolate the three local components and then take their norm: those
components can be expressed in different current N bases, and norm is nonlinear
even when the bases happen to agree. For endpoint triples(3,4,12) and(-3,-4,-12),
the per-cut resultants are13 and13, so the midpoint resultant display is13;
component interpolation followed by norm would incorrectly display0. Signed
component modes still retain their own positive-to-negative interpolation.

This is an explicitly declared visualization approximation. It does not claim
the scalar is a newly evaluated provider value between cuts, or the polyline
location is the exact off-node current N position. The wire values are exact
samples at the declared cuts. Prefer showing exact cut values in tooltips and
plots, and identify the component and display interpolation in the UI.

Intervals on the same line/base edge cannot overlap in a nonzero material
range; touching endpoints are allowed. Different intervals are never implicitly
joined, averaged or smoothed. If the upper endpoint of one interval and the
lower endpoint of another share a coordinate but have separate sided cut IDs,
both values survive, including their jump. Their `sided_breaks` entry enforces
left-end/right-start use. A shared cut ID is allowed only at a continuous station
where the producer explicitly declared that same physical/provider address and
no sided break exists.
If either endpoint is unavailable for a frame, suppress that whole display
interval; do not fill it with zero or interpolate across the gap.
This suppression applies equally to component and full-resultant modes. A valid
zero triple gives a valid zero resultant; a clear mask with the same stored
positive-zero triple remains unavailable.

All those identity, ownership, ordering, range and non-overlap checks are
implementable using catalog integers and finite material coordinates. The
reader cannot prove the physical continuity declaration or native address; those
remain Core responsibilities. The initial renderer does not rotate local-N
moment arrows using an interpolated base quaternion. Actual off-node vector
glyphs would require a separately reviewed current-N pose product or additional
pose payload; silently approximating that basis is outside this layout.

Example: an element[0,1] and an element[1,2] can use cuts `(0,right)`,
`(.37,right)`, `(1,left)`, `(1,right)`, `(2,left)`. On the first base edge, two
intervals[0,.37] and[.37,1] share the continuous off-node cut ID. The second edge
has its own interval[1,2]. The two s=1 cuts are distinct and listed as a
left/right `sided_breaks` pair even if the base N pose is shared. Opposite signed
values there remain an explicit jump.

## Payload and typed API

Let C be the number of cuts and M=ceil(C/8). Every record is exactly
`80 + M + 24*C` bytes. No alignment padding is added.

| Offset | Bytes | Contents |
| --- | ---: | --- |
| 0 | 64 | Exact common structural stamp, including physical P and recorder ordinal |
| 64 | 8 | Positive source preparation generation |
| 72 | 4 | Common source phase0..3 |
| 76 | 4 | Reserved zero |
| 80 | M | Packed validity, LSB-first; bit j names cut j; unused high bits zero |
| 80+M | 24*C | Dense cut-order binary64 triples Mx,My,Mz in current N, N.m |

All scalar values must be finite. A valid row preserves caller bits, including
signed zero, and can be the true zero moment. An unavailable row is exactly
three positive binary64 zeros and has its validity bit clear. The mask is
authoritative; no magnitude or sign threshold determines validity. No absolute
value, norm-only replacement, sign alignment, component permutation or
normalization is performed. Encode/decode unaligned values bytewise or through
memcpy; C7/C8 starts double rows at81, C9 at82.

Kind7 uses stream5, flags0, layout1, raw encoding0, previous0. Stored and decoded
sizes equal checked `R*(80+ceil(C/8)+24*C)`. First ordinal is the next structural
ordinal; count/identity/stamps join every enabled population exactly. It appears
after enabled5/6 and before enabled8, then optional winch4 immediately followed
by pose1. Frozen whole-group admission, failure, pending recovery, opaque-reader
reporting and structural-only backlinks/index/completion rules apply unchanged.
Validate every independently detectable completed-record defect even if a
later structural counterpart is truncated.

Public addition, using the already accepted `SourcePhase`:

```cpp
struct ElasticMoment {
    std::array<double, 3> moment_N_Nm{}; // Signed current-N Mx/My/Mz about N.
    bool valid{};                      // False requires exact positive-zero triple.
};
struct ElasticMomentFrameView {
    Stamp stamp;
    std::uint64_t preparation_generation{};
    SourcePhase phase{SourcePhase::initialized};
    std::span<const ElasticMoment> moments;
};
// Add to FrameGroupView at the later slice's ABI/package boundary:
const ElasticMomentFrameView* elastic_moments{};
```

The new pointer/span borrow only for synchronous `append_frame(group)`. Exactly
C rows are required when the descriptor is enabled; missing/extra populations,
wrong generation/phase/stamp, nonfinite values or noncanonical unavailable rows
fail before any population is admitted/copied. Existing overloads cannot drop
this population. Adding the member requires a new explicitly identified package
and matching installed headers/libraries; it does not modify package0.2.5's
qualified F-only prefix or source snapshot during later implementation.

Core prepares every native elastic and N observation binding while inactive and
allocates one bounded row buffer. Sampling reads coherent accepted state into
fixed scratch, transports and rotates once, then publishes the complete group.
No native state refresh, extra solve, selected-output history, per-step lookup
allocation or duration-sized cache is added. This row width participates in the
existing64MiB aggregate raw budget and128MiB workspace accounting, including any
new retained recorder mapping tables at their actual capacities; there is no
new uncharged scratch/table exemption.

## Lifecycle and legitimate unavailability

The stream is present at every enabled structural capture. Its common phase is
initialized/prepared0, certified static1, transient initial2 or transient
accepted3, exactly as frozen. Prepared data is an initialized diagnostic, not a
static certificate. Initial capture can be omitted, making a positive accepted
step the first stored frame. An empty failed recording may declare the catalog
without containing any group.

For prepared0, if explicitly requested and admitted, evaluate the actual
initialized elastic state only after setup has produced coherent native carriers
and N bindings. Do not assert that every initialized moment is zero, and do not
label an unqualified provider result valid. Existing inactive initialized probes
support feasibility; the new capture route still needs its own qualification
before Core enables phase0. It is valid to support certified/accepted captures
without phase0. If an unproved phase is requested, reject capture setup with its
provider/cut/phase diagnostic; do not omit a declared batch, silently downgrade
the phase or clear masks to conceal the unsupported request. Static1
uses the exact certified state/time/load factor. Transient2/3 uses the exact
accepted state and coherent native rotations, with no trial/rejected state
published and no resampling from a later state.

The wire admits explicit legitimate unavailability, including an all-unavailable
frame; it is not numerical failure recovery. Recommended first Core production
policy: admit only qualified cuts and require every admitted cut to be valid at
every captured phase. Phase capability is checked for the requested capture
plan, not by requiring phase0 or every enum value in every recording. An
unexpected stale generation, native failure, rejected
rotation chart or unavailable N basis fails the complete capture rather than
quietly clearing its mask. A later deliberate predeclared-unavailability policy
needs a specific owner decision and retained provenance, without changing the
canonical binary distinction between unavailable and true zero.

## Concrete independent fixture plan

The following are proposed fixtures, not newly generated evidence. Start from
one structural base edge with material endpoints0 and1, one native provider
address covering[0,1], and explicit F/winch/base group partners as appropriate.
For the in-element discontinuity use four cuts: `c0=(0,right)`,
`cL=(.5,left)`, `cR=(.5,right)`, `c1=(1,left)`. Both middle cuts have the same
native element and eta0. Declare `sided_breaks=[{left_cut_id:cL,right_cut_id:cR}]`
and display intervals[c0,cL] and[cR,c1]. Independent row oracles assign different
signed triples to cL and cR even though their material coordinate is identical.

| Case | Authored facts | Required result |
| --- | --- | --- |
| Valid in-element sided break | Same provider/base edge for all four cuts; upper-left and lower-right endpoint IDs exactly as above. | Two separate display intervals; no value/basis averaging or joining at.5. |
| Valid continuous shared cut | One cut `cM=(.37,right)` and intervals[c0,cM],[cM,c1]; `sided_breaks=[]`; no opposite-sided duplicate at.37. | Shared exact cut value allowed; no forced discontinuity. |
| Valid native-interface pair | Adjacent native elements with left/right cuts at their common endpoint, listed as one sided pair; each interval remains in its own provider. | Retain two IDs even when base geometry or moment values coincide. |
| Opposed signed endpoints | On one valid continuous interval, lower=(3,4,12), upper=(-3,-4,-12), masks set. | Full-resultant endpoints and midpoint all13; component midpoints0. Taking norm after component interpolation must fail the oracle. |
| Third-component contribution | Valid triple(0,0,-12), including at a cut whose model calls its third component torsional. | Full resultant12, not0; signed Mz remains-12. |
| Valid zero and unavailable | A valid positive-zero triple and a clear-mask canonical positive-zero triple in distinct rows; also preserve valid signed-zero components. | Valid zero resultant is visible; an interval touching unavailable suppresses its component/resultant display, without zero filling. |
| Valid phase subset | A certified static group, or a positive-step first transient capture, using the unchanged common phase/stamp rules. | Valid without an initialized-phase record or required capability metadata. |
| Wrong upper-side ID | Replace[c0,cL] by[c0,cR], retaining the declared pair. | Reject despite increasing coordinates, same provider and same base edge. |
| Wrong lower-side ID | Replace[cR,c1] by[cL,c1]. | Reject for ending/starting on the wrong physical limit. |
| Crossed break | Use[c0,c1] while the pair at.5 remains declared. | Reject because the open interval contains a declared break. |
| Reversed endpoint order | Use[cL,c0], or reverse `left_cut_id` and `right_cut_id`. | Reject decreasing coordinates or incorrect pair sides respectively. |
| Missing or malformed pair | Remove the pair while both opposite-sided cuts remain; duplicate a pair; reuse a cut in another pair; pair different coordinates/lines; or pair one ID with itself. | Reject each catalog independently. |
| Ordinary range/ownership violations | Overlap two intervals over a positive range; use cuts on another line/provider; map across base segments; or use a decreasing/zero-length base edge. | Reject using catalog identities and finite coordinate checks alone. |

Core setup tests separately request an unproved phase0 or an unqualified
component/provider/phase combination and require rejection before recording.
They do not add a wire-membership negative: a valid common phase0 record remains
wire-representable. No unsupported setup request may become a silently missing
declared batch or a fabricated zero component.

Reader fixtures additionally cover masks C7/C8/C9, nonfinite components,
noncanonical unavailable triples, exact generation/phase/stamp joins and torn
later partners. Core numerical tests separately use an actually supported
Euler/Rayleigh or FPM configuration with an off-node lever arm, a bent/twisted
state and **nonzero torsion**, including a nonzero current-N Mz. Independently
compare its same-wrench P-to-N shift and current-N projection against all three
components; verify the full resultant includes that torsional contribution.
Cover differently oriented current N bases and the actual requested supported
phases rather than claiming one provider's phase proof for another. The viewer
fixture must assert the per-cut norm operation order directly; metadata or
coincident world geometry cannot substitute for that arithmetic oracle.

## Owner integration and qualification gates

Core owner:

1. The accepted first scope is Euler/Rayleigh and FPM only within their actually
   supported components and phases; graph and other action providers are
   deferred. Confirm the exact provider/phase/component capability matrix and
   actual native law/projection provenance, plus source/effective fingerprints
   available without inventing identifiers. A generic backend name is no proof.
2. Confirm that the capture plan can prepare a separate elastic provider and
   matching current-N observation for each cut without requiring a scientific
   scalar request. Retain exact owner/native generation and stable binding keys.
3. Confirm side resolution at native interfaces, off-node N-reference jumps
   inside an element, and inward ends. Produce the explicit `sided_breaks` pairs
   and continuity intervals, rather than relying on reference-triad equality to
   prove equal moments. An in-element jump must end left and restart right;
   continuous interior stations may share one cut ID.
4. Qualify all three current-N components for each admitted phase, including the
   off-node lever-arm/bent-twisted nonzero-torsion case, exact-once same-wrench
   transport and both sided limits. Initialized snapshots require separate
   proof only if phase0 is requested. Support for actual certified/accepted
   callbacks does not depend on admitting an unproved initialized phase.
5. Confirm default endpoint-per-native-element sampling and how optional
   additional off-node cuts are selected in this separate capture plan. Output
   selection syntax/reference-to-assembly policy remains a separate owner task.

Viewer owner:

1. Implement the agreed Mx/My/Mz and full-resultant coloring/plots with explicit
   base-edge interpolation. Derive `hypot(Mx,My,Mz)` at each valid cut before
   scalar interpolation. No extra norm storage or off-node vector glyph payload
   is proposed in this slice.
2. Confirm support for multiple nonoverlapping intervals on one base edge,
   touching independently sided endpoints, different signs and missing endpoints
   without smoothing, averaging, bridging or zero filling. Exact cut tooltips
   must retain original signed values and native/material provenance.
3. Confirm selectable Mx/My/Mz labeling in current N, signed component scales and
   a nonnegative full-resultant scale including all three components. Component
   meaning cannot be inferred as flap/edge/torsion from model family.
4. Confirm bounded expanded edge/GPU storage in addition to common reader logical
   payload limits. Material-coordinate interpolation is a viewer display choice,
   never a new physical output or a redefinition of base geometry.

Outputs/independent fixture owner:

1. Review the small explicit mapping tables, sided-pair proof and row/API names,
   then freeze exact catalog integer/string/range/overlap/break checks before
   creating accepted fixtures.
2. Author independent asymmetric positive/negative triples, distinct left/right
   values at shared coordinates, off-node cuts, true signed zero and canonical
   unavailable triples, C7/C8/C9 mask boundaries, mixed enabled groups and
   complete-defect-versus-truncated-counterpart recovery cases.
3. Include rejected native address/range/side contradictions, repeated sided
   addresses with different owners, reversed/cross-owner/cross-provider mappings,
   cross-segment links, overlapping ranges, unavailable-mask garbage, nonfinite
   components and source identity mismatches. Retain pinned unaware and partly
   aware reader results separately from full moment-semantic validation.

Main, Core and viewer accepted this moment-only cut/mapping layout.
Independent bytes and actual Core providers must qualify its implementation;
historical F or J07 evidence does not qualify the moment stream.
