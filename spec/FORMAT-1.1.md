# Serene native outputs 1.1 — joint freeze candidate

Status: **NORMATIVE CONTRACT ACCEPTED**. The N11–N14 joint contract was accepted in `serene-theory/validation/native_output_extensions_contract_acceptance.json`, SHA-256 `84147366e8534197fa1063db769b0060e3966aebfdc4f35757906bf342248c6b`. This publication changes only this status paragraph from the frozen candidate. Producer and reader implementation qualification is recorded separately; the accepted 1.0 contract in `spec/FORMAT.md` remains unchanged.

This document has three parts. Part A defines exactly the three extension features. Part B reproduces the accepted 1.0 baseline verbatim. Part C supplies precise joint clarifications and source/producer rules. For version 1.0, Part B alone governs its byte and logical contract; no 1.1 feature or changed admission is silently backported. For version 1.1, Part B applies except for the explicit version, feature, ownership, provenance, diagnostic and profile amendments in Parts A/C. Part C resolves wording in Part A. No other field or physical layout changes. The root/bootstrap version must agree. Static multiple-level and multi-run capability already fit 1.0.

The exact baseline SHA-256 is `1ac724b78d1b61fc289dd68489a181dbd92bd9f8f08d1684c5b27aa8503ff66c`; its independent corpus manifest remains `910ab1b766e79e015a78d4060db31d84b2b59eb265c904d1b7361493a7ddbf7a`. The copied baseline beside this file retains identical bytes. Unknown optional JSON fields never authorize an otherwise unknown binary owner/reference. All 1.0 limits and checked arithmetic remain applicable unless explicitly stated.

## Part A. Exact extension layout

Notation: A(dtype,[shape]) means the existing {section,dtype,shape} descriptor;
P[N] means the existing UTF-8 pool descriptor for N strings. All referenced
arrays own distinct sections, including zero-length arrays. JSON scalars obey
Part B strict integer/finite/string rules. Absent f64 evidence is positive0;
absent integer evidence is0; presence flags are u8 values0/1. No new directory
kind, header size, directory size, frame size or compression encoding is added.

1. Version/feature admission and ownership

Version1.0: exactly its existing five base feature tokens; unchanged semantics.
Version1.1: all five base tokens plus a nonempty subset of exactly:
  run-diagnostics-1
  provenance-sources-1
  timeseries-zfp-fixed-accuracy-1
Writer emits1.0 if none is used. Bootstrap and root version must agree. Unknown
versions/features are unsupported; missing mandatory tokens and body/token
mismatches are invalid_contract. Duplicate feature tokens remain malformed.
An advertised supported feature is a file-wide promise, including unselected
runs. Declaring only known features never permits arbitrary unknown sections.

Open checks header/directory/root/global catalogs and support for every token.
It does not parse all run/source/trace metadata merely to prove token use.
Selecting a run validates that run's extension envelopes and referenced trace
metadata; reading provenance opens requested source descriptors lazily. Selecting
a trace validates its complete fixed column shapes, hashes and bounded column
scans without retaining the whole trace. verify_all traverses every source/run/
trace, validates body/token agreement globally, and proves no aliases/orphans.
A missing-token body in an unvisited run may therefore be detected only on run
selection or verify_all. Document this limit instead of claiming eager global
semantic validation. Unknown declarations themselves fail immediately at open.

New owning metadata edges, only under their feature:
  root -> source metadata, from provenance.sources.metadata_section;
  run -> diagnostic trace metadata, from diagnostics.traces[].metadata_section.
Existing root -> run metadata remains. Source/run/trace metadata IDs are distinct,
cannot equal root, and cannot refer cyclically to each other. A metadata owner
owns its data descriptors. Source-index associations are NON-OWNING logical
references, never second owners of source metadata or its binary sections.
Full verification budgets the section-ownership bitmap PLUS a separate
source-used bitmap (ceil(S/8), at most125,000bytes when S=1,000,000), or equally
charged per-source flags. Section ownership alone cannot detect an unreferenced
source catalog entry that happens to own valid sections. The memory claim is
retained root/catalog and explicitly requested scopes plus bounded traversal
scopes; lazy handles do not imply retaining every referenced catalog.

2. provenance-sources-1: root and source catalog

When enabled, root contains provenance and MUST NOT contain old inputs, logs
or context branches. format.sections retains inputs/logs/context state codes,
but they summarize the corresponding source roles across all run associations.
All-disabled -> disabled; all included -> included; no source included/partial
with at least one unavailable -> unavailable; every other mixed/incomplete retained
population -> partial. Association/source states provide the detailed truth.
Included empty sources remain included; an included empty log and an unavailable
log therefore aggregate to partial. Zero associations for a role mean disabled.
Open validates these aggregate state
codes syntactically; verify_all recomputes their semantics from every associated
source. A selective open does not claim to have verified every aggregate state.

provenance = {
  schema:1,
  authored_input_sha256:A(u8,[32]),
  original_source_index:u64,
  sources:{source_index:A(u64,[S]), kind:A(u8,[S]),
           metadata_section:A(u64,[S])}
}
S is1..1,000,000 subject to the existing directory and metadata budgets.
source_index is0..S-1. kind0=document,1=JSONL. metadata_section entries are
distinct kind1 metadata sections. The digest is always present independently
of retained input bytes. First bundle producer requires one common original
authored input. original_source_index names a kind0 source; included bytes must
hash to authored_input_sha256. Disabled/unavailable bytes do not erase identity.
Original-source role0 allows included/disabled/unavailable, never partial.
A valid1.0 spool with partial original-input provenance is therefore outside
the initial bundle-import capability: reject it explicitly as unsupported for
this merge, without relabeling it malformed, changing its state or discarding
the partial evidence. Other document roles retain their existing partial state.
No arbitrary filesystem lookup or source reopening is encoded in the archive.

Document source metadata:
  {source_schema:1, source_index:u64, kind:0, document:<existing document>}
JSONL source metadata:
  {source_schema:1, source_index:u64, kind:1, state:u8,
   records_jsonl:A(u8,[K]), record_offsets:A(u64,[Q+1]),
   complete_record_count:Q, trailing_fragment_bytes:u64,
   dropped_count_present:u8, dropped_count:u64,
   schema:<nonempty text>, info_json:<existing document>}
For JSONL disabled/unavailable state only source_schema/source_index/kind/state
are present. Included/partial uses all listed fields with existing offset,
fragment,1MiB-record,4GiB-source and strict complete-JSON-object rules. Each log
source remains separate, including its exact bytes and trailing fragment.

Every run metadata has provenance = {
  references:{role:A(u8,[K]), source_index:A(u64,[K]), order:A(u64,[K])}
}
Roles0=original,1=effective,2=overrides,3=resources,4=execution_context,5=log.
Exactly one entry for each role0..4, with order0; role0 equals the root original
source index. Their source kind is0. Role5 has0..64 entries, each kind1, with
order0..L-1 expressing original source order. Thus K=5+L and K<=69. Physical
reference-array order is role then order; duplicate(role,order) is invalid.
Disabled/unavailable role documents still have a source descriptor and explicit
state. Multiple runs may reference the same source ID; do not deduplicate solely
by equal bytes when scope/meaning differs. Every source must be referenced by a
run or be the root original source. Verify no unused source catalog entries.

These roles deliberately cover the present four input documents, execution
context and original logs only. Future source roles require another feature.
The global catalog has no large inline JSON list: typed arrays are bounded by
caller metadata budget before retention; producer budgets a catalog proportional
to S and run count, plus one source/run working set. No constant-memory-in-run-
count claim is made. Formats may reach local budget before protocol ceilings.

3. run-diagnostics-1: independent domain and trace envelope

Every run has diagnostics={status:u8,traces:[{kind:u8,metadata_section:u64},...]}
when the feature is used. The array contains at most5 entries, unique kind,
in authored trace-request order; it is empty for unrequested diagnostics.
At least one trace entry somewhere in the file is required to advertise this
feature; empty successful populations still have a configured trace entry.
Run diagnostic status:0 complete,1 partial,2 failed,3 disabled,4 unavailable.
This is independent of numerical_status, observation_status and logging_status.
Complete means complete capture of the declared eligible population during the
execution that actually occurred; it does not mean the numerical target was met.
Partial means incomplete optional/mixed recording; failed means required runtime
recording failed; disabled means no trace requested; unavailable means no
requested trace could start, e.g. preparation failure. Empty successful
populations are complete. Compute aggregate status in this priority order:
  no trace entries -> disabled;
  any required trace with capture_status capacity/capture_error -> failed;
  all traces complete -> complete;
  all traces unavailable/not_started/source_unavailable -> unavailable;
  otherwise (partial traces or complete+unavailable mixture) -> partial.
Source capability rejection before execution remains preparation_failure plus
unavailable diagnostics, not a fabricated runtime diagnostic_failure.

Add run_outcome7=diagnostic_failure only under this feature. A required capture
failure may terminate with numerical_status stopped_before_target or retain
target_reached if already attained. Never report it as numerical/observation/
logging failure. Optional partial diagnostics can coexist with run_outcome
completed. If another terminal cause occurred first, preserve that primary
cause while diagnostics.status still reports its independent failure.
If failure follows numerical target certification, retain those certified
numerical facts and the complete selected prefix. Required diagnostic failure makes the run ineligible for published certified transfer. Optional partial typed traces alone do not. Preserve the physical packet/certification facts independently; this format never relaxes current
certified-transfer producer/consumer checks to make that case exportable.
An unavailable selected catalog may have info plus diagnostics (and indexed
provenance if enabled); it still has no channels/requests/values/selected axes.

Trace kind codes/populations:
  0 transient_accepted_steps: successful native advances, excluding initial0;
  1 static_stage_attempts: each stage attempt including initial/retried attempts;
  2 static_accepted_levels: every certified equilibrium including identity0;
  3 static_newton_iterations: published outer Newton candidates/terminal failures;
  4 run_events: typed lifecycle records for this authored run.
Kind0 means the driver's accepted advance population, distinct from HHT inner
iterations or any hidden adaptive increments; it does not claim to observe all
internal solver trials.
Static trace2 is independent of selected-history initial include/exclude policy.
There is no transient rejected-step/residual/inner-Newton/timing trace in this
proposal: current backend does not publish a qualified population for it.

Each trace metadata is {
  trace_schema:1, kind:u8, run_index:u64,
  requirement:u8, availability:u8, capture_status:u8,
  record_capacity:u64, record_count:R,
  eligible_count_present:u8, eligible_count:u64,
  omitted_count_present:u8, omitted_count:u64,
  capture_error_count:u64,
  first_missing_ordinal_present:u8, first_missing_ordinal:u64,
  failure_code:<text>, columns:{...}, [criteria:{...}], [summaries:{...}]
}
requirement0=optional,1=required. availability uses existing codes0 included,
2 unavailable,3 partial. capture_status0=complete,1=not_started,2=capacity,
3=capture_error,4=source_unavailable. Allowed combinations are exactly:

  capture_status | availability | rows/columns | count evidence | first missing
  complete       | included     | R>=0,yes    | known,R and0   | absent
  not_started    | unavailable  | R=0,no      | unknown or0/0  | absent
  capacity       | partial      | R=capacity,yes| unknown or R+O/O,O>=1 | present,R
  capture_error  | partial      | R>=0,yes    | unknown or R+O/O,O>=1 | present,R
  source_unavailable|unavailable | R=0,no      | unknown or O/O,O>=0 | optional,0

