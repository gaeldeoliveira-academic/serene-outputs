# Serene native outputs 1.0 â€” normative candidate N01

Status: joint G0 review candidate, not a released implementation. The coordinator must accept the specification and fixture identity before public v1 output is emitted. Package/library versions and input schema versions are independent of this file version. Normative words MUST, MUST NOT and MAY have their ordinary requirements meaning.

## 1. Scope and physical primitives

A `.serene.h5` file is an HDF5 container, with no prepended bytes. The application identity is `serene.outputs`, schema major 1, minor 0. Initial supported physical encoding is the HDF5 1.10-compatible format (writer bounds V110 through V110); files produced by newer HDF5 libraries must retain that encoding. V110 bounds are a writer contract; a reopened h5py libver tuple is not proof of the original upper bound. Readers validate actual supported object/type/storage features and fail unsupported encodings. Initial decoder profile is little-endian binary64 on little-endian hosts. No filter plugin, solver, or executable stored content is needed.

All named schema objects below are ordinary internal hard-linked groups or datasets. Reject soft/external links, object aliases/cycles among schema objects, dataset filter pipelines, virtual datasets and external raw storage before content reads. Contiguous or unfiltered chunked datasets are permitted. Scalar numeric datasets have rank zero; vectors have rank one. Empty vectors use zero-length SIMPLE dataspaces, never HDF5 NULL dataspaces. File types are exact standard little-endian unsigned u8/u32/u64, signed i8/i32, or IEEE f64. Conversion from a different on-disk type is not validation. Unknown optional objects can be ignored without traversal; unknown required feature tokens fail. Required objects cannot be replaced by attributes. Attributes are not normative in 1.0.

These reusable structures have exact layouts:

| Name | Physical representation |
|---|---|
| `text` | Rank-one u8 UTF-8 bytes, strict decoding, no BOM and no U+0000. Not an HDF5 variable-length string. Empty text is allowed only where noted. |
| `strings[N]` | Group with `bytes:u8[K]` and `offsets:u64[N+1]`. First offset 0, nondecreasing, last K; each slice strictly decodes under text rules. Optional entries also have `present:u8[N]` in {0,1}; absent slice is empty, present empty is distinct. |
| `digest` | u8[32], raw SHA-256 digest bytes, not hexadecimal text. |
| `document` | Group with `state:u8` plus, for state included/partial, `bytes:u8[K]`, `media_type:text`, `sha256:digest`. Absent states have only `state`. JSON media type is `application/json`; document bytes are retained exactly and document JSON/UTF-8 validity is checked separately on explicit access, never executed. The text primitive rules govern identifiers/media types, not rewriting original document bytes. |

All presence and equality flags are exactly0 or1; absent numerical values use integer0 or binary64 positive zero as applicable.

Section/document state codes: 0=included, 1=disabled, 2=unavailable, 3=partial. A present empty document is different from absence; JSON empty bytes are invalid JSON. Strict JSON forbids NaN/Infinity constants and duplicate object keys; document access checks these rather than accepting Python default parser extensions. Partial JSON is evidence only and MUST NOT be returned as a parsed complete document. Full verification checks its digest and declared partial state without failing merely for expected truncation; included JSON is strictly parsed only under an explicit checked materialization budget. Input source JSON has included or unavailable state, never silently normalized. Byte digests cover all exact retained bytes including whitespace and newline sequences.

### Limits

The initial protocol rejects: more than 65,536 runs; more than 1,000,000 channels or requests per run; text entries over 1,048,576 bytes; aggregate catalog text over 268,435,456 bytes per run or global catalog; documents/log byte streams over 4,294,967,296 bytes each; individual complete JSONL records over 1,048,576 bytes including the newline; frame counts over 2^32; dimensions over 2^40; any product/offset extent over 2^63-1; frame decoded counts over 1,048,576; encoded frame bytes over 16,777,216; schema path depth over 16. Readers apply configurable *stricter* limits before allocation. Default reader retained metadata budget is 64 MiB, document materialization 16 MiB and explicit numerical materialization 256 MiB. Streaming access may handle a larger protocol-valid document within a bounded tile budget. No HDF5 extent or caller-provided count is trusted merely because it fits size_t. Limits include retained metadata, not only transient read tiles.

## 2. Required root and format

Required root groups are `/format` and `/results` with `/results/index`, `/results/timeseries` and `/results/steady_state`, including when one kind is empty. Optional root groups are `/inputs`, `/logs`, `/context`. Modal and postprocessing payloads are not supported.

`/format` contains these required datasets/groups:

