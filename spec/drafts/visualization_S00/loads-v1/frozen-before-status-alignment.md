# Applied physical load density layout1

Accepted wire contract, 20 November 2026. Main, Core, viewer and (for planes)
Inflow approved the layout and clarified scope. The common contract in
POSE_OPTIONAL_OVERLAYS.md remains unchanged. The source review draft is retained
verbatim under spec/drafts/visualization_S00/loads-v1/DECISION.md.
Implementation and numerical/renderer qualification remain separate gates;
review questions below are retained as owner integration checks, not unresolved
wire choices. No new commit, push or qualification is implied by this freeze.

The product is the physical **world force density in N/m of current F** that
was integrated and transferred for one completed weak-coupling interval. It is
not generalized native force/torque, integrated force, aerodynamic free moment,
or a freshly evaluated endpoint diagnostic. Store actual source F world XYZ
beside each vector. A loads-only request need not enable F pose recording or
display. It still requires the qualified physical F provider and its exact
inline source points. Recording the interval's start is unnecessary.

## What the maintained producer actually exposes

`FluidLogicalNode` carries attachment index, exact material coordinate,
`FluidNodeSide` (`continuous`, `left`, `right`) and a `physical_side` continuity
token. `PreparedFluidModelNodes` deduplicates consumers only when their physical
identity and geometry agree. Its separate `consumers()` retains each original
consumer index, logical-node index, native owning element and resolved
`LimitSide`. A continuous logical point may therefore have two distinct native
consumers with different owners/limits. Logical physical side must not be
replaced by the resolved left/right address used by F display samples.

Current `WeakFluidDriver` entries each own one model and one attachment. The
model manifest supplies `instance`, `model_id`, `run_id`, model type/version,
layout revision and attachment/node metadata. `AttachmentDimensions` supplies
the actual attachment ID, target occurrence path and authored/defaulted source
spanline ID. Current model definitions are **run-scoped**; the target flexible
occurrence is not the model's defining scope. An enclosing assembly must not be
invented from the target path. A future assembly-scoped provider needs a real
scope binding and an explicit extension review.

`publish_packet()` completes `coordinator.write_loads()`, expands logical force
and moment densities to their consumers, integrates them using the matching
source motion, and arms native six-number slices. The new overlay must snapshot
logical `force_world` and matching `entry.pF` before either is overwritten; it
must not recover force density from `held_Q`. The existing `applied_provenance()`
identifies the last **armed** packet, not necessarily a completed interval.
`publish_observation()` can replace logical motion/products at the accepted end.
`discard_step()` clears pending eligibility but does not restore an already
armed packet to the last completed interval. These are reasons for a separate
completed-overlay publication, not reasons to change numerical coupling.

## Self-contained catalog

`applied_loads` has required exact integer `kind=6`, `stream_id=4`,
`layout_version=1`, `encoding="raw_f64_le"`, positive
`source_preparation_generation`, `product="completed_weak_force_density"`,
`force_unit="N/m"`, `position_unit="m"`, `vector_frame="world"`, and nonempty
`coordinators`, `models`, `attachments`, `stations` arrays. Empty selection omits
the descriptor. The first qualified route has one coordinator, ID 0; every model
references it. Every array count and total native-consumer count is at most
1,000,000 and is additionally limited by catalog/frame/workspace admission.

Identifiers/counts are JSON integer tokens, never Boolean or floating tokens;
their values fit INT64_MAX and host extents. Dense `id` means array ordinal.
All required strings are nonempty valid UTF-8 without NUL; numeric metadata is
finite. Extra descriptive metadata cannot alter the meanings below.

| Array | Required fields and constraints |
| --- | --- |
| `coordinators` | Dense `id`; `scope={"kind":"run","run_id":...}` with run ID equal to the base catalog; `coupling="fixed_step_weak_held"`. No process pointer is serialized. First implementation admits exactly one record. |
| `models` | Dense `id`; existing `coordinator_id`; positive `instance` copied from `FluidModelInstanceIdentity`; original `model_id`, `run_id`, `model_type`, `model_version`; positive `layout_revision`; positive `logical_node_count`. `(coordinator_id, instance)` and `(run_id, model_id)` are unique in the current run-scoped route. |
| `attachments` | Dense `id`; existing `model_catalog_id` (integer reference into `models`); `model_attachment_index` and original string `attachment_id`; existing flexible `target_occurrence_id`; original `spanline_id`; `source_geometry_fingerprint`, `effective_geometry_fingerprint`, `model_nodes_fingerprint`, `approximation_policy`; positive `native_element_count`; `model_node_begin`, positive `station_count`, and `station_begin`. |
| `stations` | Dense `id`; existing `attachment_catalog_id` (integer reference into `attachments`); actual model-wide `logical_node_index`; finite nonnegative `material_coordinate_m`; `physical_side` in `continuous/left/right`; unsigned `physical_side_token` copied from the prepared geometry; nonempty `native_consumers` array described below. |

