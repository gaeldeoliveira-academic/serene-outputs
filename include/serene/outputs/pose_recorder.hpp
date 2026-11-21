#pragma once
#include "types.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
namespace serene::outputs::pose {
/// Wire v1 event bits; combine scheduled and target for one accepted state.
enum Event : std::uint32_t { prepared = 1, initial = 2, scheduled = 4, target = 8 };
/// A prepared snapshot is neither a transient acceptance nor an equilibrium certificate.
enum class Analysis : std::uint32_t { prepared = 0, transient = 1, static_equilibrium = 2 };
/// Owned source state facts passed by core capture adapters to Recorder::append_frame.
/// Contains physical time/load and event identity, never sample geometry or solver pointers.
/// The recorder validates analysis-specific combinations and assigns its own consecutive ordinal.
struct Stamp {
    Analysis analysis{Analysis::prepared}; ///< Source state family, independent of recorder file completion.
    std::uint32_t events{Event::prepared}; ///< Compatible Event bits; scheduled/target may share one frame.
    double time{};            ///< Physical evaluation time in seconds, including nonzero starts.
    std::uint64_t state_id{}; ///< Accepted step/equilibrium identity; zero for prepared.
    double load_factor{};     ///< Static only; zero otherwise, never used as time.
};
/// Provenance of finite four-point geometry, not a solver accuracy or tension result.
enum class WinchGeometryStatus : std::uint32_t {
    valid_positive_tension = 0, ///< Geometry evaluated with positive effective tension.
    zero_tension_limit = 1 ///< Deterministic positive-tension limiting position at zero effective tension.
};
/// Accepted Core packet lifecycle, deliberately separate from capture-plan identity.
enum class WinchSourcePhase : std::uint32_t {
    initialized = 0, ///< Prepared diagnostic geometry, not an equilibrium certificate.
    static_certified = 1, ///< Complete geometry from a certified static state.
    transient_initial = 2, ///< Complete initial transient packet at accepted step zero.
    transient_accepted = 3 ///< Complete positive-step packet; may be the first captured frame.
};
/// One caller-owned accepted winch arrangement in immutable catalog order.
/// Core adapters fill these prepared rows from accepted packets, never a new geometry solve.
/// All coordinates must be finite world metres; A/B/D retain physical-port provenance.
/// C is an algebraic position with no orientation, material coordinate or physical sample.
struct WinchGeometry {
    std::array<double, 12> world_xyz_m{}; ///< Axyz,Bxyz,Cxyz,Dxyz, unchanged caller binary64 values.
    WinchGeometryStatus geometry{WinchGeometryStatus::valid_positive_tension}; ///< Positive or zero-limit geometry provenance.
};
/// Synchronous borrowed view of every admitted selected prepared winch arrangement.
/// Core checks each row's exact binding/accepted identity before supplying this common stamp.
/// The recorder copies complete rows, owns no caller reference and never changes either input population.
struct WinchFrameView {
    Stamp stamp; ///< Independently asserted wire stamp; prepared/transient factor is zero.
    std::uint64_t preparation_generation{}; ///< Exact owning Core generation, not the capture-plan counter.
    WinchSourcePhase phase{WinchSourcePhase::initialized}; ///< Source lifecycle checked against stamp analysis/events.
    std::span<const WinchGeometry> geometries; ///< Exactly catalog F rows; borrowed only during append_frame.
};
/// Shared accepted-source lifecycle; its four wire values retain the winch meanings.
using SourcePhase = WinchSourcePhase;
/// One caller-owned fluid F pose in immutable fluid sample order, separate from structural N.
/// Valid samples preserve finite world metres and a unit local-F-to-world Hamilton quaternion.
/// Unavailable samples use exactly positive-zero xyz and quaternion (1,+0,+0,+0).
struct FluidPose {
    std::array<double, 7> world_pose{0, 0, 0, 1, 0, 0, 0}; ///< px,py,pz,qw,qx,qy,qz; original bits are copied.
    bool valid{}; ///< True is a real pose, including zero position; false is the canonical unavailable row.
};
/// Synchronous borrowed complete fluid pose population supplied by a prepared Core adapter.
/// Source binding and effective-field identities are checked by Core before this view is built.
/// No pointer or span survives append_frame; failed validation copies no population.
struct FluidFrameView {
    Stamp stamp; ///< Independently asserted exact structural wire stamp.
    std::uint64_t preparation_generation{}; ///< Owning Core generation, identical to every enabled descriptor.
    SourcePhase phase{SourcePhase::initialized}; ///< Accepted lifecycle checked against stamp analysis/events.
    std::span<const FluidPose> poses; ///< Exactly the fluid catalog sample count, in dense sample order.
};
/// Availability of the completed applied interval, independent of valid zero force.
enum class AppliedIntervalStatus : std::uint32_t {
    unavailable = 0, ///< Prepared/static/initial capture has no completed interval.
    completed = 1 ///< A positive transient capture owns one completed applied interval.
};
/// Pointer-free source evaluation facts copied by Core from its applied physical packet.
/// These positive opaque identities are not derived from accepted step IDs or ordered across frames.
/// Completed identity values lie in [1, INT64_MAX]; unavailable values are zero.
/// Core capture adapters own this metadata while the recorder copies its unchanged wire fields.
struct AppliedSourceEvaluation {
    std::uint64_t publication{}; ///< Exact source evaluation publication, zero only when unavailable.
    std::uint64_t mechanical_revision{}; ///< Opaque source mechanics revision.
    std::uint64_t state_revision{}; ///< Opaque source all-model state revision.
    std::uint64_t geometry_revision{}; ///< Opaque source prepared geometry revision.
    std::uint64_t environment_revision{}; ///< Opaque source environment revision.
    double time_s{}; ///< Exact accepted-start evaluation time in seconds; unavailable is positive zero.
};
/// Owned interval identity supplied by Core only after checked native/coordinator acceptance.
/// Core capture adapters preserve source facts independently of endpoint observation products.
/// Unavailable intervals require every field zero; completed intervals join the current structural stamp.
/// Completed source/end IDs satisfy 0 <= source < end <= INT64_MAX; finite source time
/// precedes end time, whose bits and accepted ID equal the structural endpoint stamp.
/// Against a prior saved transient stamp, source ID/time cannot precede that stamp.
/// Equal source/prior IDs require identical time bits; a greater source ID requires later time.
/// Prepared display time imposes no bound; a first positive capture needs no saved initial frame.
/// Opaque source, sequence and packet identities have no derived or monotonic relationship.
struct AppliedInterval {
    AppliedIntervalStatus status{AppliedIntervalStatus::unavailable}; ///< Completed-interval availability.
    std::uint64_t source_accepted_id{}; ///< Actual accepted source state, not an opaque-token reconstruction.
    AppliedSourceEvaluation source; ///< Exact pointer-free source evaluation facts.
    std::uint64_t end_accepted_id{}; ///< Actual completed endpoint, equal to the structural accepted state ID.
    double end_time_s{}; ///< Exact completed endpoint in seconds, bit-identical to the structural time.
    std::uint64_t prepared_step_sequence{}; ///< Opaque positive applied prepared-step identity.
    std::uint64_t packet{}; ///< Opaque positive held-packet identity, independent of accepted IDs.
};
/// One caller-owned logical physical load row, independent of any F display sampling.
/// Core supplies actual source F world metres and pre-transfer world force density in N/m.
/// Valid values retain their bits; unavailable rows contain six positive zeros with valid=false.
struct AppliedLoadSample {
    std::array<double, 3> source_position_world_m{}; ///< Actual accepted-start F origin, world xyz metres.
    std::array<double, 3> force_density_world_N_per_m{}; ///< Physical world xyz force density before quadrature/transfer.
    bool valid{}; ///< True includes a physical zero load; false requires canonical unavailable values.
};
/// Borrow one attachment's complete catalog-ordered logical load population during append_frame.
/// Its interval is owned metadata; its samples remain caller-owned until the synchronous copy returns.
/// Core's capture adapter supplies the full range even when its catalog range is permuted globally.
struct AppliedAttachmentFrameView {
    AppliedInterval interval; ///< Same exact wire interval prefix as every attachment on the coordinator.
    std::span<const AppliedLoadSample> samples; ///< Exactly this attachment's station count, in its catalog range order.
};
/// Synchronous complete applied-load view supplied by Core's accepted capture adapter.
/// Requires a qualified physical F provider but no fluid_poses display descriptor.
/// No pointer/span survives append_frame; all populations validate before any copy.
/// The owner serializes capture calls. Phase 0/1/2 permits only canonical unavailable intervals;
/// phase 3 requires a completed interval. Every attachment has the identical 96-byte source prefix.
struct AppliedLoadsFrameView {
    Stamp stamp; ///< Independently asserted structural endpoint stamp.
    std::uint64_t preparation_generation{}; ///< Common prepared lifetime; source and step generations were checked by Core.
    SourcePhase phase{SourcePhase::initialized}; ///< Prepared/static/initial unavailable or positive accepted completed phase.
    std::span<const AppliedAttachmentFrameView> attachments; ///< Complete attachment population in catalog order.
};
/// One caller-owned signed elastic moment at an independently cataloged sided cut.
/// Core transports the same coherent elastic wrench to the actual current structural N point
/// exactly once, then expresses all three components in that cut's actual current N axes.
/// Units are N.m; positive-span material acts on negative-span material for either side.
/// Recorder copying preserves finite component bits, including valid signed zeros; no norm,
/// frame rotation, interpolation or sign adjustment is performed. A false mask requires +0 bits.
struct ElasticMoment {
    std::array<double, 3> moment_N_Nm{}; ///< Signed current-N Mx,My,Mz about N, in dense cut order.
    bool valid{}; ///< True includes a real zero moment; false requires three positive binary64 zeros.
};
/// Synchronous borrowed complete moment population supplied by Core's prepared capture adapter.
/// Core proves provider/component capability for every requested phase and samples one coherent
/// accepted state. Wire-valid unavailability is explicit; it does not excuse a failed Core capture.
/// Use this view through FrameGroupView when elastic_moments is declared. Exactly the catalog cut
/// count is required; no pointer/span survives append_frame. The owner serializes capture calls.
struct ElasticMomentFrameView {
    Stamp stamp; ///< Independently asserted exact structural source stamp, not an applied interval's earlier source.
    std::uint64_t preparation_generation{}; ///< Owning Core generation, identical to every enabled descriptor.
    SourcePhase phase{SourcePhase::initialized}; ///< Common source lifecycle checked against stamp analysis/events.
    std::span<const ElasticMoment> moments; ///< Complete caller-owned rows in immutable dense cut order.
};
/// One caller-owned absolute unperturbed inflow velocity at an original fixed world point.
/// World xyz components include the prepared provider's mean/shear and turbulence in m/s;
/// they exclude induction, body-relative velocity and display displacement. Core owns coverage
/// classification and coherent provider sampling. Recorder copying never reconstructs the field.
struct InflowVelocity {
    std::array<double, 3> world_velocity_mps{0, 0, 0}; ///< Finite world ux,uy,uz; valid signed-zero bits are preserved.
    bool valid{}; ///< True includes calm zero velocity; false requires three exact positive binary64 zeros.
};
/// Borrow one complete plane population in its immutable catalog geometry and sample order.
/// Use dense plane order inside InflowPlanesFrameView; no geometry or provider pointer crosses
/// this boundary. Sample j=u+nu*v has the original realized position recorded by the catalog.
struct InflowPlaneView {
    std::uint64_t plane_id{}; ///< Dense catalog integer ID, not the user/request plane_id string.
    std::span<const InflowVelocity> samples; ///< Exactly nu*nv caller-owned rows, with u as the fast axis.
};
/// Synchronous borrowed complete plane packet supplied by Core's accepted capture adapter.
/// Core validates actual source/generation/logger readiness and samples the original fixed points
/// at this exact state time; unexpected provider failure rejects the whole capture. Explicit
/// legitimate unavailability retains every slot. Use through FrameGroupView when declared;
/// no pointer/span survives append_frame, and the owner serializes calls.
struct InflowPlanesFrameView {
    Stamp stamp; ///< Independently asserted exact structural state stamp, not a remapped provider clock.
    std::uint64_t preparation_generation{}; ///< Common positive prepared Core lifetime, identical to the catalog.
    SourcePhase phase{}; ///< Common accepted lifecycle, checked against stamp analysis/events.
    std::span<const InflowPlaneView> planes; ///< Complete dense plane order with exact IDs and fixed sample extents.
};
/// Borrow one complete structural/optional frame group for synchronous recorder copying.
/// Core supplies exactly the enabled populations with a common accepted state; absent pointers
/// mean absent descriptors, not unavailable samples. The recorder validates the whole group first.
struct FrameGroupView {
    Stamp stamp; ///< Common structural source state; recorder ordinal is assigned internally.
    std::span<const double> structural_poses; ///< P*7 physical xyz/wxyz scalars in base catalog order.
    const WinchFrameView* winches{}; ///< Borrowed complete winch view, or null only without its descriptor.
    const FluidFrameView* fluid_poses{}; ///< Borrowed complete F view, or null only without its descriptor.
    const AppliedLoadsFrameView* applied_loads{}; ///< Borrowed complete applied-load view, or null only without its descriptor.
    const ElasticMomentFrameView* elastic_moments{}; ///< Borrowed complete sided-cut view, or null only without its descriptor.
    const InflowPlanesFrameView* inflow_planes{}; ///< Borrowed complete finite-plane view, or null only without its descriptor.
};
/// Independent external outcomes; file completion does not imply numerical success.
enum class Outcome : std::uint32_t { not_run = 0, completed = 1, failed = 2, stopped = 3 };
/// Owned terminal facts supplied by core when closing a visualization capture.
/// Recorder writes both outcomes; publication never upgrades a failed numerical run.
struct Completion {
    Outcome run{Outcome::not_run}; ///< Source numerical execution outcome.
    Outcome capture{Outcome::completed}; ///< Source capture outcome, independent of execution.
};
/// Owned finite recorder limits supplied at open by core capture or recorder tests.
/// Effective batch size shares raw and workspace limits across every enabled frame population.
/// Capacity must hold at least one complete catalog-sized frame; open rejects impossible limits.
struct Options {
    std::uint64_t max_batch_frames{32}; ///< Upper bound on frames retained in the pose batch.
    std::uint64_t max_batch_bytes{4 * 1024 * 1024}; ///< Upper bound on combined raw pose/feature batch storage in bytes.
    std::uint64_t max_catalog_bytes{4 * 1024 * 1024}; ///< Admission limit for the supplied catalog JSON text.
    std::uint64_t max_workspace_bytes{128 * 1024 * 1024}; ///< Hard-capped payload budget: raw batches, actual physical/overlay reference capacities and 2048 scratch bytes.
};
/// Narrow deterministic I/O fault seam, also used to qualify optional sink failure.
enum class IoOperation : std::uint32_t { write = 1, flush = 2, close = 3, publish = 4 };
/// Borrowed synchronous fault callbacks used by recorder failure tests.
/// The context outlives Recorder, and callbacks must not throw/reenter the same recorder.
/// Null callbacks disable their seam; they do not imply asynchronous I/O.
struct FaultHook {
    void *context{}; ///< Borrowed test-owned state forwarded to callbacks.
    bool (*fail)(void *, IoOperation, std::uint64_t) noexcept {}; ///< context/operation/ordinal callback; true injects failure.
    std::size_t (*write_limit)(void *, std::uint64_t, std::size_t) noexcept {}; ///< context/write ordinal/requested bytes to permitted attempted bytes.
};
/// Recorder lifecycle; publication and failure are terminal.
enum class State : std::uint8_t { open, published, failed };
/// Owned constant-size recorder accounting consumed by core capture reporting and tests.
/// No per-frame or per-chunk index is retained in memory; counters describe checked I/O
/// progress and requested batch/index workspace, not total allocator/process memory.
struct Statistics {
    std::uint64_t accepted_frames{};  ///< Validated/copied, including a subsequent flush failure.
    std::uint64_t persisted_frames{}; ///< Complete pose chunks, with matching feature chunks when enabled; checked stdio flush, not fsync.
    std::uint64_t buffered_frames{}; ///< Accepted frames awaiting the next chunk flush.
    std::uint64_t pose_chunks{}; ///< Complete raw pose chunks written so far.
    std::uint64_t written_bytes{}; ///< Successfully written bytes across all file records.
    std::uint64_t batch_capacity_bytes{}; ///< Reserved combined raw batch bytes; excludes retained reference/layout tables.
    std::uint64_t effective_batch_frames{}; ///< Frames admitted per batch after byte/count limits.
    std::uint64_t index_workspace_bytes{}; ///< Fixed upper bound for linked-index state/scratch,
                                           ///< excluding general statistics.
    std::uint64_t physical_reference_bytes{}; ///< Actual retained winch reference-vector capacity in bytes; F tables are setup-only.
    std::uint64_t encoding_workspace_bytes{}; ///< Fixed 2048-byte active encoding/index allowance, including index_workspace_bytes.
    std::uint64_t workspace_capacity_bytes{}; ///< Admitted raw buffer capacities plus references and encoding allowance; excludes setup/general overhead.
    std::uint64_t effective_raw_budget_bytes{}; ///< min(max_batch_bytes, max_workspace_bytes minus reference and scratch charges).
    std::uint64_t overlay_reference_bytes{}; ///< Sum of actual retained applied attachment and inflow plane layout capacities in bytes; topology/JSON is setup-only.
};
/// Headless synchronous raw recorder, independent of scientific ArchiveSession and core.
/// open allocates; append copies one full borrowed frame into bounded storage. Owner-serialized.
/// Core capture adapters and independent recorder tests use open, repeated append_frame,
/// then finalize. A default/moved-from handle is not open; failed operations retain status.
/// Destructor closes but never publishes; unsuccessful recordings retain <destination>.partial.
class Recorder {
  public:
    /// Construct an empty handle without opening a file.
    Recorder() noexcept;
    /// Close/release owned resources without implicit publication.
    ~Recorder();
    /// Transfer sole session ownership; the source becomes empty.
    Recorder(Recorder &&) noexcept;
    /// Release prior state and transfer ownership from the rvalue source.
    Recorder &operator=(Recorder &&) noexcept;
    /// Copying is forbidden because file/session state has one owner.
    Recorder(const Recorder &) = delete;
    /// Copy assignment cannot share mutable file/session state.
    Recorder &operator=(const Recorder &) = delete;
    /// Validate/write borrowed catalog_json once, reserve Options-bounded storage and
    /// open destination.partial under optional FaultHook. Return an owner or Status.
    /// Catalog bytes are consumed synchronously; destination and .partial must both be absent.
    static std::expected<Recorder, Status> open(const std::filesystem::path &destination,
                                                std::string_view catalog_json, Options = {},
                                                FaultHook = {});
    /// Exactly sample_count*7 finite values: world xyz metres, then unit Hamilton wxyz
    /// quaternion rotating sample-local coordinates into world coordinates.
    /// No value is normalized, sign-adjusted, interpolated or retained beyond the bounded batch.
    /// Stamp supplies source identity/events; poses is borrowed only during the call.
    /// Choose this overload only for feature-free catalogs. Feature-enabled use fails explicitly.
    /// Return failure on invalid state/frame or I/O, retaining the failed partial recording.
    Status append_frame(const Stamp &, std::span<const double> poses) noexcept;
    /// Validate structural_stamp/poses and every winches row before copying one complete paired frame.
    /// poses borrows P*7 physical pose scalars; winches borrows F arrangements in catalog order.
    /// Its translated wire stamp, source generation and phase must match exact catalog/state facts.
    /// A/B/D are checked against their canonical physical samples with the fixed representation
    /// consistency bound 64*2^-52*max(1, norm_inf(feature), norm_inf(physical)); signed zeros agree.
    /// Neither population is overwritten, averaged or normalized. This is not a solver tolerance.
    /// Choose this overload only for a winch-enabled catalog without F, loads, moments or planes; new-overlay use fails.
    /// Return a latched failure for
    /// invalid lifecycle, extents, geometry/status/stamps or I/O; accepted counts advance only
    /// after the entire packet validates. No caller pointer survives this owner-serialized call.
    Status append_frame(const Stamp &structural_stamp, std::span<const double> poses,
                        const WinchFrameView &winches) noexcept;
    /// Validate and synchronously copy a complete group into setup-sized raw buffers.
    /// group borrows every enabled population with exact common state/generation/phase.
    /// Choose this overload for F/load/moment/plane catalogs; it also supports base and winch-only catalogs.
    /// Applied intervals obey AppliedInterval's source/prior/end joins, and all attachments share
    /// the exact interval prefix. No relationship is inferred between their opaque identity fields.
    /// Missing/extra views, nonfinite values, noncanonical unavailable rows or invalid bindings fail before any
    /// population is copied. Plane views retain dense IDs and exact nu*nv extents; all sample bits
    /// remain unchanged. Full batches flush F, loads, moments, planes, winch, then pose; persisted counts advance
    /// only after the complete group succeeds. Return a latched validation/I/O failure on error.
    Status append_frame(const FrameGroupView& group) noexcept;
    /// Flush last partial batch, linked seek index, outcomes and footer; checked no-clobber
    /// publish. Completion supplies external outcomes; return Status and never publish on failure.
    Status finalize(Completion = {}) noexcept;
    /// Return lifecycle state; an empty/moved-from handle reports failed.
    State state() const noexcept;
    /// Return retained failure/success status; empty handles report invalid_state.
    Status status() const noexcept;
    /// Return an owned accounting snapshot, or zero counters for an empty handle.
    Statistics statistics() const noexcept;
    /// Borrow the partial path until recorder move/destruction; empty handle returns empty.
    const std::filesystem::path &temporary_path() const noexcept;

  private:
    /// Private file handle, buffers and lifecycle state; defined in the implementation.
    struct Impl;
    std::unique_ptr<Impl> impl_; ///< Sole implementation ownership; null means an empty/moved-from handle.
};
} // namespace serene::outputs::pose
