#pragma once
#include "codec.hpp"
#include "model.hpp"
#include <filesystem>
#include <memory>
namespace serene::outputs {
/// Narrow deterministic fault seam for publication and source failure tests.
enum class IoOperation : std::uint8_t { source, codec, write, flush, close, publish, seek };
struct FaultHook {
    using Function = Status (*)(void *, IoOperation, std::uint64_t) noexcept;
    void *context{};
    Function function{};
    /// Optional test seam limiting a physical write request; a short write fails publication.
    /// The callback returns the maximum bytes to attempt, bounded by the requested count.
    using WriteLimit = std::size_t (*)(void *, std::uint64_t, std::size_t) noexcept;
    WriteLimit write_limit{};
};
enum class SessionState : std::uint8_t { opened, writing, written, finalizing, published, failed };
struct SaveStatistics {
    std::uint64_t frames{}, payload_bytes{};
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
/// session.
class ArchiveSession {
  public:
    ArchiveSession() noexcept;
    ~ArchiveSession();
    ArchiveSession(ArchiveSession &&) noexcept;
    ArchiveSession &operator=(ArchiveSession &&) noexcept;
    ArchiveSession(const ArchiveSession &) = delete;
    ArchiveSession &operator=(const ArchiveSession &) = delete;
    static std::expected<ArchiveSession, Status>
    open(PreparedExport &, Codec &, const std::filesystem::path &, FaultHook = {});
    Status append_run(const RunResultView &, const ProvenanceView &provenance = {}) noexcept;
    Status append_run(const RunResultView &, const IndexedProvenanceView &) noexcept;
    Status finalize() noexcept;
    SessionState state() const noexcept;
    SaveStatistics statistics() const noexcept;
    const std::filesystem::path &temporary_path() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace serene::outputs
