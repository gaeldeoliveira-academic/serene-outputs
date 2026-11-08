# Native outputs handoff — custom-container G0C review

The user chose an in-house container to reduce deployment and maintenance dependencies. Serene main directed a switch to `.serene` / `native_archive` / `serene::outputs_archive`. The model/view and exact ZFP codec remain reusable. **G0C is pending joint acceptance; no production backend migration has begun.** HDF5 implementation and receipts below are historical, not custom-container qualification or G1 acceptance.

The authoritative self-contained specification is `spec/FORMAT.md`, SHA-256 `1ac724b78d1b61fc289dd68489a181dbd92bd9f8f08d1684c5b27aa8503ff66c`; acceptance/freeze is governed by the coordinator receipt. Exact review candidate a0eae980 is preserved under spec/history. It fixes a128-byte bootstrap,64-byte dense-ID section directory, explicit little-endian arrays, bounded root/per-run JSON descriptors, binary UTF-8 catalog pools and unchanged canonical77-byte frame indexes. Each section has an exact SHA-256 digest; selective value reads use frame digests and full verification checks whole payload sections. JSON preflight/resource limits, explicit document parsing budgets and caller-owned returned objects are specified. No checksum is authentication or an OS file-immutability guarantee.

The independent corpus is `fixtures_custom/manifest.json`, SHA-256 `910ab1b766e79e015a78d4060db31d84b2b59eb265c904d1b7361493a7ddbf7a`:13 positive archives,81 targeted negative archives and185 pinned artifacts. `tools/conformance/custom_author.py` uses Python struct/hashlib/json, NumPy and public zfPy, with no HDF5 or Serene production helper. Original HDF5 fixtures are not overwritten. Positives include both orientations, mixed raw/ZFP, exact special finite bits, Unicode/scoped identities, empty/unprepared/failed prefixes, optional metadata, partial logs and a128 KiB original JSON document.

Independent C++ inspection is recorded in `build/g0c-cpp-evidence.json`, SHA-256 `2e031b7e2ab26241caa9c106cd7b94e37690eff4609723a413be722a9cb7ad21`. An isolated CMake/MSVC /MT inspector reads actual custom bootstraps/directories/JSON descriptors, verifies907 section digests and all185 artifact hashes, locates actual axes/index/payload sections, compares exact sidecars, then decodes actual ZFP bytes using the matching public research codec. CTest1/1 passed:13 prepared runs,74 frames(44 raw/30 ZFP),236 exact binary64 values. This trusted tiny-file inspector materializes bounded fixtures and does not claim production hostile-input validation, allocation behavior or installed custom-writer success. Python independent review passed all13 positive and81 negative cases, matching all185 artifact hashes and74 frames/236 values; theory receipt SHA-256 `25e403bcfc27e5d3db037a792c5174d27f0f4d1317eb2623994058f34aad58ed`. Final identity bridges and exact unchanged-artifact proof are in `docs/g0c_promotion.json` and `docs/g0c_document_clarification.json`; aggregate owner receipt `docs/g0c_evidence.json` SHA-256 `d8f30642e2d8fc3011d148e5b371176037ec3d0742a30910df3754a277e79643`. No repeated numerical decode was claimed for status/path-only promotion or the original-document escaped-NUL clarification.

Completed HDF5 work was stopped and preserved in `build/hdf5-historical-state.json`, SHA-256 `1c5a305625ff95ca758c4faa06d26aa1acee56e4b70579e8fbe09d5d64f8d610`. Release/Debug builds/tests, six relocated component consumers and memory/provenance measurements had passed; clean installed Python readback had not closed G1. `HDF5_ENABLE_PLUGIN_SUPPORT=OFF` disabled bundled plugin builds, not its native runtime loader. No HDF52.2 rebuild or patch work was started. Custom migration will use fresh build/install prefixes and preserve all existing historical receipts.

## Historical N02/N03/HDF5 implementation record

# Native outputs implementation handoff — N02/N03 passed, N04 in validation

G0 was accepted by Serene main in the theory coordinator receipt. N02 model and N03 exact codec components are implemented and passed their Release and relocated installation checks. N04 HDF5 writer is implemented and compiling; its paired-reader, failure, memory and Debug/install checks remain pending. **G1 is not accepted yet.** No commit, push, package publication or platform qualification beyond executed Windows checks is implied.

## N02/N03 consumer boundary

