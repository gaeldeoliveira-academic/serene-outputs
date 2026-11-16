#pragma once
// Private archive/merger implementation interface; never installed.
#include <serene/outputs/codec.hpp>
#include <serene/outputs/model.hpp>
#include <serene/outputs/document.hpp>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace serene::outputs::detail {
using Json = nlohmann::json;
/** Owned producer/file binding supplied by the bundle importer. Used by VerifiedSpoolData::open to tie retained bytes to a pre-solve authored digest. A trusted binding requires complete producer/run/dependency identity and an exact file extent; untrusted imports are restricted capabilities, not guessed producer provenance. */
struct SpoolBinding {
    Digest expected_file_sha256{}, ///< Exact complete-spool SHA-256 expected by the binding.
     authored_input_sha256{}; ///< Immutable pre-solve authored original-input SHA-256.
    std::uint64_t expected_file_bytes{}; ///< Exact file extent; zero requests the restricted unbound-import path.
    std::string expected_run_id, ///< Owned expected single-run identity.
     producer_identity, ///< Owned immutable producer provenance label.
     dependency_identity; ///< Owned immutable dependency-set identity label.
    bool trusted_manifest_binding{}; ///< Caller asserts an immutable producer manifest bound before solve; not inferred from writer_name.
};
/** Admission ceilings for one verified spool working set. Bundle/public-preflight adapters supply these independently of retained numerical history size. Byte limits are conservative planning charges; the complete metadata graph is retained while numerical sections are visited through bounded buffers. */
struct SpoolReadLimits {
    std::uint64_t metadata_budget_bytes{64ull*1024*1024}; ///< Maximum retained plus temporary metadata charge in bytes.
    std::uint64_t workspace_budget_bytes{64ull*1024*1024}; ///< Maximum total admitted metadata, control and I/O/codec working bytes.
    std::uint64_t copy_tile_bytes{65536}; ///< Reusable I/O/hash tile; admitted range is 4096 through 1048576 bytes.
    std::uint64_t document_bytes{16ull*1024*1024}; ///< Maximum materialized complete JSON document bytes; raw/partial retention uses streamed hashing.
    std::uint64_t document_parser_budget_bytes{64ull*1024*1024}; ///< Conservative complexity charge allowed for strict document JSON parsing.
};
/// Validate the retained common original before a spool working set is resident.
/// Included JSON accepts any strict JSON value, as the original-document contract
/// requires; other media types retain exact bytes and are only hash-verified.
/// document is the borrowed common original, authored_digest its immutable expected SHA-256,
/// and limits the independent admission ceilings. Included JSON may be any strict value;
/// partial originals are an unsupported merger capability. Returns peak workspace bytes
/// or Status; inactive documents have no source reads. Borrowed bytes stay immutable.
std::expected<std::uint64_t,Status> validate_original_document(
    const DocumentView&, const Digest& authored_digest, SpoolReadLimits={});
