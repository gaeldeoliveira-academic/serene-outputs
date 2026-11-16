#pragma once
#include "codec.hpp"
#include "model.hpp"
#include <filesystem>
#include <memory>
namespace serene::outputs {
/// Narrow deterministic fault seam for publication and source failure tests.
enum class IoOperation : std::uint8_t { source, codec, write, flush, close, publish, seek };
/// Borrowed deterministic I/O failure injection used by writer/publication tests.
/// ArchiveSession copies this descriptor, not its context. The caller keeps callbacks
/// and context alive; callbacks are synchronous and must not throw or mutate the session.
struct FaultHook {
    /// Callback receives context, operation kind and operation ordinal; failure Status aborts saving.
    using Function = Status (*)(void *, IoOperation, std::uint64_t) noexcept;
    void *context{}; ///< Borrowed test state passed unchanged to both callbacks.
    Function function{}; ///< Optional operation fault callback; null disables injected failures.
    /// Optional test seam limiting a physical write request; a short write fails publication.
    /// The callback returns the maximum bytes to attempt, bounded by the requested count.
    using WriteLimit = std::size_t (*)(void *, std::uint64_t, std::size_t) noexcept;
    WriteLimit write_limit{}; ///< Optional per-write cap; null allows the complete requested write.
};
/// Observable publication lifecycle; failed and published are terminal session states.
enum class SessionState : std::uint8_t { opened, writing, written, finalizing, published, failed };
/// Owned constant-size counters returned by archive/bundle sessions and memory tests.
/// Counts describe actual save work and prepared application payloads; they neither
/// measure allocator overhead nor certify process-wide peak memory.
struct SaveStatistics {
    std::uint64_t frames{}, ///< Number of accepted encoded result frames.
        payload_bytes{}; ///< Encoded result payload bytes, excluding metadata/index overhead.
    /// Actual attempted physical write operations, including bootstrap/backfill.
    std::uint64_t write_calls{};
    /// Actual positional seek operations.
    std::uint64_t seek_calls{};
    /// Bytes successfully written, including bootstrap patch and backfill writes.
    std::uint64_t written_bytes{};
    /// Requested preallocated application workspace, excluding caller histories.
    std::uint64_t application_reserved_bytes{};
};
/// Post-gate single-run native-container session. PreparedExport and Codec are borrowed and
/// preallocated. All source owners survive append/finalize/retry. No concurrent mutation or second
/// session. Core export adapters and synthetic writer tests append one finalized prefix,
/// then finalize to publish a no-clobber destination. Failure never publishes;
/// destruction closes resources and removes unpublished scratch but never publishes. This is checked I/O, not power-loss durability.
class ArchiveSession {
  public:
    /// Construct an empty handle without opening a file.
    ArchiveSession() noexcept;
    /// Close/release owned resources without implicit publication.
    ~ArchiveSession();
    /// Transfer sole session ownership; the source becomes empty.
    ArchiveSession(ArchiveSession &&) noexcept;
    /// Release prior state and transfer ownership from the rvalue source.
    ArchiveSession &operator=(ArchiveSession &&) noexcept;
    /// Copying is forbidden because file/session state has one owner.
    ArchiveSession(const ArchiveSession &) = delete;
    /// Copy assignment cannot share mutable file/session state.
    ArchiveSession &operator=(const ArchiveSession &) = delete;
    /// Borrow valid preparation/codec and open a new destination plus exclusive partial
    /// file under the optional fault hook. Return a session or admission/I/O error.
    static std::expected<ArchiveSession, Status>
    open(PreparedExport &, Codec &, const std::filesystem::path &, FaultHook = {});
    /// Write the complete borrowed result prefix and legacy provenance roles once.
    /// Choose indexed overload for prepared indexed provenance; failure poisons the session.
    Status append_run(const RunResultView &, const ProvenanceView &provenance = {}) noexcept;
    /// Write the complete borrowed result prefix and indexed source graph once.
    /// The prepared description must enable indexed provenance; no source ownership transfers.
    Status append_run(const RunResultView &, const IndexedProvenanceView &) noexcept;
    /// Complete directory/bootstrap, checked flush/close and no-clobber publication.
    /// Require a successfully written run; return failure without claiming publication.
    Status finalize() noexcept;
    /// Return current lifecycle state; an empty/moved-from handle reports failed.
    SessionState state() const noexcept;
    /// Return an owned snapshot of current counters, or zeros for an empty handle.
    SaveStatistics statistics() const noexcept;
    /// Borrow the partial-file path until session destruction/move; empty handle returns empty.
    const std::filesystem::path &temporary_path() const noexcept;

  private:
    /// Private file handle, buffers and lifecycle state; defined in the implementation.
    struct Impl;
    std::unique_ptr<Impl> impl_; ///< Sole implementation ownership; null means an empty/moved-from handle.
};
} // namespace serene::outputs