Numeric references use the `_catalog_id` suffix to distinguish them from the
original model and attachment string identities.

Each attachment owns a contiguous station range, ordered by its actual manifest
logical-node indices. Attachment ranges partition `stations`; each represented
attachment includes its complete logical population. `logical_node_index` must
equal `model_node_begin + local station ordinal`, and its range must lie inside
the model's `logical_node_count`. Selected attachment ranges of a model cannot
overlap. `(model_catalog_id, model_attachment_index)` and `(model_catalog_id, attachment_id)`
are unique. The initial Core route has one attachment per selected model with
`model_attachment_index=0`, `model_node_begin=0` and its actual attachment ID
`spanline`; this is a producer fact, not a replacement for reading the manifest.

Each `native_consumers` entry is `{consumer_index, native_element, limit_side}`.
`consumer_index` is the actual index in that attachment's prepared station-request
buffer; `native_element < native_element_count`; `limit_side` is resolved
`left` or `right`. Consumer indices are unique across the attachment and dense
from zero; preserve every consumer of each represented logical node. Repeated
native elements/limits at different original consumer indices are not silently
deduplicated. The exact logical address `(attachment_catalog_id, material coordinate,
physical_side_token)` is unique. A contradictory logical side for that address
is invalid; coordinate equality alone is not a join rule. The physical token may
be zero and is meaningful only under its attachment and fingerprints.

The catalog preserves `PreparedFluidReference::source_fingerprint()`,
`effective_fingerprint()`, `approximation_policy()` and
`PreparedFluidModelNodes::geometry_fingerprint()` separately. Keep the actual
source spanline ID; never substitute `F` for another authored source name.
Core must join the exact `AttachmentDimensions::instance_path` to the physical
occurrence catalog, validate prepared identities, and preserve the current
refusal of overlapping full-span active attachments on one occurrence.

There is **no required reference to `fluid_poses.lines` or its display samples**.
That population may be disabled or sampled differently. No nearest-station
mapping, material interpolation, inferred continuous segment, or structural
sample substitution is permitted. This first overlay draws point arrows; a
connected load curve would need explicitly reviewed continuity metadata.

## Exact record

The chunk has optional flags 0, raw encoding 0, layout 1, previous 0, and all
common group count/ordinal/CRC rules. For attachment `a`, let `K_a` be its station
count, `M_a=ceil(K_a/8)`, and `T_a=96+M_a+48*K_a`. A record is exactly

`S_load = 80 + sum_a(T_a)` bytes.

The first 80 bytes are the accepted common prefix: exact structural stamp,
positive common preparation generation, accepted source phase, reserved zero.
Attachment blocks follow in catalog order, with no alignment padding. Block
`a` starts at `80 + sum_{b<a}(T_b)` and contains:

| Relative offset | Type | Meaning |
| ---: | --- | --- |
| 0 | u32 | Interval status: 0 unavailable/no completed interval, 1 completed applied interval |
| 4 | u32 | Reserved zero |
| 8 | u64 | Source accepted mechanical step ID |
| 16 | f64 | Exact source accepted/evaluation time in seconds |
| 24 | u64 | Completed accepted end step ID |
| 32 | f64 | Exact held-packet endpoint / accepted end time in seconds |
| 40 | u64 | Source `FluidModelEvaluationId.publication` |
| 48 | u64 | Source `mechanical_revision` |
| 56 | u64 | Source `state_revision` |
| 64 | u64 | Source `geometry_revision` |
| 72 | u64 | Source `environment_revision` |
| 80 | u64 | `PreparedStepId.sequence` |
| 88 | u64 | Held `WeakFluidPacketIdentity.packet` |
| 96 | byte[M_a] | LSB-first validity mask; unused high bits zero |
| 96+M_a | f64[6*K_a] | Station-major `px,py,pz,fx,fy,fz`, source world metres then world N/m |

