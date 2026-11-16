# Serene pose sidecar 1.0

Wire-contract-ready, 13 November 2026. Implementation/qualification status is in
`docs/pose_recorder_handoff.md`. This is a separate `.spose` visualization file,
not a `.serene` scientific archive. No HDF5, ZFP, core, Chrono or Qt is required.
Unknown mandatory semantics are rejected, never interpreted as scientific channels.

## Catalog and conventions

All integer and binary64 fields below are explicitly little-endian. IEEE-754 binary64
is required. Counts/offsets are unsigned 64-bit unless stated. Reserved fields are zero.
CRC is IEEE CRC-32 (reflected polynomial 0xedb88320, initial/final xor 0xffffffff;
Python `zlib.crc32`). Header CRC is computed over the entire header with its own four
bytes zero. CRC detects accidental corruption; it is not authentication.

The UTF-8 JSON catalog has no byte-order mark and is an object with:

- `catalog_version`: integer 1; `recording_id`, `run_id`, `preparation_id`: nonempty strings.
- `world`: `{ "axes": "right_handed_xyz", "length_unit": "m",
  "orientation": "hamilton_wxyz_local_to_world" }`.
- `occurrences`: array of `{id, parent_id, path, kind}`. IDs are unique unsigned integers,
  parent is null or an existing ID; the hierarchy is acyclic. Path is a nonempty canonical
  occurrence path, unique in the catalog. Kind is `assembly`, `flexible`, `rigid` or `anchor`.
- `samples`: dense fixed frame order, each `{id, kind, occurrence_id, ...}`. ID equals its
  zero-based array position. Kind is `structural_node`, `body_port` or `anchor_port`.
  Structural samples additionally have `material_coordinate_m` (finite) and `side`
  (`none`, `left`, `right`); ports have nonempty `port_id`. All samples refer to occurrences.
  Port identities are physical identities, not scoped aliases; they are unique within
  their occurrence; anchor_port belongs to an anchor occurrence and body_port does not. Actual offsets/orientations are present in poses, not replaced by
  the closest structural node. A body port can use a qualified fluid-defined frame.
- `segments`: array of `{id, occurrence_id, sample_ids}`; IDs are unique, ordered sample
  IDs must be structural samples of that occurrence and cannot repeat within one segment. Each segment explicitly defines
  continuity. Equal coordinates do not authorize joining segments. Empty arrays are valid.
- `port_aliases`: array of `{scope_id, alias, sample_id}` records, separate from physical
  identities; `connections`: array of `{id, kind, scope_id, junction_id, first_endpoint_id,
  second_endpoint_id, first_sample_id, second_sample_id}` records. Both arrays are required
  (empty when none). Scope IDs refer to occurrences, endpoint/sample references name actual
  ports, kind and endpoint IDs are nonempty strings; junction_id is a nonempty string or null.
  Preserve every existing resolved connection, its endpoint identities, kind, scope and
  shared junction grouping. The adapter must not omit available connectivity. No new
  connection is inferred from hierarchy or co-location. An unavailable required resolved
  graph is a setup diagnostic, not an empty graph; empty means known no resolved connections.

Additional JSON metadata is allowed but cannot change these required meanings or binary
layout. Producers may preserve source/effective fingerprints, component IDs, native node
IDs and resolved connection kinds here. The adapter preserves available display/component
names, structural spanline identity, and port material coordinates/sides rather than
dropping them. `recording_settings` may record authored interval, fixed integration step,
resolved stride, initial/target flags and actual selected schedule; these are descriptive
application metadata and never override the explicit frame stamps. Hierarchy and co-location do not imply connection.
The topology is immutable; a change starts another file. No standalone fluid/load/wind
stream is produced by v1. The sample count equals `samples.size()`.

## Bootstrap (64 bytes), then exactly catalog_bytes UTF-8 bytes

| Offset | Type | Meaning |
|---:|---|---|
| 0 | byte[8] | `SRNPOSE\0` |
| 8, 10 | u16, u16 | major=1, minor=0 |
| 12 | u32 | header bytes=64 |
| 16 | u64 | required feature mask=0 |
| 24 | u64 | catalog byte count |
| 32 | u64 | sample count |
| 40 | u64 | maximum decoded chunk payload bytes |
| 48 | u32 | catalog CRC |
| 52 | u32 | bootstrap CRC |
| 56 | u64 | reserved=0 |

Hard v1 bounds: catalog <=16 MiB, samples <=1,000,000, decoded/stored chunk <=64 MiB,
all file offsets/counters <=INT64_MAX. Readers may impose smaller documented limits.
A full frame must fit the declared chunk limit. No implicit padding exists anywhere.

## Chunk header (80 bytes), then stored_bytes payload

| Offset | Type | Meaning |
|---:|---|---|
| 0 | byte[4] | `PCHK` |
| 4 | u16 | kind: 1 poses, 2 linked seek index, 3 completion |
| 6 | u16 | flags: bit0 required; other bits reserved |
| 8 | u32 | header bytes=80 |
| 12 | u32 | encoding: 0 raw; others reserved |
| 16 | u32 | stream ID: 1 poses; 0 index/completion |
| 20 | u32 | layout version=1 |
| 24 | u64 | consecutive chunk sequence, starting at 0 |
| 32 | u64 | record count (frames for poses; 1 for index/completion) |
| 40 | u64 | first frame ordinal (0 for index/completion) |
| 48, 56 | u64, u64 | stored bytes, decoded bytes; equal for raw |
| 64 | u32 | payload CRC |
| 68 | u32 | chunk-header CRC |
| 72 | u64 | prior pose-chunk offset, or 0; zero for index/completion |

