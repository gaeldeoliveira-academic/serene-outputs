#pragma once
#include "archive.hpp"
#include <memory>
namespace serene::outputs {
/// Producer assertion captured before solving, committed with the immutable spool manifest.
/// Hashes bind exact bytes and detect corruption; they do not authenticate the producer.
/// Owns the immutable manifest facts used by VerifiedSpool::open and core bundle retry.
/// The producer establishes this binding before solve/manifest publication; the library
/// never substitutes a reopened input file for the authored digest.
struct SpoolBinding {
    Digest expected_file_sha256{}, ///< Digest of the complete finalized spool file.
        authored_input_sha256{}; ///< Digest of the immutable original authored input snapshot.
    std::uint64_t expected_file_bytes{}; ///< Exact committed spool extent, including bootstrap and directory.
    std::string expected_run_id, ///< Exact sole run ID expected in the spool.
        producer_identity, ///< Nonempty producer/build identity asserted by the manifest.
        dependency_identity; ///< Nonempty dependency identity asserted by the manifest.
    bool trusted_manifest_binding{}; ///< Caller explicitly vouches for the immutable manifest provenance.
};
/// Finite parser/metadata/copy limits used by VerifiedSpool and bundle import.
/// Stores ceilings only; open accounts its retained graph and transient verification workspace.
/// Core uses the same document limits in producer preflight before activation.
struct SpoolLimits {
    std::uint64_t metadata_budget_bytes{32ull*1024*1024}, ///< Retained normalized metadata/directory ceiling.
        workspace_budget_bytes{32ull*1024*1024}; ///< Combined admitted verification workspace ceiling.
    std::uint64_t copy_tile_bytes{65536}, ///< Bounded streaming copy/hash tile capacity.
        document_bytes{16ull*1024*1024}, ///< Complete JSON document materialization limit.
        document_parser_budget_bytes{32ull*1024*1024}; ///< Conservative JSON parser complexity ceiling.
};
/// Owned verified spool facts exposed to core planning and BundleSession admission.
/// Contains source identities, feature flags and measured accounting, not borrowed file data.
/// Copies own their facts/strings independently; only summary()'s borrowed reference
/// depends on its VerifiedSpool owner. No producer trust follows from names.
struct SpoolSummary {
    std::string run_id, ///< Authoritative sole run ID.
        run_name; ///< Optional display text, independent of identity.
    bool run_name_present{}; ///< Distinguishes absent display name from a present empty name.
    RunKind kind{}; ///< Transient versus accepted-static result layout.
    bool diagnostics{}, ///< Spool uses run-diagnostics-1.
        indexed_provenance{}, ///< Spool carries an indexed source graph.
        fixed_accuracy{}; ///< At least one resolved profile uses fixed accuracy.
    std::uint64_t file_bytes{}, ///< Verified complete file extent.
        section_count{}, ///< Admitted directory entry count.
        maximum_frame_values{}, ///< Largest decoded frame needed during verification.
        retained_metadata_bytes{}, ///< Accounted owned normalized metadata/directory bytes.
        workspace_bytes{}; ///< Admitted peak requested verification workspace.
    Digest authored_input_sha256{}; ///< Original-input digest established by the trusted binding.
    SectionState original_state{SectionState::unavailable}; ///< Original-role retention state, preserving disabled versus unavailable.
};
/// Move-only owner of a fully verified immutable single-run source file and its graph.
/// Core imports finalized spools through open; BundleSession consumes each handle on an
/// append attempt. No frame is recompressed. The caller retains the disk spool/binding
/// for retry and keeps the file unchanged until copying and final rehash finish.
class VerifiedSpool {
public:
    /// Construct an empty owner without opening or allocating a working session.
    VerifiedSpool() noexcept;
    /// Release owned resources; never implicitly publish output.
    ~VerifiedSpool();
    /// Transfer sole ownership; source becomes empty and must not have active borrows.
    VerifiedSpool(VerifiedSpool&&) noexcept;
    /// Release prior state and transfer sole ownership; no active borrow may be invalidated.
    VerifiedSpool& operator=(VerifiedSpool&&) noexcept;
    /// Copying is forbidden because owned state and mutable workspace are unique.
    VerifiedSpool(const VerifiedSpool&)=delete;
    /// Copy assignment cannot share unique implementation state.
    VerifiedSpool& operator=(const VerifiedSpool&)=delete;
    /// Initial bundle import requires trusted manifest binding with exact file extent/digest,
    /// authored digest, run identity and nonempty producer/dependency identities.
    /// The path is opened read-only; limits bound parsing/verification. Return a fully
    /// admitted owner or Status; partial original evidence is an unsupported import capability.
    static std::expected<VerifiedSpool,Status> open(const std::filesystem::path&,const SpoolBinding&,SpoolLimits={});
    /// Borrow verified immutable facts; empty handles return the implementation empty summary.
    const SpoolSummary& summary() const noexcept;
    /// Return whether this owner retains successfully prepared/admitted state.
    bool valid() const noexcept;
private:
    /// Private storage/lifecycle implementation, defined in bundle.cpp.
    struct Impl;
    std::unique_ptr<Impl> impl_; ///< Sole implementation ownership; null denotes an empty/moved-from handle.
    friend class BundleSession;
};
/// Owned finite pre-execution reservation for one selected run in authoritative order.
/// Core derives bounds from exact or count-only Outputs planning before execution;
/// PreparedBundle checks every later imported spool fits this immutable reservation.
struct BundleRunPlan {
    std::string run_id, ///< Authoritative sole run ID.
        run_name; ///< Optional display text, independent of identity.
    bool run_name_present{}; ///< Distinguishes absent display name from a present empty name.
    RunKind kind{}; ///< Transient versus accepted-static result layout.
    std::uint64_t section_capacity{}, ///< Maximum normalized section contribution of this run.
        maximum_archive_bytes{}, ///< Conservative spool/archive byte reservation.
        source_capacity{69}; ///< Maximum normalized document/log sources during import.
};
/// Owned bundle catalog/policy plus one borrowed common-original document view.
/// Core prepares it before selected runs execute; PreparedBundle retains run plans
/// while the caller keeps original bytes immutable through all sessions/retries.
struct BundleDescription {
    std::string file_id, ///< Caller-provided bundle identity.
        created_utc, ///< Producer-supplied UTC creation text.
        writer_name{"serene-outputs"}, ///< Producer name, never a grammar-profile selector.
        writer_version{"0.2.0"}; ///< Producer release label, distinct from wire version.
    Digest authored_input_sha256{}; ///< Original-input digest established by the trusted binding.
    /// Core's retained immutable input snapshot; never reopened by the library.
    /// Included if any spool retained it, otherwise unavailable if any enabled capture failed,
    /// disabled if all were disabled. Explicit extra retention is a separate caller policy.
    DocumentView original_input{};
    bool explicitly_retain_original{}; ///< Explicit caller policy allowing common-original retention beyond spool union.
    std::vector<BundleRunPlan> runs; ///< Owned authoritative catalog order, independent of execution/copy order.
};
/// Owned finite limits for preparing the bundle directory, catalog and import workspace.
/// PreparedBundle uses these before opening any output file; spool limits apply to each
/// imported source working set, not all spools resident simultaneously.
struct BundleLimits {
    std::uint64_t workspace_budget_bytes{64ull*1024*1024}, ///< Aggregate prepared/import workspace ceiling.
        metadata_budget_bytes{64ull*1024*1024}; ///< Owned bundle metadata capacity ceiling.
    std::uint64_t copy_tile_bytes{65536}; ///< Bounded encoded-section copy tile size in bytes.
    SpoolLimits spool{}; ///< Per-spool parser, graph and frame-verification limits.
};
/// Owned conservative bundle reservation consumed by core and BundleSession.
/// Counts all selected run plans, including not-yet-committed spools; describes requested
/// payload storage and disk bounds rather than total process allocation.
struct BundlePlan {
    std::uint64_t section_capacity{}, ///< Maximum output directory entries.
        source_capacity{}, ///< Maximum normalized source descriptors.
        directory_bytes{}, ///< Reserved canonical directory storage.
        catalog_bytes{}; ///< Reserved ordered run-catalog storage.
    std::uint64_t remap_bytes{}, ///< Section/source ID remap storage.
        copy_bytes{}, ///< Bounded encoded-byte copy tile.
        serialization_bytes{}, ///< Reusable metadata rewrite buffer.
        spool_workspace_bytes{}; ///< Reserved peak one-spool verification working set.
    std::uint64_t workspace_bytes{}, ///< Aggregate requested workspace charge.
        maximum_archive_bytes{}; ///< Conservative final output-file byte bound.
};
/// Move-only owner of validated run reservations and reusable bundle workspace.
/// Core calls prepare before execution, then lends it to one BundleSession at a time.
/// No file is opened during prepare; all borrowed original-input bytes outlive save/retry.
/// Moving/destroying the preparation while a session borrows it is invalid caller behavior.
class PreparedBundle {
public:
    /// Construct an empty owner without opening or allocating a working session.
    PreparedBundle() noexcept;
    /// Release owned resources; never implicitly publish output.
    ~PreparedBundle();
    /// Transfer sole ownership; source becomes empty and must not have active borrows.
    PreparedBundle(PreparedBundle&&) noexcept;
    /// Release prior state and transfer sole ownership; no active borrow may be invalidated.
    PreparedBundle& operator=(PreparedBundle&&) noexcept;
    /// Copying is forbidden because owned state and mutable workspace are unique.
    PreparedBundle(const PreparedBundle&)=delete;
    /// Copy assignment cannot share unique implementation state.
    PreparedBundle& operator=(const PreparedBundle&)=delete;
    /// Validate/move description and allocate buffers under limits; return owner or Status.
    static std::expected<PreparedBundle,Status> prepare(BundleDescription,BundleLimits={});
    /// Borrow immutable prepared accounting, or the empty plan for an invalid handle.
    const BundlePlan& plan() const noexcept;
    /// Borrow retained description, or the empty description for an invalid handle.
    const BundleDescription& description() const noexcept;
    /// Return whether this owner retains successfully prepared/admitted state.
    bool valid() const noexcept;
private:
    /// Private storage/lifecycle implementation, defined in bundle.cpp.
    struct Impl;
    std::unique_ptr<Impl> impl_; ///< Sole implementation ownership; null denotes an empty/moved-from handle.
    friend class BundleSession;
};
/// Actual union from finalized spool manifest facts, separate from conservative capacity plans.
struct BundleFeatures {
    bool diagnostics{}, ///< At least one finalized spool actually carries diagnostics.
        fixed_accuracy{}; ///< At least one finalized spool actually uses fixed accuracy.
};
/// Move-only no-clobber publication owner that copies verified encoded sections.
/// Core opens it over PreparedBundle, appends each selected run once by catalog index,
/// then finalizes. It owns output I/O state while borrowing prepared buffers and original
/// bytes. Any failed append poisons the attempt; retry uses a fresh session and spool handles.
class BundleSession {
public:
    /// Construct an empty owner without opening or allocating a working session.
    BundleSession() noexcept;
    /// Release owned resources; never implicitly publish output.
    ~BundleSession();
    /// Transfer sole ownership; source becomes empty and must not have active borrows.
    BundleSession(BundleSession&&) noexcept;
    /// Release prior state and transfer sole ownership; no active borrow may be invalidated.
    BundleSession& operator=(BundleSession&&) noexcept;
    /// Copying is forbidden because owned state and mutable workspace are unique.
    BundleSession(const BundleSession&)=delete;
    /// Copy assignment cannot share unique implementation state.
    BundleSession& operator=(const BundleSession&)=delete;
    /// PreparedBundle and its borrowed original bytes outlive this session. Only one live
    /// session may borrow a prepared bundle; a second open returns invalid_state.
    /// Included JSON is strictly checked within the reserved spool workspace before copying.
    /// destination must be absent; scratch is exclusively created beside it. features describes actual spool
    /// union and hook supplies optional synchronous fault injection. Return owner or Status.
    static std::expected<BundleSession,Status> open(PreparedBundle&,const std::filesystem::path&,BundleFeatures={},FaultHook={});
    /// Global index decouples authored catalog order from physical execution/copy order.
    /// The caller may release this spool handle after append; retained disk spools remain owned
    /// by core for retry. The handle is consumed by an append attempt; retry opens it again from
    /// its immutable binding. Copy rehashes actual section bytes and the complete open source file.
    /// Return Status after remapping metadata/IDs and copying payload bytes without recompression.
    Status append_verified_spool(std::uint64_t global_run_index,VerifiedSpool&) noexcept;
    /// Require every planned run, write catalog/directory/bootstrap and checked-publish.
    /// Failure leaves unpublished scratch owned by the session for cleanup on destruction;
    /// success is not a power-loss durability claim.
    Status finalize() noexcept;
    /// Return lifecycle state; an empty/moved-from handle reports failed.
    SessionState state() const noexcept;
    /// Return owned counters, or zero values when the session is empty.
    SaveStatistics statistics() const noexcept;
private:
    /// Private storage/lifecycle implementation, defined in bundle.cpp.
    struct Impl;
    std::unique_ptr<Impl> impl_; ///< Sole implementation ownership; null denotes an empty/moved-from handle.
};
} // namespace serene::outputs