Here known count evidence means both eligible_count_present and
omitted_count_present are1 and eligible_count=R+omitted_count; unknown means
both flags0 with canonical0 placeholders. Presence flags must agree in this
initial contract. Losing counter precision/overflow makes both totals unknown,
never an invented zero total. A known first missing ordinal remains meaningful
even when the final total eligible/omitted population is unknown. For a
source_unavailable trace, present first_missing means an actual eligible record
was known to be missed: if totals are known, O>=1. Absent first_missing with
known totals requires O=0. It may remain absent with unknown totals when the
source never established whether any eligible record existed.

Complete/not_started/capacity/source_unavailable have capture_error_count0;
capture_error has a positive capture_error_count. Complete has empty failure_code;
capacity/capture_error/source_unavailable have nonempty source-owned failure_code;
not_started may carry a source-owned preparation/skip reason token or empty text. Capacity admission
failure before execution is not_started/unavailable, not a runtime overflow row.
Unavailable traces have no criteria or summaries. Prepared empty included or
partial traces have real empty column sections. All configured capacities are
positive; R<=capacity and R<=2^40, with checked extents/materialization budgets.

On first optional capture failure, freeze the immutable complete prefix and
continue counting all later eligible records as omitted where possible. Record
capture errors separately from capacity omissions. Required admission/orderly
stop follows the schema/core contract. No summary or success outcome conceals
a missing record. A solver-specific first-missing identity can follow only
after source semantics are qualified; prefix ordinal is sufficient initially.

4. Fixed trace columns (all lossless, arrays of R unless stated)

All recorded traces have record_index:A(u64,[R]), exactly0..R-1.
Kind0: accepted_step:A(u64,[R]), time_s:A(f64,[R]),
       nonlinear_iterations:A(u64,[R]).
Complete-prefix steps are exactly1..R;
times are finite and strictly increasing. These
facts publish after native/finite/clock validation and before observer/logger
effects. No absent residuals, timing or per-step linear-solve counts are encoded.

Kinds1 and3 share bounded status_names:P[Q] (1<=Q<=64) and
status_index:A(u32,[R]); every index<Q. Names are stable source-owned status
tokens, not native enum ordinals. They are descriptive: success does not alone
establish certification. Part C defines the generic dictionary rules and a separately qualified core producer table.

Kind1: attempt_id:u64[R] (exactly1..R), factor:f64[R],
       certified:u8[R], rolled_back:u8[R],
       accepted_level_present:u8[R], accepted_level_id:u64[R],
       newton_record_count:u64[R], status_index:u32[R], plus metric columns below.
newton_record_count counts solver-published Newton/failure rows during the
attempt, independent of retained kind3 rows; it is not an exact count of all
native solves or line-search evaluations. accepted_level_present iff certified.
rolled_back true means confirmed restoration; false makes no claim that
restoration was unnecessary. A retry is a later attempt, never an overwritten row.

Kind2: accepted_level_id:u64[R] (exactly0..R-1),
       attempt_id:u64[R], factor:f64[R], plus metric columns below.
Every row is certified. Factors may repeat/decrease; they are dimensionless
continuation parameters, not timestamps. Static force evaluation time belongs
in criteria, distinct from this axis.

Kind3: attempt_id:u64[R], iteration_id:u64[R], factor:f64[R],
       row_kind:u8[R] (0 Newton candidate,1 terminal exception/failure-only),
       certified:u8[R], rolled_back:u8[R], status_index:u32[R],
       plus metric columns below. iteration_id is one-based for candidate rows;
       zero is allowed only for a terminal row with no iteration identity.
Rows are publication order, not all internal line-search trials. Per-attempt
iteration IDs must follow core's qualified publication rules, not an invented
contiguous-count invariant. Rolled-back rows remain diagnostic evidence.

Static metric columns: metric_valid:A(u8,[R]) with bits0..5 respectively, and
  force_residual_N:f64[R]          world translation residual max |component|;
  moment_residual_Nm:f64[R]        node-local moment residual max |component|;
  constraint_translation_m:f64[R] physical attachment gap max |component|;
  constraint_rotation_rad:f64[R]  relative SO(3)-log max |component|;
  increment_translation_m:f64[R]  applied translation increment max |component|;
  increment_rotation_rad:f64[R]   applied local rotation increment max |component|.
Unknown mask bits are unsupported; invalid fields are positive0, valid fields
finite and nonnegative. Core MUST publish validity at measurement boundaries:
current certify initializes zeros and may exit early, so values cannot establish
validity. Every kind2 row and every kind1/kind3 row with certified=1 require
all six bits (metric_valid=0x3f). Kind1 uses final published
certification/failure measurements, not stale values from an earlier attempt.

Static criteria is bounded JSON with norm="block_max_abs_component",
evaluation_time_s, force_absolute_N, moment_absolute_Nm, relative_tolerance,
force_scale_N, moment_scale_Nm, force_limit_N, moment_limit_Nm,
constraint_translation_limit_m, constraint_rotation_limit_rad,
increment_translation_limit_m, increment_rotation_limit_rad.
evaluation_time_s is an actual finite signed physical time; negative values are
permitted. Tolerances/scales are finite and nonnegative; limits are positive.
Force/moment limits are the actual computed absolute+relative*scale values.
No mixed-unit combined norm or inferred transient stopping criterion is added.

Kind4: sequence:u64[R] (strictly increasing), event_names:P[E] (1..64),
       event_name_index:u32[R], severity:u8[R], identity_valid:u8[R],
       time_s:f64[R], accepted_step:u64[R], accepted_level:u64[R], attempt_id:u64[R].
Valid-mask bits0=time,1=accepted step,2=accepted level,3=attempt; absent fields
use canonical0. Step/level IDs do not share one untyped identity field. Scope is
this authored run via trace.run_index. Part C defines the severity vocabulary and qualified core event conventions; no parsing formatted log messages.
Process-scoped events remain explicitly scoped original JSONL evidence for now,
not fabricated or duplicated per-run trace rows. Typed process traces would
need a separately agreed root population, outside this minimal draft.

Optional summaries permits only these exact measure columns by kind:
  kind0: nonlinear_iterations;
  kind1: newton_record_count and the six static metric columns listed above;
  kind2: the six static metric columns;
  kind3: the six static metric columns;
  kind4: none (summaries must be absent).
No IDs, times, factors, status/mask fields or arbitrary plugin metrics qualify.
For each permitted field, the summary shape is
{population:"retained_valid_records", fields:{<column>:{count:u64,
 min:<column scalar type>,max:<same>,mean:f64,
 median_present:u8,median:f64}}}. count=0 uses canonical0 values and median
absent. Missing field summary means not recorded. Integer min/max stay exact
integers; mean is a stable rounded binary64 reported statistic. Population is
only retained rows where this field is valid, independent of omitted records.

If median_present1, count must be positive. Exact median means exact rank
selection of that population, followed by binary64 round-to-nearest, ties-to-
even representation: for odd count select the central order statistic; for
even count take the exact-real arithmetic mean of the two central statistics
before the single specified rounding. This includes u64 statistics above2^53;
do not pre-round the central integers or overflow their sum. An odd binary64
statistic is already representable; an even mean may require rounding or
subnormal underflow. Do not substitute a median of chunks or a sketch.
Median computation needs full retained values plus separately budgeted post-run
scratch. Initial producers may leave every median absent with positive0
placeholders; no median implementation work is required just to expose traces.
Partial-trace summaries never claim the full eligible population.

5. timeseries-zfp-fixed-accuracy-1: same profile/verification columns

Keep existing profiles columns and shapes; extend mode from0=reversible to
1=fixed_accuracy. Shared identity stays ZFP1.0.1 revisionf40868a6..., codec5,
little-endian, serial,64-bit words, ROUND_NEVER; no DAZ/tight-error/OpenMP/CUDA.
Mode0: tolerance_present0, requested/effective positive0, mode_code2176,
tuple(1,16658,64,-1075), canonical96-bit full short header.
Mode1: tolerance_present1, finite positive requested_tau,
e=floor(log2(requested_tau)) derived from binary64 bits, -1073<=e<=843,
effective_tau exactly2^e, mode_code2177+(e+1074), tuple(1,16658,64,e),
canonical96-bit full short header. Query/check actual pinned codec agreement.
No clamp, long expert header, default expert tuple at e=-1074 or automatic
reversible replacement. Out-of-range positive profile is unsupported_profile.
Static run profiles must all remain mode0. Axes/diagnostics never use lossy frames.

No new tuple columns: tuple is uniquely determined by the qualified mode/code/
tolerance fields. Validate every profile and row_profile_index, including empty
rows, unused profiles and raw frames. At least one mode1 profile must exist in
the file when this token is declared; selected-run checks enforce local profile
rules, while verify_all proves global feature use. A requested fixed profile
retains mode1 even if all its frames become raw or no values were recorded.
Existing raw/ZFP encoding0/1 and canonical77-byte frame records are unchanged.

Per frame verification keeps max_abs_error:f64, rms_error:f64,
verified_count:u64 and bitwise_equal:u8. Count equals frame count. Exact profile
or actual raw encoding requires +0,+0,count,1. Compressed mode1 requires finite
0<=rms_error<=max_abs_error<=requested_tau; bitwise_equal1 additionally requires
zero errors. bitwise_equal0 may have zero errors, e.g. a signed-zero change.
Max error is rounded upward to binary64; RMS is a scaled-sum-of-squares rounded
statistic. Producer compares exact-real binary64 differences without slack,
using the fixed33-limb2^-1074 comparator defined in Part C.
Verification columns assert producer measurements; reader cannot independently
recover the originals. Every compressed candidate must pass before size-based
raw fallback. Failed verification fails export; a reversible retry is explicit.

Schema/core binding is: scalar override beats
homogeneous (group,request) rule, duplicate/unselected/unit-incompatible rules
reject, every rule validates even if superseded, unmatched scalars are reversible.
No binding or physical unit is reconstructed by parsing scalar names in outputs.


## Part B. Unchanged 1.0 baseline

# Serene native outputs 1.0 — normative custom-container specification

Status: the authoritative format text for G0C acceptance. Acceptance/freeze is governed by Serene main's coordinator receipt identifying this exact file hash; this status line does not claim implementation or test acceptance. The format uses `.serene`, storage `native_archive`, and target `serene::outputs_archive` following the user's explicit container decision. There is no obligation to read the earlier HDF5 delivery. All HDF5 evidence remains historical.

