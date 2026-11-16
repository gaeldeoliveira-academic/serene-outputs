#pragma once
#include "types.hpp"
#include "diagnostics.hpp"
#include "profile.hpp"
#include <expected>
#include <span>
#include <string>
#include <vector>
namespace serene::outputs {
/// Physical result organization: selected transient samples or accepted static levels.
enum class RunKind : std::uint8_t { timeseries, steady_state };
/// Retention state; an included empty section is distinct from unavailable bytes.
enum class SectionState : std::uint8_t { included, disabled, unavailable, partial };
/// Overall producer outcome, independent of individual observation/logging status.
enum class RunOutcome : std::uint8_t {
    completed,
    numerical_failure,
    observation_failure,
    logging_failure,
    preparation_failure,
    skipped,
    cancelled,
    diagnostic_failure
};
/// Numerical progress classification supplied by the solver owner.
enum class NumericalStatus : std::uint8_t {
    not_started,
    target_reached,
    stopped_before_target,
    failed
};
/// Scalar observation outcome; numerical success alone does not imply complete observations.
enum class ObservationStatus : std::uint8_t { complete, failed, disabled, not_started };
/// Log-capture outcome independent of numerical and observation outcomes.
enum class LoggingStatus : std::uint8_t { complete, partial, failed, disabled, unavailable };
/// Nonowning byte stream used by provenance views and document preflight.
/// Either borrows contiguous memory or an opaque owner plus a complete-fill callback;
/// size is the readable extent, not capacity. The caller keeps all owners immutable
/// until the synchronous consumer finishes. Callbacks fill the complete requested span
/// without allocation and propagate Status; this object never owns or frees source bytes.
struct ByteSource {
    /// Read callback: borrowed context, byte offset and complete writable destination.
    using Fill = Status (*)(const void *, std::uint64_t, std::span<std::byte>) noexcept;
    /// Borrowed contiguous bytes when callback is null.
    std::span<const std::byte> contiguous{};
    /// Borrowed opaque callback state.
    const void *context{};
    /// Non-owning complete-fill operation; null selects contiguous storage.
    Fill callback{};
    /// Exact readable byte count, distinct from allocated capacity.
    std::uint64_t size{};
    /// Borrow b without copying; the returned size equals its byte length.
    static ByteSource memory(std::span<const std::byte> b) noexcept {
        // Bind the supplied span and select direct-memory reads.
        return {b, nullptr, nullptr, b.size()};
    }
    /// Borrow the exact bytes of s without requiring a terminator or interpreting UTF-8.
    static ByteSource text(std::string_view s) noexcept {
        // Reinterpret the string view as bytes, retaining the same source lifetime.
        return memory(std::as_bytes(std::span(s.data(), s.size())));
    }
    /// Fill out from byte offset after checked bounds; return source/contract failure.
    /// The destination is caller-owned, must not overlap direct source storage, and may
    /// be partially modified on callback failure.
    Status read(std::uint64_t offset, std::span<std::byte> out) const noexcept;
};
/// Document bytes are borrowed through publication; identity and unavailable state are explicit.
/// Caller validates included JSON before saving and preserves byte-source validity/immutability.
/// The writer copies and hashes bytes in bounded tiles; it does not parse document JSON.
struct DocumentView {
    SectionState state{SectionState::unavailable}; ///< Explicit retention state of this document role.
    ByteSource bytes{}; ///< Borrowed retained content; inactive documents have zero bytes.
    std::string_view media_type{"application/json"}; ///< Borrowed format label, meaningful for retained documents.
};
/// Nonowning logical rank-two value source consumed by PreparedExport/ArchiveSession.
/// Direct storage uses element strides; callbacks expose rows without another retained
/// history. Shape describes the committed prefix, not allocation capacity. Source
/// owners outlive all fills; destination/source aliasing is the caller's responsibility.
struct ValueView {
    /// Callback receives borrowed context, logical row, inner offset and complete destination.
    using Fill = Status (*)(const void *, std::uint64_t, std::uint64_t, std::span<double>) noexcept;
    std::span<const double> storage{}; ///< Borrowed binary64 backing storage for strided access.
    /// Borrowed opaque callback state.
    const void *context{};
    /// Non-owning complete-fill operation; null selects contiguous storage.
    Fill callback{};
    /// Logical outer row count.
    std::uint64_t outer{};
    /// Logical values per row.
    std::uint64_t inner{};
    /// Source element displacement between outer rows.
    std::uint64_t outer_stride{};
    /// Source element displacement along an inner row.
    std::uint64_t inner_stride{};
    /// Admit borrowed storage and outer/inner shape using element strides; return a
    /// view only if checked arithmetic proves every addressed value lies in the span.
    static std::expected<ValueView, Status> strided(std::span<const double>, std::uint64_t outer,
                                                    std::uint64_t inner, std::uint64_t outer_stride,
                                                    std::uint64_t inner_stride) noexcept;
    /// Bind opaque source context and Fill callback to outer/inner shape without copying.
    /// Return invalid_contract if the required callback or shape cannot be admitted.
    static std::expected<ValueView, Status> borrowed(const void *, Fill, std::uint64_t outer,
                                                     std::uint64_t inner) noexcept;
    /// Copy one logical row slice starting at begin into destination; reject out-of-range
    /// slices and propagate callback errors. destination length selects the slice length.
    Status fill(std::uint64_t row, std::uint64_t begin,
                std::span<double> destination) const noexcept;
};
/// Owned scalar-catalog entry prepared by core and serialized by ArchiveSession.
/// Strings name the resolved scalar/unit; numerical meaning and order come from the caller.
/// PreparedExport validates identities and request linkage without compiling physical selectors.
struct ChannelDescription {
    /// Unique scalar identity in declaration order.
    std::string scalar_id;
    /// Authored channel-group identity.
    std::string group_id;
    /// Request identity scoped to the group.
    std::string request_id;
    /// Resolved scalar component path.
    std::string component_path;
    /// Unit of the stored numerical value.
    std::string unit;
    /// Index into the prepared request identity table.
    std::uint64_t request_index{};
};
/// Owned group-scoped request catalog entry used by PreparedExport and archive metadata.
/// Retains effective JSON and schema/construction identities; no physical selector is interpreted
/// by outputs. Initialization validates retained JSON before immutable export use.
struct RequestDescription {
    /// Authored group identity.
    std::string group_id;
    /// Authored request identity scoped to its group.
    std::string request_id;
    /// Input schema identity of the effective specification.
    std::string schema_version;
    /// Construction profile identity.
    std::string construction_profile;
    /// Strict JSON object, retained only when enabled.
    std::string effective_json;
};
/// Owned per-run preparation input used by core adapters and synthetic export tests.
/// Stores ordered scalar/request catalogs, capacities, diagnostic descriptions and resolved
/// codec profiles; prepare validates and moves it into immutable PreparedExport metadata.
/// Capacity bounds future retained records; it does not assert their eventual population.
struct RunDescription {
    RunKind kind{RunKind::timeseries}; ///< Determines sample-major static versus row-wise transient writing.
    /// Authoritative authored run ID.
    std::string run_id;
    /// Optional non-unique display name.
    std::string run_name;
    /// Bound root assembly identity, empty only when unavailable.
    std::string assembly_ref;
    /// Separates absent display name from present empty text.
    bool run_name_present{};
    std::vector<ChannelDescription> channels; ///< Owned resolved scalars in authoritative output order.
    std::vector<RequestDescription> requests; ///< Owned unique group/request catalog indexed by channels.
    SectionState effective_requests{SectionState::included}; ///< Whether effective request JSON is retained.
    /// Maximum samples/levels, independent of the committed prefix.
    std::uint64_t record_capacity{};
    /// Empty maps retain the reversible preset. Nonempty maps bind transient scalar rows.
    std::vector<CodecProfile> profiles;
    std::vector<std::uint32_t> row_profile_index; ///< Per-transient-scalar profile indices; empty with the reversible preset.
    /// Unique kinds in authored order; all storage is prepared before active execution.
    std::vector<DiagnosticTraceDescription> diagnostics;
};
/// Owned single-run archive identity moved into PreparedExport before writing.
/// Used by core export adapters and writer tests; bundle orchestration uses bundle.hpp.
/// Strings and catalogs are retained independently of caller memory after preparation.
struct ArchiveDescription {
    std::string file_id, ///< Caller-provided archive identity.
        created_utc, ///< UTC creation text supplied by the producer.
        writer_name{"serene-outputs"}, ///< Producer name, never a parser-profile discriminator.
        writer_version{"0.2.0"}; ///< Producer version text; distinct from container version.
    RunDescription run; ///< Owned catalog and capacities of the sole run.
    bool indexed_provenance{}; ///< Selects the 1.1 indexed source graph when true.
    std::uint64_t maximum_log_sources{1}; ///< Prepared upper bound on distinct ordered log streams.
};
/// Producer-selected finite limits for export preparation and count-only planning.
/// Core supplies actual source-byte bounds before activation; PreparedExport reserves buffers.
/// Accounting excludes caller-owned retained histories and unmodeled library/allocator internals.
struct ExportLimits {
    std::uint64_t frame_values{65536}, ///< Maximum values gathered/verified per codec frame.
        workspace_budget_bytes{16 * 1024 * 1024}, ///< Aggregate requested workspace ceiling.
        metadata_budget_bytes{64 * 1024 * 1024}; ///< Owned catalog/metadata storage ceiling.
    std::uint64_t index_tile_records{256}, ///< Index records buffered per write tile.
        copy_tile_bytes{65536}; ///< Borrowed byte-source copy/hash tile capacity.
    /// Sum of original/info/configuration/log source-byte bounds, excluding typed catalogs.
    /// Callers should provide their actual finite bound for disk reservation.
    std::uint64_t provenance_bytes_bound{32ull * 1024 * 1024 * 1024};
};
/// Owned accounting returned by exact and capacity-only export preparation.
/// Core uses it for pre-solve reservation and ArchiveSession uses its directory limits.
/// Values are conservative requested payload bytes, not a total-process heap measurement.
struct WorkspacePlan {
    /// Owned source-frame buffer reservation.
    std::uint64_t gather_bytes{};
    /// Owned verification-frame buffer reservation.
    std::uint64_t decode_bytes{};
    /// Reserved codec payload; Codec must be prepared before activation and fit this bound.
    std::uint64_t codec_bytes{};
    /// Owned index tile buffer.
    std::uint64_t index_bytes{};
    /// Owned document/payload copy tile.
    std::uint64_t copy_bytes{};
    /// Owned fixed-capacity canonical section-directory workspace.
    std::uint64_t directory_bytes{};
    /// Owned bounded JSON serialization workspace, reused for root and run metadata.
    std::uint64_t serialization_bytes{};
    /// Conservative owned metadata reservation, including retained vector/string capacities.
    std::uint64_t metadata_bytes{};
    /// Aggregate reservations, counting codec exactly once.
    std::uint64_t total_bytes{};
    /// Maximum frames derived from declared result capacity.
    std::uint64_t maximum_frames{};
    std::uint64_t section_capacity{}, ///< Maximum directory entries, including retained empty sections.
        maximum_archive_bytes{}; ///< Conservative final-file bound for the admitted capacities.
    /// Normalized document/log source count bound for a subsequent verified bundle import.
    std::uint64_t source_capacity{};
};
/// Borrow-free diagnostic kind/capacity pair used by count-only reservation.
/// Core knows these counts before dependent catalogs exist; exact preparation separately
/// validates dictionaries/criteria. A large authored capacity is not a retained wire count.
struct DiagnosticCapacity {
    DiagnosticKind kind{}; ///< Fixed trace column schema whose section cost is reserved.
    std::uint64_t record_capacity{}; ///< Authored retained-row capacity, subject to checked size arithmetic.
};
/// Count-only upper bounds available before dependent native catalogs exist. These are ceilings,
/// not claims that the maximum populations will be realized. No authored/solver state is invented.
struct ExportCapacity {
    RunKind kind{}; ///< Determines static/transient frame organization.
    std::uint64_t scalar_capacity{}, ///< Upper bound on resolved scalar rows.
        record_capacity{}, ///< Upper bound on retained samples/levels.
        profile_capacity{1}; ///< Maximum resolved codec profiles, including reversible.
    bool effective_requests_included{true}, ///< Reserve effective request sections when true.
        indexed_provenance{}; ///< Reserve normalized indexed source metadata when true.
    std::uint64_t maximum_log_sources{1}; ///< Upper bound on ordered retained log sources.
    /// Ceiling on PreparedExport::plan().metadata_bytes, including owned vector/string capacities
    /// and type storage; this is NOT the byte length of serialized metadata JSON.
    std::uint64_t metadata_bytes_bound{};
    std::span<const DiagnosticCapacity> diagnostics{}; ///< Borrowed kind/capacity list, needed only during planning.
};
/// Allocation-free, checked conservative planning. Uses exactly the arithmetic of exact prepare.
/// ExportLimits::provenance_bytes_bound includes every document/log/configuration/info ByteSource.
/// The returned workspace includes the full metadata ceiling and must fit the supplied limits.
/// Before activation the caller enforces actual counts/kinds, metadata and source-byte ceilings;
/// exact preparation still validates real catalogs/profiles/criteria/JSON and must fit this plan.
/// The capacity argument supplies ceilings; limits supplies byte/frame policy. Returns the
/// complete conservative plan or contract/resource failure without allocating.
std::expected<WorkspacePlan,Status> plan_export_capacity(const ExportCapacity&,ExportLimits={}) noexcept;
/// Separately tracked numerical/observation/logging results. Views must remain valid through
/// saving. Core supplies these source facts; outputs serializes rather than infers solver
/// success. All text and document fields borrow owners through synchronous export.
struct RunInfoView {
    RunOutcome outcome{RunOutcome::completed}; ///< Overall source-owned run outcome.
    NumericalStatus numerical{NumericalStatus::target_reached}; ///< Numerical target/progress status.
    ObservationStatus observation{ObservationStatus::complete}; ///< Scalar observation capture outcome.
    LoggingStatus logging{LoggingStatus::complete}; ///< Log stream capture outcome.
    SectionState catalog_state{SectionState::included}; ///< Availability of the resolved scalar catalog.
    std::string_view failure_stage{}, ///< Borrowed failing lifecycle stage, empty when inapplicable.
        failure_code{}, ///< Borrowed producer failure identifier.
        termination_reason{}; ///< Borrowed explanation of why execution ended.
    std::uint64_t accepted_count{}; ///< Source-qualified accepted advances, not selected output row count.
    bool last_accepted_present{}, ///< Whether last_accepted_value has source evidence.
        requested_bounds_present{true}, ///< Whether requested_begin/end are meaningful.
        elapsed_seconds_present{true}; ///< Whether elapsed_seconds was measured.
    double last_accepted_value{}, ///< Last accepted time/load coordinate when present.
        requested_begin{}, ///< Requested lower coordinate bound.
        requested_end{}, ///< Requested upper coordinate bound.
        elapsed_seconds{}; ///< Measured duration in seconds when present.
    std::string_view elapsed_seconds_scope{"solve_only"}, ///< Borrowed measurement scope label.
        identity_axis_convention{}, ///< Borrowed accepted-identity convention, never inferred.
        axis_name{"time"}, ///< Borrowed coordinate label, e.g. time or load factor.
        axis_unit{"s"}; ///< Borrowed stored coordinate unit.
    DocumentView solver_configuration{}, ///< Borrowed source solver configuration JSON.
        sampling_configuration{}, ///< Borrowed source output sampling policy JSON.
        execution{}; ///< Borrowed source execution facts JSON.
};
/// Committed immutable prefix. All owners remain alive until successful publication or abandoned
/// retry.
struct RunResultView {
    RunInfoView info; ///< Borrowed outcome/configuration facts accompanying this prefix.
    /// Actual committed time/load axis; never capacity-sized.
    std::span<const double> axis;
    /// Actual accepted solver identities when known.
    std::span<const std::uint64_t> accepted_indices;
    /// Whether accepted_indices has meaningful solver identities.
    bool identity_axis_present{};
    /// Borrowed logical committed values.
    ValueView values;
    std::span<const DiagnosticTraceView> diagnostics{}; ///< Borrowed traces in prepared description order.
};
/// Borrowed input evidence used by the legacy single-run provenance writer.
/// Stores each document role separately; aggregate state must match its members.
/// Core owns immutable source bytes through save/retry, independently of scalar catalogs.
struct InputsView {
    SectionState state{SectionState::unavailable}; ///< Aggregate input-evidence availability.
    DocumentView source_json, ///< Original authored source bytes, including legitimate partial evidence.
        effective_json, ///< Effective interpreted input evidence.
        overrides_json, ///< Applied override evidence.
        resources_json; ///< External-resource provenance evidence.
};
/// Borrowed execution-context provenance used by ArchiveSession; contains no runtime owner.
/// The aggregate state and document role are serialized independently of run outcomes.
struct ContextView {
    SectionState state{SectionState::unavailable}; ///< Aggregate context-document state.
    DocumentView execution_json; ///< Borrowed execution-context bytes.
};
/// Exact post-close JSONL bytes. Offsets are scanned and written in bounded tiles by the exporter.
/// Caller guarantees each complete LF-terminated record is a strict JSON object.
/// The writer preserves a trailing fragment and does not parse or reinterpret log events.
struct LogsView {
    SectionState state{SectionState::unavailable}; ///< Exact retained log-stream availability.
    ByteSource bytes; ///< Borrowed complete records plus any trailing partial fragment.
    bool dropped_count_present{}; ///< Whether the producer knows the dropped-message count.
    std::uint64_t dropped_count{}; ///< Known dropped messages; canonical zero when absent.
    std::string_view schema{"serene-jsonl"}; ///< Borrowed source log schema identifier.
    DocumentView info_json; ///< Borrowed source log-capture metadata.
};
/// Borrowed legacy provenance bundle consumed by the nonindexed append_run overload.
/// Roles remain separate and all referenced owners outlive the synchronous save attempt.
struct ProvenanceView {
    InputsView inputs; ///< Input-role evidence.
    LogsView logs; ///< One original JSONL stream and capture metadata.
    ContextView context; ///< Execution-context evidence.
};
/// Indexed provenance for a single-run spool: roles original/effective/overrides/resources/context.
/// Each ordered log remains a separate original stream. Borrowed sources survive publication/retry.
struct IndexedProvenanceView {
    Digest authored_input_sha256{}; ///< Pre-solve digest binding original authored input bytes.
    std::array<DocumentView,5> documents{}; ///< Roles in original/effective/overrides/resources/context order.
    std::span<const LogsView> logs{}; ///< Borrowed distinct original streams in authored order.
};
/// Initialization-owned metadata and reusable buffers. Does not open or create any file.
/// The caller also prepares Codec before activation and checks workspace_bytes() <=
/// plan().codec_bytes. Keep this object, Codec and all borrowed source owners alive and immutable
/// through publication/retry.
class PreparedExport {
  public:
    /// Construct invalid empty preparation; use prepare before opening a session.
    PreparedExport() = default;
    /// Owned reusable buffers and metadata are not implicitly copied.
    PreparedExport(const PreparedExport &) = delete;
    /// Copy assignment cannot duplicate a session-borrowed workspace.
    PreparedExport &operator=(const PreparedExport &) = delete;
    /// Transfer metadata/buffer ownership; source validity is cleared. No live session may borrow it.
    PreparedExport(PreparedExport &&) noexcept;
    /// Replace ownership from an rvalue; neither object may be borrowed by a live session.
    PreparedExport &operator=(PreparedExport &&) noexcept;
    /// Validate and own ArchiveDescription under ExportLimits, reserve reusable storage,
    /// and return an initialized object or contract/resource failure. No file is opened.
    static std::expected<PreparedExport, Status> prepare(ArchiveDescription, ExportLimits = {});
    /// Return immutable owned catalog metadata; the reference lasts only as long as this owner.
    const ArchiveDescription &description() const noexcept {
        // Expose the retained description without copying.
        return description_;
    }
    /// Return the admitted immutable limits borrowed from this preparation.
    const ExportLimits &limits() const noexcept {
        // Expose the retained limit values.
        return limits_;
    }
    /// Return immutable reservation accounting borrowed from this preparation.
    const WorkspacePlan &plan() const noexcept {
        // Expose the completed checked plan.
        return plan_;
    }
    /// Return whether preparation succeeded and ownership has not moved away.
    bool valid() const noexcept {
        // Read the preparation-state flag.
        return valid_;
    }
    /// Borrow mutable source-frame scratch; only the owning serialized session may write it.
    std::span<double> gather_buffer() noexcept {
        // Expose the preallocated gather storage.
        return gather_;
    }
    /// Borrow mutable reconstruction scratch for serialized codec verification.
    std::span<double> decode_buffer() noexcept {
        // Expose the preallocated decode storage.
        return decode_;
    }
    /// Borrow mutable canonical index tile storage for the serialized writer.
    std::span<std::byte> index_buffer() noexcept {
        // Expose the preallocated index tile.
        return index_;
    }
    /// Borrow mutable source-copy scratch for the serialized writer.
    std::span<std::byte> copy_buffer() noexcept {
        // Expose the preallocated copy tile.
        return copy_;
    }
    /// Borrow fixed directory storage; the writer enforces the prepared section capacity.
    std::span<std::byte> directory_buffer() noexcept {
        // Expose the preallocated directory bytes.
        return directory_;
    }
    /// Borrow bounded metadata serialization scratch; no owner may use it concurrently.
    std::span<std::byte> serialization_buffer() noexcept {
        // Expose the preallocated serialization bytes.
        return serialization_;
    }
    /// Check borrowed committed results against this immutable catalog/capacity and
    /// cross-field outcome rules; return Status without changing results or preparation.
    Status validate(const RunResultView &) const noexcept;

