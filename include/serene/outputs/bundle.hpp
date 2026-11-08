#pragma once
#include "archive.hpp"
#include <memory>
namespace serene::outputs {
/// Producer assertion captured before solving, committed with the immutable spool manifest.
/// Hashes bind exact bytes and detect corruption; they do not authenticate the producer.
struct SpoolBinding {
    Digest expected_file_sha256{}, authored_input_sha256{};
    std::uint64_t expected_file_bytes{};
    std::string expected_run_id, producer_identity, dependency_identity;
    bool trusted_manifest_binding{};
};
struct SpoolLimits {
    std::uint64_t metadata_budget_bytes{32ull*1024*1024}, workspace_budget_bytes{32ull*1024*1024};
    std::uint64_t copy_tile_bytes{65536}, document_bytes{16ull*1024*1024}, document_parser_budget_bytes{32ull*1024*1024};
};
struct SpoolSummary {
    std::string run_id,run_name;
    bool run_name_present{};
    RunKind kind{};
    bool diagnostics{},indexed_provenance{},fixed_accuracy{};
    std::uint64_t file_bytes{},section_count{},maximum_frame_values{},retained_metadata_bytes{},workspace_bytes{};
    Digest authored_input_sha256{};
    SectionState original_state{SectionState::unavailable};
};
class VerifiedSpool {
public:
    VerifiedSpool() noexcept;
    ~VerifiedSpool();
    VerifiedSpool(VerifiedSpool&&) noexcept;
    VerifiedSpool& operator=(VerifiedSpool&&) noexcept;
    VerifiedSpool(const VerifiedSpool&)=delete;
    VerifiedSpool& operator=(const VerifiedSpool&)=delete;
    /// Initial bundle import requires trusted manifest binding with exact file extent/digest,
    /// authored digest, run identity and nonempty producer/dependency identities.
    static std::expected<VerifiedSpool,Status> open(const std::filesystem::path&,const SpoolBinding&,SpoolLimits={});
    const SpoolSummary& summary() const noexcept;
    bool valid() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class BundleSession;
};
/// Finite pre-execution plan for each selected run, in authoritative catalog order.
struct BundleRunPlan {
    std::string run_id,run_name;
    bool run_name_present{};
    RunKind kind{};
    std::uint64_t section_capacity{},maximum_archive_bytes{},source_capacity{69};
};
struct BundleDescription {
    std::string file_id,created_utc,writer_name{"serene-outputs"},writer_version{"0.2.0"};
    Digest authored_input_sha256{};
    /// Core's retained immutable input snapshot; never reopened by the library.
    /// Included if any spool retained it, otherwise unavailable if any enabled capture failed,
    /// disabled if all were disabled. Explicit extra retention is a separate caller policy.
    DocumentView original_input{};
    bool explicitly_retain_original{};
    std::vector<BundleRunPlan> runs;
};
struct BundleLimits {
    std::uint64_t workspace_budget_bytes{64ull*1024*1024},metadata_budget_bytes{64ull*1024*1024};
    std::uint64_t copy_tile_bytes{65536};
    SpoolLimits spool{};
};
struct BundlePlan {
    std::uint64_t section_capacity{},source_capacity{},directory_bytes{},catalog_bytes{};
    std::uint64_t remap_bytes{},copy_bytes{},serialization_bytes{},spool_workspace_bytes{};
    std::uint64_t workspace_bytes{},maximum_archive_bytes{};
};
class PreparedBundle {
public:
    PreparedBundle() noexcept;
    ~PreparedBundle();
    PreparedBundle(PreparedBundle&&) noexcept;
    PreparedBundle& operator=(PreparedBundle&&) noexcept;
    PreparedBundle(const PreparedBundle&)=delete;
    PreparedBundle& operator=(const PreparedBundle&)=delete;
    static std::expected<PreparedBundle,Status> prepare(BundleDescription,BundleLimits={});
    const BundlePlan& plan() const noexcept;
    const BundleDescription& description() const noexcept;
    bool valid() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class BundleSession;
};
/// Actual union from finalized spool manifest facts, separate from conservative capacity plans.
struct BundleFeatures { bool diagnostics{},fixed_accuracy{}; };
class BundleSession {
public:
    BundleSession() noexcept;
    ~BundleSession();
    BundleSession(BundleSession&&) noexcept;
    BundleSession& operator=(BundleSession&&) noexcept;
    BundleSession(const BundleSession&)=delete;
    BundleSession& operator=(const BundleSession&)=delete;
    /// PreparedBundle and its borrowed original bytes outlive this session. Only one live
    /// session may borrow a prepared bundle; a second open returns invalid_state.
    /// Included JSON is strictly checked within the reserved spool workspace before copying.
    static std::expected<BundleSession,Status> open(PreparedBundle&,const std::filesystem::path&,BundleFeatures={},FaultHook={});
    /// Global index decouples authored catalog order from physical execution/copy order.
    /// The caller may release this spool handle after append; retained disk spools remain owned
    /// by core for retry. The handle is consumed by an append attempt; retry opens it again from
    /// its immutable binding. Copy rehashes actual section bytes and the complete open source file.
    Status append_verified_spool(std::uint64_t global_run_index,VerifiedSpool&) noexcept;
    Status finalize() noexcept;
    SessionState state() const noexcept;
    SaveStatistics statistics() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace serene::outputs