| Member | Type and rule |
|---|---|
| name | text, exactly `serene.outputs` |
| version_major, version_minor | u32 scalars, 1 and 0 |
| file_id | text, lowercase UUID with hyphens |
| created_utc | text, UTC RFC3339 `YYYY-MM-DDTHH:MM:SS[.fraction]Z` |
| writer_name, writer_version | nonempty text; package identity, not protocol capability |
| write_state | text, exactly `complete` in a readable final file |
| required_features | strings[F], unique nonempty tokens, at most1,024 entries |
| sections | group with inputs/logs/context u8 scalar state codes |

Initial required feature tokens are exactly `serene-layout-1`, `utf8-pools-1`, `frames-77-le-1`, `zfp-5-le64-serial-1`. Features added later require explicit reader support; an unknown major fails. A newer minor can only be accepted when all required features are supported and existing required semantics are unchanged. Unknown optional metadata does not imply support for unknown codecs. `write_state=complete` means successful container finalization, not a successful numerical solve or power-loss durability guarantee.

Each optional root branch is present iff its format section state is included or partial. Disabled/unavailable branches MUST be absent. Disabling archive log retention is independent of disabling runtime logging. State partial means useful data exists with explicitly declared incompleteness, not permission to omit required schema objects without an absence state. For inputs/context: included means all enabled required child documents included; partial means at least one included/partial child and at least one unavailable/partial child; if no child bytes are available the root branch is unavailable/absent. A producer-disabled root is disabled/absent regardless of runtime execution.

## 3. Run index and identities

`/results/index` is a columnar table with N rows: `run_index:u64[N]` equals 0..N-1; `run_id:strings[N]` contains nonempty unique authored IDs; `run_name:strings[N]` includes presence flags and may contain duplicate display names; `kind:u8[N]` is 0=timeseries, 1=steady_state; `group_path:strings[N]` contains the exact path for the run. The key is `r` plus decimal global run index padded to at least six digits, for example `/results/steady_state/r000001`. IDs/names are never embedded in HDF5 paths. Every run group is indexed once and every index path exists with matching kind; group iteration order has no meaning. Name lookup rejects ambiguity. A length-one catalog is the first production delivery; bundle fixtures exercise the same wire structure without claiming application bundling support.

`info.run_id` MUST equal the global catalog run_id. Each prepared run group contains required `info`, `channels`, `requests`, `values`; and `time:f64[T]` for timeseries or `load_levels:f64[L]` for steady state. Optional `step_index:u64[T]` / `level_index:u64[L]` carry *actual accepted solver identities* only when known. They are strictly increasing and their origin/convention is declared in info; stored row ordinal is inherent and MUST NOT be substituted for solver identity. Absence is explicit. Times are finite and strictly increasing. Load parameters are finite but may repeat/decrease; they are not time. Axis bytes are lossless.

### Required info

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
| solver_configuration, sampling_configuration, execution | document groups, unavailable when not known |

If `catalog_state=unavailable`, only `info` exists in that run group; channels, requests, values and axes MUST be absent, committed_count=0, observation_status=not_started, identity_axis_present=0. This is allowed only for preparation_failure/skipped/cancelled before preparation. Info fields and unavailable document states remain required; unknown requested bounds have presence0. A prepared empty selection instead has catalog_state=included and actual empty schema structures.

`last_recorded_present` iff committed_count>0 and equals the final stored axis value. Last accepted solver state is tracked independently, including accepted advances between output samples and initial accepted configuration; its presence may be true with accepted_count=0. A failed observation cannot commit a partial row. `run_outcome` is the terminal cause; the three independent status domains preserve other outcomes. All present numerical info fields are finite; requested bounds must be ordered when known. `run_outcome=completed` requires numerical_status=target_reached, observation_status=complete or disabled, logging_status=complete or disabled, and catalog_state=included. Unavailable catalog requires numerical_status=not_started and last_accepted_present=0. Other domain failures retain their independent status and terminal cause; no numerical success is inferred from complete archive publication. The producer must not relabel logger rejection after solver acceptance as numerical failure. An observer/logger stop before target uses numerical_status=stopped_before_target with an independent terminal cause; complete observations mean complete capture over the executed output clock, not that the last sparse sample equals requested_end. A stop at the successfully reached target may retain numerical_status=target_reached. An archive/publication error is an output-library/application status, not a fabricated failed solve inside a success-named partial file.

### Catalogs and effective requests

`channels` has `channel_index:u64[C]` equal to 0..C-1, `scalar_id`, `group_id`, `request_id`, `component_path`, `unit` as strings[C], and `request_index:u64[C]`. Scalar IDs are nonempty/unique. Basic identities and units remain present when detailed request metadata is disabled. Units may be empty only for dimensionless quantities explicitly designated by the producer. Scalar order is the prepared core order; names are never parsed to recover semantics.