/** Owned canonical section descriptor retained by VerifiedSpoolData for bounded reads and merger remapping. Payload bytes remain in the open file; header() verifies contiguity and digest before exposing this descriptor. */
struct DirectoryEntry {
    std::uint32_t kind{}; ///< Canonical section-kind code; supported wire values are one through eight.
    std::uint64_t offset{}, ///< Absolute payload byte offset inside the open spool.
     size{}; ///< Exact payload byte extent, including zero for an empty section.
    Digest digest{}; ///< SHA-256 of this exact section payload, including an empty payload.
};
/** Owned run-to-source association used by the bundle merger. Source indexes address the verified source catalog; role and order preserve authored document roles and log ordering without merging distinct evidence. */
struct SourceReference {
    std::uint8_t role{}; ///< Document role zero through four, or log role five.
    std::uint64_t source_index{}, ///< Index into the verified source catalog.
     order{}; ///< Within-role order; singleton document roles require zero.
};
/** Owned normalized provenance source retained for encoded bundle assembly. Legacy descriptors are copied into this common shape; their underlying payload section identities stay unchanged. Consumed by bundle source-role remapping after full validation. */
struct SourceMetadata {
    std::uint64_t source_index{}, ///< Dense normalized source identity.
     metadata_section{UINT64_MAX}; ///< Original metadata section; UINT64_MAX marks synthesized legacy metadata.
    std::uint8_t kind{}; ///< Source kind: zero is a document, one is JSONL log evidence.
    /// Normalized source_schema/source_index/kind plus document or JSONL fields.
    /// Legacy root descriptors are normalized without changing their data section IDs.
    Json metadata;
};
/** Owned typed-trace descriptor for bundle remapping. Its JSON owns criteria and dictionary/column descriptors; numerical diagnostic columns stay in the verified file and are scanned via bounded cursors during admission. */
struct TraceMetadata {
    std::uint8_t kind{}; ///< Closed diagnostic trace kind code in the range zero through four.
    std::uint64_t metadata_section{}; ///< Canonical metadata section belonging exclusively to this trace.
    Json metadata; ///< Owned validated trace JSON; no parser or source-buffer borrow escapes.
};
/** Compact owned result of complete spool admission, consumed by public VerifiedSpool and bundle planning. Numeric history remains on disk; summary fields describe the admitted graph, feature set and resource charges. */
struct SpoolSummary {
    std::string run_id, ///< Owned unique run identity copied from the catalog.
     run_name; ///< Owned optional display label; presence is recorded separately.
    bool run_name_present{}; ///< Whether the optional run-name pool entry was authored as present.
    RunKind kind{}; ///< Time-series or steady-state interpretation of matrix/axis rows.
    bool diagnostics{}, ///< Whether run-diagnostics-1 is required and admitted.
     indexed_provenance{}, ///< Whether provenance-sources-1 is required and admitted.
     fixed_accuracy{}; ///< Whether timeseries-zfp-fixed-accuracy-1 is required and admitted.
    std::uint64_t file_bytes{}, ///< Exact admitted file extent in bytes.
     section_count{}, ///< Canonical directory-entry population.
     maximum_frame_values{}; ///< Nominal frame-value capacity; zero when no values layout is available.
    std::uint64_t retained_metadata_bytes{}, ///< Conservative retained metadata-graph charge in bytes.
     workspace_bytes{}; ///< Peak admitted metadata/control/I/O/codec charge in bytes.
    Digest authored_input_sha256{}; ///< Original-input digest verified from retained bytes or a trusted pre-solve binding.
    SectionState original_state{SectionState::unavailable}; ///< Distinguishes included empty evidence, disabled capture and unavailable bytes.
};
/** Move-only private owner of a completely verified single-run spool and metadata graph.
 * Public VerifiedSpool/bundle assembly use open(), bounded read(), and a final
 * verify_unchanged() rehash. The FILE cursor, tile and Array caches are mutable
 * implementation state: do not read concurrently through one owner. Moving
 * transfers file ownership; default/moved-from objects have no usable file.
 * All numerical frames and section hashes are checked at open; decoded histories
 * are never retained. Error summaries without original values cannot re-prove a
 * producer's exact-real compression error, so admission checks their declared bounds. */
class VerifiedSpoolData {
public:
    /// Construct an empty owner; call open() to obtain an admitted file/graph.
    VerifiedSpoolData();
    /// Release owned metadata, tile and FILE handle; no final archive is published here.
    ~VerifiedSpoolData();
    /// Transfer file and graph ownership from the argument; the source retains no usable FILE.
    VerifiedSpoolData(VerifiedSpoolData&&) noexcept;
    /// Release this owner's prior file/graph and move in the argument; returns *this.
    VerifiedSpoolData& operator=(VerifiedSpoolData&&) noexcept;
    /// Copying is forbidden because the mutable FILE cursor and validation identity have one owner.
    VerifiedSpoolData(const VerifiedSpoolData&)=delete;
    /// Copy assignment is forbidden; transfer an existing owner with move assignment.
    VerifiedSpoolData& operator=(const VerifiedSpoolData&)=delete;
    /// Open the first path read-only, verify the binding under supplied limits, and return an owned admitted spool or precise Status. No path/argument borrow survives; multi-run and partial-original imports are unsupported initial capabilities.
    static std::expected<VerifiedSpoolData,Status> open(
        const std::filesystem::path&, const SpoolBinding&, SpoolReadLimits={});
    /// Fill the output byte span from section at relative_offset; bounds are section-relative. Returns success or contract/I/O status without allocating history or changing the immutable descriptors.
    Status read(std::uint64_t section, std::uint64_t relative_offset,
                std::span<std::byte>) noexcept;
    /// Rehash the actual open file in bounded tiles, including extent, after a merger copy.
    /// Returns invalid_state without a file, verification_failure on byte/extent change,
    /// or I/O status. This detects changes; it is not an exclusion lock against external writers.
    Status verify_unchanged() noexcept;
    SpoolSummary summary; ///< Owned admission outcome and graph/resource bounds.
    Json root, ///< Owned root descriptor tree, independent of parser input.
     run; ///< Owned single-run descriptor tree, independent of parser input.
    std::vector<DirectoryEntry> directory; ///< Dense section directory indexed by canonical section ID.
    std::vector<SourceMetadata> sources; ///< Dense normalized provenance source catalog.
    std::vector<SourceReference> references; ///< Ordered run-role associations into sources.
    std::vector<TraceMetadata> traces; ///< Owned declared diagnostics in authored trace order.
    std::uint64_t root_section{}, ///< Root metadata section ID in the admitted source file.
     run_section{}, ///< Single-run metadata section ID in the source file.
     original_source_index{}; ///< Verified source-catalog index of original-input evidence.
private:
    struct Impl; ///< Private file/binding/tile owner defined only in the implementation.
    std::unique_ptr<Impl> impl_; ///< Sole ownership of the open spool; null for default or moved-from objects.
    friend struct SpoolValidator;
};
} // namespace serene::outputs::detail