This self-contained specification retains the logical run, channel, request, clock, outcome, provenance, profile and verification semantics from the preceding `FORMAT.md` (historical SHA-256 `ea598cdf51a3013417b80bcc4312fc294f72b70aadff66ab59ba8d17520b14ee`), while replacing its physical container. Appendix A states the retained logical requirements; sections2–7 define their sole current physical representation. No HDF5 bytes, paths, library or plugin is used.

## 1. Design constraints

The file consists of a fixed bootstrap, ordinary data/metadata sections, then a fixed-record section directory. It contains exactly one root JSON metadata section and one separate JSON metadata section per run. Numerical values, axes, string pools, frame indexes, exact provenance bytes and JSONL records are binary sections. JSON is for small scalars and section descriptors; it never contains numerical histories or base64 original documents. Large catalogs retain bounded UTF-8 pools and integer offsets, avoiding millions of JSON objects and preserving current selective-reader behavior.

Only little-endian wire encoding and little-endian binary64 hosts are qualified initially. Explicit endianness identifies/rejects unsupported representations; it does not silently authorize a byte-swapping codec. Native C++ padding, `size_t`, enums and native struct layouts never define file bytes. Directory/header digests detect accidental corruption and are not authentication.

The model/view/codec APIs and exact shared ZFP snapshot remain unchanged. The archive target changes from `serene::outputs_hdf5` to `serene::outputs_archive`; no compatibility target is required. A new build/install prefix must prove the production archive target has no HDF5 dependency.

## 2. Fixed bootstrap: 128 bytes

All integer fields below are unsigned little-endian. Hashes are raw 32-byte SHA-256 values. Byte offsets are from the start of the file.

| Offset | Size | Field | Initial required value / semantics |
|---:|---:|---|---|
| 0 | 8 | magic | bytes `53 45 52 45 4e 45 00 00` (`SERENE` followed by two NUL bytes) |
| 8 | 2 | version_major | 1 |
| 10 | 2 | version_minor | 0 |
| 12 | 4 | endian_marker | integer `0x01020304`, wire bytes `04 03 02 01` |
| 16 | 4 | bootstrap_bytes | 128 |
| 20 | 4 | directory_record_bytes | 64 |
| 24 | 8 | file_bytes | Exact physical file size, no ignored tail |
| 32 | 8 | directory_offset | Offset of the final directory |
| 40 | 8 | directory_count | Number of section records, positive |
| 48 | 8 | root_section_id | ID of the root JSON section, in `[0,directory_count)` |
| 56 | 8 | flags | Exactly 1: finalized; all other bits unsupported |
| 64 | 32 | directory_sha256 | SHA-256 of exactly `64*directory_count` directory bytes |
| 96 | 32 | bootstrap_sha256 | SHA-256 of the first 96 bootstrap bytes |

A readable final file MUST satisfy `directory_offset + 64*directory_count == file_bytes == actual file extent`, with checked arithmetic. `directory_offset >= 128`. Reject truncated, incomplete or inconsistent bootstraps before following offsets. Initial writers reserve this bootstrap, write all content/directory, then write the completed bootstrap before checked flush, close and no-clobber publication. A partially patched temporary is never a supported recovery format.

Version 1.0 is an application file version, independent of input schema and package versions. This is a new unreleased physical format; earlier HDF5 test artifacts use a different magic and are not readable by this format. The initial reader accepts version1.0 exactly. Any unsupported major or minor, or an unsupported required feature, yields an unsupported-format/feature error, not an attempted legacy fallback. No generic forward minor compatibility is promised.

## 3. Section directory: 64 bytes per record

| Offset within record | Size | Field |
|---:|---:|---|
| 0 | 8 | section_id |
| 8 | 4 | kind |
| 12 | 4 | flags |
| 16 | 8 | byte_offset |
| 24 | 8 | byte_count |
| 32 | 32 | sha256 |

`section_id` is exactly the zero-based directory row ordinal. Records are also in physical section order. `flags` is exactly 0. The first section starts at offset128; each next offset equals its predecessor's offset+count; the last section ends at `directory_offset`. There is no padding, overlap, hole, alias, external extent or hidden trailing data. Zero-length sections are allowed, share the current end offset, and have SHA256(empty). JSON metadata sections are nonempty. Section offset+count and all products must be checked against the actual file extent using subtraction-safe bounds before reads or allocation.

Kinds are: 1=JSON metadata, 2=u8 bytes, 3=u32 array, 4=u64 array, 5=IEEE binary64 array, 6=canonical77-byte frame records, 7=i8 array, 8=i32 array. No other kind is initially supported. Element counts derive from a JSON descriptor's checked shape; divisibility/byte extent must agree with the kind. Signed integers are two's-complement. Numerical histories occur only inside kind2 raw/ZFP payload sections, not an unframed kind5 matrix.

Every section digest covers exactly its section bytes. The directory digest anchors its order, type, extent and section hashes. Root and selected run metadata are verified before parsing; catalogs/axes/indexes are verified before exposing them. A payload section's whole-section digest is deferred to `verify_all` so selective reads remain selective; each selected frame is independently checked against the canonical frame hash. Large documents/logs likewise permit bounded streaming; full verification eventually checks their complete section digest. A reader must not imply it has verified an unvisited payload section.

A reader may retain the directory only after checking its entire requested memory cost against a configured metadata budget. A bounded tiled scan plus on-demand directory lookups is also valid. No directory count becomes an unchecked allocation. There is no stored pathname or executable reference to follow.

## 4. JSON metadata and typed descriptors

Metadata sections are strict UTF-8 JSON objects, without BOM, duplicate keys, NaN/Infinity or trailing non-whitespace. Container nesting is at most16, each metadata section is at most1,048,576 bytes, and each section has at most16,384 syntactic values and16,384 object members. The root object has container depth1; each nested object/array increments it, while scalar children do not add a container level. Each primitive or container, including the root, counts as one syntactic value; object keys count as members rather than values. Every key/value pair adds one member. Every array element adds one array-entry count. A numerical JSON token is at most128 ASCII bytes. All string keys/values must decode to Unicode scalar values, without lone surrogate escapes or U+0000. Duplicate keys are compared after JSON escape decoding. Readers preflight byte length, nesting, token length and value/member counts before general-purpose materialization, and enforce a retained-metadata budget independently of those protocol ceilings. For a portable conservative admission rule define B=encoded metadata byte length, S=total decoded Unicode scalar count in keys and string values, V=value count, M=object-member count, A=array-entry count and D=maximum container depth. Check the charge `5*B + 4*S + 128*V + 128*M + 16*A + 256*D` with overflow-safe arithmetic against the available metadata budget before parsing; an implementation may use a demonstrably stricter estimate. Other retained directories/catalogs/pools and parse workspaces must be charged separately. This is a bounded admission contract, not a claim about exact OS allocator or RSS bytes. A stricter local budget yields `resource_limit`, not a malformed-file claim. The same1 MiB/depth16/value-member16,384/numeric-token128 preflight limits, conservative admission charge and strict decoded-Unicode/duplicate-key rules apply to each effective-request JSON object and complete JSONL object parsed from binary byte pools. A complete log record's1 MiB bound includes its LF/CRLF bytes. Original input/context/info JSON document sections instead remain exact raw bytes and are not limited by the kind1 metadata ceiling. Their parsed string values must contain valid Unicode scalar values, but are not identifier metadata: escaped U+0000 is permitted, and the metadata1 MiB per-string/no-NUL rules do not apply. Their separate finite local document parsing and admission limits still apply; unescaped control bytes remain invalid JSON. Explicit parsed-document access must preflight before materialization under finite configurable local limits: default encoded materialization16 MiB, container depth128, values/members1,048,576 each, numeric token128 bytes, and a separate64 MiB parser/object admission budget using the same conservative charge. Stricter caller limits are allowed; these local document limits can reject an otherwise protocol-valid retained document with resource_limit. The document parser/object budget applies per explicit parse operation. Returned parsed documents and yielded JSONL objects become caller-owned; retaining many returned objects is outside the archive session budget. Effective-request/JSONL parse admission is temporary and released after validation or ownership transfer, while retained raw pool/catalog bytes remain charged. Byte/hash streaming does not parse or assemble it. `verify_all` applies these limits to included-document parsing and can return resource_limit rather than claiming arbitrary4 GiB JSON is parsed in constant memory. Partial document bytes verify as declared evidence without complete-JSON parsing.

Unsigned integer fields accept only JSON integer tokens in their declared range; a float or string is not an integer field. JSON floating fields must represent finite binary64 values. Writers emit enough decimal digits for binary64 round-trip; absent floating evidence is canonical positive0.0. Integer negative zero is forbidden in metadata; floating -0.0 is rejected where positive zero is required. Boolean JSON values are not integer flags. Existing numerical flags remain integer0 or1. Text scalar values obey the earlier strict UTF-8/no-NUL identity rules and per-entry1 MiB limit. Harmless unknown optional JSON metadata fields may be ignored within the bounded JSON contract. They cannot authorize a new binary-section reference. Any future extension adding sections or changing typed semantics requires a specifically supported required feature, even under a future minor version.

An array descriptor is exactly an object containing `section` (u64 ID), `dtype` (one of `u8,u32,u64,f64,frames77,i8,i32`) and `shape` (array of unsigned integer dimensions). The section kind must agree; checked element-count times exact element size equals the directory byte_count. Rank-zero arrays are not used: small scalar fields live directly in metadata JSON. A rank-one empty descriptor still references an actual zero-length section. A descriptor never permits reinterpretation through a different type. `shape` is at most rank2 in this initial schema.

A string-pool descriptor contains `count` (u64), `bytes` (u8[K] array descriptor), `offsets` (u64[count+1] descriptor) and optional `present` (u8[count] descriptor). Presence is required only for the optional-string fields defined below. Offsets start0, are nondecreasing, endK and have exact lengths; each slice decodes under the text rules. Optional absent strings have flag0 and an empty slice. No string decoding can overrun the verified pool bounds. Effective request JSON retains its exact UTF-8 slices rather than being silently normalized.

A document descriptor contains `state` and, for included/partial state, `media_type` plus `bytes` (u8[K] descriptor). Disabled/unavailable states have only state. The exact document digest is its referenced section's directory hash; no hexadecimal duplicate is required in metadata. Included JSON is parsed on explicit access under document budget; partial JSON is evidence, never a complete parsed document. Writer callers continue to guarantee included document/complete JSONL validity before saving; the archive writer copies and hashes bounded tiles and does not become an active JSON logger/parser.

## 5. Root JSON

