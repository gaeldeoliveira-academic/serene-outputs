#pragma once
#include "types.hpp"
#include <expected>
#include <span>
namespace serene::outputs {
/// Incremental fixed-state SHA-256 wrapper around pinned public-domain B-Con implementation.
class Sha256 {
  public:
    Sha256() noexcept;
    void update(std::span<const std::byte>) noexcept;
    Digest finish() noexcept;

  private:
    alignas(std::uint64_t) std::array<std::byte, 128> state_{};
};
Digest sha256(std::span<const std::byte>) noexcept;
/// Explicit canonical representation; native struct padding never reaches disk or a digest.
struct FrameRecord {
    std::uint64_t outer_index{}, inner_begin{}, count{}, byte_offset{}, byte_count{};
    Encoding encoding{Encoding::raw};
    std::uint32_t profile_index{};
    Digest digest{};
};
std::array<std::byte, 77> encode_record(const FrameRecord &) noexcept;
std::expected<FrameRecord, Status> decode_record(std::span<const std::byte>) noexcept;
} // namespace serene::outputs
