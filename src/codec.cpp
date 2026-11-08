#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <serene/outputs/codec.hpp>
#include <vector>
#include <zfp.h>
namespace serene::outputs {
namespace {
// Every finite binary64 is an integer in units 2^-1074. Opposite-sign differences
// need at most 2099 bits; 33 limbs reserve 2112 bits without floating subtraction.
using Magnitude = std::array<std::uint64_t, 33>;
Magnitude magnitude(double x) noexcept {
    Magnitude a{};
    const auto bits = std::bit_cast<std::uint64_t>(x);
    const auto exponent = unsigned((bits >> 52) & 2047);
    const auto significand = (bits & 0x000fffffffffffffull) |
                             (exponent ? std::uint64_t{1} << 52 : 0);
    const auto shift = exponent ? exponent - 1 : 0;
    a[shift / 64] = significand << (shift % 64);
    if (shift % 64)
        a[shift / 64 + 1] = significand >> (64 - shift % 64);
    return a;
}
int compare(const Magnitude &a, const Magnitude &b) noexcept {
    for (std::size_t j = a.size(); j-- > 0;)
        if (a[j] != b[j])
            return a[j] > b[j] ? 1 : -1;
    return 0;
}
Magnitude difference(double x, double y) noexcept {
    auto a = magnitude(x), b = magnitude(y);
    Magnitude result{};
    if ((std::bit_cast<std::uint64_t>(x) ^ std::bit_cast<std::uint64_t>(y)) >> 63) {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < a.size(); ++j) {
            const auto first = a[j] + b[j];
            const auto second = first + carry;
            carry = (first < a[j]) || (second < first);
            result[j] = second;
        }
    } else {
        if (compare(a, b) < 0)
            std::swap(a, b);
        std::uint64_t borrow = 0;
        for (std::size_t j = 0; j < a.size(); ++j) {
            const auto first = a[j] - b[j];
            const auto second = first - borrow;
            borrow = (a[j] < b[j]) || (first < borrow);
            result[j] = second;
        }
    }
    return result;
}
bool any_below(const Magnitude &a, unsigned bit) noexcept {
    for (unsigned j = 0; j < bit / 64; ++j)
        if (a[j])
            return true;
    return bit % 64 && (a[bit / 64] & ((std::uint64_t{1} << (bit % 64)) - 1));
}
// Integer rounding constructs IEEE bits directly; no rounding-mode or long-double assumption.
double rounded(const Magnitude &a, bool upward) noexcept {
    unsigned words = unsigned(a.size());
    while (words && !a[words - 1])
        --words;
    if (!words)
        return 0.;
    unsigned high = (words - 1) * 64 + unsigned(std::bit_width(a[words - 1])) - 1;
    if (high <= 52)
        return std::bit_cast<double>(a[0]);
    const unsigned shift = high - 52;
    std::uint64_t significand = a[shift / 64] >> (shift % 64);
    if (shift % 64 && shift / 64 + 1 < a.size())
        significand |= a[shift / 64 + 1] << (64 - shift % 64);
    bool increment = any_below(a, shift);
    if (!upward) {
        const unsigned half = shift - 1;
        increment = ((a[half / 64] >> (half % 64)) & 1) &&
                    (any_below(a, half) || (significand & 1));
    }
    if (increment && ++significand == (std::uint64_t{1} << 53)) {
        significand >>= 1;
        ++high;
    }
    if (high > 2097)
        return std::numeric_limits<double>::infinity();
    return std::bit_cast<double>((std::uint64_t(high - 51) << 52) |
                                (significand & 0x000fffffffffffffull));
}
bool parameters_match(zfp_stream *stream, const CodecProfile &profile) noexcept {
    uint minimum, maximum, precision;
    int exponent;
    zfp_stream_params(stream, &minimum, &maximum, &precision, &exponent);
    return minimum == 1 && maximum == 16658 && precision == 64 &&
           exponent == profile.minimum_exponent() &&
           zfp_stream_mode(stream) == profile.mode_code() &&
           zfp_stream_execution(stream) == zfp_exec_serial;
}
Status set_profile(zfp_stream *stream, const CodecProfile &profile) noexcept {
    if (!zfp_stream_set_execution(stream, zfp_exec_serial))
        return error(StatusCode::unsupported_profile, 21);
    if (profile.mode() == CodecMode::reversible)
        zfp_stream_set_reversible(stream);
    else if (std::bit_cast<std::uint64_t>(zfp_stream_set_accuracy(stream, profile.effective_tolerance())) !=
             std::bit_cast<std::uint64_t>(profile.effective_tolerance()))
        return error(StatusCode::unsupported_profile, 21);
    return parameters_match(stream, profile) ? Status{} : error(StatusCode::unsupported_profile, 21);
}
} // namespace
struct Codec::Impl {
    std::uint64_t maximum{};
    std::vector<std::byte> buffer;
    bitstream *bits{};
    zfp_stream *stream{};
    zfp_field *field{};
    ~Impl() {
        if (field) zfp_field_free(field);
        if (stream) zfp_stream_close(stream);
        if (bits) stream_close(bits);
    }
};
Codec::Codec() noexcept = default;
Codec::~Codec() = default;
Codec::Codec(Codec &&) noexcept = default;
Codec &Codec::operator=(Codec &&) noexcept = default;
std::expected<Codec, Status> Codec::create(std::uint64_t max_values) {
    try {
        if (!max_values || max_values > maximum_frame_values)
            return std::unexpected(error(StatusCode::resource_limit, 21));
        if (std::endian::native != std::endian::little || sizeof(double) != 8 ||
            !std::numeric_limits<double>::is_iec559 ||
            zfp_codec_version != 5 || zfp_library_version != ZFP_MAKE_VERSION(1, 0, 1, 0) ||
            stream_word_bits != 64)
            return std::unexpected(error(StatusCode::unsupported_profile, 21));
        Codec c;
        c.impl_ = std::make_unique<Impl>();
        auto &i = *c.impl_;
        i.maximum = max_values;
        i.field = zfp_field_1d(nullptr, zfp_type_double, max_values);
        i.stream = zfp_stream_open(nullptr);
        if (!i.field || !i.stream)
            return std::unexpected(error(StatusCode::resource_limit, 21));
        if (auto st = set_profile(i.stream, CodecProfile::reversible()); !st)
            return std::unexpected(st);
        auto need = zfp_stream_maximum_size(i.stream, i.field);
        auto capacity = encoded_capacity(max_values);
        if (!need || need > capacity - 64 || capacity > maximum_frame_bytes)
            return std::unexpected(error(StatusCode::unsupported_profile, 21));
        i.buffer.resize(capacity);
        i.bits = stream_open(i.buffer.data(), i.buffer.size());
        if (!i.bits)
            return std::unexpected(error(StatusCode::resource_limit, 21));
        zfp_stream_set_bit_stream(i.stream, i.bits);
        return c;
    } catch (const std::bad_alloc &) {
        return std::unexpected(error(StatusCode::resource_limit, 21));
    } catch (...) {
        return std::unexpected(error(StatusCode::invalid_contract, 21));
    }
}
std::uint64_t Codec::max_values() const noexcept { return impl_ ? impl_->maximum : 0; }
std::uint64_t Codec::workspace_bytes() const noexcept {
    return impl_ ? impl_->buffer.capacity() + sizeof(Impl) + 2048 : 0;
}
Status Codec::inspect_header(std::span<const std::byte> b, std::uint64_t count,
                             const CodecProfile &profile) noexcept {
    if (!count || count > maximum_frame_values || b.size() < 16 || b.size() % 8 ||
        b.size() > encoded_capacity(count) - 64)
        return error(StatusCode::codec_failure, 22);
    if (b[0] != std::byte{'z'} || b[1] != std::byte{'f'} || b[2] != std::byte{'p'} ||
        b[3] != std::byte{5})
        return error(StatusCode::unsupported_profile, 22);
    auto bits = [&](unsigned start, unsigned n) {
        std::uint64_t v = 0;
        for (unsigned j = 0; j < n; j++) {
            unsigned k = start + j;
            v |= std::uint64_t((std::to_integer<unsigned>(b[k / 8]) >> (k % 8)) & 1) << j;
        }
        return v;
    };
    auto metadata = bits(32, 52);
    if ((metadata & 15) != 3 || (metadata >> 4) + 1 != count)
        return error(StatusCode::codec_failure, 22);
    if (bits(84, 12) != profile.mode_code())
        return error(StatusCode::unsupported_profile, 22);
    return {};
}
Status Codec::decode(Encoding encoding, std::uint64_t count, std::span<const std::byte> bytes,
                     std::span<double> dst, const CodecProfile &profile) noexcept {
    if (!impl_)
        return error(StatusCode::invalid_state, 23);
    auto &i = *impl_;
    if (!count || count > i.maximum || dst.size() < count)
        return error(StatusCode::resource_limit, 23);
    if (encoding == Encoding::raw) {
        if (bytes.size() != count * 8)
            return error(StatusCode::invalid_contract, 23);
        std::memmove(dst.data(), bytes.data(), bytes.size());
        for (std::size_t j = 0; j < count; j++)
            if (!std::isfinite(dst[j]))
                return error(StatusCode::invalid_contract, 23);
        return {};
    }
    if (encoding != Encoding::zfp)
        return error(StatusCode::unsupported_feature, 23);
    auto status = inspect_header(bytes, count, profile);
    if (!status)
        return status;
    // Bound/canonical inspection precedes native header parsing. A guard region protects
    // the pinned decoder's final word reads; the actual consumed extent is checked below.
    if (bytes.data() != i.buffer.data())
        std::memmove(i.buffer.data(), bytes.data(), bytes.size());
    std::fill(i.buffer.begin() + bytes.size(), i.buffer.end(), std::byte{});
    stream_rewind(i.bits);
    zfp_field_set_size_1d(i.field, count);
    auto header = zfp_read_header(i.stream, i.field, ZFP_HEADER_FULL);
    if (header != 96 || zfp_field_type(i.field) != zfp_type_double ||
        zfp_field_dimensionality(i.field) != 1 || zfp_field_size(i.field, nullptr) != count)
        return error(StatusCode::codec_failure, 23);
    if (!parameters_match(i.stream, profile) ||
        zfp_stream_maximum_size(i.stream, i.field) > i.buffer.size() - 64)
        return error(StatusCode::unsupported_profile, 23);
    zfp_field_set_pointer(i.field, dst.data());
    auto consumed = zfp_decompress(i.stream, i.field);
    if (consumed != bytes.size())
        return error(StatusCode::codec_failure, 23);
    for (std::size_t j = 0; j < count; j++)
        if (!std::isfinite(dst[j]))
            return error(StatusCode::invalid_contract, 23);
    return {};
}
std::expected<Verification, Status> Codec::verify_accuracy(std::span<const double> src,
    std::span<const double> decoded, double requested) noexcept {
    if (src.empty() || src.size() != decoded.size() || src.size() > maximum_frame_values ||
        !std::isfinite(requested) || requested <= 0)
        return std::unexpected(error(StatusCode::invalid_contract, 25));
    const auto tolerance = magnitude(requested);
    Magnitude maximum{};
    double scale = 0, sum_squares = 1;
    bool equal = true;
    for (std::size_t j = 0; j < src.size(); ++j) {
        if (!std::isfinite(src[j]) || !std::isfinite(decoded[j]))
            return std::unexpected(error(StatusCode::invalid_contract, 25));
        equal &= std::bit_cast<std::uint64_t>(src[j]) == std::bit_cast<std::uint64_t>(decoded[j]);
        const auto d = difference(src[j], decoded[j]);
        if (compare(d, tolerance) > 0)
            return std::unexpected(error(StatusCode::verification_failure, 25));
        if (compare(d, maximum) > 0)
            maximum = d;
        const double x = rounded(d, false);
        if (x != 0) {
            if (scale < x) {
                const double ratio = scale / x;
                sum_squares = 1 + sum_squares * ratio * ratio;
                scale = x;
            } else {
                const double ratio = x / scale;
                sum_squares += ratio * ratio;
            }
        }
    }
    const double max_error = rounded(maximum, true);
    // The RMS multiplier is <= 1 mathematically. Clamp only reported rounding drift;
    // acceptance above is solely the exact integer comparison with requested tau.
    const double rms = scale * std::sqrt(std::min(1., sum_squares / double(src.size())));
    return Verification{max_error, std::min(rms, max_error), src.size(), equal};
}
std::expected<Verification, Status> Codec::verify_values(std::span<const double> src,
    std::span<const double> decoded, const CodecProfile &profile) noexcept {
    if (profile.mode() == CodecMode::fixed_accuracy)
        return verify_accuracy(src, decoded, profile.requested_tolerance());
    if (src.empty() || src.size() != decoded.size() || src.size() > maximum_frame_values)
        return std::unexpected(error(StatusCode::invalid_contract, 25));
    for (std::size_t j = 0; j < src.size(); ++j)
        if (!std::isfinite(src[j]) || !std::isfinite(decoded[j]))
            return std::unexpected(error(StatusCode::invalid_contract, 25));
    if (std::memcmp(src.data(), decoded.data(), src.size_bytes()))
        return std::unexpected(error(StatusCode::verification_failure, 25));
    return Verification{0, 0, src.size(), true};
}
std::expected<EncodedFrame, Status> Codec::accept_candidate(std::span<const double> src,
    std::span<const double> decoded, std::size_t used, const CodecProfile &profile) noexcept {
    auto verification = verify_values(src, decoded, profile);
    if (!verification)
        return std::unexpected(verification.error());
    auto &i = *impl_;
    Encoding actual = Encoding::zfp;
    if (used >= src.size_bytes()) {
        actual = Encoding::raw;
        used = src.size_bytes();
        std::memcpy(i.buffer.data(), src.data(), used);
        *verification = Verification{0, 0, src.size(), true};
    }
    auto bytes = std::span<const std::byte>(i.buffer.data(), used);
    return EncodedFrame{actual, src.size(), bytes, sha256(bytes), verification->max_abs_error,
        verification->rms_error, verification->bitwise_equal, verification->verified_count};
}
std::expected<EncodedFrame, Status> Codec::encode(std::span<const double> src,
    std::span<double> scratch, const CodecProfile &profile) noexcept {
    if (!impl_)
        return std::unexpected(error(StatusCode::invalid_state, 24));
    auto &i = *impl_;
    auto n = src.size();
    if (!n || n > i.maximum || scratch.size() < n)
        return std::unexpected(error(StatusCode::resource_limit, 24));
    auto a = reinterpret_cast<std::uintptr_t>(src.data()),
         b = reinterpret_cast<std::uintptr_t>(scratch.data());
    if (a < b + n * 8 && b < a + n * 8)
        return std::unexpected(error(StatusCode::invalid_contract, 24));
    for (auto x : src)
        if (!std::isfinite(x))
            return std::unexpected(error(StatusCode::invalid_contract, 24));
    std::fill(i.buffer.begin(), i.buffer.end(), std::byte{});
    stream_rewind(i.bits);
    if (auto st = set_profile(i.stream, profile); !st)
        return std::unexpected(st);
    zfp_field_set_type(i.field, zfp_type_double);
    zfp_field_set_size_1d(i.field, n);
    zfp_field_set_pointer(i.field, const_cast<double *>(src.data()));
    if (zfp_stream_maximum_size(i.stream, i.field) > i.buffer.size() - 64 ||
        zfp_write_header(i.stream, i.field, ZFP_HEADER_FULL) != 96)
        return std::unexpected(error(StatusCode::codec_failure, 24));
    auto used = zfp_compress(i.stream, i.field);
    if (!used || used > i.buffer.size() - 64)
        return std::unexpected(error(StatusCode::codec_failure, 24));
    auto compressed = std::span<const std::byte>(i.buffer.data(), used);
    if (auto st = decode(Encoding::zfp, n, compressed, scratch, profile); !st)
        return std::unexpected(st);
    return accept_candidate(src, scratch.first(n), used, profile);
}
} // namespace serene::outputs
