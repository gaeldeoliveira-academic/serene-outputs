#pragma once
#include "types.hpp"
#include <bit>
#include <expected>
#include <type_traits>
namespace serene::outputs {
enum class CodecMode : std::uint8_t { reversible = 0, fixed_accuracy = 1 };
/// Immutable validated pinned ZFP profile. Archive identity strings are validated by the caller.
/// No payload/header data is used to construct the authoritative expected profile.
class CodecProfile {
  public:
    constexpr CodecProfile() noexcept = default;
    static constexpr CodecProfile reversible() noexcept { return {}; }
    static std::expected<CodecProfile, Status> fixed_accuracy(double requested) noexcept {
        const auto bits = std::bit_cast<std::uint64_t>(requested);
        const auto exponent = unsigned((bits >> 52) & 2047);
        const auto fraction = bits & 0x000fffffffffffffull;
        if ((bits >> 63) || exponent == 2047 || (!exponent && !fraction))
            return std::unexpected(error(StatusCode::invalid_contract, 20));
        const int e = exponent ? int(exponent) - 1023 : int(std::bit_width(fraction)) - 1075;
        if (e < -1073 || e > 843)
            return std::unexpected(error(StatusCode::unsupported_profile, 20));
        CodecProfile p;
        p.mode_ = CodecMode::fixed_accuracy;
        p.requested_ = requested;
        p.effective_ = std::bit_cast<double>(e < -1022 ? std::uint64_t{1} << (e + 1074)
                                                      : std::uint64_t(e + 1023) << 52);
        p.exponent_ = e;
        p.code_ = std::uint64_t(2177 + e + 1074);
        return p;
    }
    static std::expected<CodecProfile, Status> from_wire(std::uint8_t mode,
        std::uint8_t tolerance_present, double requested, double effective,
        std::uint64_t mode_code) noexcept {
        if (mode > 1)
            return std::unexpected(error(StatusCode::unsupported_profile, 20));
        if (!mode) {
            if (tolerance_present || std::bit_cast<std::uint64_t>(requested) ||
                std::bit_cast<std::uint64_t>(effective) || mode_code != 2176)
                return std::unexpected(error(StatusCode::unsupported_profile, 20));
            return reversible();
        }
        if (tolerance_present != 1)
            return std::unexpected(error(StatusCode::invalid_contract, 20));
        auto p = fixed_accuracy(requested);
        if (!p)
            return p;
        if (std::bit_cast<std::uint64_t>(p->effective_) != std::bit_cast<std::uint64_t>(effective) ||
            p->code_ != mode_code)
            return std::unexpected(error(StatusCode::unsupported_profile, 20));
        return p;
    }
    constexpr CodecMode mode() const noexcept { return mode_; }
    constexpr bool tolerance_present() const noexcept { return mode_ == CodecMode::fixed_accuracy; }
    constexpr double requested_tolerance() const noexcept { return requested_; }
    constexpr double effective_tolerance() const noexcept { return effective_; }
    constexpr std::uint64_t mode_code() const noexcept { return code_; }
    constexpr int minimum_exponent() const noexcept { return exponent_; }
    constexpr bool operator==(const CodecProfile &) const noexcept = default;
  private:
    CodecMode mode_{CodecMode::reversible};
    double requested_{}, effective_{};
    std::uint64_t code_{2176};
    int exponent_{-1075};
};
static_assert(std::is_trivially_copyable_v<CodecProfile>);
} // namespace serene::outputs