Required fields are `format`, `results`; optional fields `inputs`, `logs`, `context` occur exactly according to format section state. `format` contains the same required logical scalar fields as the preceding contract: name=`serene.outputs`, version_major/minor=1/0, file_id, created_utc, writer_name/version, write_state=`complete`, required_features, sections. `required_features` is a JSON array of at most1024 unique nonempty tokens (rather than a binary string-pool descriptor). Initial tokens must contain exactly the five unique mandatory tokens `serene-container-le-1`, `json-descriptors-1`, `utf8-pools-1`, `frames-77-le-1`, `zfp-5-le64-serial-1`; omission, duplication or unknown tokens are rejected. Root JSON version agrees with the bootstrap. Inputs/context aggregate states and logging retention/runtime independence remain unchanged.

`results` contains `index` with these fields: `run_index` (u64[N], exactly0..N-1), `run_id` (strings[N], unique nonempty), `run_name` (strings[N] with present flags), `kind` (u8[N],0 timeseries/1steady_state), and `run_metadata_section` (u64[N]). Each latter entry names a distinct kind1 JSON metadata section, and none may equal bootstrap.root_section_id. No HDF5 group_path is retained. Catalog order is the run order; display-name lookup still rejects ambiguity. First production writes N=1, while independent fixtures may contain multiple runs without claiming application bundle orchestration.

A run JSON object contains `info` and, when prepared, `channels`, `requests`, `values`, plus `time` or `load_levels` array descriptor and optional actual `step_index`/`level_index` descriptor. The earlier required field names/cardinalities and absence/outcome rules are retained exactly. An unprepared catalog has `info` only. Run metadata section IDs are not solver identities and are never used as user-facing run IDs.

Inputs/context children keep their earlier document names. Logs contain `records_jsonl` (u8[K]), `record_offsets` (u64[R+1]), complete_record_count, trailing_fragment_bytes, dropped_count_present/count, schema and info_json. Their old separate `sha256` member is omitted: the records section directory hash is authoritative. Complete records remain individually bounded1 MiB strict JSON objects; trailing fragments are retained but never parsed as complete events. Clean LF boundaries do not imply complete capture.

## 6. Run metadata mapping

`info` scalar fields map directly to JSON numbers/strings; its solver_configuration, sampling_configuration and execution are document descriptors. Their meanings remain unchanged: accepted_count excludes initialization, last accepted and last recorded are independent, sparse output clocks are actual, unknown evidence uses explicit absence, numerical/logging/observation outcomes are independent. Last recorded value equals the final actual axis when present. Requested bounds, finite values, canonical absence and the allowed outcome combinations are unchanged.

`channels` keeps channel_index, scalar_id, group_id, request_id, component_path, unit and request_index. Integer columns become typed array descriptors; string columns are string-pool descriptors. `requests` keeps group_id,request_id,schema_version,construction_profile string pools and effective_state scalar. Included effective_json remains a string pool with effective_sha256 u8[R,32]. Run selection checks each effective request's exact digest and strict complete object validity under the per-entry bound. This per-request digest is deliberately retained even though the complete pool is also a hashed section.

`values` keeps shape (JSON array of exactly2 u64 values), dtype=`float64`, axis_names (JSON array of two strings), frame_schema=1, nominal_frame_values, row_profile_index (u32[outer]), profiles, frame_index (frames77[F]), payload (u8[P]) and verification. Its old index_sha256 property is omitted: the frame-index section directory hash covers the exact canonical record concatenation. Profiles remain columnar typed/string-pool descriptors with exactly the earlier qualified ZFP1.0.1 profile, canonical short mode2176 and required tuple. Verification remains max_abs_error f64[F],rms_error f64[F],verified_count u64[F],bitwise_equal u8[F], with exact-mode +0,+0,count,1.

Frames remain the exact77-byte records with fields at offsets0/8/16/24/32/40/41/45. byte_offset is relative to that run's payload section, not the file start. They cover logical rows [C,T] or[L,C] and payload bytes exactly, with no gap/overlap. Raw fallback, tails, signedzero/subnormal fidelity, finite checks, no solver identity fabrication, no interpolated samples, prepared-empty [C,0]/[0,C] and selection-empty[0,0] remain unchanged. The codec snapshot and all raw/ZFP bit semantics are reusable without modification.

## 7. Reference graph, validation and limits

The bootstrap names the root kind1 section; root results.index names each run kind1 section. JSON array/string/document descriptors name their data sections. A schema-owned data section must not be reused by two logical fields, even when empty. No metadata section references itself or another metadata section except the root's explicit run_metadata_section column. No run metadata can name the root or another run metadata as a byte section. Initial files have no unreferenced sections. Future extensions adding sections must have an explicitly supported required feature and a qualified version; unknown JSON comments alone cannot authorize hidden payloads. Selected-run validation does not claim cross-run alias/orphan validation for unvisited runs. Full structural verification traverses all known descriptors and proves exact section ownership; opening one run does not require all other run metadata to be parsed. A metadata-budgeted section-ID bitmap needs at most128 KiB at the protocol directory ceiling; verification can process and release one run metadata object at a time rather than retaining every run catalog.

Protocol limits retained: runs65,536; channels/requests/profiles1,000,000 per run; text1 MiB; aggregate catalogtext256 MiB perrun/global; original document/log4 GiB; completeJSONLrecord1 MiB; frames2^32; dimensions2^40; extents2^63-1; decodedframevalues1,048,576; encodedframebytes16,777,216. Added directory count maximum1,048,576 and JSON metadata limits above apply independently; maxima do not promise that every maximum can occur simultaneously. Default reader retained metadata budget64 MiB, document materialization16 MiB, explicit numeric materialization256 MiB; any stricter caller budget is checked before dependent allocation. A reader must account for directory, parsed metadata, string pools, offsets and retained catalogs, not only byte read tiles.

Open validates fixed header/file size, directory hash/order/extents/types, root sectionhash/JSON and bounded globalcatalog. Selecting a run checks its metadata/catalognumericsections/axes/profile/verification/index hashes and scans its full index in bounded tiles, without reading numerical payload. Selective reads seek only required frame extents, verifyframeSHA and decode through the existing bounded codec; numerical return arrays publish only according to documented atomicity. `verify_all` validates every run and ownership, every sectiondigest including unselected numericalpayload, every selected-valuefinite check, complete JSONL objects and included-document parsing under checked budgets. Partialdocument evidence verifies bytes without being rejected merely for declared truncation.

The file session is immutable. All lazy handles expire on close. Each accessed directory descriptor is rechecked for ID/kind, count/extent and subtraction-safe file bounds before allocation or I/O, even after a cached directory scan. Unaligned wire arrays are read through explicit LE loads or copied into properly aligned native storage; casting a file byte-buffer pointer to a native array is forbidden. Even after a successful cached structural index scan, each subsequently accessed frame record is cheaply rechecked for count<=B, encoding/profile, encoded-byte limit and subtraction-safe payload/file bounds before allocation or I/O. Concurrent mutation remains unsupported; cached validation never licenses unchecked reads. An open OS handle, file-size query and digests do not guarantee OS-level immutability or authenticity; callers must keep the file unchanged for the session. Hashes cover exact JSON UTF-8 bytes, with no canonical reserialization requirement. Unknown required features/profile/version/endian, directory kind or reserved flags are unsupported. Missing required feature tokens, unfinished bootstrap flags=0, impossible types/bounds/references, overflow and inconsistent file size are malformed (`invalid_contract`). A nonzero unknown bootstrap flag bit is unsupported, even if finalized bit0 is also unset. Declared protocol-count/byte/complexity ceilings and stricter local budgets yield `resource_limit`; the latter may reject an otherwise protocol-valid archive. Hash failures are integrity errors (`verification_failure`); operating-system failures are I/O errors. A known kind that disagrees with a descriptor is malformed, not an unsupported codec. Unknown profile values are unsupported_profile. No unsupported container is retried as a legacy format. No checksum constitutes authentication, and a custom parser still needs malformed-input/fuzz tests.

## 8. Writer lifecycle and test migration

PreparedExport owns copied catalogs and bounded gather/decode/index/copy resources; Codec is prepared separately before active simulation and counted once in the aggregate reservation. All borrowed source owners remain immutable/alive through publication or abandoned retry. ArchiveSession starts only after active simulation and checked physical logging closure. It writes to a unique temporary sibling, checks every seek/write, flush and physicalclose, then no-clobber publishes. Numerical failure can yield a complete valid prefix archive; storage failure cannot masquerade as successful publication. Replacement is initially unsupported; rename alone makes no powerlossdurability promise.

Known committed shapes determine frame counts and exact index sizes. A writer may reserve each frameindex region, stream a payload, backfill records in bounded batches, and compute indexSHA incrementally over canonical record order. This avoids retaining all compressed frames or a full frameindex in memory. It then emits bounded JSON descriptors/directory and completes the bootstrap. It must not rely on sparse-file holes as valid unwritten content: all reserved bytes are actually filled before publication. Seeking/backfill is post-gate and has explicit fault seams. Record fileextent and cumulative write/seek metrics in qualification, separately from final filebytes.

Retain prior model/codec tests and independent bitpattern fixtures; regenerate container envelopes independently from production code. Positive corpus must cover both orientations, raw/ZFP mixtures, Unicode/scopedIDs, optional/partial metadata, empty/unprepared/failedprefix, exactaxes and >onecopytile documents. Negatives include header/directory truncation, digest mismatch, hugecounts/overflow, unknownkind/endian/version/features, extents/gaps/overlap/tail, malformed/duplicate/deepJSON, wrongtypedshapes, badrefs/aliases/orphans, canonicalindex/header failures, nonfinitevalues and invalidoutcomes. C++ and Python independently inspect the same bytecontract; no HDF5 fixture author/parser is required for G0C. Historical HDF5 tests are not claimed as custom-container qualification.

## 9. Input and package interface

Replace required storage discriminator `native_hdf5` and matching payload with `native_archive`; keep explicit `disabled` and `temporary_csv`. `archive_file` is required and ends `.serene`. Preserve all other agreed native options/defaults: explicit reversible codec, metadata retention enums, rawfallbackties, framebounds, requiredbudget, no_clobber. Keep artifactdirectory output.path, --output, native-only --archive and their exact relativepathresolution; do not introduce a second authority. Remove oldspelling without backwardcompatibility. Coordinator-approved input wire is3.0-wip.4, constructionprofile1.0; the archive version remains1.0. Keep certified manifestv2/static_result transfer independent and externalarchivereferences outside its confinedfilesindex.

CMake components model,codec,archive must be independently installed/relocatable. Model/codec public API/source identities remain pinned. Archive has no HDF5dependency; Python reader uses standardfileI/O plus the same codec extension, with h5py removed from requiredruntime. Rebuild and qualify Release/Debug /MD and /MDd in fresh prefixes; Linux platform claims still require actual builds. Preserve prior dependency/source/license receipts rather than rewriting them into a new-format success.

## Appendix A. Complete logical requirements

