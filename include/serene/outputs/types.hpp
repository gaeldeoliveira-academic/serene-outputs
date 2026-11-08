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
/// Fixed error context. Indices are UINT64_MAX when no run/frame applies.
struct Status {
    StatusCode code{StatusCode::ok};
    std::uint32_t operation{};
    std::uint64_t run_index{UINT64_MAX};
    std::uint64_t frame_index{UINT64_MAX};
    std::int64_t native_code{};
    constexpr explicit operator bool() const noexcept {
        return code == StatusCode::ok;
    }
};
constexpr Status error(StatusCode c, std::uint32_t op = 0) noexcept {
    return {c, op};
}
constexpr std::string_view status_name(StatusCode c) noexcept {
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
enum class Encoding : std::uint8_t { raw = 0, zfp = 1 };
using Digest = std::array<std::byte, 32>;
inline constexpr std::uint64_t maximum_frame_values = 1048576;
inline constexpr std::uint64_t maximum_frame_bytes = 16777216;
inline constexpr std::uint64_t maximum_extent = INT64_MAX;
constexpr bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t &out) noexcept {
    if (a > maximum_extent || b > maximum_extent - a)
        return false;
    out = a + b;
    return true;
}
constexpr bool checked_multiply(std::uint64_t a, std::uint64_t b, std::uint64_t &out) noexcept {
    if (a && b > maximum_extent / a)
        return false;
    out = a * b;
    return true;
}
/// Pinned reversible 1D binary64 conservative bound, including maximum header and guard word.
constexpr std::uint64_t encoded_capacity(std::uint64_t n) noexcept {
    return ((148 + ((n + 3) / 4) * 278 + 63) / 64) * 8 + 64;
}
} // namespace serene::outputs
