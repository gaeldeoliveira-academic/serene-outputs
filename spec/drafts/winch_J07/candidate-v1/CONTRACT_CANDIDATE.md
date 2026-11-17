# J07 optional winch geometry: contract candidate v1

17 November 2026. **For Main/Core/viewer review only. Not a frozen specification.**
All new names, numeric assignments and policies below are proposals. The current
`spec/POSE_SIDECAR.md`, production recorder/checker and old fixtures remain unchanged.
J07 recorder implementation and J08/J09 integration are not released by this document.

## Recommendation and exact compatibility boundary

Retain bootstrap major=1/minor=0, `catalog_version=1`, required feature mask=0,
the structural pose layout, physical sample identities, structural index and
completion records. Add one optional, separately versioned **winch geometry**
stream. It follows the structural capture cadence; scientific sampling stays
independent. It is not a general feature registry or a scientific archive change.

Candidate identifiers are kind=4, stream=2, layout=1, encoding=0 (raw), flags=0.
They are unused in the inspected contract; joint review must approve their
assignment. Do not reuse kind1/stream1 with a different layout or optional flag.
The old viewer explicitly rejects unsupported semantics on known streams; the
old Python checker is less conservative there. A new kind AND stream avoid that
discrepancy without changing either production reader at this gate.

Both current readers reject bootstrap minor1. Both validate the bounds, global
sequence and header/payload CRC of an unknown optional chunk before skipping it;
such chunks must precede the seek index. Additional catalog fields are allowed.
The executable probes in `compatibility_probe.json` establish this behavior for
the precise candidate bytes, including rejection of a repaired-CRC minor1 file.
Old readers recover structural poses only. They do not validate or render winch
semantics and deliberately accept several internally malformed feature payloads
whose outer CRC is valid. That is an explicit compatibility limit.

An aware reader dispatches the reserved winch family **before** generic optional
skipping: either kind4 or stream2 engages these checks. It rejects a missing
`winch_geometry` descriptor, kind/stream disagreement, unsupported layout/encoding,
or flags other than0, even though it understands this optional feature. It must
not downgrade malformed known-family bytes to an unknown optional stream. With
no descriptor and no reserved-family chunks, the feature-free path is unchanged.

## Catalog amendment

When recording this feature, add the top-level object `winch_geometry` shown
verbatim in `fixtures/catalog.json`. If absent, use the unchanged feature-free
path. An empty arrangement array is not emitted; omit the object instead.

Required object fields:

| Field | Candidate meaning |
| --- | --- |
| `layout_version` | Integer 1. |
| `kind`, `stream_id` | Integers 4 and 2, binding the optional chunk identity. |
| `encoding` | Exact string `raw_f64_le`. |
| `source_preparation_generation` | Unsigned integer Core context generation, positive and at most INT64_MAX; **not** capture-plan identity. |
| `arrangements` | Nonempty immutable array, count F; dense `index` order is payload order. |

Each arrangement has `index`, `key`, `owner_occurrence_id`, `force_id`,
`feature_type`, `points`, and `edges`:

* `owner_occurrence_id` references an existing **assembly** occurrence.
  `force_id` is the exact nonempty force identifier within that owning occurrence.
  `(owner_occurrence_id, force_id)` must be unique. Reusing `pull` in two different
  assembly occurrences is valid and covered by the fixture.
* `feature_type` is exactly `winch_with_saddle`.
* `key` is a redundant, checked canonical identity. For exact UTF-8 occurrence
  path O and force ID FID, encode
  `winch_with_saddle.v1.o<decimal byte length>_<O>.f<decimal byte length>_<FID>`.
  Decimal lengths have no leading zero; parsing consumes the declared UTF-8 bytes
  before the next delimiter. Preserve spelling, case and Unicode; do not normalize,
  resolve paths, trim or percent-encode. Strings must be valid UTF-8 without NUL.
  O must equal the referenced catalog occurrence's canonical `path`. This is an
  occurrence/force identity inside the file, not a scientific channel ID.
* `points` has exactly four entries in role order A/B/C/D, with stable local
  point indices 0/1/2/3. A and B have `kind=physical_port` and a `sample_id`
  referencing two distinct `body_port` samples of the same body occurrence.
  D references an `anchor_port` sample. Those existing samples remain the physical
  identities; no additional port is created by the feature record.
