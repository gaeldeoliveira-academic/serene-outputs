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

