#pragma once
#include "types.hpp"
#include <array>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace serene::outputs {
enum class DiagnosticKind : std::uint8_t {
    transient_accepted_steps, static_stage_attempts, static_accepted_levels,
    static_newton_iterations, run_events
};
enum class DiagnosticRequirement : std::uint8_t { optional, required };
enum class DiagnosticAvailability : std::uint8_t { included=0, unavailable=2, partial=3 };
enum class DiagnosticCaptureStatus : std::uint8_t {
    complete, not_started, capacity, capture_error, source_unavailable
};
enum class DiagnosticStatus : std::uint8_t { complete, partial, failed, disabled, unavailable };
/// Prepared immutable physical criteria. All limits are finite positive; scales/tolerances
/// are finite nonnegative. Evaluation time is signed physical time, not the load parameter.
struct StaticCriteria {
    double evaluation_time_s{}, force_absolute_N{}, moment_absolute_Nm{}, relative_tolerance{};
    double force_scale_N{}, moment_scale_Nm{}, force_limit_N{}, moment_limit_Nm{};
    double constraint_translation_limit_m{}, constraint_rotation_limit_rad{};
    double increment_translation_limit_m{}, increment_rotation_limit_rad{};
};
struct DiagnosticTraceDescription {
    DiagnosticKind kind{};
    DiagnosticRequirement requirement{DiagnosticRequirement::optional};
    std::uint64_t record_capacity{};
    std::vector<std::string> status_names, event_names;
    std::optional<StaticCriteria> criteria;
};
/// Plain source record, projected only onto the fixed columns belonging to its trace kind.
/// Every absent metric/identity uses zero. Unused fields have no wire meaning.
struct DiagnosticRecord {
    std::uint64_t record_index{}, accepted_step{}, nonlinear_iterations{};
    double time_s{};
    std::uint64_t attempt_id{}, accepted_level_id{}, iteration_id{}, newton_record_count{};
    double factor{};
    std::uint8_t certified{}, rolled_back{}, accepted_level_present{}, row_kind{}, metric_valid{};
    std::uint32_t status_index{};
    std::array<double,6> metrics{};
    std::uint64_t sequence{};
    std::uint32_t event_name_index{};
    std::uint8_t severity{}, identity_valid{};
};
/// Borrowed complete-prefix records. Callback must copy one complete immutable record without
/// allocation. Allows adapters over existing source-owned record layouts without an extra history.
struct DiagnosticRecords {
    using Read = Status (*)(const void *, std::uint64_t, DiagnosticRecord &) noexcept;
    std::span<const DiagnosticRecord> contiguous{};
    const void *context{};
    Read callback{};
    std::uint64_t count{};
    static DiagnosticRecords memory(std::span<const DiagnosticRecord> values) noexcept {
        return {values,nullptr,nullptr,values.size()};
    }
    Status read(std::uint64_t, DiagnosticRecord &) const noexcept;
};
enum class DiagnosticMeasure : std::uint8_t {
    nonlinear_iterations, newton_record_count, force_residual_N, moment_residual_Nm,
    constraint_translation_m, constraint_rotation_rad, increment_translation_m,
    increment_rotation_rad
};
struct DiagnosticSummary {
    DiagnosticMeasure measure{};
    std::uint64_t count{}, min_integer{}, max_integer{};
    double min{}, max{}, mean{};
    bool median_present{};
    double median{};
};
struct DiagnosticTraceView {
    DiagnosticKind kind{};
    DiagnosticAvailability availability{DiagnosticAvailability::unavailable};
    DiagnosticCaptureStatus capture_status{DiagnosticCaptureStatus::not_started};
    DiagnosticRecords records{};
    bool counts_present{};
    std::uint64_t eligible_count{}, omitted_count{}, capture_error_count{};
    bool first_missing_ordinal_present{};
    std::uint64_t first_missing_ordinal{};
    std::string_view failure_code{};
    std::span<const DiagnosticSummary> summaries{};
};
/// Initialization/post-run validation only. Never invoked during active source capture.
Status validate_diagnostic_description(const DiagnosticTraceDescription &) noexcept;
std::expected<DiagnosticStatus,Status> validate_diagnostics(
    std::span<const DiagnosticTraceDescription>, std::span<const DiagnosticTraceView>,
    std::uint64_t accepted_count) noexcept;
/// Exact maximum directory-section contribution, including metadata and empty columns.
std::uint64_t diagnostic_section_capacity(DiagnosticKind) noexcept;
const char *diagnostic_measure_name(DiagnosticMeasure) noexcept;
} // namespace serene::outputs
