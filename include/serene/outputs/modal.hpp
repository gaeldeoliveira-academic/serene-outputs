#pragma once
#include "archive.hpp"
#include <array>
#include <optional>

namespace serene::outputs {
/// Closed physical classification, independent of search completion and publication.
enum class ModalModeClass : std::uint8_t { near_zero_unresolved, indeterminate_negative, adverse_negative, elastic };
/// Whether the producer certified the complete requested prefix or retained failure evidence.
enum class ModalResultKind : std::uint8_t { certified_spectrum, failure_diagnostics };
/// Search coverage, independent of the signs of the returned eigenvalues.
enum class ModalSearchStatus : std::uint8_t { complete, incomplete, failed, not_started };
/// Numerical certification status; a certified adverse direction is still certified.
enum class ModalCertificationStatus : std::uint8_t { certified, failed, unavailable };
/// Physical address type. Material locations retain an explicit sided coordinate.
enum class ModalObservationKind : std::uint8_t { port, material_station };
/// Material limit convention; an absent material coordinate requires unspecified.
enum class ModalStationSide : std::uint8_t { unspecified, minus, plus };

/// Authored selection flags, copied at preparation. Signed eigenvalues and classes remain
/// mandatory identity/certification facts even when eigenvalues was not requested explicitly.
struct ModalQuantities {
    bool eigenvalues{}; ///< Authored eigenvalue selection, independent of required identity data.
    bool frequencies_hz{}; ///< Request real frequencies for certified elastic directions.
    bool mode_shapes{}; ///< Request physical observation coefficients.
    bool mode_diagnostics{}; ///< Request per-direction residual/constraint/mass evidence.
};

/// Owned immutable physical observation catalog row, copied before inactive export.
/// World reference orientation maps local section axes to world with scalar-first quaternion.
struct ModalObservationDescription {
    std::string request_id; ///< Unique authored observation/output ID in request order.
    std::string occurrence_id; ///< Resolved physical occurrence identity.
    ModalObservationKind kind{ModalObservationKind::port}; ///< Address vocabulary for this row.
    std::string port_id; ///< Resolved port identity; empty for direct material stations.
    std::string reference_triad; ///< Source triad: port_local, beam_material, blade_chord or blade_structural_principal.
    bool material_s_present{}; ///< Whether this port/station has a resolved material coordinate; required for material stations.
    double material_s_m{}; ///< Present material coordinate in metres; canonical positive zero when absent.
    ModalStationSide side{ModalStationSide::unspecified}; ///< Requested/resolved material limit.
    std::array<double,3> position_world_m{}; ///< Reference world point in metres.
    std::array<double,4> orientation_world_wxyz{1.,0.,0.,0.}; ///< Unit local-to-world reference quaternion.
};

/// Owned modal preparation identity and physical catalog; no solver or native backend types.
struct ModalArchiveDescription {
    std::string file_id; ///< Canonical lowercase UUID archive identity.
    std::string created_utc; ///< Valid UTC calendar timestamp ending in Z, with optional fractional seconds.
    std::string writer_name{"serene-outputs"}; ///< Producer identity, not a capability selector.
    std::string writer_version{"0.3.0"}; ///< Package version, distinct from wire version 1.2.
    std::string run_id; ///< Nonempty authored run identity.
    std::string run_name; ///< Optional nonunique display name.
    bool run_name_present{}; ///< Distinguishes absent name from present empty text.
    std::string assembly_ref; ///< Bound root assembly identity.
    SectionState reference_state{SectionState::included}; ///< Included or unavailable physical reference.
    std::string physical_id; ///< Physical reference identity; empty when unavailable.
    std::string binding_id; ///< Exact numerical-coordinate identity; empty when unavailable.
    ModalQuantities requested; ///< Authoritative requested quantities.
    std::vector<ModalObservationDescription> observations; ///< Owned ordered observation references.
};

/// Finite retained-result ceilings; these are capacities, never claimed solved populations.
struct ModalCapacity {
    std::uint64_t modes{}; ///< Maximum retained independent directions, at most 1,000,000.
    std::uint64_t clusters{}; ///< Maximum retained clusters, at most modes.
    std::uint64_t report_entries{1000000}; ///< Maximum dependent-row or effective-shift entries.
};

/// Borrowed result scalar for one certified direction. Optional evidence is absent rather
/// than NaN; serialization uses presence bytes and canonical positive zero for missing data.
struct ModalModeView {
    std::uint64_t mode_id{}; ///< Stable unique identity within this run, not a mesh-independent label.
    std::uint64_t cluster_index{}; ///< Ordinal of this direction's cluster in the borrowed cluster table.
    double eigenvalue_per_s2{}; ///< Finite signed eigenvalue, preserved reversibly including signed zero.
    ModalModeClass classification{ModalModeClass::near_zero_unresolved}; ///< Source-certified physical class.
    std::optional<double> frequency_hz; ///< Positive real Hz only for requested elastic modes.
    std::optional<double> projected_relative_residual; ///< Nonnegative normalized projected residual.
    std::optional<double> constraint_relative_error; ///< Nonnegative scaled constraint violation.
    std::optional<double> modal_mass; ///< Positive physical mass norm, nominally one.
    std::optional<double> display_scale; ///< Optional finite positive display multiplier, never applied to shapes.
};

/// Borrowed cluster identity and complete contiguous member range in signed eigenvalue order.
struct ModalClusterView {
    std::uint64_t cluster_id{}; ///< Stable unique cluster identity within this run.
    std::uint64_t first_mode_index{}; ///< First mode ordinal in the cluster.
    std::uint64_t mode_count{}; ///< Positive number of independent retained directions.
};

/// Source-authored selection and certification tolerances, preserved independently of outcomes.
struct ModalCriteriaView {
    double minimum_eigenvalue_per_s2{}; ///< Closed authored interval lower bound.
    double maximum_eigenvalue_per_s2{}; ///< Closed authored interval upper bound.
    double zero_eigenvalue_tolerance_per_s2{}; ///< Near-zero threshold applied first.
    double negative_eigenvalue_tolerance_per_s2{}; ///< Adverse threshold on remaining negative estimates.
    double relative_residual_tolerance{}; ///< Accepted projected residual threshold.
    double constraint_tolerance{}; ///< Accepted physical admissibility threshold.
    double mass_orthogonality_tolerance{}; ///< Accepted mass normalization/orthogonality threshold.
    double cluster_relative_tolerance{}; ///< Adjacent gap relative tolerance.
    double cluster_absolute_tolerance_per_s2{}; ///< Adjacent gap absolute tolerance.
    double cluster_subspace_tolerance{1e-5}; ///< Accepted upper bound on the estimated mass-weighted cluster subspace sine.
};

/// Borrowed typed solver report matching Core's M1 report. Optional scalar facts distinguish
/// unavailable evidence from a known zero. Count arrays and shifts borrow immutable owners.
struct ModalSolveReportView {
    std::optional<std::uint64_t> physical_coordinate_count, fixed_coordinate_count, constraint_rows, constraint_rank; ///< Physical/constraint dimensions when established.
    std::span<const std::uint64_t> dependent_constraint_rows; ///< Original dependent constraint row identities.
    SectionState dependent_constraint_rows_state{SectionState::unavailable}; ///< Included (possibly empty) or unavailable row evidence.
    std::optional<std::uint64_t> reduced_dimension, reduced_mass_nonzeros, reduced_stiffness_nonzeros; ///< Reduced operator dimensions/nonzeros.
    std::optional<double> lift_constraint_error, mass_symmetry_error, stiffness_symmetry_error, maximum_mass_orthogonality_error; ///< Nonnegative measured certification diagnostics.
    std::optional<double> maximum_cluster_subspace_sine_bound; ///< Nonnegative source-estimated mass-weighted cluster subspace sine bound.
    std::optional<double> maximum_projected_relative_residual; ///< Nonnegative maximum accepted raw projected eigenpair residual.
    std::uint64_t requested_elastic_modes{}; ///< Required positive quota on success; zero may preserve invalid-options failure evidence.
    std::uint64_t searched_candidates{}; ///< Actual source-reported searched direction count.
    std::uint64_t retained_elastic_modes{}; ///< Certified positive count, zero on a failure record.
    std::optional<std::uint64_t> dense_counted_eigenvalues, cutoff_cluster_size; ///< Dense count and final cluster evidence.
    std::optional<double> certified_lower_eigenvalue_per_s2, certified_upper_eigenvalue_per_s2, upper_separator_eigenvalue_per_s2; ///< Optional source-counted prefix/separator, independent of returned Ritz estimates.
    std::optional<double> dense_absolute_uncertainty_per_s2; ///< Nonnegative dense comparison uncertainty.
    bool exhausted_complete_spectrum{}, used_sparse_path{}, used_dense_fallback{}; ///< Independent source facts.
    std::string_view dense_fallback_reason; ///< Borrowed fallback explanation, possibly empty.
    std::optional<std::uint64_t> sparse_requested_candidates, sparse_converged_candidates, sparse_iterations, krylov_basis_size; ///< Direct search attempt counts.
    std::span<const double> effective_shifts_per_s2; ///< Ordered finite attempted signed shifts.
    SectionState effective_shifts_state{SectionState::unavailable}; ///< Included (possibly empty) or unavailable shift evidence.
    std::optional<std::uint64_t> constraint_workspace_bytes, operator_workspace_bytes, dense_workspace_bytes, dense_work_bound, factor_workspace_bytes, basis_workspace_bytes, result_payload_bytes; ///< Source preflight payload/work estimates, not total-process memory.
    std::optional<double> reduction_seconds, dense_count_seconds, sparse_seconds, dense_fallback_seconds; ///< Nonnegative measured inactive solver durations.
};

/// Immutable borrowed final result for one inactive export. The caller owns an external lease
/// retaining every span, string, callback context and preparation/result owner through append,
/// finalize or abandonment; it prevents context replacement and concurrent mutation. Outputs
/// neither acquires Core objects nor samples an active solver. A failed save may be retried with
/// a new session while the same lease remains valid. Time/load/committed-row axes do not apply.
struct ModalRunResultView {
    ModalResultKind result_kind{ModalResultKind::failure_diagnostics}; ///< Spectrum versus explicitly identified failure record.
    ModalSearchStatus search_status{ModalSearchStatus::not_started}; ///< Independent search completion.
    ModalCertificationStatus certification_status{ModalCertificationStatus::unavailable}; ///< Independent numerical certification.
    std::string_view solve_code; ///< Nonempty stable producer code; "success" exactly for a certified spectrum.
    std::string_view diagnostic; ///< Located failure explanation; empty on success.
    ModalCriteriaView criteria; ///< Borrowed-by-value authored thresholds.
    ModalSolveReportView report; ///< Typed borrowed report and optional scalar evidence.
    std::span<const ModalModeView> modes; ///< Complete successful prefix, empty for failure diagnostics.
    std::span<const ModalClusterView> clusters; ///< Complete contiguous partition of modes, empty for failure diagnostics.
    SectionState frequencies_state{SectionState::disabled}; ///< Included, disabled/unrequested or unavailable.
    SectionState mode_diagnostics_state{SectionState::disabled}; ///< Included, disabled/unrequested or unavailable.
    SectionState shapes_state{SectionState::disabled}; ///< Included, disabled/unrequested or unavailable.
    ValueView shapes; ///< Borrowed [mode,6*observation] rows: world translation XYZ then spatial rotation XYZ.
    DocumentView solver_configuration; ///< Optional exact inactive solver configuration evidence.
    DocumentView execution; ///< Optional exact execution/provenance evidence.
};

/// Owned modal metadata and reusable frame/copy buffers. Preparation allocates within explicit
/// budgets; inactive export uses fixed frame/index/directory and bounded JSON-emission scratch.
/// The plan is requested payload accounting, not a total-process allocation guarantee.
class PreparedModalExport {
public:
    /// Construct an invalid empty preparation; use prepare before writing.
    PreparedModalExport() = default;
    /// Move sole preparation ownership; no live session may borrow either object.
    PreparedModalExport(PreparedModalExport&&) noexcept = default;
    /// Replace sole ownership; no live session may borrow either object.
    PreparedModalExport& operator=(PreparedModalExport&&) noexcept = default;
    /// Copies are disabled to avoid unaccounted workspace duplication.
    PreparedModalExport(const PreparedModalExport&) = delete;
    /// Copies are disabled to avoid shared mutable workspace.
    PreparedModalExport& operator=(const PreparedModalExport&) = delete;
    /// Validate/copy owned metadata and reserve buffers; opens no files. Returns bounded failure.
    static std::expected<PreparedModalExport,Status> prepare(ModalArchiveDescription,ModalCapacity,ExportLimits = {});
    /// Validate immutable borrowed result/state/dimensions without modifying it or opening files.
    Status validate(const ModalRunResultView&) const noexcept;
    /// Borrow retained description through preparation replacement/destruction.
    const ModalArchiveDescription& description() const noexcept;
    /// Borrow admitted capacity ceilings through preparation replacement/destruction.
    const ModalCapacity& capacity() const noexcept;
    /// Borrow admitted frame/byte budgets through preparation replacement/destruction.
    const ExportLimits& limits() const noexcept;
    /// Borrow conservative requested workspace and archive-size accounting.
    const WorkspacePlan& plan() const noexcept;
    /// Return whether preparation currently has its required buffers and metadata.
    bool valid() const noexcept;
    /// Borrow exclusive frame gather scratch; no concurrent validation/write is supported.
    std::span<double> gather_buffer() noexcept;
    /// Borrow disjoint frame reconstruction scratch for exact codec verification.
    std::span<double> decode_buffer() noexcept;
    /// Borrow canonical frame-index scratch for a serialized session.
    std::span<std::byte> index_buffer() noexcept;
    /// Borrow source/canonical-column copy scratch for a serialized session.
    std::span<std::byte> copy_buffer() noexcept;
    /// Borrow fixed directory scratch for a serialized session.
    std::span<std::byte> directory_buffer() noexcept;
    /// Borrow bounded strict-JSON emission scratch for a serialized session.
    std::span<std::byte> serialization_buffer() noexcept;
private:
    friend class ModalArchiveSession;
    ModalArchiveDescription description_; ///< Sole catalog/identity ownership.
    ModalCapacity capacity_; ///< Admitted result ceilings.
    ExportLimits limits_; ///< Admitted workspace policy.
    WorkspacePlan plan_; ///< Requested payload/extent accounting.
    std::vector<double> gather_, decode_; ///< Reusable disjoint frame scratch.
    std::vector<std::byte> copy_; ///< Reusable source/canonical-array byte tile.
    std::vector<std::byte> index_, directory_, serialization_; ///< Fixed reusable index/directory/JSON scratch.
    mutable std::vector<std::uint64_t> identity_scratch_; ///< Serial validation scratch for exact unique mode/cluster IDs.
};

/// Single-run 1.2 session with exclusive temporary file and no-clobber publication. Borrows
/// preparation/codec/result owners; the caller serializes inactive calls. At most one live
/// session may borrow a preparation or codec. Destruction never publishes.
/// Failed/unpublished temporary files are removed on destruction; no power-loss durability claim.
class ModalArchiveSession {
public:
    /// Construct an empty handle; open creates the exclusive temporary file.
    ModalArchiveSession() noexcept;
    /// Close and remove unpublished temporary state without implicit publication.
    ~ModalArchiveSession();
    /// Transfer sole file/session ownership; the source becomes empty.
    ModalArchiveSession(ModalArchiveSession&&) noexcept;
    /// Replace sole file/session ownership, releasing previous unpublished resources.
    ModalArchiveSession& operator=(ModalArchiveSession&&) noexcept;
    /// Copies cannot share a mutable publication session.
    ModalArchiveSession(const ModalArchiveSession&) = delete;
    /// Copies cannot share a mutable publication session.
    ModalArchiveSession& operator=(const ModalArchiveSession&) = delete;
    /// Borrow valid preparation/codec, reserve bounded session metadata and create scratch.
    static std::expected<ModalArchiveSession,Status> open(PreparedModalExport&,Codec&,const std::filesystem::path&,FaultHook = {});
    /// Write one complete result plus legacy provenance roles; failure poisons this session.
    Status append_run(const ModalRunResultView&,const ProvenanceView& = {}) noexcept;
    /// Finalize header/directory, checked close and no-clobber publish; requires written state.
    Status finalize() noexcept;
    /// Return lifecycle state; empty/moved-from handles report failed.
    SessionState state() const noexcept;
    /// Return current owned constant-size work counters; empty handles return zero counters.
    SaveStatistics statistics() const noexcept;
    /// Borrow the scratch path through move/destruction; empty handles return an empty path.
    const std::filesystem::path& temporary_path() const noexcept;
private:
    struct Impl; ///< Private owned checked I/O, directory and publication state.
    std::unique_ptr<Impl> impl_; ///< Sole implementation ownership.
};
} // namespace serene::outputs
