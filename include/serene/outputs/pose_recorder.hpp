#pragma once
#include "types.hpp"
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
/// Independent external outcomes; file completion does not imply numerical success.
enum class Outcome : std::uint32_t { not_run = 0, completed = 1, failed = 2, stopped = 3 };
/// Owned terminal facts supplied by core when closing a visualization capture.
/// Recorder writes both outcomes; publication never upgrades a failed numerical run.
struct Completion {
    Outcome run{Outcome::not_run}; ///< Source numerical execution outcome.
    Outcome capture{Outcome::completed}; ///< Source capture outcome, independent of execution.
};
/// Owned finite recorder limits supplied at open by core capture or recorder tests.
/// Effective batch size is min(max_batch_frames, max_batch_bytes/frame_bytes).
/// Capacity must hold at least one complete catalog-sized frame; open rejects impossible limits.
struct Options {
    std::uint64_t max_batch_frames{32}; ///< Upper bound on frames retained in the pose batch.
    std::uint64_t max_batch_bytes{4 * 1024 * 1024}; ///< Upper bound on raw pose-batch storage in bytes.
    std::uint64_t max_catalog_bytes{4 * 1024 * 1024}; ///< Admission limit for the supplied catalog JSON text.
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
    std::uint64_t persisted_frames{}; ///< In complete chunks with checked stdio flush; not fsync.
    std::uint64_t buffered_frames{}; ///< Accepted frames awaiting the next chunk flush.
    std::uint64_t pose_chunks{}; ///< Complete raw pose chunks written so far.
    std::uint64_t written_bytes{}; ///< Successfully written bytes across all file records.
    std::uint64_t batch_capacity_bytes{}; ///< Reserved raw pose-batch byte capacity.
    std::uint64_t effective_batch_frames{}; ///< Frames admitted per batch after byte/count limits.
    std::uint64_t index_workspace_bytes{}; ///< Fixed upper bound for linked-index state/scratch,
                                           ///< excluding general statistics.
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
    /// Return failure on invalid state/frame or I/O, retaining the failed partial recording.
    Status append_frame(const Stamp &, std::span<const double> poses) noexcept;
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