* C has only `role=C`, `kind=algebraic`, `feature=pulley` within the owning
  arrangement. No `sample_id`, node, port, orientation or material coordinate is
  permitted for C. Its full provenance is the row's owner and force plus pulley.
* `edges` is exactly the ordered list `(0,2,bridle)`, `(2,1,bridle)`,
  `(2,3,main_cable)`, encoded as the fixture's `points`/`kind` objects. These are
  visualization lines, not constraints, structural segments or inferred joints.

Indices/references are integer tokens, not fractional JSON numbers or booleans.
Reject duplicate/missing roles, duplicate arrangement identities, inconsistent
keys, invalid references or unsupported required type/layout meanings. Reject
nonfinite JSON numeric metadata. Additional descriptive fields may be retained
only when they cannot redefine these meanings. Unknown layouts under this known
`winch_geometry` descriptor require an explicit supported decoder; an aware
reader must not silently claim successful feature loading.

Core may add an existing physical port to the ordinary pose catalog if needed for
provenance. This changes the selected physical population, not its meaning. C
never enters `samples`, `segments`, `connections` or the physical-port alias table.

## Chunk amendment

Use the existing 80-byte chunk header with the candidate tuple `(4,2,1,0,0)`
for kind/stream/layout/encoding/flags. `record_count` is the number R of capture
frames, `first_frame_ordinal` names the first corresponding structural frame,
`previous` is zero, and stored=decoded=`R * (80 + 104*F)`. R is positive.
Global sequence increments for every feature, pose, index and completion chunk.
Structural predecessor offsets still link only structural pose chunks.

A feature record has this exact raw little-endian layout:

| Byte offset | Type | Meaning |
| ---: | --- | --- |
| 0..63 | 64 bytes | **Byte-identical copy of its structural frame stamp**, including structural sample count P and zero reserved fields. F comes from the feature catalog, not this copied count. |
| 64 | u64 | Source Core preparation generation; equals the feature catalog field. |
| 72 | u32 | Source phase: 0 initialized, 1 static_certified, 2 transient_initial, 3 transient_accepted. |
| 76 | u32 | Reserved zero. |
| 80 + 104*j | u32 | Geometry status of arrangement j: 0 valid_positive_tension, 1 zero_tension_limit. |
| 84 + 104*j | u32 | Reserved zero. |
| 88 + 104*j | 12 f64 | A.x,A.y,A.z,B.x,B.y,B.z,C.x,C.y,C.z,D.x,D.y,D.z; finite world metres. |

F entries follow in dense catalog order. No padding, orientations, loads, rates,
material coordinates or implicit timestamps are appended. A zero-tension entry
contains the deterministic positive-tension limiting geometry already supplied
by Core; it is not an unconstrained physical pulley location. Invalid/missing
geometry is a rejected frame, never a finite-looking substitute or NaN payload.
The recorder validates the status tag, not mechanical equilibrium or cable length.

The existing bootstrap declares P physical samples only. Its decoded-chunk limit
must cover both kinds of chunk separately. Retain the existing hard limits:
catalog <=16 MiB, P<=1,000,000, chunks <=64 MiB, counters/offsets<=INT64_MAX.
For F impose the same hard upper count 1,000,000 **and** checked catalog/chunk/
workspace bounds, which are stricter in practice. Check every add/multiply before
allocation or conversion to `size_t`. Require at least one full pair to fit.
Pose bytes per frame S=`64+56*P`; feature bytes W=`80+104*F`.
For byte budget B and frame cap M, admit R at most `min(M,floor(B/(S+W)))`,
and additionally require R*S and R*W each within the bootstrap/hard chunk limit.
The byte budget covers both encoded batches, not each separately. Header/constant
index scratch is documented separately; no duration-sized index is introduced.

## Accepted-state join

The copied stamp supplies actual physical evaluation time, accepted step or
static certificate ID, analysis/events and static load factor. Do not infer time
as step*dt: nonzero start 1.25 s and an off-stride target step7/time1.32/event8 are
in the fixture. Scheduled=false/target=true remains legal and explicit.

| Source phase | Required structural stamp |
| --- | --- |
| initialized | analysis prepared, event1, state_id0; diagnostic, not a solve. |
| static_certified | analysis static equilibrium, event8, matching certified ID/time/load factor. |
| transient_initial | analysis transient, initial event bit set, state_id0; first transient frame. |
| transient_accepted | analysis transient, positive accepted step, matching actual time; no initial bit. |