`requests` has `group_id`, `request_id`, `schema_version`, `construction_profile` as strings[R], unique (group_id,request_id), and `effective_state:u8` (included/disabled/unavailable). When included it additionally has `effective_json:strings[R]` (no presence flags), and `effective_sha256:u8[R,32]`. During run selection each JSON entry is checked under the text-entry bound for UTF-8, its digest, and strict JSON object syntax (no duplicate keys or nonfinite literals). Each entry retains the effective request semantics, including frames/observer/reference point, generated by the schema/core owner. Channels reference a valid request_index whose group/request IDs match. Same request_id in different groups is valid. A request may have no selected scalar; unused request rows are allowed, including C=0 with R>=0 retained request identities. All channel columns have exactly C rows, all request identity columns exactly R rows, and effective_sha256 has exactly shape[R,32]. Effective JSON digests cover the exact UTF-8 slice bytes. When disabled/unavailable those two columns are absent; identity relation remains available.

## 4. Values, profiles and canonical frames

Timeseries logical shape is [C,T]; steady state [L,C]. `values` is a group, never an implicit filtered numeric dataset. Required members: `shape:u64[2]`, `dtype:text` exactly `float64`, `axis_names:strings[2]` (`channel`,`sample`) or (`level`,`channel`), `frame_schema:u32`=1, `nominal_frame_values:u32`, `row_profile_index:u32[outer]`, `profiles` group, `frame_index` compound[F], `payload:u8[P]`, `index_sha256:digest`, `verification` group.

Nominal frame values B is a positive multiple of 4, at most 1,048,576; default 65,536. Frames are per logical outer row and exactly partition inner extents into B-sized chunks except the final tail. Catalog C equals the channel dimension of shape; committed_count equals the sample/level dimension and axis length. No frame crosses a row. True tail count is passed to ZFP; logical padding is forbidden. Empty selection has C=0 and axis length 0, shape[0,0], an empty channel catalog and no values or axis entries; request identities may remain as allowed in section3. A failed run with known channels and no committed sample is [C,0] or [0,C], not [0,0]. Zero inner or outer extent implies no frames/payload. It is valid to retain a failed committed prefix; failed candidates are never rows.

The rank-one frame_index HDF5 compound type has itemsize exactly 77, the listed member names/order, exact offsets and no implicit padding:

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

Canonical bytes concatenate these members using the specified little-endian numeric representation and raw digest bytes. `index_sha256` hashes the concatenation of all canonical records in order (SHA256(empty) for no records). C++ memory padding is never hashed or written as the wire record. Each record digest hashes exactly its stored payload bytes including stream padding. Records are ordered outer_index then inner_begin, cover every logical value once, and payload extents are contiguous from zero through P with no gaps or overlap. Checked addition/multiplication precede reads.

Encoding 0 is raw little-endian binary64 and byte_count=8*count. Encoding 1 is a complete ZFP_HEADER_FULL serial 1D binary64 stream, including its header and final word padding. The indexed count/type/mode/profile must agree with the header; consumed bytes must equal byte_count. count is positive and at most B. Raw frames keep the requested row profile index; every record profile equals row_profile_index[outer_index]. Choose raw when compressed bytes are equal to or larger than raw bytes. Raw fallback handles size, not a failed codec/verification check. Writers reject nonfinite committed selected values.

### Profile table

`profiles` is columnar with 1<=M<=1,000,000 rows: `profile_index:u32[M]` equals 0..M-1; `codec`, `library_version`, `library_revision`, `byte_order`, `execution` as strings[M]; `mode:u8[M]`; `codec_version:u32[M]`; `stream_word_bits:u32[M]`; `rounding:i8[M]`; `mode_code:u64[M]`; `tolerance_present:u8[M]`; `requested_tolerance`, `effective_tolerance` as f64[M]. All profile columns have exactly M rows and every row_profile_index is <M even if that logical row has zero inner extent. Initial exact profile values are codec=`zfp`, version=`1.0.1`, revision=`f40868a6a1c190c802e7d8b5987064f044bf7812`, byte_order=`little`, execution=`serial`, mode=0=reversible, codec_version=5, stream_word_bits=64, rounding=0=ROUND_NEVER, mode_code=2176, tolerance_present=0 and both tolerances +0. OpenMP/CUDA/DAZ/tight-error/strided-bitstream options are disabled. Producer library build/CRT provenance is recorded in context; CRT is not a wire setting. The full header MUST use the canonical SHORT reversible mode code 2176, with effective (minbits,maxbits,maxprec,minexp)=(1,16658,64,-1075). A long expert header is rejected even if zfp_stream_mode would normalize it to2176. For this pinned profile the required short header is32+52+12=96bits; the parser still bounds the pinned codec maximum148bits before interpreting it. Other profile values fail until explicitly qualified. Fixed accuracy will require a new required feature and later N14 acceptance; static lossy remains unsupported.

