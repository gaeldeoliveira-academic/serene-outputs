#pragma once
#include "model.hpp"
#include "sha256.hpp"
namespace serene::outputs {
/// Finite admission budgets shared by producer preflight and verified-spool import.
/// Owns scalar ceilings only; core uses them before activation to admit borrowed documents.
/// Budgets account for raw materialization, conservative parser complexity and copy scratch;
/// they do not describe total process memory or change the retained bytes.
struct DocumentPreflightLimits {
    /// Materialization limit for included application/json documents; partial/other media stream.
    std::uint64_t document_bytes{16ull*1024*1024};
    std::uint64_t document_parser_budget_bytes{32ull*1024*1024}; ///< Maximum conservative complete-JSON parser charge.
    std::uint64_t metadata_budget_bytes{32ull*1024*1024}; ///< Total retained metadata admission ceiling.
    std::uint64_t workspace_budget_bytes{32ull*1024*1024}; ///< Maximum temporary requested workspace charge.
    std::uint64_t copy_tile_bytes{65536}; ///< Bounded streaming read/hash tile size in bytes.
};
/// Distinguishes inactive sources, retained uninterpreted bytes and fully parsed JSON.
enum class DocumentPreflightKind : std::uint8_t { inactive, retained_bytes, json_document };
/// Owned admission result consumed by core's pre-solve provenance reservation.
/// Stores the exact digest and charged sizes, not a parsed tree or borrowed document.
/// A successful result qualifies the observed immutable bytes only; it is not spool proof.
struct DocumentPreflight {
    DocumentPreflightKind kind{DocumentPreflightKind::inactive}; ///< Admission path actually performed.
    std::uint64_t source_bytes{}, ///< Exact retained byte count; zero for inactive sources.
        parser_charge_bytes{}, ///< Conservative JSON complexity charge; zero when not parsed.
        workspace_bytes{}; ///< Combined temporary charge compared with the supplied budget.
    /// Exact retained-byte digest; all zero for inactive documents, including no source reads.
    Digest sha256{};
};
/// Archive component. Included application/json admits any strict JSON value using the SAME
/// parser and conservative complexity charge as VerifiedSpool document validation. It rejects
/// duplicate keys, invalid/truncated UTF-8, BOM, nonfinite numbers and malformed JSON.
/// Partial documents and other media are streamed and hashed without a complete-JSON claim.
/// Retained media_type must be nonempty valid UTF-8 (the supported producer preflight policy).
/// Disabled/unavailable require zero bytes and read nothing; their media type is inapplicable.
/// Retained bytes obey the 4 GiB wire bound. document_bytes limits complete JSON materialization;
/// callers may enforce a smaller raw-byte policy. Metadata/workspace budgets include raw bytes,
/// parser charge and bounded overhead. Source callback failures propagate unchanged.
/// This is document admission, not metadata-object/JSONL-record validation or whole-spool proof.
/// Borrowed bytes remain immutable through actual saving; callers compare/use the returned hash.
/// The DocumentView supplies state, media type and source; limits bound each admission stage.
/// Returns owned accounting/digest on success or Status without changing the source.
std::expected<DocumentPreflight,Status> preflight_document(const DocumentView&,DocumentPreflightLimits={}) noexcept;
} // namespace serene::outputs