  private:
    ArchiveDescription description_; ///< Owned validated identity, catalogs and trace/profile policies.
    ExportLimits limits_; ///< Retained admitted policy limits for subsequent writing.
    WorkspacePlan plan_; ///< Checked reservation and final-file accounting.
    bool valid_{}; ///< True after successful preparation; false after move/empty construction.
    std::vector<double> gather_, ///< Owned source-frame gather storage.
        decode_; ///< Owned reconstructed-frame verification storage.
    std::vector<std::byte> index_, ///< Owned tiled canonical frame-index storage.
        copy_, ///< Owned bounded borrowed-source copy tile.
        directory_, ///< Owned fixed-capacity section-directory storage.
        serialization_; ///< Owned reusable metadata serialization storage.
};
/// Strict bounded JSON/UTF-8 checks during initialization/post-run only, never active capture.
/// Admit the borrowed text as a strict JSON object; report malformed/duplicate/UTF-8 failures.
/// Unlike preflight_document this validates metadata objects, not arbitrary JSON values.
Status validate_json_object(std::string_view) noexcept;
/// Return whether borrowed text is well-formed UTF-8; forbid_nul additionally rejects NUL.
/// Text over 1 MiB and a leading UTF-8 BOM are rejected. No normalization, allocation
/// or source mutation is performed.
bool valid_utf8(std::string_view, bool forbid_nul = true) noexcept;
} // namespace serene::outputs