The generic pinned-codec header maximum is148bits; this protocol requires the96-bit canonical short reversible header derived from the specified fields, not inferred from the earlier probe. Future profiles cannot assume that same short-header length. A parser must first bound bytes, inspect magic/shape/mode without trusting header-driven allocations, check expected count/type/dimensionality/profile, and use a queried maximum-size bound. zfp_stream_maximum_size includes the maximum header allowance for this revision. Native scratch is sized before decoding. Ordinary ZFP bitstream routines are not a sandbox for arbitrary hostile input; parser bounds/fuzzing are a separate N03 obligation.

`verification` contains `max_abs_error:f64[F]`, `rms_error:f64[F]`, `verified_count:u64[F]`, `bitwise_equal:u8[F]`. Exact-mode values are +0,+0,count,1. These are producer assertions over originals, not independent proof available to a reader. Required frame/index hashes detect accidental corruption only. Axes, catalogs and all metadata are **not** covered by those hashes; no authenticity or whole-file integrity claim follows.

## 5. Optional provenance

`inputs` when present contains required document groups `source_json`, `effective_json`, `overrides_json`, `resources_json`. Source preserves original bytes; effective is resolved/defaulted/overridden representation; overrides documents authored versus CLI changes. Unavailable documents carry state unavailable. Resource manifests describe references/hashes/embedding honestly; copying input JSON does not embed referenced resources.

`context` when present contains document `execution_json`: actual argv is a JSON string array; versions/revisions/dirty states, compiler/build/solver/backend/codec/HDF5 identities, OS/architecture, working directory, execution timestamps and deliberate environment allowlist are retained when known. No unrestricted environment capture or stored-path reopening.

`logs` when present contains `records_jsonl:u8[K]`, `sha256:digest`, `record_offsets:u64[R+1]`, `complete_record_count:u64`=R, `trailing_fragment_bytes:u64`, `dropped_count_present:u8`, `dropped_count:u64`, `schema:text`, `info_json:document`. Offsets start0 and each next offset is immediately after a complete LF-terminated record (CRLF bytes remain unchanged). The final offset marks the end of the last complete record and may be less than K; trailing_fragment_bytes=K-finaloffset. Empty logs have offsets[0],count0,fragment0. A partial stream retains the trailing bytes but never presents them as a complete JSON record. Dropped count unknown uses present0 and count0; zero is not inferred. Every complete record must parse as a strict JSON object when iterated/verified. Check each offset difference against the protocol 1 MiB record bound and the caller's stricter parse budget before reading/allocating/parsing that record; an otherwise valid over-budget record gives resource_limit. Scan/hash a retained trailing fragment in bounded tiles without assembling or parsing it. Complete-record scans likewise do not materialize the whole log. A fragment, known dropped records, or a known incomplete capture (including a clean LF-terminated prefix with unknown drop count) makes the root logs state partial; info_json records the incompleteness reason. Clean record boundaries alone do not certify complete logging. Initial events need not already contain authored run IDs; per-file run association must be declared in info_json, not inferred from thread ID. N12 later defines richer typed events without changing raw evidence.

## 6. Validation, ownership, status and publication

Opening reads bounded format/global catalog metadata only. Selecting a run validates its catalogs/axes/shape/profile and streams its entire frame index in bounded tiles, O(number of frames) index I/O, without reading payload. It caches structural validation only for the same immutable open file session. Selective reads hash/check/decode only intersecting frames and reject nonfinite raw/decoded selected values before returning them. `verify_all` first structurally validates every run (including verification columns and effective-request digests/strict JSON objects), then streams every payload and retained document/log digest/JSONL record; it reports structural/integrity status separately from run outcome. Corruption in an unselected payload can leave another selective read usable; full verification detects it. Reader APIs use half-open unit-stride slices, owned result arrays by default, and explicit materialization budgets; lazy handles expire on close.