Both evaluation and prepared-step generation are the common generation. Core
must validate equality before constructing the view; this encoding does not
drop either check. Source evaluation time equals source accepted time bitwise,
and held endpoint time equals the accepted end time bitwise, hence each pair
uses one wire field. Coordinator identity comes from the model's catalog
reference; no address, pointer hash or capture-plan counter substitutes for it.

For status 1, require transient-accepted common phase, positive end/sequence/
packet/publication/revision values, nonnegative source step ID, all IDs <=
INT64_MAX, `source_id < end_id`, and finite `source_time < end_time`. Copy the actual source and completed
end accepted IDs; the end ID and time equal the common structural stamp, with
bitwise time equality. Do not reconstruct either accepted ID from a prepared
sequence, publication, packet or revision. Prepared-step sequence, evaluation
publication, every revision and held packet are opaque positive identity
copies. No derived, arithmetic or monotonic relationship between them, to the
accepted IDs, or across frames is part of this wire contract. Initialization,
evaluation reuse and unsuccessful staging are separate activities.

| Identity category | Candidate validation boundary |
| --- | --- |
| Common preparation generation | Positive exact identity, shared under the frozen common contract; Core checks source and step generations before encoding. |
| Accepted source/end IDs and times | Exact producer-owned accepted interval facts; source ID/time precedes end ID/time; endpoint ID/time matches the structural stamp. Apply the saved-transient ordering clarification below. Neither accepted ID is derived from an opaque token. |
| Prepared-step sequence | Opaque positive transition identity, copied exactly from the applied prepared-step token. Do not derive or equate it to accepted IDs, publication, packet or revisions; do not impose cross-frame monotonicity. |
| Evaluation publication and mechanical/state/geometry/environment revisions | Opaque source identities. Preserve exact supplied values and their defined validity/ranges; do not derive them from accepted IDs, equate distinct revision fields, or impose cross-frame monotonicity. |
| Held packet ID | Opaque complete applied-publication identity. Preserve its supplied positive value; do not derive it from a step/publication/revision or impose cross-frame monotonicity. |

Attachments on the same synchronous coordinator describe the same accepted
start and completed accepted end, including exact time bits. Core confirms
that its actual single-coordinator producer supplies a common complete 96-byte
interval prefix, including the copied opaque provenance fields. This layout
therefore requires byte-identical prefixes across that producer's attachments
within one frame. Equality follows from this explicit Core guarantee, not
from an arithmetic relationship between token fields. It creates no cross-frame
ordering rule for opaque identities. A future coordinator/provider without
that guarantee requires review before using this producer profile.

All six sample scalars are finite. A valid row preserves supplied binary64 bits,
including signed zeros; zero force with mask set is a real physical result.
An invalid row is exactly six positive-zero values with its mask bit clear.
The first qualified Core route publishes all stations valid for a completed
interval. The layout can represent a legitimate predeclared unavailable row;
nonfinite products, failed provider calls, identity mismatches or partially
staged data must fail the capture instead of manufacturing invalid rows.

For status 0 the entire 96-byte interval prefix, mask and all rows are **zero
bytes**. The common 80-byte stamp/generation/phase remains real and nonzero where
required. Prepared, static and transient-initial frames have status 0; the initial
force-balance packet is not a completed interval, even when its physical loads
are nonzero. This layout's qualified positive transient captures require
status 1. Unsupported applicable models/modes are reported by Core during
setup; no fabricated model/station catalog is emitted to represent an absent
provider. An explicit request with no eligible provider fails setup. Status 0
is permitted only at prepared/static/initial phases. A
positive weak capture with a missing or stale completed packet fails capture;
it cannot publish status 0 or relabel an older packet. Empty failed files may
still have zero groups.

### Proposed accepted-interval ordering clarification

**Awaiting viewer/Main confirmation before freeze.** For every status-1
interval, require `source_id < end_id` and `source_time < end_time`. If a
previous saved **transient accepted structural stamp** exists, including a
saved transient initial step 0, additionally require:

- `source_id >= previous.state_id` and `source_time >= previous.time`.
- If `source_id == previous.state_id`, source time and previous time must have
  exactly the same binary64 bits, including the sign of zero.
- If `source_id > previous.state_id`, require `source_time > previous.time`.

