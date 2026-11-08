#pragma once
#include "profile.hpp"
#include "sha256.hpp"
#include <expected>
#include <memory>
#include <span>
namespace serene::outputs {
struct Verification {
    double max_abs_error{}, rms_error{};
    std::uint64_t verified_count{};
    bool bitwise_equal{true};
};
/// A frame borrows Codec storage until the next codec operation or destruction.
struct EncodedFrame {
    Encoding encoding{};
    std::uint64_t count{};
    std::span<const std::byte> bytes;
    Digest digest{};
    double max_abs_error{}, rms_error{};
    bool bitwise_equal{true};
    std::uint64_t verified_count{};
};
/// Bounded serial pinned codec. Buffers and native descriptors are allocated only by create.
class Codec {
  public:
    Codec() noexcept;
    ~Codec();
    Codec(Codec &&) noexcept;
    Codec &operator=(Codec &&) noexcept;
    Codec(const Codec &) = delete;
    Codec &operator=(const Codec &) = delete;
    static std::expected<Codec, Status> create(std::uint64_t max_values);
    static Status inspect_header(std::span<const std::byte>, std::uint64_t expected_count,
                                 const CodecProfile &) noexcept;
    /// Compatibility overloads authorize only the reversible profile.
    static Status inspect_header(std::span<const std::byte> b, std::uint64_t n) noexcept {
        return inspect_header(b, n, CodecProfile::reversible());
    }
    std::expected<EncodedFrame, Status> encode(std::span<const double>,
        std::span<double> decode_scratch, const CodecProfile &) noexcept;
    std::expected<EncodedFrame, Status> encode(std::span<const double> s,
        std::span<double> d) noexcept { return encode(s, d, CodecProfile::reversible()); }
    /// Destination is unspecified on failure; callers needing atomic output use their own scratch.
    Status decode(Encoding, std::uint64_t expected_count, std::span<const std::byte>,
                  std::span<double>, const CodecProfile &) noexcept;
    Status decode(Encoding e, std::uint64_t n, std::span<const std::byte> b,
                  std::span<double> d) noexcept { return decode(e, n, b, d, CodecProfile::reversible()); }
    /// Exact-real verifier, independent of ZFP's admitted exponent range. Fixed stack workspace;
    /// accepts every finite positive binary64 tolerance, including the smallest subnormal.
    static std::expected<Verification, Status> verify_accuracy(std::span<const double>,
        std::span<const double>, double requested_tolerance) noexcept;
    static std::expected<Verification, Status> verify_values(std::span<const double>,
        std::span<const double>, const CodecProfile &) noexcept;
    std::uint64_t max_values() const noexcept;
    /// Conservative owned-payload accounting, including bounded opaque native descriptor reserve.
    /// At most encoded_capacity(max_values()) + 4096; fixed verification stack scratch is < 4096.
    std::uint64_t workspace_bytes() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::expected<EncodedFrame, Status> accept_candidate(std::span<const double>,
        std::span<const double>, std::size_t compressed_bytes, const CodecProfile &) noexcept;
    // Test access injects decoded candidates through the same verify-before-size decision.
    friend struct CodecTestAccess;
};
} // namespace serene::outputs