Apply all original structural phase/time/order rules, including at most one
prepared prefix and terminal target. The source generation is stable within the
file and checked against the owning Core context. Existing catalog
`preparation_id` binds capture-plan identity; it is an independent counter/string,
not an alias for generation. J08 binds recording, run, capture plan, source
generation and arrangement identities at setup and checks each against its owner.
The transient WinchReport factor is 1; the structural wire stamp's transient
load_factor remains 0. Translate deliberately rather than comparing those fields
as identical concepts. Static factors do compare exactly.

For scientific/visual comparison, join the run/binding/preparation context,
phase, actual time and accepted step or certificate. File ordinal is useful only
inside a recording; scientific row ordinals are never join keys. Scientific
sampling need not occur at every feature/capture frame.

**Proposed stronger duplicate-coordinate invariant, requiring Core review:**
A/B/D feature XYZ equals the referenced physical-port XYZ byte for byte in the
same structural frame. J08 should copy accepted-packet coordinates into both
flattened populations after checking identities, while retaining physical port
quaternions. It must not silently reconcile unrelated separately evaluated
positions. Multiple arrangements referencing one physical sample must agree
exactly or fail capture. Current A/B routes share arithmetic; universal D and
signed-zero agreement is not established by source inspection. Qualify this
common-source policy before accepting the stronger invariant. If Core cannot
support it without violating physical capture semantics, revise this candidate
explicitly; do not introduce an unrecorded tolerance at implementation time.

## Pairing, prefix visibility and completion

For a feature-enabled file, write **feature batch immediately followed by matching
pose batch**, with the same R, first ordinal and copied per-frame stamps. No
unknown chunk may intervene inside a pair. Other optional streams may occur only
between complete pairs and before the seek index. Every structural frame has
exactly one complete feature record containing every arrangement; no sparse
population, independent feature cadence or feature-only frame is supported.

An aware decoder stages F and commits a complete pair only after both chunks'
CRC, extents, semantics, references, stamps and values validate. It cannot expose
a newly staged C with an older structural pose. Truncation in F or the adjacent
P, including EOF just after F, exposes only prior complete pairs as an incomplete
prefix. Invalid CRC/semantics is an error, not an accepted incomplete prefix.
A completed file with missing feature batches, a pending unpaired F, duplicate
batches or mismatched stamps fails feature-aware validation. Seek index arriving
while F is pending is malformed, even if the legacy structure-only index matches.

Keep existing index/completion/footer payloads byte-for-byte in their old layouts.
They count structural pose chunks/frames only. Feature-aware completion also
establishes the one-to-one pair count; no additional trailer is needed. An empty
failed recording may contain a valid nonempty catalog and zero paired frames;
unchanged completion records its failed/stopped outcomes and zero pose counts.
For seeking, scan forward through chunk headers to pair offsets, or build an
optional reader-side index. The old backward structural chain alone does not
locate the preceding variable-sized feature chunk; do not promise O(1) feature seek.

The future recorder validates and copies a whole structural/feature packet in
one call before accepting it. Flush writes F then P synchronously and checks stdio
flush before incrementing persisted paired frames. `accepted_frames` may include
copied frames whose flush later fails; no successful Core callback is implied.
Counts in a successfully published file are equal. Failed I/O latches and retains
the owned `.partial`; destructor does not publish. Existing no-clobber close/link
publication and separate numerical/capture/recorder outcomes remain. Complete
chunks, wire completion and filesystem publication are separate facts; no power-loss
durability, rollback of partial writes or in-place retry is promised.

## Review evidence versus remaining implementation

The independent corpus has 15 cases totaling 73,472 binary bytes. Four positive
files cover three transient states, one certified static state, one prepared
diagnostic and an empty failed recording. Values are literal asymmetric byte
oracles, not a numerically solved winch trajectory. Two occurrences share local
force ID `pull`; one arrangement carries zero-tension-limit status.

Both existing readers passed the 15 expected legacy outcomes; the existing viewer
test also passed its five focused checks. Independent raw-byte assertions verified
five paired frames and all 120 coordinate scalars exactly. Feature-aware rejection
of semantic negatives remains **prospective**, since that reader and recorder have
not been implemented. See `fixtures/expected.json` for the two distinct expectation
columns. I/O-fault behavior, producer readback, shared-port/signed-zero invariants,
capacity edges, actual simulation capture and rendering remain gated stage work.