Names with scalar u8/u32/u64/i8/i32/f64 notation in this appendix specify validated JSON scalar types/ranges. Array notation specifies section descriptors of the exact dtype/shape under section4, and strings[N] means its binary string-pool descriptor. Document names mean document descriptors, never executable content. Arrays are ordered and all cardinalities are exact. All flags are integer0 or1. Unavailable numerical evidence uses integer0 or binary64 positivezero, neverNaN. Strict JSON excludes duplicatekeys and nonfinite literals. Section states are0 included,1 disabled,2 unavailable,3 partial.

### A.1 Root fields, run identities and actual axes

The format JSON object has the following required fields. Required feature tokens and optional root branch presence follow section5.

| Member | JSON type and rule |
|---|---|
| name | string, exactly `serene.outputs` |
| version_major, version_minor | unsigned integer scalars, 1 and 0 |
| file_id | string, lowercase UUID with hyphens |
| created_utc | string, UTC RFC3339 `YYYY-MM-DDTHH:MM:SS[.fraction]Z` |
| writer_name, writer_version | nonempty string; package identity, not protocol capability |
| write_state | string, exactly `complete` in a readable final file |
| required_features | JSON string array[F], unique nonempty tokens, at most1,024 entries |
| sections | object with inputs/logs/context integer state codes |


The root file UUID is lowercase with hyphens. Created UTC uses valid calendar date/time and RFC3339UTC syntax. Inputs/context are included when every enabled required child is included; partial when at least one included/partial child exists and another is unavailable/partial; unavailable/absent when no childbytes are available. Producer-disabled roots are disabled/absent. Logs retention does not disable runtime logging.

Run index IDs are unique/nonempty authored IDs; duplicate present displaynames are allowed and name lookup rejects ambiguity. Each run metadata section is indexed once and agrees in kind and run_id. The initial writer emits one run; the wire can describe multiple runs for later orchestration. `info.run_id` equals the global catalog run_id.

Prepared runs include info,channels,requests,values and actual time f64[T] for transient or load_levels f64[L] for static. Optional step_index u64[T] or level_index u64[L] stores actual accepted solver identities only, strictly increasing with declared convention. Stored row ordinal is inherent and must not masquerade as solveridentity. Times are finite and strictly increasing; load parameters are finite but mayrepeat/decrease and are nottime. Axis sections preserve exact binary64 bytes.

### A.2 Required info and outcome rules

All following fields are required. Absence is expressed through presence/state fields, never NaN or fabricated zero evidence.

| Field | Type / semantics |
|---|---|
| run_id, assembly_ref | text; assembly_ref may be empty for an unprepared preparation_failure/skipped/cancelled outcome |
| kind | u8, agrees with catalog |
| run_outcome | u8: 0 completed, 1 numerical_failure, 2 observation_failure, 3 logging_failure, 4 preparation_failure, 5 skipped, 6 cancelled |
| numerical_status | u8: 0 not_started, 1 target_reached, 2 stopped_before_target, 3 failed (solver failure only) |
| observation_status | u8: 0 complete, 1 failed, 2 disabled, 3 not_started |
| logging_status | u8: 0 complete, 1 partial, 2 failed, 3 disabled, 4 unavailable |
| failure_stage, failure_code, termination_reason | text, empty if none; producer-native bounded code converted after active gate |
| catalog_state | u8: 0 included, 2 unavailable; distinguishes no selection from unprepared catalog |
| committed_count | u64, T or L; distinct from capacity and solver acceptance |
| accepted_count | u64, successful solver increments/levels, excluding initialization; a successful native advance counts even if later logging/observation fails |
| last_accepted_present, last_recorded_present | u8 presence flags |
| last_accepted_value, last_recorded_value | f64, actual time/load parameter when present; exactly +0 otherwise |
| requested_bounds_present | u8 presence flag |
| requested_begin, requested_end | f64; requested physical bounds when known, both +0 otherwise (never inferred from recorded axis) |
| elapsed_seconds_present | u8 presence flag |
| elapsed_seconds | f64, finite nonnegative when present, +0 otherwise |
| elapsed_seconds_scope | text, nonempty when present (e.g. `solve_only`), empty otherwise; independent of optional documents |
| identity_axis_present | u8; matches step_index/level_index existence |
| identity_axis_convention | text; nonempty when present, empty otherwise (e.g. `integration_step_zero_is_initial`) |
| axis_name, axis_unit | text; timeseries `time`,`s`; static actual continuation parameter and unit |
| solver_configuration, sampling_configuration, execution | document descriptors, unavailable when not known |

If `catalog_state=unavailable`, only `info` exists in that run JSON object; channels, requests, values and axes MUST be absent, committed_count=0, observation_status=not_started, identity_axis_present=0. This is allowed only for preparation_failure/skipped/cancelled before preparation. Info fields and unavailable document states remain required; unknown requested bounds have presence0. A prepared empty selection instead has catalog_state=included and actual empty schema structures.

`last_recorded_present` iff committed_count>0 and equals the final stored axis value. Last accepted solver state is tracked independently, including accepted advances between output samples and initial accepted configuration; its presence may be true with accepted_count=0. A failed observation cannot commit a partial row. `run_outcome` is the terminal cause; the three independent status domains preserve other outcomes. All present numerical info fields are finite; requested bounds must be ordered when known. `run_outcome=completed` requires numerical_status=target_reached, observation_status=complete or disabled, logging_status=complete or disabled, and catalog_state=included. Unavailable catalog requires numerical_status=not_started and last_accepted_present=0. Other domain failures retain their independent status and terminal cause; no numerical success is inferred from complete archive publication. The producer must not relabel logger rejection after solver acceptance as numerical failure. An observer/logger stop before target uses numerical_status=stopped_before_target with an independent terminal cause; complete observations mean complete capture over the executed output clock, not that the last sparse sample equals requested_end. A stop at the successfully reached target may retain numerical_status=target_reached. An archive/publication error is an output-library/application status, not a fabricated failed solve inside a success-named partial file.

### A.3 Catalogs and effective requests

`channels` has `channel_index:u64[C]` equal to 0..C-1, `scalar_id`, `group_id`, `request_id`, `component_path`, `unit` as strings[C], and `request_index:u64[C]`. Scalar IDs are nonempty/unique. Basic identities and units remain present when detailed request metadata is disabled. Units may be empty only for dimensionless quantities explicitly designated by the producer. Scalar order is the prepared core order; names are never parsed to recover semantics.

`requests` has `group_id`, `request_id`, `schema_version`, `construction_profile` as strings[R], unique (group_id,request_id), and `effective_state:u8` (included/disabled/unavailable). When included it additionally has `effective_json:strings[R]` (no presence flags), and `effective_sha256:u8[R,32]`. During run selection each JSON entry is checked under the text-entry bound for UTF-8, its digest, and strict JSON object syntax (no duplicate keys or nonfinite literals). Each entry retains the effective request semantics, including frames/observer/reference point, generated by the schema/core owner. Channels reference a valid request_index whose group/request IDs match. Same request_id in different groups is valid. A request may have no selected scalar; unused request rows are allowed, including C=0 with R>=0 retained request identities. All channel columns have exactly C rows, all request identity columns exactly R rows, and effective_sha256 has exactly shape[R,32]. Effective JSON digests cover the exact UTF-8 slice bytes. When disabled/unavailable those two columns are absent; identity relation remains available.

### A.4 Logical values and frames

Nominal frame values B is a positive multiple of 4, at most 1,048,576; default 65,536. Frames are per logical outer row and exactly partition inner extents into B-sized chunks except the final tail. Catalog C equals the channel dimension of shape; committed_count equals the sample/level dimension and axis length. No frame crosses a row. True tail count is passed to ZFP; logical padding is forbidden. Empty selection has C=0 and axis length 0, shape[0,0], an empty channel catalog and no values or axis entries; request identities may remain as allowed in section3. A failed run with known channels and no committed sample is [C,0] or [0,C], not [0,0]. Zero inner or outer extent implies no frames/payload. It is valid to retain a failed committed prefix; failed candidates are never rows.

The frame_index section has77-byte records with the following fields/offsets and no implicit padding:

| Field | File type | Byte offset |
|---|---|---:|
| outer_index | u64 | 0 |
| inner_begin | u64 | 8 |
| count | u64 | 16 |
| byte_offset | u64 | 24 |
| byte_count | u64 | 32 |
| encoding | u8 | 40 |
| profile_index | u32 | 41 |
| sha256 | u8[32] | 45 |

Canonical bytes concatenate these members using the specified little-endian numeric representation and raw digest bytes. The frame-index section directory digest hashes the concatenation of all canonical records in order (SHA256(empty) for no records). C++ memory padding is never hashed or written as the wire record. Each record digest hashes exactly its stored payload bytes including stream padding. Records are ordered outer_index then inner_begin, cover every logical value once, and payload extents are contiguous from zero through P with no gaps or overlap. Checked addition/multiplication precede reads.

Encoding 0 is raw little-endian binary64 and byte_count=8*count. Encoding 1 is a complete ZFP_HEADER_FULL serial 1D binary64 stream, including its header and final word padding. The indexed count/type/mode/profile must agree with the header; consumed bytes must equal byte_count. count is positive and at most B. Raw frames keep the requested row profile index; every record profile equals row_profile_index[outer_index]. Choose raw when compressed bytes are equal to or larger than raw bytes. Raw fallback handles size, not a failed codec/verification check. Writers reject nonfinite committed selected values.

### A.5 Profile and verification tables

`profiles` is columnar with 1<=M<=1,000,000 rows: `profile_index:u32[M]` equals 0..M-1; `codec`, `library_version`, `library_revision`, `byte_order`, `execution` as strings[M]; `mode:u8[M]`; `codec_version:u32[M]`; `stream_word_bits:u32[M]`; `rounding:i8[M]`; `mode_code:u64[M]`; `tolerance_present:u8[M]`; `requested_tolerance`, `effective_tolerance` as f64[M]. All profile columns have exactly M rows and every row_profile_index is <M even if that logical row has zero inner extent. Initial exact profile values are codec=`zfp`, version=`1.0.1`, revision=`f40868a6a1c190c802e7d8b5987064f044bf7812`, byte_order=`little`, execution=`serial`, mode=0=reversible, codec_version=5, stream_word_bits=64, rounding=0=ROUND_NEVER, mode_code=2176, tolerance_present=0 and both tolerances +0. OpenMP/CUDA/DAZ/tight-error/strided-bitstream options are disabled. Producer library build/CRT provenance is recorded in context; CRT is not a wire setting. The full header MUST use the canonical SHORT reversible mode code 2176, with effective (minbits,maxbits,maxprec,minexp)=(1,16658,64,-1075). A long expert header is rejected even if zfp_stream_mode would normalize it to2176. For this pinned profile the required short header is32+52+12=96bits; the parser still bounds the pinned codec maximum148bits before interpreting it. Other profile values fail until explicitly qualified. Fixed accuracy will require a new required feature and later N14 acceptance; static lossy remains unsupported.