C++ components are `serene::outputs_model`, `serene::outputs_codec`, `serene::outputs_hdf5`. The model headers expose no HDF5/ZFP/Chrono types. PreparedExport owns copied immutable descriptions, limits and reusable bounded workspaces during initialization. ArchiveSession opens only after active simulation and logger completion/flush/finish, audit end, then checked physical log-file close. RunResultView borrows actual committed axes, catalog and values through checked strides or a non-allocating fill callback/context. No whole-history transpose or compressed-byte accumulation. Owners stay alive and immutable through publication/retry; freeze and fill are allocation-free. Codec extension links codec/ZFP only; h5py is the companion's sole HDF5 interface.

Output status codes are stable model enums: ok, invalid_contract, unsupported_feature, unsupported_profile, resource_limit, codec_failure, verification_failure, io_failure, publication_failure, invalid_state. Bounded operation/run/frame indices and native error codes accompany them; human strings are formatted after active execution. These are separate from numerical/observation/logging outcomes. Destructors clean up but cannot declare publication success.

A session writes a unique temporary sibling, streams frames/index/documents in reusable buffers, checks flush and physical close, then publishes with no-clobber only; replacement is unsupported in the initial interface. A failed publication never returns success and leaves no success-named partial output. A numerical-failure archive may be complete. One-run sources stay alive for retry. Incomplete temporary HDF5 files are not a recovery protocol; rename is not a power-loss durability promise. Later bundles will define per-run spool retention, disk budgeting and provenance merging before releasing sources.

## 7. Accepted storage interface for schema-owner implementation

Schema-owner review chooses input wire `3.0-wip.3`, construction profile `1.0`; archive schema remains 1.0. Remove `output.format` and reject the old format field/spellings without a compatibility layer. Keep required `output.path` as the artifact-directory setting and `--output DIR` as its override. Physical selectors, clocks, provider checks and explicit scalar capacity are unchanged.

Add required `output.storage` object. Its `kind` enum is required and explicit: `disabled`, `temporary_csv`, or `native_hdf5`; it has no default. The optional `native_hdf5` object is present exactly for matching kind; disabled/temporary_csv have no native payload. Native fields are:

| Field | Type / default / constraints |
|---|---|
| archive_file | Required nonempty path ending `.serene.h5`; no default filename |
| include_inputs, include_logs, include_context, include_effective_request_json | MetadataRetention enum `enabled` or `disabled`, each default enabled; use existing mapped enum style, not unsupported DOM booleans |
| codec | Required explicit `reversible` only, no default and no conditional payload |
| raw_fallback | `when_not_smaller` only, default; ties choose raw |
| frame_values | u64, default 65,536, multiple 4 in [4,1,048,576] |
| workspace_budget_bytes | Required positive u64, checked against prepared export requirements and core aggregate budget |
| overwrite_policy | `no_clobber` only, default |

Relative authored archive_file resolves against the effective artifact directory; an absolute archive path remains absolute even with --output. New `--archive FILE` is valid only for native_hdf5 and overrides the archive file; a relative CLI archive resolves against process cwd. Existing relative output.path/--output directory resolves against process cwd. Provenance records authored/defaulted/effective paths and CLI overrides. Reject aliases/collisions with inputs/logs/transfer artifacts before simulation. Disabled storage suppresses selected persistence only: diagnostics, runtime logging and certified transfer remain independent; selection/capacity/provider checks still apply. Temporary CSV is a development/test mode, not dual-written by default with native storage.

No N11 capture-history or N14 lossy controls are introduced in this initial interface. Core's `--initial-result DIRECTORY` contract still validates manifest.json v2 and all indexed digests before static_result.json; native selected outputs do not replace either artifact. External archive references stay outside the transfer manifest v2 confined files index. Current core omits optional static level_index until actual accepted identities are explicitly captured; attempted-level counters and row ordinals are not substitutes. Implementation goes through N07a generated schemas, N07b preparation, then N08/N09 core. N01 contains no competing parser.

## 8. Conformance and compatibility evidence

`fixtures/manifest.json` identifies tiny independently authored HDF5 files, expected bit patterns/metadata, negative-case invariant and all hashes. Authoring uses h5py and public zfpy directly, not a production Serene writer or reader. A small C++/CMake inspector separately checks canonical records, exact raw values and pinned public-ZFP header/count/consumption/decoded bits. Research /MT binaries may be used by that isolated matching-/MT inspector only; production dependencies must match the consumers' runtime and are rebuilt in N03.

Positive fixtures include C3xT10/B4 and L2xC5/B4, tails/raw tie/expansion, signed zero/subnormals, Unicode/scoped IDs, duplicate run display names, optional branches, failed prefixes and empty results. Negative files change one primary invariant with supporting hashes/extents repaired where needed. These are conformance inputs for future reader/writer tests, not claims that N01 implements a production validator, allocation qualification, core integration, bundle orchestration or cross-platform release.