Public headers are under `include/serene/outputs`; targets are `serene::outputs_model`, `serene::outputs_codec`, `serene::outputs_hdf5`. `PreparedExport::prepare` owns copied/moved catalog metadata and gather/decode/index/copy buffers. Prepare a separate move-only `Codec` before entering active simulation, check `codec.workspace_bytes() <= prepared.plan().codec_bytes`, and count the aggregate plan once. `codec_bytes` is a conservative reservation including opaque native descriptor allowance; it is not a second allocation or an independently additive budget. Metadata/setup peak and allocator overhead are reported separately from this requested-payload reservation.

Timeseries from core sample-major storage uses `ValueView::strided(values, C, T, 1, C)`. Static level-major storage uses `(values, L, C, C, 1)`. `record_capacity` is the maximum planned records; the source shape/axis expose only the committed prefix. The core owner holds a freeze lease on every borrowed axis, accepted index, scalar value, info string and document/log byte source. It preserves owners unchanged until publication or abandoned retry. Freeze/fill do not allocate; lifetime errors cannot be detected generically from arbitrary raw pointers.

`ArchiveSession::open(prepared, codec, destination)` is post-gate only, after checked physical logger closure. It borrows both prepared objects. `append_run` accepts one immutable committed view and optional provenance; `finalize` checks writes, flush, file closure and no-clobber publication. Reuse retained sources in a new session after failure. The library does not parse original input/context/log JSON during saving: the caller guarantees included documents and complete JSONL records are valid and immutable. Effective request JSON is strictly validated during preparation. Saving copies/hashes exact bytes in bounded tiles and preserves partial trailing log bytes.

## Exact codec and shared Python source identity

The codec-only API is `Codec::create`, `inspect_header`, `encode`, `decode`, `max_values` and `workspace_bytes`. It links pinned serial ZFP and fixed-state SHA only, without model implementation, HDF5, Chrono or logging. Decode validates an explicit 96-bit short reversible header, expected count/type and bounded input before calling ZFP. External encoded bytes are copied into one bounded, zero-padded ingress buffer; the queried ZFP maximum is checked against the reserved capacity. Native descriptors and buffers are reused. Destination contents are unspecified on failed decode; callers needing atomic publication use scratch. EncodedFrame borrows codec storage until its next operation.

`docs/codec_snapshot.json` file SHA-256 is `821a9c583cc72cbc02a57076c9e7aac9037f1c0678bc1b58bcc9e307f8926911`. Its `content_identity_sha256` is `3a4f8d17348199ec708f706ecf540e0fa6fb23917f1b59a2aae08fe21a88df9d`: this hashes the canonical snapshot object before that identity field is added. The manifest's individual file hashes identify the exact nine shared source/license files; Python independently verified them. These are distinct hash domains, not mismatched source versions. The pinned ZFP archive and build options are recorded there and in `third_party/dependencies.json`.

## Executed foundation/codec evidence

`build/n03-cmake-evidence.json` SHA-256 `678ce0250688120126ec2f62ab77aa73a6cb108c50176966c73f82ca75a4f0b4` records Release model+codec CTest2/2, relocated installed model consumer1/1 and codec consumer1/1. The codec consumer links only outputs_codec, pinned ZFP and Windows system/runtime libraries; dumpbin shows no HDF5/model/solver DLL dependency. Installed CMake exports contain no machine-local source paths. Codec tests decoded all56 independent frames/176 binary64 values exactly, checked SHA known vectors, raw ties/expansion, signed zero/subnormals, capacities, malformed headers and20,000 header-boundary fuzz cases. Two thousand repeated encode/decode operations kept workspace capacity fixed and observed zero C++ allocations; that instrumentation does not claim complete coverage of unhooked C/native/OS allocation routes or hostile-stream safety.

Subsequent model-only metadata preflight improvements and documentation clarifications are included in the forthcoming N04 matching source receipt; the historical N03 receipt remains an exact snapshot of what it tested. Both /MD Release and /MDd Debug outputs/dependencies are being installed for core. No legacy core Debug waiver applies to these new output tests. Linux/macOS and remote CI are not run or advertised.

## Historical N01 receipt and decisions

# Native outputs handoff — N01 candidate

N00 was accepted by Serene main before this checkout was changed. This is the exclusive scoped C++ owner's N01 specification/conformance work. **G0 remains pending coordinator acceptance.** No production library has been implemented, no commit/push performed, and the existing unborn-main Git checkout was preserved.

## Current identities