The generic pinned-codec header maximum is148bits; this protocol requires the96-bit canonical short reversible header derived from the specified fields, not inferred from the earlier probe. Future profiles cannot assume that same short-header length. A parser must first bound bytes, inspect magic/shape/mode without trusting header-driven allocations, check expected count/type/dimensionality/profile, and use a queried maximum-size bound. zfp_stream_maximum_size includes the maximum header allowance for this revision. Native scratch is sized before decoding. Ordinary ZFP bitstream routines are not a sandbox for arbitrary hostile input; parser bounds/fuzzing are a separate N03 obligation.

`verification` contains `max_abs_error:f64[F]`, `rms_error:f64[F]`, `verified_count:u64[F]`, `bitwise_equal:u8[F]`. Exact-mode values are +0,+0,count,1. These are producer assertions over originals, not independent proof available to a reader. Required frame/index hashes detect accidental corruption only. Axes, catalogs and metadata have their own section digests in this container. A selective read does not verify every section and none of these digests establishes authenticity.

### A.6 Optional provenance and JSONL semantics

`inputs` when present contains required document descriptors `source_json`, `effective_json`, `overrides_json`, `resources_json`. Source preserves original bytes; effective is resolved/defaulted/overridden representation; overrides documents authored versus CLI changes. Unavailable documents carry state unavailable. Resource manifests describe references/hashes/embedding honestly; copying input JSON does not embed referenced resources.

`context` when present contains document `execution_json`: actual argv is a JSON string array; versions/revisions/dirty states, compiler/build/solver/backend/codec/container identities, OS/architecture, working directory, execution timestamps and deliberate environment allowlist are retained when known. No unrestricted environment capture or stored-path reopening.

`logs` when present contains `records_jsonl:u8[K]`, `record_offsets:u64[R+1]`, `complete_record_count:u64`=R, `trailing_fragment_bytes:u64`, `dropped_count_present:u8`, `dropped_count:u64`, `schema:text`, `info_json:document`. Offsets start0 and each next offset is immediately after a complete LF-terminated record (CRLF bytes remain unchanged). The final offset marks the end of the last complete record and may be less than K; trailing_fragment_bytes=K-finaloffset. Empty logs have offsets[0],count0,fragment0. A partial stream retains the trailing bytes but never presents them as a complete JSON record. Dropped count unknown uses present0 and count0; zero is not inferred. Every complete record must parse as a strict JSON object when iterated/verified. Check each offset difference against the protocol 1 MiB record bound and the caller's stricter parse budget before reading/allocating/parsing that record; an otherwise valid over-budget record gives resource_limit. Scan/hash a retained trailing fragment in bounded tiles without assembling or parsing it. Complete-record scans likewise do not materialize the whole log. A fragment, known dropped records, or a known incomplete capture (including a clean LF-terminated prefix with unknown drop count) makes the root logs state partial; info_json records the incompleteness reason. Clean record boundaries alone do not certify complete logging. Initial events need not already contain authored run IDs; per-file run association must be declared in info_json, not inferred from thread ID. N12 later defines richer typed events without changing raw evidence.


## Part C. Joint clarifications and producer constraints

### C.1 Validation phases and ownership

Opening validates the complete bootstrap/directory and bounded root/global catalogs, including every advertised feature token. It does not parse unselected run/source/trace metadata to prove global feature use. Run selection validates the selected run's complete catalog/axes/profiles/verification/index, provenance reference arrays and diagnostic trace metadata envelopes. Selecting a trace validates all its fixed column types/shapes, hashes and bounded scans. Reading a requested source validates its source metadata and requested evidence; document parsing retains the baseline independent local materialization budget. Full verification visits all runs, sources and traces, validates feature use/aggregate states, and proves no section aliases/orphans or unused source entries. A known extension body with a missing token can be rejected only when that body is visited; an unknown declared token is rejected at open even if its run is never selected. Selected numerical reads still verify only intersecting frames. Neither selective opening nor producer verification metadata proves unvisited payloads or original numerical values.

Strict1.0 dispatch treats any token beyond its five base tokens as unsupported_feature, including a token supported only under1.1. This preserves baseline rejection categories. A1.1 file with no extension token is invalid_contract. Missing mandatory base tokens and visited body/token inconsistencies are invalid_contract; unknown declarations/versions are unsupported_feature.

Root owns run/source metadata edges and run owns trace metadata edges. These kind1 sections are distinct from root and each other. Every metadata owner owns each data descriptor once, including zero-length sections. Per-run source IDs are non-owning logical associations; shared source IDs never create multiple binary owners. Source metadata must match its catalog source_index/kind; trace metadata must match run_index/kind. Bundle remapping includes trace.run_index, every metadata/data section reference and the complete source association table.

### C.2 Prepared memory and spool validation

Preparation computes the exact selected section graph, including all real empty arrays and trace/source metadata, before admission. Let D be the final directory capacity, S the source count and Ds the current spool section count. Directory storage costs at least 64*D bytes; section ownership costs ceil(D/8); source-used flags cost ceil(S/8), at most125,000 bytes at S=1,000,000; a dense uint64 old/new spool remap costs8*Ds. Actual retained container/string capacities, alignment/object overhead and implementation-specific lookup structures must be accounted in the owner's prepared plan rather than hidden behind these wire minima. A sparse/remapped representation is allowed only with its actual proven memory charge.

The prepared aggregate includes directory storage (or an explicitly bounded, owned directory-record stream), retained global run/source catalogs and pools, root metadata admission/storage, both ownership populations, maximum one-spool metadata/catalog and remap working set, source/trace metadata parse scopes, bounded copy/index/verification tiles, reusable decoded frame and codec workspaces, and bounded JSON serialization scratch. The baseline metadata preflight charge applies per parsed metadata scope before parsing. Count catalogs, pools, offsets and actual retained capacities separately. No claim of constant memory in run/source count is made. Reject insufficient aggregate workspace before enabling an export; no opportunistic vector growth during capture/save. The accepted128-entry directory is not a promise that all diagnostic configurations fit. Zero-length fields still count as sections. Do not count a shared allocation twice when its ownership/lifetime proof permits one charge.

Source-spool validation checks header/directory, support/version/body agreement, all source/run/trace references and ownership, catalogs/axes/profile/index semantics and hashes. Each numerical frame is decoded into bounded reusable scratch to validate header/count/profile/finite values, without recompression. Copy verified encoded bytes in bounded tiles; verify the bytes actually copied against the manifest and section/frame identities rather than relying solely on an earlier pass or an open handle. Retain one spool's remap and working metadata at a time, plus explicitly budgeted global catalogs/directory. Preserve unchanged frame records/payload-relative offsets, profiles, raw documents and original log boundaries wherever possible, rebuilding only mapped descriptors, root/global catalogs and envelope hashes. A merger cannot reconstruct originals or independently prove lossy producer error assertions. No whole-history accumulation or whole-file materialization for hashing/copying is authorized; complete-file digest passes remain streamed in bounded tiles. Live append and incomplete-container recovery are unsupported.

### C.3 Immutable producer manifest and original-input identity

Initial bundle import requires an immutable producer/spool manifest binding each finalized spool's exact SHA-256, byte extent, authored run ID and producer/dependency identity to the authored-input SHA-256 captured before solving. The association must survive release of numerical histories and remain through final publication or explicit abandonment. Verify actual spool bytes against that binding. Original included bytes must additionally hash to the authored digest. When original bytes are disabled/unavailable, the manifest-bound digest is a producer assertion; it cannot be reconstructed from missing bytes and is not authentication. Never reopen a changed input afterward to manufacture earlier identity, use effective JSON as original identity, or infer equality from two missing documents. Arbitrary external1.0 spools with missing original bytes and no trusted producer record are unsupported initial merge sources, not malformed archives.

The unchanged1.0 document grammar permits partial source_json evidence. An initial bundle importer rejects such a valid spool as unsupported_merge_source even with a manifest; it never relabels that state, discards its partial evidence or calls the input malformed. The provenance-sources-1 original role itself allows only included/disabled/unavailable. Included empty sources remain included. Aggregate codes depend on states, not byte counts; included-empty + unavailable logs aggregate partial.

Core owns explicit selection/dependency order, immutable input snapshot, transfer directories, workspace generations and exclusive ownership, quota/free-space checks, failure precedence and retry lifetime. Outputs owns bounded verified assembly and no-replace publication. The generated execution/retry manifest is separate from the archive wire and from certified transfer manifest-v2. Source spools remain retained while the final temporary exists. Disk accounting includes all live owned snapshots, manifest generations, logs/sidecars/transfer files, spools, abandoned attempts and final/spool temporaries; rename does not double-count one byte population. The quota is not OS space reservation or a power-loss guarantee. Committed spools are never silently recomputed after corruption/final-save failure.

The outputs planning API, with final C++ names selected during implementation, must return at least required_workspace_bytes, section_count and maximum_archive_bytes from checked prepared capacities and declared source extents. The maximum archive bound includes128 bootstrap bytes,64*section_count directory bytes, all section upper bounds,77 bytes per possible frame,25 verification bytes per frame, and all JSON metadata sections bounded before writing. Because valid size-based raw fallback stores no more than8*n payload bytes per n-value frame, numerical stored payload is bounded by8 times admitted values; separately reserve the maximum queried codec candidate scratch. Catalog/pool/axis/profile/trace/source extents are known or explicit upper bounds, and retained JSON metadata bounds are charged per section. Every sum/product is checked against protocol/local/addressability limits; inability to prove an upper bound rejects admission. The merger returns the analogous final bundle bound from selected spool/run/source plans, including rebuilt metadata and final directory. Core combines those library bounds with its own separately bounded log/artifact/manifest reservation. Actual writes remain checked against the returned reservation. This API obligation is concrete; type/method names and allocation implementation remain implementation work, not new wire fields.

The final bundle admission bound includes committed spools plus conservative plans for every uncommitted selected run; it cannot omit future runs merely because their actual bytes do not yet exist. Manifest publication writes immutable numbered generations with checked write/flush/physical close, then checks replacement of the current-generation reference under exclusive workspace ownership. Retain the current and previous valid generations. Test faults before/after generation publication and before/after reference publication. Corruption never triggers a silent older-generation fallback. No power-loss durability is claimed. An explicit reversible retry may be a programmatic post-active save over retained originals with changed storage-policy provenance in a manifest generation; no new CLI flag is required. It is never silent codec fallback or merger recompression.

### C.4 Trace criteria, summaries and population checks

Recorded included/partial static kinds1/2/3 REQUIRE criteria. Kinds0/4 and all unavailable/not_started/source_unavailable traces forbid criteria. Static criteria fields are exactly those in Part A; evaluation_time_s is finite and may be negative, tolerances/scales are finite nonnegative, every limit is finite positive. A terminal-only Newton row may carry a positive iteration identity if known; zero is allowed only when unknown. row_kind is explicit source evidence, never inferred solely from iteration_id or status text. Certified kind1/2/3 rows all require metric_valid0x3f.