Use the previous saved transient structural stamp, not a previously saved
source point, arbitrary earlier diagnostic, or the previous recording ordinal.
A prepared display timestamp supplies no interval bound. The first positive
capture without any saved transient initial/prior frame has no earlier-stamp
bound; it still obeys its own strict source-before-end checks. These rules do
not require `end_id=source_id+1` and do not constrain opaque sequences,
publications, packets or revisions.

This matches the documented weak-driver use of an accepted-start time and a
subsequently checked positive native interval. No contradiction with the
inspected producer headers was found. Core still owns the exact accepted-ID
mapping; the clarification is not an inference from prepared-step tokens.

Capture sparsity changes none of these rules. A capture at accepted step 10
contains the actual source at step 9 and endpoint 10 even if the previous saved
frame was step 5. It is neither a five-step aggregate nor a reference to saved
frame 5. Inline origins remain the step-9 points when an F overlay depicts step
10. Across captured positive frames, completed end IDs/times follow the
structural accepted-state ordering; gaps are allowed. A reader must not require
consecutive captured IDs or the presence of the source frame. It applies no
monotonicity requirement to opaque sequence, publication, revision or packet
identities.

## Typed Outputs API

```cpp
enum class AppliedIntervalStatus : std::uint32_t {
    unavailable = 0, completed = 1
};
struct AppliedSourceEvaluation {
    std::uint64_t publication{}, mechanical_revision{}, state_revision{};
    std::uint64_t geometry_revision{}, environment_revision{};
    double time_s{};
};
struct AppliedInterval {
    AppliedIntervalStatus status{AppliedIntervalStatus::unavailable};
    std::uint64_t source_accepted_id{};
    AppliedSourceEvaluation source;
    std::uint64_t end_accepted_id{};
    double end_time_s{};
    std::uint64_t prepared_step_sequence{}, packet{};
};
struct AppliedLoadSample {
    std::array<double,3> source_position_world_m{};
    std::array<double,3> force_density_world_N_per_m{};
    bool valid{};
};
struct AppliedAttachmentFrameView {
    AppliedInterval interval;
    std::span<const AppliedLoadSample> samples;
};
struct AppliedLoadsFrameView {
    Stamp stamp;
    std::uint64_t preparation_generation{};
    SourcePhase phase{SourcePhase::initialized};
    std::span<const AppliedAttachmentFrameView> attachments;
};
// Add one trailing default-null member to FrameGroupView:
const AppliedLoadsFrameView* applied_loads{};
```

These are borrowed, synchronous views, not exported Core/Chrono structs. The
attachment span exactly matches the catalog, and each nested span matches its
station count. Reject missing/extra views, bad status/masks/values, mixed
provenance and every failed stamp check before copying any group population.
Fields unused for unavailable intervals must contain canonical positive-zero or
integer zero values in the caller, rather than being silently repaired. Encode
all f64 values bytewise because attachment masks leave rows and later prefixes
potentially unaligned. Legacy overloads reject any newly enabled overlay.

## Core publication and bounded storage

Prepare opt-in fixed capture mappings and **candidate plus completed** physical
payload buffers. Charge their actual capacities and interval metadata to Core's
prepared budget, separately from Outputs raw workspace. Disabled capture adds
no buffers, evaluations or active-path work. A reasonable minimum is two sets
of six f64 values per selected logical node plus validity and provenance; actual
struct padding, capacity, mapping arrays and view arrays must be charged.

At `publish_packet`, snapshot source logical F positions and `force_world` from
the same coherent publication used by transfer. Stage into candidate storage
and bind the current `accepted` source ID and candidate packet/step identity.
Keep the snapshot unpublished through transfer, native arm and native stepping.
After checked native success and successful coordinator acceptance at the exact
authoritative endpoint, publish by a **nonfallible swap** of complete candidate
and completed storage. There must be no fallible partial publication after this
commit. A failed density writer, integration, arm, native solve, acceptance or
discard leaves the preceding completed overlay unchanged and revokes the new
candidate. Capture at a later accepted state must not relabel that older
completed overlay with the new state. Accepted-end observations cannot overwrite
either completed values or their source points/identity.

