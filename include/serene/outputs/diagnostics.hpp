#pragma once
#include "types.hpp"
#include <array>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace serene::outputs {
/// Closed trace schemas; order also indexes fixed wire column/section layouts.
enum class DiagnosticKind : std::uint8_t {
    transient_accepted_steps, static_stage_attempts, static_accepted_levels,
    static_newton_iterations, run_events
};
/// Whether capture exhaustion/failure makes the aggregate diagnostic result failed.
enum class DiagnosticRequirement : std::uint8_t { optional, required };
/// Wire availability; included empty traces are observable data, not unavailable.
enum class DiagnosticAvailability : std::uint8_t { included=0, unavailable=2, partial=3 };
/// Source capture lifecycle outcome, distinct from numerical success.
enum class DiagnosticCaptureStatus : std::uint8_t {
    complete, not_started, capacity, capture_error, source_unavailable
};
/// Aggregate status derived from the ordered requested traces and their requirements.
enum class DiagnosticStatus : std::uint8_t { complete, partial, failed, disabled, unavailable };
/// Prepared immutable physical criteria. All limits are finite positive; scales/tolerances
/// are finite nonnegative. Evaluation time is signed physical time, not the load parameter.
/// Owns scalar criteria captured by core before static execution; archive metadata preserves
/// them unchanged. Prepared criteria remain available for later numerical-failure rows.
struct StaticCriteria {
    double evaluation_time_s{}, ///< Physical load-evaluation time in seconds.
        force_absolute_N{}, ///< Absolute force convergence tolerance in newtons.
        moment_absolute_Nm{}, ///< Absolute moment convergence tolerance in newton-metres.
        relative_tolerance{}; ///< Dimensionless relative convergence tolerance.
    double force_scale_N{}, ///< Prepared force reference scale in newtons.
        moment_scale_Nm{}, ///< Prepared moment reference scale in newton-metres.
        force_limit_N{}, ///< Finite positive computed force criterion in newtons.
        moment_limit_Nm{}; ///< Finite positive computed moment criterion in newton-metres.
    double constraint_translation_limit_m{}, ///< Positive translational constraint limit in metres.
        constraint_rotation_limit_rad{}; ///< Positive rotational constraint limit in radians.
    double increment_translation_limit_m{}, ///< Positive translation increment limit in metres.
        increment_rotation_limit_rad{}; ///< Positive rotation increment limit in radians.
};
/// Owned immutable trace policy prepared before capture by core/producer adapters.
/// Holds dictionaries, capacity and optional static criteria; it stores no records.
/// Static recorded traces require criteria, while unavailable traces can lack them.
struct DiagnosticTraceDescription {
    DiagnosticKind kind{}; ///< Selects the lossless column schema; must match the prepared trace at this ordered position.
    DiagnosticRequirement requirement{DiagnosticRequirement::optional}; ///< Capture-failure policy for this trace.
    std::uint64_t record_capacity{}; ///< Authored retained-row capacity; not the realized eligible population.
    std::vector<std::string> status_names, ///< Source-owned status dictionary for attempt/Newton traces.
        event_names; ///< Source-owned event dictionary for run-event traces.
    std::optional<StaticCriteria> criteria; ///< Validated prepared criteria for recorded static kinds only.
};
/// Plain source record, projected only onto the fixed columns belonging to its trace kind.
/// Every absent metric/identity uses zero. Unused fields have no wire meaning.
/// Owns scalar facts copied from source callbacks; diagnostics validation and typed
/// column serialization consume it without interpreting source status/event names.
struct DiagnosticRecord {
    std::uint64_t record_index{}, ///< Consecutive retained-row ordinal.
        accepted_step{}, ///< Source accepted transient step, or masked event identity.
        nonlinear_iterations{}; ///< Accepted-step nonlinear iteration count.
    double time_s{}; ///< Physical time in seconds; canonical +0 when event time is absent.
    std::uint64_t attempt_id{}, ///< Source static attempt identity, or masked event identity.
        accepted_level_id{}, ///< Accepted static level index when present.
        iteration_id{}, ///< Source Newton iteration; terminal rows are distinguished by row_kind.
        newton_record_count{}; ///< Newton rows observed for a static attempt.
    double factor{}; ///< Static continuation/load factor; not physical time.
    std::uint8_t certified{}, ///< Boolean equilibrium certification; requires all six valid metrics.
        rolled_back{}, ///< Boolean source rollback indication.
        accepted_level_present{}, ///< Boolean presence of an accepted-level identity.
        row_kind{}, ///< Newton row classification: iteration or terminal.
        metric_valid{}; ///< Six-bit validity mask for metrics in fixed order.
    std::uint32_t status_index{}; ///< Index into the prepared source-owned status dictionary.
    std::array<double,6> metrics{}; ///< Force N, moment Nm, constraint m/rad, increment m/rad; absent slots are +0.
    std::uint64_t sequence{}; ///< Strictly increasing source event sequence, not necessarily consecutive.
    std::uint32_t event_name_index{}; ///< Index into the prepared event-name dictionary.
    std::uint8_t severity{}, ///< Closed event severity range 0..3.
        identity_valid{}; ///< Event bits: time, accepted step, accepted level, attempt.
};
/// Borrowed complete-prefix records. Callback must copy one complete immutable record without
/// allocation. Allows adapters over existing source-owned record layouts without an extra history.
struct DiagnosticRecords {
    /// Copy one complete record from borrowed context/index into the output reference.
    using Read = Status (*)(const void *, std::uint64_t, DiagnosticRecord &) noexcept;
    std::span<const DiagnosticRecord> contiguous{}; ///< Borrowed records when callback is null.
    const void *context{}; ///< Borrowed source adapter state; owned by the caller.
    Read callback{}; ///< Optional complete-record reader; null selects contiguous storage.
    std::uint64_t count{}; ///< Exact retained prefix length, independent of source capacity.
    /// Borrow all values as the complete prefix, without copying or allocating.
    static DiagnosticRecords memory(std::span<const DiagnosticRecord> values) noexcept {
        // Bind the complete contiguous span and select direct record access.
        return {values,nullptr,nullptr,values.size()};
    }
    /// Read the zero-based retained ordinal into the output record; reject bounds or
    /// propagate callback failure. The callback output is unspecified on failure.
    Status read(std::uint64_t, DiagnosticRecord &) const noexcept;
};
/// Permitted summary measures; the first two are exact u64 counts, the rest metric columns.
enum class DiagnosticMeasure : std::uint8_t {
    nonlinear_iterations, newton_record_count, force_residual_N, moment_residual_Nm,
    constraint_translation_m, constraint_rotation_rad, increment_translation_m,
    increment_rotation_rad
};
/// Owned producer summary for one permitted trace column, consumed by validation/writing.
/// Count/min/max are checked by bounded scans over valid records; mean and optional median
/// remain producer assertions with finite/range checks, not independently recomputed ranks.
struct DiagnosticSummary {
    DiagnosticMeasure measure{}; ///< Column summarized over retained valid records.
    std::uint64_t count{}, ///< Number of retained records with this metric valid.
        min_integer{}, ///< Exact minimum for an integer measure; +0 population uses zero.
        max_integer{}; ///< Exact maximum for an integer measure; +0 population uses zero.
    double min{}, ///< Exact retained floating minimum; canonical +0 for empty populations.
        max{}, ///< Exact retained floating maximum; canonical +0 for empty populations.
        mean{}; ///< Producer-asserted rounded binary64 mean in column units.
    bool median_present{}; ///< Whether a producer median is supplied; false for zero population.
    double median{}; ///< Producer-asserted rounded median, or canonical +0 when absent.
};
/// Borrowed finalized trace prefix and capture-population facts supplied by core.
/// Describes complete, unavailable or partial capture independently of solver success.
/// All records, summaries and text remain immutable/alive through validation and writing.
struct DiagnosticTraceView {
    DiagnosticKind kind{};
    DiagnosticAvailability availability{DiagnosticAvailability::unavailable}; ///< Retained-data state derived from capture status.
    DiagnosticCaptureStatus capture_status{DiagnosticCaptureStatus::not_started}; ///< Final source capture lifecycle status.
    DiagnosticRecords records{}; ///< Borrowed contiguous or callback-backed retained prefix.
    bool counts_present{}; ///< Whether both eligible/omitted total populations are known.
    std::uint64_t eligible_count{}, ///< Known eligible population; zero when counts are absent.
        omitted_count{}, ///< Known eligible rows after the retained prefix; zero when absent.
        capture_error_count{}; ///< Source capture errors; nonzero only for capture_error status.
    bool first_missing_ordinal_present{}; ///< Missing-prefix boundary may be known even without total counts.
    std::uint64_t first_missing_ordinal{}; ///< First missing zero-based eligible ordinal, or zero when absent.
    std::string_view failure_code{}; ///< Borrowed source capture failure identifier.
    std::span<const DiagnosticSummary> summaries{}; ///< Borrowed unique permitted summaries; unavailable traces have none.
};
/// Initialization/post-run validation only. Never invoked during active source capture.
/// Check one owned policy for kind/dictionary/criteria/capacity validity; no source records are read.
Status validate_diagnostic_description(const DiagnosticTraceDescription &) noexcept;
/// Validate ordered description/view pairs and the accepted transient population.
/// Borrowed spans must remain stable; returns aggregate status or the first validation/source
/// failure after checking prefix rows, masks, populations and bounded summary extrema.
std::expected<DiagnosticStatus,Status> validate_diagnostics(
    std::span<const DiagnosticTraceDescription>, std::span<const DiagnosticTraceView>,
    std::uint64_t accepted_count) noexcept;
/// Return the maximum directory-section contribution for kind, including metadata and
/// empty columns; return zero for an unknown kind.
std::uint64_t diagnostic_section_capacity(DiagnosticKind) noexcept;
/// Return a borrowed static wire column name for measure, or an empty string if unknown.
const char *diagnostic_measure_name(DiagnosticMeasure) noexcept;
} // namespace serene::outputs