Static kinds1/2/3 become recordable only after complete immutable criteria, including finite positive computed force/moment limits, are validated during preparation. An earlier failure is preparation_failure with unavailable/not_started requested traces: zero rows and absent columns/criteria/summaries. Known-empty included static traces require prepared criteria, known zero eligible population and real empty column sections. Later numerical failures preserve those criteria and each row's actual validity bits.

When kind0 eligible_count_present=1, eligible_count equals run.info.accepted_count. Both count all driver accepted advances, including an advance accepted immediately before a required diagnostic capture failure. Retained kind0 accepted_step is exactly1..R, independently of selected sparse output cadence. Unknown count evidence does not assert equality. No static eligible-count/accepted-count relation is invented from selected-history row count; exact source facts govern that independent population.

Generic status_names and event_names are source-owned dictionaries of1..64 unique nonempty strict text tokens; each index is in range. Severity codes are0 debug,1 info,2 warning,3 error. Generic event sequence is strictly increasing and identities obey the universal presence mask/canonical placeholders; a core-specific start/terminal convention is not inferred from writer_name. The exact13-status/3-event tables and lifecycle identities in C.7 qualify the core producer only. Other source-owned tokens within this grammar remain valid. Likewise the core-only static kind2 relation eligible_count=accepted_count+1 when last_accepted_present establishes initial certification is checked in core qualification, not imposed on arbitrary producers without a declared source contract. No new wire profile flag is introduced.

summaries, when present, is exactly one {population:"retained_valid_records",fields:{...}} wrapper. fields is nonempty; each key is one permitted measure column for that trace kind. Kinds4 forbid summaries. Each field summary has count,min,max,mean,median_present,median as specified in Part A. Reader bounded scans recompute count/min/max over retained rows with that field's validity bit. These exact checks are required in trace selection/full verification. Mean and optional median are producer-asserted rounded statistics: readers validate types, presence, finite values, canonical absent/empty positive zeros and rounded extrema interval, without claiming independent arithmetic-mean or exact-rank proof. For u64 min/max, that interval is [RN-even-binary64(min),RN-even-binary64(max)], not an exact integer interval; e.g.2^53+1 rounds to2^53 and maxu64 rounds to2^64. For f64 extrema the endpoints are already representable. Nonempty absent median is +0. Empty fields have count0, integer/floating zero extrema of the column type, mean+0, median_present0, median+0.

A producer advertising median uses exact central-rank selection and, for even counts, exact-real central-pair averaging followed by one binary64 RN-ties-even rounding, including u64 values beyond2^53. It must budget retained values plus post-run scratch. Initial producers may omit every median; no reader rank-selection implementation or median scratch is implied by verify_all. The retained-valid population never claims omitted records.

### C.5 Exact-real fixed-accuracy verification

Pinned ZFP is1.0.1 revisionf40868a6a1c190c802e7d8b5987064f044bf7812, codec5, serial little-endian64-bit words and ROUND_NEVER. For finite positive requested binary64 tau, extract floor(log2(tau)) from exponent/fraction bits: normal e=raw_exponent-1023, subnormal e=bit_width(fraction)-1-1074. An independently qualified frexp path is equivalent; rounded log2 is forbidden. Only e in[-1073,843] is supported, producing exact effective2^e, tuple(1,16658,64,e), canonical96-bit full short header code2177+(e+1074), i.e.2178..4094. The setter, queried tuple/mode/effective value and header must agree.2^-1074 selects the expert/default tuple and is unsupported;2^844 and above are unsupported. No clamp, long expert header or implicit reversible substitution is permitted. Every profile is validated even unused, empty or on a raw frame. The fixed feature denotes requested mode, not observed compressed encoding. Static remains mode0.

For exact error decisions, represent each finite binary64 as a signed integer multiple of2^-1074. Normal magnitudes use the53-bit significand shifted by raw_exponent-1; subnormal magnitudes use fraction with shift0. Each magnitude is below2^2098 scaled units; opposite-sign sums are below2^2099. Fixed33 uint64 limbs (2112bits) hold every difference. Subtract magnitudes for equal signs or add for opposite signs, then compare that exact unsigned magnitude with the similarly represented requested tolerance. Reject nonfinite source/decoded values first. No floating subtraction overflow, subnormal rounding, epsilon/slack or MSVC long-double assumption enters acceptance. Signed-zero real error0 and bitwise equality are separate.

Every compressed candidate decodes and passes this bound before raw size fallback. Failed verification fails the requested export; an explicit later reversible retry is separate. Stored raw frames report +0,+0,count,bitwise1 even if an abandoned valid compressed candidate had nonzero error. Compressed maximum error is the least representable binary64 >= actual maximum exact-real difference. The bound comparison guarantees this rounded-up statistic cannot exceed representable requested tau. RMS is a stable scaled-sum-of-squares rounded reported statistic, not the bound proof. Reader verification assertions cannot recover missing originals. Producer-only deliberate bound failures live in separate oracle fixtures, never as supposedly successful completed archives.

### C.6 Source and authoring freeze boundary

Input wire3.0-wip.5 and construction profile1.0 are separate from archive versioning. The schema-owner contract review fixes explicit tags, ordinary initial_equilibrium=include and requirement=optional defaults with origin preservation, positive capacities, homogeneous request quantity-family/unit binding, scalar overrides and unmatched-scalar reversible policy. N11 selected levels are actual certified0..L-1, first at initial_factor; excluding initial selection never suppresses a target initial equilibrium. Required nonempty selected-history capacity is at least maximum_stages; empty selection records no rows. Required diagnostics capacities are compiled transient advances, maximum_stages for attempt/level traces, checked maximum_stages*maximum_iterations_per_stage for Newton rows, and2 for run events. Source capability rejection occurs during preparation, not as invented runtime diagnostic failure.

Static Newton records publish the finalized attempt subspan exactly once, after terminal status mutation and rollback. Optional capture failure freezes the complete prefix; required failure preserves the first primary cause and true numerical/selected certification facts while prohibiting published certified transfer. Process events remain explicitly process-scoped original JSONL only. The exact core status/event table is appended below. The core's operational limits and quota/CLI rules are producer requirements, not new archive fields or generic reader restrictions. Until this candidate and corpus receive joint review, this remains an unaccepted draft.


### C.7 Core source-owner review (identified candidate)

# Native extensions core contract review

8 November 2026. Core decisions for the joint N11–N14 freeze. G2 is accepted. Serene main confirmed the core event/status contract and initial bundle operating profile below. Implementation release remains with Serene main.

## Reviewed identities

| Input | SHA-256 |
|---|---|
| G2 acceptance | `b478f6388ba3e4599b902b39f3a9cf6f7ddf9d39f1473d8bdd0996e75ee9e233` |
| Format 1.1 proposal | `ac7cd539b9aaa52cfc5ecfba0be0cd44db98b768ace9c7561723dc1d83eb2891` |
| Core source decisions | `4c28c3274a35c34089dc8a1ed90b1d01c37f1e2120c5c8516454cef29754594b` |
| Implementation readiness | `58d33ddb87db98b30a9b66bac1fbb1540747fb63a68b7d1f11d115701a4f25e3` |
| Bundle CLI planning | `6c3b28c929eeefd408fa678c8fc25a9fd86ba27120013b4ccd655edd26ea5214` |
| Schema-owner contract review | `fc383f71e4f491e3a4734a1351afb2d4b5394ee4f4a41675840cdda24152403f` |

The reviewed core source aggregate is `44c50ad64a07fcab7bc5cc31b076b1704e491874e0d505303bf2bf10be2e163a`. The schema proposal's input wire `3.0-wip.5`, construction profile `1.0`, ordinary defaults and typed tolerance bindings agree with the core semantics below. Archive version follows enabled features independently. The previously accepted review SHA-256 `75a9f87e918a038dbdfc1e9bd86ae6d6bf1dd159f87b4b296141a79fe727a0f5` is preserved at `build/native-extensions-review/core-review-75a9f87e.md`.

## Populations, initial level and capacity

Let `N` be the compiled transient advance count, `S` the maximum static attempts and `M` the maximum iterations per attempt. Required capacities use checked products and actual supported extents.

| Trace | Eligible population | Required capacity |
|---|---|---|
| 0 transient accepted steps | Validated driver advances `1..N` | `N` |
| 1 static stage attempts | Initial and retried attempts `1..A` | `S` |
| 2 static accepted levels | Certified equilibria `0..L-1` | `S` |
| 3 static Newton iterations | Finalized published outer candidate/failure rows | `S*M` |
| 4 run events | Start and one orderly terminal execution event | `2` |

Transient publication follows native success, finite-state checks and clock validation, before typed capture, selected observation and logger effects. `time_s` is the validated driver time. When kind 0 has known eligible count, require `eligible_count == info.accepted_count`, including an advance accepted immediately before required recording failure.

Static level 0 requires actual certification at the configured initial factor. Failed attempts consume attempt IDs. `accepted_count` excludes initial level 0. For core-produced static runs, `info.last_accepted_present=1` establishes certified-level presence. A known kind-2 eligible count then equals checked `info.accepted_count+1`. Other presence cases retain independent trace evidence. Selected rows supply no eligible-count inference.

Static selected capture defaults to `final_only`. For `accepted_levels`, `initial_equilibrium` defaults to `include`, and nonempty selection requires `level_capacity >= S`. Exclusion suppresses a nonterminal initial row. An initial level that also reaches the target is retained. Empty selection has zero selected rows. Typed kind 2 always includes certified level 0. Carry actual factors independently of evaluation time. Current continuation retains its qualified increasing accepted factors, while the generic history representation permits repeated/decreasing factors.

Optional capacities may be smaller. First failure freezes that trace's complete prefix. Count subsequent eligible records as omitted, including the rest of a finalized attempt. Counter overflow clears both eligible/omitted presence flags and uses canonical placeholders. Preserve a known first missing prefix ordinal. Required undersizing rejects preparation. Required runtime capture failure stops further numerical advancement after bounded boundary finalization. [Transient source](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/transient_execution.cpp:138), [static source](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/static_execution.cpp:209)

## Finalized Newton rows and criteria

Publish each attempt's existing bounded row subspan exactly once after iteration-exhaustion status and confirmed rollback flags settle. The same finalizer covers exceptional exits. Save requested attempt factor before rollback restores the accepted factor. Kind 1 carries the final mechanical attempt status and latest published measurements from that attempt. Its `newton_record_count` counts published source rows independently of typed kind-3 retention.

Assign explicit `row_kind`. Code `0` identifies post-correction candidates, including failed line searches and injected candidate failures. Code `1` identifies baseline-certification failure, linear-solve failure and exception-only rows. Terminal failure-only rows can have positive iteration IDs. Iteration 0 represents unavailable iteration identity.

Reset the metric mask and six positive-zero placeholders at every certification entry, including copied candidates. Set bits only after complete measurements. Bits `0x01/0x02` identify world-force/node-local-moment residual maxima, `0x04/0x08` physical translation/SO(3)-log gap maxima, and `0x10/0x20` applied translation/local-rotation increment maxima. Publish both gap bits after successful complete gap calculation into temporaries. Preserve earlier valid measurements on later failure. Certified records require `0x3f`. Certification follows the accepted mechanical boundary.