V1 writer marks all three kinds required. A future unrecognized optional chunk (bit0=0)
can be skipped by checked stored length after header/payload integrity verification;
it does not add pose records. An unrecognized required kind/stream/layout/encoding fails.
Unknown encoding in an optional unknown stream may be skipped without decoding, subject
to both stored and decoded bounds. Unsupported major/minor/required bootstrap features fail.
No JSON field can waive these rules. Each chunk is independently decodable; no delta chain.

## Pose payload

Each frame is 64 + 56*sample_count bytes. It begins with this 64-byte stamp:

| Offset | Type | Meaning |
|---:|---|---|
| 0 | u64 | consecutive recording frame ordinal, starting at 0 |
| 8 | u32 | analysis: 0 prepared, 1 transient, 2 certified static equilibrium |
| 12 | u32 | event bits: 1 prepared, 2 initial, 4 scheduled, 8 target |
| 16 | f64 | actual finite physical evaluation time |
| 24 | u64 | accepted step/equilibrium ID; zero for prepared |
| 32 | f64 | finite static load factor; zero for other analyses |
| 40 | u64 | sample count |
| 48, 56 | u64, u64 | reserved zero |

Then each sample has seven f64 values: px,py,pz,qw,qx,qy,qz. Positions are world metres;
quaternions map local N/actual-port axes to world, Hamilton scalar-first. All values finite;
quaternion squared norm differs from 1 by at most 1e-6. No normalization/sign adjustment
is performed. Sign continuity is not promised. No rates, loads or wind are inferred.
Prepared is allowed once as first frame, event=1. Transient events use a nonempty subset
of 2/4/8; initial has step ID 0 and can only be the first transient frame. Later transient
IDs and times strictly increase. Static event=8 and only one static frame is allowed.
Prepared may precede one analysis; transient/static cannot mix. Prepared time does not
participate in transient ordering and is not an interpolation endpoint for accepted frames.
Target is terminal.
An off-stride target is one frame, with target bit, not an interpolated state.

## Bounded seek index, completion and footer

Each pose chunk points backward to its predecessor. This is a linked seek index: a reader
can seek backward through batch headers without decoding intervening poses. Seeking an
arbitrary frame is O(number of batches), not a promised O(log n) lookup. The writer stores
only first/last chunk offsets and counters, O(1) index memory; no duration-sized index list.
At finalization kind2 has four u64 values: pose_chunk_count, total_frames,
first_pose_chunk_offset, last_pose_chunk_offset (both 0 for no frames). Readers can build
an optional faster index externally. The index payload is 32 bytes.

Kind3 completion has a 64-byte payload: u32 run outcome, u32 capture outcome,
u32 recorder record outcome (=1 fully serialized record), u32 reserved0; then u64 accepted_frames,
u64 persisted_frames, u64 pose_chunks, u64 last_frame_ordinal (UINT64_MAX if none),
u64 index_chunk_offset, u64 reserved0. Outcomes: 0 not_run, 1 completed, 2 failed,
3 stopped. Completion does not imply numerical/capture success. Accepted and persisted
counts are equal on publication. No error details are fabricated from outcome codes.

A 32-byte footer follows: `POSEEND\0` (8 bytes), u64 index offset, u64 completion offset,
u32 CRC over all32 bytes with this field zero, u32 reserved0. There are no trailing bytes.
A reader declares a file complete only after validating index, completion, footer and
counts/offsets against the chunks. A complete file may describe a failed numerical run. Target denotes the numerically
accepted/certified target even if later mandatory output delivery fails; it does not
assert successful overall execution.

## Ownership, buffering and publication

`serene::outputs::pose::Recorder` owns one setup-allocated byte batch. Capacity is bounded
by both frames and bytes, independently of duration. Appending copies an entire borrowed
frame; no borrowed pointer survives. Full and final partial batches flush synchronously.
`accepted_frames` counts validated/copied frames, including a frame whose ensuing flush
fails. `persisted_frames` counts complete chunks with checked stdio flush; this is not
fsync/power-loss durability. A callback returning failure is not acknowledged by core,
even if the recorder's accepted counter includes the attempted frame.

Destination and `<destination>.partial` must be absent. The latter is exclusively created.
Failures latch, stop appending, retain the incomplete file, and never silently overwrite.
Destruction closes but does not finalize/publish. Checked finalization writes the remaining
batch/index/completion/footer, flushes/closes, then publishes by a same-directory no-clobber
hard link and removes its own partial name. Filesystems without hard-link support report
publication failure and preserve the complete partial file; no overwrite fallback exists.
A failed removal after successful link publication may leave an identical partial name.
A close/publication failure can leave a wire-complete `.partial`: the serialized record
outcome means the completion record was assembled, not that close or publication succeeded.
A reader reports wire completeness separately from the recorder API publication status.

Without valid completion/footer, scan only CRC-validated complete chunks. Do not expose
frames from a truncated/corrupt payload; report the prefix as incomplete, never complete.
Malformed required semantics are errors; recovery does not silently bless them. Buffered
RAM frames may be lost on interruption. Capture, numerical and recorder outcomes remain
independent. No automatic retry or asynchronous queue is implied.

Future typed optional streams can carry separate populations/cadences/units and layouts.
Fluid poses, force+moment+application-point records and absolute wind samples must not be
silently reinterpreted as v1 N/port poses. Compression remains reserved, raw only for v1.
