#pragma once
#include "types.hpp"
#include <bit>
#include <expected>
#include <type_traits>
namespace serene::outputs {
/// Qualified wire policy: exact reversible or explicitly requested absolute accuracy.
enum class CodecMode : std::uint8_t { reversible = 0, fixed_accuracy = 1 };
/// Immutable validated pinned ZFP profile. Archive identity strings are validated by the caller.
/// No payload/header data is used to construct the authoritative expected profile.
/// Owns only scalar policy data and is trivially copyable. PreparedExport catalogs,
/// the Python bridge and Codec callers use it to bind decoding to admitted metadata.
/// Default construction is reversible; factories reject invalid/unqualified policies
/// rather than clamping tolerance or substituting another compression mode.
class CodecProfile {
  public:
    /// Construct the canonical reversible policy without allocation.
    constexpr CodecProfile() noexcept = default;
    /// Return the canonical reversible policy; choose this when no accuracy loss is requested.
    static constexpr CodecProfile reversible() noexcept {
        // Use the canonical default member values.
        return {};
    }
    /// Admit finite positive requested absolute error in stored-value units.
    /// Return the immutable pinned profile, invalid_contract for nonpositive/nonfinite
    /// input, or unsupported_profile outside effective exponents [-1073,843].
    static std::expected<CodecProfile, Status> fixed_accuracy(double requested) noexcept {
        // Decode the binary64 magnitude, choose its downward power-of-two bound,
        // enforce the pinned exponent domain, then build its canonical short mode.
        const auto bits /* Exact binary64 representation, avoiding rounding-mode dependence. */ = std::bit_cast<std::uint64_t>(requested);
        const auto exponent /* Biased binary64 exponent field. */ = unsigned((bits >> 52) & 2047);
        const auto fraction /* Fraction bits, also the significand for subnormals. */ = bits & 0x000fffffffffffffull;
        if ((bits >> 63) || exponent == 2047 || (!exponent && !fraction))
            return std::unexpected(error(StatusCode::invalid_contract, 20));
        const int e /* floor(log2(requested)), derived exactly from the bit fields. */ = exponent ? int(exponent) - 1023 : int(std::bit_width(fraction)) - 1075;
        if (e < -1073 || e > 843)
            return std::unexpected(error(StatusCode::unsupported_profile, 20));
        // Populate only after the requested tolerance has passed admission.
        CodecProfile p; // Owned result, initially the canonical reversible profile.
        p.mode_ = CodecMode::fixed_accuracy;
        p.requested_ = requested;
        p.effective_ = std::bit_cast<double>(e < -1022 ? std::uint64_t{1} << (e + 1074)
                                                      : std::uint64_t(e + 1023) << 52);
        p.exponent_ = e;
        p.code_ = std::uint64_t(2177 + e + 1074);
        return p;
    }
    /// Reconstruct from authoritative wire fields, never from a compressed header.
    /// mode/tolerance_present are closed discriminators; requested/effective are
    /// stored-unit absolute tolerances and mode_code is the pinned ZFP short mode.
    /// Return the matching canonical profile or an admission error on disagreement.
    static std::expected<CodecProfile, Status> from_wire(std::uint8_t mode,
        std::uint8_t tolerance_present, double requested, double effective,
        std::uint64_t mode_code) noexcept {
        // Dispatch the mode, check canonical absent values, then independently
        // derive fixed-accuracy metadata and compare its exact wire representation.
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
        auto p = fixed_accuracy(requested); // Independently admitted expected policy.
        if (!p)
            return p;
        if (std::bit_cast<std::uint64_t>(p->effective_) != std::bit_cast<std::uint64_t>(effective) ||
            p->code_ != mode_code)
            return std::unexpected(error(StatusCode::unsupported_profile, 20));
        return p;
    }
    /// Return the admitted compression mode.
    constexpr CodecMode mode() const noexcept { /* Read the stored policy tag. */ return mode_; }
    /// Return whether fixed-accuracy tolerance fields are meaningful.
    constexpr bool tolerance_present() const noexcept { /* Derive presence from the policy tag. */ return mode_ == CodecMode::fixed_accuracy; }
    /// Return the authored absolute tolerance; canonical +0 for reversible mode.
    constexpr double requested_tolerance() const noexcept { /* Read the authored bound. */ return requested_; }
    /// Return the admitted power-of-two tolerance; canonical +0 for reversible mode.
    constexpr double effective_tolerance() const noexcept { /* Read the codec bound. */ return effective_; }
    /// Return the canonical short ZFP mode code expected in each compressed frame.
    constexpr std::uint64_t mode_code() const noexcept { /* Read the admitted wire code. */ return code_; }
    /// Return the ZFP minimum exponent; -1075 is the reversible preset sentinel.
    constexpr int minimum_exponent() const noexcept { /* Read the native codec exponent. */ return exponent_; }
    /// Compare every stored policy field; valid profiles have canonical finite values.
    constexpr bool operator==(const CodecProfile &) const noexcept = default;
  private:
    CodecMode mode_{CodecMode::reversible}; ///< Admitted policy discriminator.
    double requested_{}, ///< Authored absolute bound in stored units; +0 when absent.
        effective_{}; ///< Downward power-of-two codec bound in stored units; +0 when absent.
    std::uint64_t code_{2176}; ///< Canonical short ZFP mode; 2176 denotes reversible.
    int exponent_{-1075}; ///< Native minimum exponent, with reversible sentinel -1075.
};
static_assert(std::is_trivially_copyable_v<CodecProfile>);
} // namespace serene::outputs