Prepare immutable criteria before admitting kinds 1–3. Validate evaluation time, authored scales/tolerances and actual finite positive absolute-plus-relative force/moment limits. Earlier failure produces `preparation_failure`, with requested static traces `availability=unavailable`, `capture_status=not_started`, `record_count=0`, absent columns/criteria/summaries and failure code `criteria_unavailable` when applicable. Known zero totals require proof that execution was never admitted. Otherwise totals remain unknown.

A known-empty included static trace of kind 1, 2 or 3 requires successful criteria preparation and a known zero eligible population. It has real empty column sections and complete criteria. Post-admission failures retain those prepared criteria and publish measurement validity per row. [Certification and row branches](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/static_execution.cpp:25), [late status and rollback](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/static_execution.cpp:176), [prepared controls](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/preparation.cpp:184)

## Status and event dictionaries

Core-produced kinds 1 and 3 use this fixed `status_names` order through explicit source mappings.

```text
0 success                     1 invalid_configuration
2 unsupported                 3 capacity_exceeded
4 not_prepared                5 numerical_failure
6 native_linear_failure       7 native_nonlinear_failure
8 native_exception            9 output_failure
10 logging_failure            11 quarantined
12 diagnostic_failure
```

Map source enum values explicitly to tokens. Keep human message text separate. Mechanical row statuses survive later observation, logging and typed-recording failures. The new typed diagnostic outcome has its own mapping. Current `RuntimeCode::diagnostic_failure` describes logger rejection and requires contextual handling. [Status source](C:/Users/12650029/serene-academic/serene-core/includes/serene/core/simulation/types.hpp:9), [existing token mapper](C:/Users/12650029/serene-academic/serene-core/src/output/prepared_export.cpp:161)

| Event index and name | Severity code | Eligible sequence |
|---|---|---|
| 0 `core.execution_started` | 1 info | 0 |
| 1 `core.execution_completed` | 1 info | 1 |
| 2 `core.execution_failed` | 3 error | 1 |

The exact 13 status tokens/order and three event names/order define the qualified core-producer contract. Generic archives retain bounded unique nonempty source-owned dictionaries and structural, range, mask and count validation. Reader admission applies those generic rules independently of `writer_name`. Core qualification checks its exact source mappings and lifecycle conventions. The static kind-2 count equality above likewise has explicit core-source scope.

Severity vocabulary is `0=debug`, `1=info`, `2=warning`, `3=error`, mapped explicitly to logging levels. Sequences are run-local eligible event ordinals, independent of JSONL sequencing/filtering and selected cadence. Emit start after execution admission. Transient start follows initial accepted-identity establishment and precedes initial observers. Static start precedes the first attempt. Emit one eligible terminal event after the execution summary and other required typed captures settle, before caller logger finalization. Completed reflects execution success with optional partial diagnostics allowed. Failed reflects an earlier execution failure.

`identity_valid` bits are time `0x01`, accepted step `0x02`, accepted level `0x04` and attempt `0x08`. Transient start uses `0x03` with initial time/step 0. Transient terminal uses `0x03` and the last accepted time/step. Static start uses `0x00`. Static terminal adds `0x08` when an attempt began and `0x04` when a certified level exists. Use `stage_attempts` for last attempt identity. Static event time/step fields remain absent with canonical zeros. Static evaluation time belongs to criteria.

Prepared authored run ID and `trace.run_index` establish scope. Preparation-failed/skipped runs and rejected repeat execution calls have zero eligible execution events. Process messages retain original scoped JSONL. Complete event traces contain two records. A missing terminal capture freezes the prefix and counts that event as omitted. Required failure of terminal capture updates diagnostic outcome without another terminal attempt. Later footer/flush/close or archive failures retain the execution event already established. [Execution admission](C:/Users/12650029/serene-academic/serene-core/src/simulation/chrono_simulation_context.cpp:37), [logging levels](C:/Users/12650029/serene-academic/serene-logging/include/serene/logging/event.hpp:20)

## Failure order and transfer admission

At a validated mechanical boundary, establish acceptance/certification, finalize eligible typed facts, then perform selected observation and logger effects. Finish bounded publication/counting for the already eligible diagnostic population before honoring a required stop. Preserve the first primary cause and independent later failures. Required typed failure uses `diagnostic_failure`. Optional partial capture remains neutral to execution success.

Retain certified factor, configuration, metrics and identities when required recording fails after target certification. Published transfer admission requires successful execution, clear required audit/logging conditions, successful required typed diagnostics, full-load certification, confined indexed-file digests and matching fingerprints. Optional partial typed traces alone preserve eligibility. The direct physical `export_equilibrium()` packet has its existing trusted programmatic scope. Extending that packet with universal admission evidence would require a separate API decision. [Physical export](C:/Users/12650029/serene-academic/serene-core/src/simulation/chrono_simulation_context.cpp:109), [published admission](C:/Users/12650029/serene-academic/serene-core/src/result_io.cpp:448), [directory consumer](C:/Users/12650029/serene-academic/serene-core/src/main.cpp:303)

## Minimal bundle CLI and operating budget

```text
serene-core --input INPUT.json --run transient --run static
  --bundle FINAL.serene --bundle-work NEW_DIRECTORY
  --bundle-disk-budget BYTES

serene-core --resume-bundle WORK/bundle-manifest.json
  [--bundle-disk-budget LARGER_BYTES] [--continue-uncommitted]
```

The explicit run list fixes catalog order. Authored IDs stay global and display names may repeat. Execute the lowest ready catalog index in stable topological order. Every dependency source must be explicitly selected. Initial bundle selection supports existing transient/static analysis and requires `native_archive` storage for every selected run. Use authored numerical settings. Single-run mode retains its existing entry point. Bundle mode rejects single-run destination/preparation/oracle controls and global numerical overrides. The initial resume profile keeps selection, input and final destination immutable. Save-only is default. Explicit continuation replays uncommitted attempts from declared initial conditions and verified committed transfer sources. Initially require the recorded producer/dependency/merger identities for either resume mode.

Use numeric catalog and fresh attempt indices. Both artifacts and spool belong under `WORK/runs/000000/attempt-000001/`, giving replay fresh artifact paths. Require an exclusively created work root and a separate final destination outside it. Capture one immutable input snapshot even when archive input retention is disabled. Bind spool extent/hash and common authored digest in a committed manifest generation before releasing history or advancing. Preserve separate original log sources. Preparation failure commits an unavailable-catalog terminal entry. Failed dependencies commit skipped entries with reason `skipped_dependency_failed`. Continue independent ready runs. Spool/quota/publication failure stops orchestration. Preserve owned abandoned attempts in quota accounting.

Serene main approved these initial operational limits for the joint freeze.

| Owner | Limit |
|---|---|
| Combined counted prepared payload | 536870912 bytes, including retained bundle state |
| Retained global catalog/manifest state | 67108864 bytes and at most 1024 selected runs |
| Final merger workspace | 67108864 bytes, within the combined cap |
| Copy tile / index tile | 65536 bytes / 256 records |
| Each archive metadata serialization tile | 1048576 bytes |
| Disk quota | Explicit decimal integer `1..INT64_MAX` bytes |

Charge actual planned directory storage, ownership/source-used bitmaps, metadata, codec and traversal scratch once. The required memory minimum is the checked returned graph plan. Reject a request exceeding either local or combined allowance before activation. The current fixed 128-section writer directory needs graph-based replacement/admission. Global catalog and maximum one-run ownership have separate accounting. Process/allocator/DOM overhead retains G2 measurement scope.

The output planning API returns checked section capacity, required workspace bytes and a conservative finalized archive byte upper bound before run activation. Overflow and insufficient-budget admission fail explicitly. Core reserves the final bundle bound using verified committed-spool summaries, uncommitted-run plans and the final catalog/provenance/directory overhead. Exact C++ names settle jointly during implementation under these fixed semantics.

Let `W` be owned live bytes, `G` next manifest-generation/reference commit headroom, `A_i` all new artifacts/logs and one spool temporary bound for the next attempt, and `F` the conservative final bundle bound. Admit that attempt only when checked `W + G + A_i + F <= quota`. Final assembly requires `W + G + F <= quota`. Temporary-to-final rename preserves one byte population. Reserve before active execution and enforce actual growth against reservations. Check each affected volume. A quota failure before a later run preserves the committed prefix for explicit higher-budget resume.

Under exclusive workspace ownership, write an immutable numbered generation through an exclusive temporary with checked write, flush and close. Publish its checked current-reference through the Windows replace primitive or POSIX rename. Retain current and previous valid generations. Durability scope is checked process-level publication and explicit resume. Fault qualification covers before and after generation publication and current-reference publication, preserving the verified resumable prefix.

Save-only validates committed sources and rejects uncommitted selection. Corrupt committed spools cause explicit rejection while their evidence is retained. Unbound external spools are rejected. Initial import of valid archive-1.0 partial original-input provenance is an unsupported capability. The reader 0.2 correction must accept that legitimate file independently of importer policy. Keep spools and transfer/artifact directories after final publication.

Bundle exit precedence is validation `1` before execution, then artifact/quota/publication `6`, required diagnostic/logging/audit `5`, other failed/preparation/skipped runs `4`, and complete publication with all runs successful `0`. Per-run outcome domains retain their evidence independently.

## N14 binding and implementation decisions

Fixed accuracy binds prepared unit/quantity-aware profiles to the same committed transient scalar buffers. Scalar rules override homogeneous request rules. Validate superseded rules too. Preserve lossless static values, axes, diagnostics and metadata. Decode-back verification precedes size-based raw fallback. Verification failure retains originals for explicit retry and fails that save. Committed encoded spools retain their profiles during assembly. Shared codec owns bit-derived exponent admission and exact-real bound comparison.

Explicit reversible retry is a programmatic post-active save request over retained immutable original buffers, using the existing freeze/retry lifecycle. Record the changed policy in effective/override provenance. A failed lossy save leaves those originals alive. CLI users can explicitly rerun with authored reversible policy. The runtime entry point settles during implementation under this fixed lifecycle contract.

Planning API names, platform publication wrappers and the post-active retry entry point are implementation decisions under the semantics above. Independent output/reader fixtures belong to the coordinated freeze and subsequent qualification. Initial implementation qualification targets Windows MSVC. Following Serene main's production release, schema regeneration needs fresh identified artifacts, and core needs deliberate level-major capture/export APIs and a finalized-attempt callback. Preserve current static `initial_accepted` displacement capability admission. [Current static provider restriction](C:/Users/12650029/serene-academic/serene-core/src/simulation/detail/output_binding.cpp:144), [single-static-row export boundary](C:/Users/12650029/serene-academic/serene-core/src/output/prepared_export.cpp:463)
