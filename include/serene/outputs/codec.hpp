#pragma once
#include "profile.hpp"
#include "sha256.hpp"
#include <expected>
#include <memory>
#include <span>
namespace serene::outputs {
/// Owned numerical comparison summary returned by Codec verification helpers.
/// Archive writers persist these statistics after checking the requested profile.
/// Errors are in stored-value units; exact equality includes signed-zero bit patterns.
struct Verification {
    double max_abs_error{}, ///< Maximum absolute reconstruction error, in stored units.
        rms_error{}; ///< Root-mean-square reconstruction error, in stored units.
    std::uint64_t verified_count{}; ///< Number of original/reconstructed pairs checked before representation acceptance.
    bool bitwise_equal{true}; ///< All paired binary64 representations are identical.
};
/// Encoded result used by ArchiveSession to copy one independently decodable frame.
/// Owns scalar accounting/digest fields and borrows Codec payload storage until the
/// next codec operation or destruction. Copy bytes before reusing that codec.
struct EncodedFrame {
    Encoding encoding{}; ///< Actual representation after verified size-based fallback.
    std::uint64_t count{}; ///< Number of source binary64 values.
    std::span<const std::byte> bytes; ///< Borrowed encoded payload; never a source-data alias.
    Digest digest{}; ///< SHA-256 of the exposed encoded bytes.
    double max_abs_error{}, rms_error{};
    bool bitwise_equal{true};
    std::uint64_t verified_count{};
};
/// Bounded serial pinned codec used by archive writers and installed reader bridges.
/// Owns payload storage and native ZFP descriptors; buffers are allocated by create.
/// A default/moved-from instance is unusable for encode/decode. One owner serializes
/// operations, supplies nonaliasing decode scratch, and copies borrowed frame bytes
/// before the next operation. Failed accuracy verification never silently changes policy.
class Codec {
  public:
    /// Construct an empty handle; call create to obtain usable bounded storage.
    Codec() noexcept;
    /// Release owned native descriptors and encoded storage.
    ~Codec();
    /// Transfer sole native/storage ownership; the source becomes empty.
    Codec(Codec &&) noexcept;
    /// Release prior state and transfer sole ownership from the rvalue source.
    Codec &operator=(Codec &&) noexcept;
    /// Copying is forbidden because native state and borrowed frame bytes have one owner.
    Codec(const Codec &) = delete;
    /// Copy assignment cannot share mutable codec storage.
    Codec &operator=(const Codec &) = delete;
    /// Allocate for at most max_values scalars per frame; return admission/allocation failure.
    static std::expected<Codec, Status> create(std::uint64_t max_values);
    /// Validate borrowed compressed bytes against expected_count and the independently
    /// admitted profile; no decoding or payload-derived policy authorization occurs.
    static Status inspect_header(std::span<const std::byte>, std::uint64_t expected_count,
                                 const CodecProfile &) noexcept;
    /// Compatibility overloads authorize only the reversible profile.
    static Status inspect_header(std::span<const std::byte> b, std::uint64_t n) noexcept {
        // Delegate borrowed bytes b and expected count n with the reversible preset.
        return inspect_header(b, n, CodecProfile::reversible());
    }
    /// Encode borrowed finite values using the supplied profile; decode_scratch must
    /// hold the complete frame and not alias input. Verify before returning borrowed
    /// encoded bytes, or an error; raw fallback is a size decision after verification.
    std::expected<EncodedFrame, Status> encode(std::span<const double>,
        std::span<double> decode_scratch, const CodecProfile &) noexcept;
    /// Encode source s exactly using decode scratch d; choose the profile overload for lossy policy.
    std::expected<EncodedFrame, Status> encode(std::span<const double> s,
        std::span<double> d) noexcept { /* Delegate with the reversible preset. */ return encode(s, d, CodecProfile::reversible()); }
    /// Decode encoding/count/bytes into the supplied destination under the expected profile.
    /// Raw decoding preserves exact bits; compressed headers must agree with that profile.
    /// Destination is unspecified on failure; callers needing atomic output use their own scratch.
    Status decode(Encoding, std::uint64_t expected_count, std::span<const std::byte>,
                  std::span<double>, const CodecProfile &) noexcept;
    /// Reversible-only compatibility decode of e/n/b into destination d.
    Status decode(Encoding e, std::uint64_t n, std::span<const std::byte> b,
                  std::span<double> d) noexcept { /* Bind legacy decoding to reversible metadata. */ return decode(e, n, b, d, CodecProfile::reversible()); }
    /// Exact-real verifier, independent of ZFP's admitted exponent range. Fixed stack workspace;
    /// accepts every finite positive binary64 tolerance, including the smallest subnormal.
    /// Compare equal-length borrowed original/decoded spans; return owned statistics or
    /// invalid input/verification failure without modifying either span.
    static std::expected<Verification, Status> verify_accuracy(std::span<const double>,
        std::span<const double>, double requested_tolerance) noexcept;
    /// Compare borrowed original/decoded spans under profile: exact bits for reversible,
    /// exact-real absolute bound for fixed accuracy. Return statistics or failure.
    static std::expected<Verification, Status> verify_values(std::span<const double>,
        std::span<const double>, const CodecProfile &) noexcept;
    /// Return allocated frame capacity, or zero for an empty/moved-from codec.
    std::uint64_t max_values() const noexcept;
    /// Conservative owned-payload accounting, including bounded opaque native descriptor reserve.
    /// At most encoded_capacity(max_values()) + 4096; fixed verification stack scratch is < 4096.
    std::uint64_t workspace_bytes() const noexcept;

  private:
    /// Private native descriptors and reusable payload buffer; defined in codec.cpp.
    struct Impl;
    std::unique_ptr<Impl> impl_; ///< Sole native/storage ownership; null denotes an empty handle.
    /// Verify original/decoded candidate spans under profile, then select compressed_bytes
    /// or exact raw storage by size. Return borrowed payload/statistics or verification error.
    std::expected<EncodedFrame, Status> accept_candidate(std::span<const double>,
        std::span<const double>, std::size_t compressed_bytes, const CodecProfile &) noexcept;
    // Test access injects decoded candidates through the same verify-before-size decision.
    friend struct CodecTestAccess;
};
} // namespace serene::outputs
