#pragma once
#include "types.hpp"
#include "diagnostics.hpp"
#include "profile.hpp"
#include <expected>
#include <span>
#include <string>
#include <vector>
namespace serene::outputs {
enum class RunKind : std::uint8_t { timeseries, steady_state };
enum class SectionState : std::uint8_t { included, disabled, unavailable, partial };
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
enum class NumericalStatus : std::uint8_t {
    not_started,
    target_reached,
    stopped_before_target,
    failed
};
enum class ObservationStatus : std::uint8_t { complete, failed, disabled, not_started };
enum class LoggingStatus : std::uint8_t { complete, partial, failed, disabled, unavailable };
/// Caller-owned bytes; callback must fill the complete requested span without allocation.
struct ByteSource {
    using Fill = Status (*)(const void *, std::uint64_t, std::span<std::byte>) noexcept;
    /// Borrowed contiguous bytes when callback is null.
    std::span<const std::byte> contiguous{};
    /// Borrowed opaque callback state.
    const void *context{};
    /// Non-owning complete-fill operation; null selects contiguous storage.
    Fill callback{};
    /// Exact readable byte count, distinct from allocated capacity.
    std::uint64_t size{};
    static ByteSource memory(std::span<const std::byte> b) noexcept {
        return {b, nullptr, nullptr, b.size()};
    }
    static ByteSource text(std::string_view s) noexcept {
        return memory(std::as_bytes(std::span(s.data(), s.size())));
    }
    Status read(std::uint64_t offset, std::span<std::byte> out) const noexcept;
};
/// Document bytes are borrowed through publication; identity and unavailable state are explicit.
/// Caller validates included JSON before saving and preserves byte-source validity/immutability.
/// The writer copies and hashes bytes in bounded tiles; it does not parse document JSON.
struct DocumentView {
    SectionState state{SectionState::unavailable};
    ByteSource bytes{};
    std::string_view media_type{"application/json"};
};
/// Logical rank-two source with checked strides or a non-owning fill callback.
struct ValueView {
    using Fill = Status (*)(const void *, std::uint64_t, std::uint64_t, std::span<double>) noexcept;
    std::span<const double> storage{};
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
    static std::expected<ValueView, Status> strided(std::span<const double>, std::uint64_t outer,
                                                    std::uint64_t inner, std::uint64_t outer_stride,
                                                    std::uint64_t inner_stride) noexcept;
    static std::expected<ValueView, Status> borrowed(const void *, Fill, std::uint64_t outer,
                                                     std::uint64_t inner) noexcept;
    Status fill(std::uint64_t row, std::uint64_t begin,
                std::span<double> destination) const noexcept;
};
/// Resolved scalar identity and unit; order is already chosen by the numerical owner.
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
/// One group-scoped effective request; no physical selector is interpreted by outputs.
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
/// Prepared immutable per-run catalog and maximum retained record count.
struct RunDescription {
    RunKind kind{RunKind::timeseries};
    /// Authoritative authored run ID.
    std::string run_id;
    /// Optional non-unique display name.
    std::string run_name;
    /// Bound root assembly identity, empty only when unavailable.
    std::string assembly_ref;
    /// Separates absent display name from present empty text.
    bool run_name_present{};
    std::vector<ChannelDescription> channels;
    std::vector<RequestDescription> requests;
    SectionState effective_requests{SectionState::included};
    /// Maximum samples/levels, independent of the committed prefix.
    std::uint64_t record_capacity{};
    /// Empty maps retain the reversible preset. Nonempty maps bind transient scalar rows.
    std::vector<CodecProfile> profiles;
    std::vector<std::uint32_t> row_profile_index;
    /// Unique kinds in authored order; all storage is prepared before active execution.
    std::vector<DiagnosticTraceDescription> diagnostics;
};
/// Prepared single-run archive identity. Bundle orchestration uses bundle.hpp.
struct ArchiveDescription {
    std::string file_id, created_utc, writer_name{"serene-outputs"}, writer_version{"0.2.0"};
    RunDescription run;
    bool indexed_provenance{};
    std::uint64_t maximum_log_sources{1};
};
/// Explicit bounded export buffers; excludes caller-owned retained histories and library internals.
struct ExportLimits {
    std::uint64_t frame_values{65536}, workspace_budget_bytes{16 * 1024 * 1024},
        metadata_budget_bytes{64 * 1024 * 1024};
    std::uint64_t index_tile_records{256}, copy_tile_bytes{65536};
    /// Sum of original/info/configuration/log source-byte bounds, excluding typed catalogs.
    /// Callers should provide their actual finite bound for disk reservation.
    std::uint64_t provenance_bytes_bound{32ull * 1024 * 1024 * 1024};
};
/// Inspectable requested-payload plan, not a total-process heap measurement.
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
    std::uint64_t section_capacity{}, maximum_archive_bytes{};
    /// Normalized document/log source count bound for a subsequent verified bundle import.
    std::uint64_t source_capacity{};
};
/// Authored diagnostic capacity; dictionary/criteria validity is checked during exact preparation.
struct DiagnosticCapacity {
    DiagnosticKind kind{};
    std::uint64_t record_capacity{};
};
/// Count-only upper bounds available before dependent native catalogs exist. These are ceilings,
/// not claims that the maximum populations will be realized. No authored/solver state is invented.
struct ExportCapacity {
    RunKind kind{};
    std::uint64_t scalar_capacity{}, record_capacity{}, profile_capacity{1};
    bool effective_requests_included{true}, indexed_provenance{};
    std::uint64_t maximum_log_sources{1};
    /// Ceiling on PreparedExport::plan().metadata_bytes, including owned vector/string capacities
    /// and type storage; this is NOT the byte length of serialized metadata JSON.
    std::uint64_t metadata_bytes_bound{};
    std::span<const DiagnosticCapacity> diagnostics{};
};
/// Allocation-free, checked conservative planning. Uses exactly the arithmetic of exact prepare.
/// ExportLimits::provenance_bytes_bound includes every document/log/configuration/info ByteSource.
/// The returned workspace includes the full metadata ceiling and must fit the supplied limits.
/// Before activation the caller enforces actual counts/kinds, metadata and source-byte ceilings;
/// exact preparation still validates real catalogs/profiles/criteria/JSON and must fit this plan.
std::expected<WorkspacePlan,Status> plan_export_capacity(const ExportCapacity&,ExportLimits={}) noexcept;
/// Separately tracked numerical/observation/logging results. Views must remain valid through
/// saving.
struct RunInfoView {
    RunOutcome outcome{RunOutcome::completed};
    NumericalStatus numerical{NumericalStatus::target_reached};
    ObservationStatus observation{ObservationStatus::complete};
    LoggingStatus logging{LoggingStatus::complete};
    SectionState catalog_state{SectionState::included};
    std::string_view failure_stage{}, failure_code{}, termination_reason{};
    std::uint64_t accepted_count{};
    bool last_accepted_present{}, requested_bounds_present{true}, elapsed_seconds_present{true};
    double last_accepted_value{}, requested_begin{}, requested_end{}, elapsed_seconds{};
    std::string_view elapsed_seconds_scope{"solve_only"}, identity_axis_convention{},
        axis_name{"time"}, axis_unit{"s"};
    DocumentView solver_configuration{}, sampling_configuration{}, execution{};
};
/// Committed immutable prefix. All owners remain alive until successful publication or abandoned
/// retry.
struct RunResultView {
    RunInfoView info;
    /// Actual committed time/load axis; never capacity-sized.
    std::span<const double> axis;
    /// Actual accepted solver identities when known.
    std::span<const std::uint64_t> accepted_indices;
    /// Whether accepted_indices has meaningful solver identities.
    bool identity_axis_present{};
    /// Borrowed logical committed values.
    ValueView values;
    std::span<const DiagnosticTraceView> diagnostics{};
};
/// Source provenance is separate from the prepared scalar catalog.
struct InputsView {
    SectionState state{SectionState::unavailable};
    DocumentView source_json, effective_json, overrides_json, resources_json;
};
struct ContextView {
    SectionState state{SectionState::unavailable};
    DocumentView execution_json;
};
/// Exact post-close JSONL bytes. Offsets are scanned and written in bounded tiles by the exporter.
/// Caller guarantees each complete LF-terminated record is a strict JSON object.
/// The writer preserves a trailing fragment and does not parse or reinterpret log events.
struct LogsView {
    SectionState state{SectionState::unavailable};
    ByteSource bytes;
    bool dropped_count_present{};
    std::uint64_t dropped_count{};
    std::string_view schema{"serene-jsonl"};
    DocumentView info_json;
};
struct ProvenanceView {
    InputsView inputs;
    LogsView logs;
    ContextView context;
};
/// Indexed provenance for a single-run spool: roles original/effective/overrides/resources/context.
/// Each ordered log remains a separate original stream. Borrowed sources survive publication/retry.
struct IndexedProvenanceView {
    Digest authored_input_sha256{};
    std::array<DocumentView,5> documents{};
    std::span<const LogsView> logs{};
};
/// Initialization-owned metadata and reusable buffers. Does not open or create any file.
/// The caller also prepares Codec before activation and checks workspace_bytes() <=
/// plan().codec_bytes. Keep this object, Codec and all borrowed source owners alive and immutable
/// through publication/retry.
class PreparedExport {
  public:
    PreparedExport() = default;
    PreparedExport(const PreparedExport &) = delete;
    PreparedExport &operator=(const PreparedExport &) = delete;
    PreparedExport(PreparedExport &&) noexcept;
    PreparedExport &operator=(PreparedExport &&) noexcept;
    static std::expected<PreparedExport, Status> prepare(ArchiveDescription, ExportLimits = {});
    const ArchiveDescription &description() const noexcept {
        return description_;
    }
    const ExportLimits &limits() const noexcept {
        return limits_;
    }
    const WorkspacePlan &plan() const noexcept {
        return plan_;
    }
    bool valid() const noexcept {
        return valid_;
    }
    std::span<double> gather_buffer() noexcept {
        return gather_;
    }
    std::span<double> decode_buffer() noexcept {
        return decode_;
    }
    std::span<std::byte> index_buffer() noexcept {
        return index_;
    }
    std::span<std::byte> copy_buffer() noexcept {
        return copy_;
    }
    std::span<std::byte> directory_buffer() noexcept {
        return directory_;
    }
    std::span<std::byte> serialization_buffer() noexcept {
        return serialization_;
    }
    Status validate(const RunResultView &) const noexcept;

  private:
    ArchiveDescription description_;
    ExportLimits limits_;
    WorkspacePlan plan_;
    bool valid_{};
    std::vector<double> gather_, decode_;
    std::vector<std::byte> index_, copy_, directory_, serialization_;
};
/// Strict bounded JSON/UTF-8 checks during initialization/post-run only, never active capture.
Status validate_json_object(std::string_view) noexcept;
bool valid_utf8(std::string_view, bool forbid_nul = true) noexcept;
} // namespace serene::outputs
