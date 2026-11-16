#pragma once
#include "types.hpp"
#include <expected>
#include <span>
namespace serene::outputs {
/// Incremental fixed-state SHA-256 wrapper around pinned public-domain B-Con implementation.
/// Owns opaque inline state, used by archive section writers and verified-spool copying.
/// One owner serializes update calls, then calls finish once; finishing mutates the state.
/// No input span is retained and no dynamic allocation is required.
class Sha256 {
  public:
    /// Initialize a fresh empty digest stream.
    Sha256() noexcept;
    /// Consume the borrowed byte span synchronously and extend this digest state.
    void update(std::span<const std::byte>) noexcept;
    /// Finalize once and return the owned 32-byte digest; do not append afterward.
    Digest finish() noexcept;

  private:
    alignas(std::uint64_t) std::array<std::byte, 128> state_{}; ///< Inline aligned native SHA state; layout stays private.
};
/// Hash one borrowed byte span synchronously and return its owned digest.
Digest sha256(std::span<const std::byte>) noexcept;
/// Logical index record used by archive writers, spool validation and codec tests.
/// Owns a frame location, encoding/profile identity and encoded-byte digest; it borrows nothing.
/// encode_record/decode_record define the 77-byte little-endian wire layout, so native
/// struct padding never reaches disk or a digest. Extent validity is checked by consumers.
struct FrameRecord {
    std::uint64_t outer_index{}, ///< Logical row containing this frame.
        inner_begin{}, ///< First scalar/sample within the row.
        count{}, ///< Number of binary64 values represented.
        byte_offset{}, ///< Offset within the run's shared encoded payload section.
        byte_count{}; ///< Encoded frame byte length.
    Encoding encoding{Encoding::raw}; ///< Raw or pinned ZFP payload interpretation.
    std::uint32_t profile_index{}; ///< Index in the run profile catalog.
    Digest digest{}; ///< SHA-256 of exactly the encoded frame bytes.
};
/// Encode record fields in the fixed little-endian layout; no native padding is copied.
std::array<std::byte, 77> encode_record(const FrameRecord &) noexcept;
/// Decode exactly 77 borrowed bytes, rejecting a wrong length.
/// This is layout decoding; callers separately validate the encoding tag, ranges, profile and payload digest.
std::expected<FrameRecord, Status> decode_record(std::span<const std::byte>) noexcept;
} // namespace serene::outputs
