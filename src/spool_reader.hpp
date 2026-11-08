#pragma once
// Private archive/merger implementation interface; never installed.
#include <serene/outputs/codec.hpp>
#include <serene/outputs/model.hpp>
#include <serene/outputs/document.hpp>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace serene::outputs::detail {
using Json = nlohmann::json;
struct SpoolBinding {
    Digest expected_file_sha256{}, authored_input_sha256{};
    std::uint64_t expected_file_bytes{};
    std::string expected_run_id, producer_identity, dependency_identity;
    bool trusted_manifest_binding{};
};
struct SpoolReadLimits {
    std::uint64_t metadata_budget_bytes{64ull*1024*1024};
    std::uint64_t workspace_budget_bytes{64ull*1024*1024};
    std::uint64_t copy_tile_bytes{65536};
    std::uint64_t document_bytes{16ull*1024*1024};
    std::uint64_t document_parser_budget_bytes{64ull*1024*1024};
};
/// Validate the retained common original before a spool working set is resident.
/// Included JSON accepts any strict JSON value, as the original-document contract
/// requires; other media types retain exact bytes and are only hash-verified.
/// Returns the admitted peak workspace charge. Borrowed bytes stay immutable.
std::expected<std::uint64_t,Status> validate_original_document(
    const DocumentView&, const Digest& authored_digest, SpoolReadLimits={});
struct DirectoryEntry {
    std::uint32_t kind{};
    std::uint64_t offset{}, size{};
    Digest digest{};
};
struct SourceReference {
    std::uint8_t role{};
    std::uint64_t source_index{}, order{};
};
struct SourceMetadata {
    std::uint64_t source_index{}, metadata_section{UINT64_MAX};
    std::uint8_t kind{};
    /// Normalized source_schema/source_index/kind plus document or JSONL fields.
    /// Legacy root descriptors are normalized without changing their data section IDs.
    Json metadata;
};
struct TraceMetadata {
    std::uint8_t kind{};
    std::uint64_t metadata_section{};
    Json metadata;
};
struct SpoolSummary {
    std::string run_id, run_name;
    bool run_name_present{};
    RunKind kind{};
    bool diagnostics{}, indexed_provenance{}, fixed_accuracy{};
    std::uint64_t file_bytes{}, section_count{}, maximum_frame_values{};
    std::uint64_t retained_metadata_bytes{}, workspace_bytes{};
    Digest authored_input_sha256{};
    SectionState original_state{SectionState::unavailable};
};
class VerifiedSpoolData {
public:
    VerifiedSpoolData();
    ~VerifiedSpoolData();
    VerifiedSpoolData(VerifiedSpoolData&&) noexcept;
    VerifiedSpoolData& operator=(VerifiedSpoolData&&) noexcept;
    VerifiedSpoolData(const VerifiedSpoolData&)=delete;
    VerifiedSpoolData& operator=(const VerifiedSpoolData&)=delete;
    static std::expected<VerifiedSpoolData,Status> open(
        const std::filesystem::path&, const SpoolBinding&, SpoolReadLimits={});
    Status read(std::uint64_t section, std::uint64_t relative_offset,
                std::span<std::byte>) noexcept;
    /// Rehash the actual open file in bounded tiles, including extent, after a merger copy.
    Status verify_unchanged() noexcept;
    SpoolSummary summary;
    Json root, run;
    std::vector<DirectoryEntry> directory;
    std::vector<SourceMetadata> sources;
    std::vector<SourceReference> references;
    std::vector<TraceMetadata> traces;
    std::uint64_t root_section{}, run_section{}, original_source_index{};
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend struct SpoolValidator;
};
} // namespace serene::outputs::detail
