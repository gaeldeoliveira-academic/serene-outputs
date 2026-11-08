#pragma once
#include "model.hpp"
#include "sha256.hpp"
namespace serene::outputs {
/// The same complete-document limits used by verified spool import. Source bytes are borrowed.
struct DocumentPreflightLimits {
    /// Materialization limit for included application/json documents; partial/other media stream.
    std::uint64_t document_bytes{16ull*1024*1024};
    std::uint64_t document_parser_budget_bytes{32ull*1024*1024};
    std::uint64_t metadata_budget_bytes{32ull*1024*1024};
    std::uint64_t workspace_budget_bytes{32ull*1024*1024};
    std::uint64_t copy_tile_bytes{65536};
};
enum class DocumentPreflightKind : std::uint8_t { inactive, retained_bytes, json_document };
struct DocumentPreflight {
    DocumentPreflightKind kind{DocumentPreflightKind::inactive};
    std::uint64_t source_bytes{},parser_charge_bytes{},workspace_bytes{};
    /// Exact retained-byte digest; all zero for inactive documents, including no source reads.
    Digest sha256{};
};
/// Archive component. Included application/json admits any strict JSON value using the SAME
/// parser and conservative complexity charge as VerifiedSpool document validation. It rejects
/// duplicate keys, invalid/truncated UTF-8, BOM, nonfinite numbers and malformed JSON.
/// Partial documents and other media are streamed and hashed without a complete-JSON claim.
/// Retained media_type must be nonempty valid UTF-8 (the supported producer preflight policy).
/// Disabled/unavailable require zero bytes and read nothing; their media type is inapplicable.
/// Retained bytes obey the4GiB wire bound. document_bytes limits complete JSON materialization;
/// callers may enforce a smaller raw-byte policy. Metadata/workspace budgets include raw bytes,
/// parser charge and bounded overhead. Source callback failures propagate unchanged.
/// This is document admission, not metadata-object/JSONL-record validation or whole-spool proof.
/// Borrowed bytes remain immutable through actual saving; callers compare/use the returned hash.
std::expected<DocumentPreflight,Status> preflight_document(const DocumentView&,DocumentPreflightLimits={}) noexcept;
} // namespace serene::outputs
