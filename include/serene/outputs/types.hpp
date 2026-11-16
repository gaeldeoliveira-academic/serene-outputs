#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace serene::outputs {
/// Independent bounded error domains; no dynamically allocated diagnostic text.
enum class StatusCode : std::uint8_t {
    ok,
    invalid_contract,
    unsupported_feature,
    unsupported_profile,
    resource_limit,
    codec_failure,
    verification_failure,
    io_failure,
    publication_failure,
    invalid_state
};
/// Allocation-free error value returned by model, codec and publication operations.
/// Stores a domain, operation discriminator and optional native/run/frame context;
/// callers such as ArchiveSession and core adapters propagate it without owning text.
/// A default value is success. Indices are UINT64_MAX when no run/frame applies.
struct Status {
    StatusCode code{StatusCode::ok}; ///< Success or the failing error domain.
    std::uint32_t operation{}; ///< Operation-specific discriminator; zero means unspecified.
    std::uint64_t run_index{UINT64_MAX}; ///< Catalog run index, or UINT64_MAX when inapplicable.
    std::uint64_t frame_index{UINT64_MAX}; ///< Frame index, or UINT64_MAX when inapplicable.
    std::int64_t native_code{}; ///< Optional underlying I/O/native error; zero when absent.
    /// Return true exactly for success; no payload is modified.
    constexpr explicit operator bool() const noexcept {
        // Test the domain; auxiliary context does not change success semantics.
        return code == StatusCode::ok;
    }
};
/// Build a status with domain c and operation op; leave indices/native context unspecified.
constexpr Status error(StatusCode c, std::uint32_t op = 0) noexcept {
    // Initialize the supplied context and retain default sentinels for the rest.
    return {c, op};
}
/// Return static diagnostic text for c, or "unknown" for an out-of-domain value.
constexpr std::string_view status_name(StatusCode c) noexcept {
    // Index the fixed domain vocabulary only after checking its range.
    // Static spellings are borrowed by callers; no string allocation occurs.
    constexpr std::string_view names[] = {"ok",
                                          "invalid_contract",
                                          "unsupported_feature",
                                          "unsupported_profile",
                                          "resource_limit",
                                          "codec_failure",
                                          "verification_failure",
                                          "io_failure",
                                          "publication_failure",
                                          "invalid_state"};
    return static_cast<unsigned>(c) < 10 ? names[static_cast<unsigned>(c)] : "unknown";
}
/// Wire payload discriminator; raw is canonical little-endian binary64, zfp is pinned framing.
enum class Encoding : std::uint8_t { raw = 0, zfp = 1 };
/// Exact 32-byte SHA-256 value, including all-zero sentinel where a contract permits it.
using Digest = std::array<std::byte, 32>;
/// Maximum admitted scalar count of one independently decoded frame.
inline constexpr std::uint64_t maximum_frame_values = 1048576;
/// Maximum encoded byte extent of one frame.
inline constexpr std::uint64_t maximum_frame_bytes = 16777216;
/// Checked arithmetic ceiling shared with signed file-offset operations.
inline constexpr std::uint64_t maximum_extent = INT64_MAX;
/// Add a and b within maximum_extent; set out only on success and return that success.
constexpr bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t &out) noexcept {
    // Reject an oversized operand or sum before performing the addition.
    if (a > maximum_extent || b > maximum_extent - a)
        return false;
    out = a + b;
    return true;
}
/// Multiply a and b within maximum_extent; set out only on success and return that success.
constexpr bool checked_multiply(std::uint64_t a, std::uint64_t b, std::uint64_t &out) noexcept {
    // Compare by division to avoid overflow, then assign the admitted product.
    if (a && b > maximum_extent / a)
        return false;
    out = a * b;
    return true;
}
/// Return the conservative pinned reversible byte capacity for n binary64 values.
/// The caller admits n against maximum_frame_values first. Includes header and guard word.
constexpr std::uint64_t encoded_capacity(std::uint64_t n) noexcept {
    // Round the four-value block bound to 64-bit words and reserve guard padding.
    return ((148 + ((n + 3) / 4) * 278 + 63) / 64) * 8 + 64;
}
} // namespace serene::outputs