- Specification: `spec/FORMAT.md`, SHA-256 `549d09f058008cd44a24b3402f1637e8ef4165467389ba913fbcd8636d6ae673`.
- Fixture manifest: `fixtures/manifest.json`, SHA-256 `d11891d170bf8eb5cb367cbb54f211e6a67fce053c004e4ece7c8caf0fc450e5`.
- Evidence/commands/source identities: `docs/n01_evidence.json`, SHA-256 `b33485497f25c30978656964f07416e7c31fb9b7304e002bafe19755c4407f50`.
- Eleven positive archives, 28 negative archives and 94 manifest-pinned archive/expected/sidecar files. Positive files cover both orientations, raw fallback ties/tails, signed zero/subnormals, Unicode/scoped IDs, duplicate display names, disabled/unavailable metadata, partial logs, failed prefixes, zero committed static/transient results and unavailable catalogs.

## Review decisions embodied in the candidate

All text uses bounded UTF-8 byte datasets/pools; exact little-endian 77-byte compound records and raw32-byte digests; SIMPLE zero-length dataspaces; request identity table retained independently of effective JSON; actual optional accepted identities distinct from stored row order; numerical, observation and logging outcomes separated; strict bounded JSON/JSONL with explicit partial evidence; finite decoded values; canonical short reversible ZFP profile with pinned effective tuple. Root's schema-owner storage interface is in section7: inputwire3.0-wip.3, retained artifact `output.path`, required tagged storage, explicit native filename and budget, enum metadata controls, unchanged physical selection and certified transfer.

The C++/Python handoff keeps PreparedExport metadata/workspaces separate from post-gate ArchiveSession. Borrowed committed data must survive publication/retry. Model/codec/HDF5 target names are fixed in the candidate; concrete C++ declarations arrive under N02 and must preserve these semantics. No third-party types enter model headers. Production dependencies must match consumers' CRT; research /MT binaries are only used for this isolated N01 probe.

## Executed checks

The fixture author imports h5py/NumPy/public zfpy directly and no Serene production helper. It independently compresses/decompresses every tiny frame. A CMake/MSVC C++23 inspector independently parses and re-encodes canonical records, hashes payloads through Windows BCrypt, checks pinned ZFP header/mode/consumed bytes and compares exact decoded/raw bits with sidecars. Current corpus passed **11 prepared run views, 56 frames (34 raw, 22 ZFP), 176 binary64 values**. A preceding CTest run passed1/1; exact timing/order is retained in evidence rather than implying unexecuted tests. Python-side independent HDF5 inspection passed: all94 artifact hashes,11 positive and28 targeted negative cases, independent header/record parsing and176 exact decoded values. Its separate theory receipt is `validation/native_output_g0_python_review.json`, SHA-256 `59ff8644c8802eda1155d136144e865a02c7dd4b6e0074fb3ea6b84d9c880513`. That review used specification40f6301f before the final no-wire-change text clarifications (empty channel catalog versus retained request identities, header bounds wording, explicit flag/feature/profile count bounds).

The negative fixture manifest names the primary expected invariant failure. Supporting index hashes and payload extents are repaired for mutations where needed. These files are *inputs for future implementation tests*; N01 does not claim a production validator has rejected them. The long-expert header negative remains valid public-ZFP data but violates this protocol's canonical short header.

## Next consumers and limitations

Root accepts G0 after Python/core/design/schema co-review. Then N02 implements owned descriptions/prepared capacities and borrowed views, N03 exact codec, N04 HDF5 publication; Python N05/N06 can consume the pinned fixtures/spec. Packaging N15a accompanies those components. Native/schema/core acceptance must not be inferred from these tiny fixtures or D's earlier CSV/provider receipt. No Linux/macOS, memory, install, hostile-input, core-native, bundle-orchestration or lossy qualification is claimed.

Final core/design review agrees: initial publication is no-clobber only; replacement is unsupported. Core omits unavailable actual static level IDs, and external native archive references remain outside transfer manifest v2 confined files. These prose clarifications leave fixture bytes unchanged.

## N07 discriminator amendment

Coordinator-approved runtime evidence requires explicit `storage.kind` and explicit `native_hdf5.codec=reversible`, without defaults. Other defaults remain unchanged. Current specification SHA-256 `ea598cdf51a3013417b80bcc4312fc294f72b70aadff66ab59ba8d17520b14ee`; original accepted G0 text is preserved at `spec/history/FORMAT-g0.md`. See `docs/n07_storage_addendum.json`. Archive wire and all fixtures are unchanged; historical G0/N01 evidence is not rewritten.