In Outputs, include `S_load` in the existing aggregate raw group calculation
before any raw allocation or file creation. Retain only checked per-attachment
station counts/offsets, e.g. two u64 per attachment; do not retain JSON, consumers
or station identity maps during capture. Add a separately reported
`overlay_reference_bytes` charge for actual retained table capacity, leaving
the existing winch `physical_reference_bytes` meaning unchanged. The effective
raw budget subtracts both charges and the fixed 2048-byte scratch allowance
from the unchanged default/hard 128 MiB workspace cap. Raw groups remain default
4 MiB / hard 64 MiB. Checked products include `R*S_load` and the full mixed group.
All common atomic commit, partial recovery, unknown-stream and retained-history
bounds apply unchanged. The viewer must separately account its stored load
values, validity/provenance and GPU arrow data.

## Owner integration and qualification gates

1. **Core accepted:** complete scoped populations, actual model/attachment and
   spanline identities, source/effective/model-node fingerprints, all consumer
   maps, and original model-wide node indices independent of display sampling.
2. **Core accepted:** exact generation/source/end identities and bitwise clocks;
   the actual single coordinator supplies the common 96-byte attachment prefix.
   Prepared sequence, publication, packet and revisions remain opaque. No
   arithmetic or monotonic token relation is admitted by this agreement.
3. **Core accepted:** candidate/completed separation through arm failure,
   discard, observations and failed steps. Checked native success followed by
   successful coordinator acceptance permits the nonfallible publication swap.
   Existing held-packet comments are not evidence of numerical rollback.
4. **Core/Main accepted:** loads-only requires the physical F provider and inline
   points, but no F recording. Status 0 is limited to prepared/static/initial;
   missing/stale completed packets at positive weak steps fail capture. An
   explicitly requested feature with no eligible target fails setup.
5. **Viewer accepted; integration qualification remains:** accept source-time origins and explicit interval labels when F/N
   depicts the accepted end; arrows must use world directions and an N/m scale.
   Confirm no hidden interpolation or requirement for a recorded source frame,
   and bound CPU history plus GPU upload capacity separately. Viewer/Main must
   also confirm the proposed accepted-interval ordering clarification above.
6. **Main freeze pending:** after viewer confirmation and the F handoff, freeze
   the 96-byte per-attachment prefix, canonical zero
   unavailable payloads, numeric reference naming, nested borrowed API and new
   retained-layout charge before independent fixtures or implementation begin.

Minimum future evidence: two distinct active attachments; continuous logical
points with two native consumers; genuine sided cuts; F disabled; differing
source/end positions; sparse capture omitting source frames; valid zero and
signed components; initial unavailable interval; failed partially staged and
successfully armed/unaccepted candidates; no extra model/wind evaluations;
capacity rejection before file creation; mixed F/load/winch grouping and torn
group recovery. Compare against the actual pre-transfer physical packet, not
selected endpoint diagnostics or integrated native loads. This layout is
neither implementation evidence nor numerical qualification.

## Source paths inspected

Paths below are relative to the sibling `serene-core` repository, read-only on
20 November 2026. They identify maintained producer code, not frozen evidence.

- `includes/serene/core/simulation/fluid_model_views.hpp`:
  `FluidModelEvaluationId`, `PreparedStepId`, `FluidLogicalNode`, `FluidNodeSide`.
- `includes/serene/core/simulation/fluid_model.hpp`:
  `FluidModelInstanceIdentity`, `AttachmentDimensions`, `FluidModelManifest`.
- `includes/serene/core/simulation/detail/fluid_model_nodes.hpp` and
  `src/simulation/detail/fluid_model_nodes.cpp`: prepared physical deduplication,
  consumer ownership/limits, logical geometry and geometry fingerprint.
- `includes/serene/core/simulation/detail/prepared_fluid_reference.hpp` and
  `src/simulation/detail/prepared_fluid_reference.cpp`: source/effective
  fingerprints, native addresses and physical continuity token.
- `includes/serene/core/simulation/detail/weak_fluid_driver.hpp` and
  `src/simulation/detail/weak_fluid_driver.cpp`: actual physical load/motion,
  publication, staging, acceptance, discard and observation lifecycles.
- `src/simulation/detail/fluid_model_preparation.cpp`:
  current run-selected one-model/one-attachment preparation and exact targets.
- `src/simulation/detail/fluid_models.cpp`: coordinator initial/publication and
  accepted-candidate identity checks.
- Theory `implementation_prompts/serene_3d_visualization/04_schemas_visualization_requests_prompt.md`
  and `06_core_applied_aero_load_prompt.md`: scope and intended acceptance work.
