#pragma once
#include <cmath>
#include "../third_party/nlohmann_json/include/nlohmann/json.hpp"
namespace serene::outputs {
struct CodecTestAccess {
    static std::expected<EncodedFrame, Status> accept(Codec &c, std::span<const double> source,
        std::span<const double> decoded, std::size_t candidate_bytes, const CodecProfile &profile) {
        return c.accept_candidate(source, decoded, candidate_bytes, profile);
    }
};
}
static nlohmann::json read_json(const std::filesystem::path &path) {
    std::ifstream stream(path);
    check(bool(stream), "JSON fixture open");
    return nlohmann::json::parse(stream);
}
static double hex_double(const nlohmann::json &value) {
    const auto text = value.get<std::string>();
    char *end = nullptr;
    const auto x = std::strtod(text.c_str(), &end);
    check(end == text.c_str() + text.size(), "hex binary64 parse");
    return x;
}
static double bit_double(const nlohmann::json &value) {
    return std::bit_cast<double>(std::stoull(value.get<std::string>(), nullptr, 16));
}
static void test_fixed_accuracy(Codec &codec, const char *extra_oracles) {
    const auto root = std::filesystem::path(SERENE_OUTPUTS_SOURCE_DIR) / "fixtures_extensions_draft";
    const auto exact = CodecProfile::reversible();
    auto fixed = CodecProfile::fixed_accuracy(0x1p-12);
    check(bool(fixed), "fixed profile");
    check(fixed->minimum_exponent() == -12 && fixed->mode_code() == 3239 &&
          fixed->effective_tolerance() == 0x1p-12 && fixed->tolerance_present(), "profile fields");
    check(bool(CodecProfile::from_wire(1, 1, 0x1p-12, 0x1p-12, 3239)), "wire profile");
    check(!CodecProfile::from_wire(0, 0, -0., 0., 2176), "negative zero reversible tolerance");
    check(!CodecProfile::from_wire(0, 0, 0., -0., 2176), "negative zero effective tolerance");
    check(!CodecProfile::from_wire(1, 0, 0x1p-12, 0x1p-12, 3239), "fixed presence");
    check(!CodecProfile::from_wire(1, 1, 0x1p-12, 0x1p-11, 3239), "effective mismatch");
    check(!CodecProfile::from_wire(1, 1, 0x1p-12, 0x1p-12, 3240), "code mismatch");
    check(!CodecProfile::from_wire(2, 1, 0x1p-12, 0x1p-12, 3239), "unknown mode");
    for (auto x : {0., -0., -1., std::numeric_limits<double>::infinity(),
                   std::numeric_limits<double>::quiet_NaN(), 0x1p-1074, 0x1p844,
                   std::numeric_limits<double>::max()})
        check(!CodecProfile::fixed_accuracy(x), "invalid or unsupported tau");
    std::array<double, 8> zeros{}, scratch{}, decoded{};
    for (int exponent = -1073; exponent <= 843; ++exponent) {
        const double tau = std::ldexp(1., exponent);
        const auto profile = CodecProfile::fixed_accuracy(tau);
        check(profile && profile->minimum_exponent() == exponent &&
              profile->effective_tolerance() == tau, "every admitted exponent");
        const auto upper = CodecProfile::fixed_accuracy(std::nextafter(std::ldexp(1., exponent + 1), 0.));
        check(upper && upper->minimum_exponent() == exponent &&
              upper->effective_tolerance() == tau, "bit-derived floor at power boundary");
        const auto frame = codec.encode(zeros, scratch, *profile);
        check(frame && frame->encoding == Encoding::zfp && frame->verified_count == zeros.size(),
              "all exponent native setter/header agreement");
        check(bool(Codec::inspect_header(frame->bytes, zeros.size(), *profile)), "expected mode header");
        check(!Codec::inspect_header(frame->bytes, zeros.size(), exact), "fixed header exact mismatch");
        auto mismatch = CodecProfile::fixed_accuracy(std::ldexp(1., exponent == 843 ? 842 : exponent + 1));
        check(!codec.decode(Encoding::zfp, zeros.size(), frame->bytes, decoded, *mismatch),
              "valid canonical other profile rejected");
    }
    auto frame = codec.encode(zeros, scratch, *fixed);
    check(bool(frame), "header frame");
    std::vector<std::byte> bytes(frame->bytes.begin(), frame->bytes.end());
    bytes[10] |= std::byte{0xf0}; bytes[11] = std::byte{0xff};
    check(!Codec::inspect_header(bytes, zeros.size(), *fixed), "long expert header rejected");
    check(!codec.decode(Encoding::zfp, zeros.size(), bytes, decoded, *fixed), "long header decoder rejected");
    std::uint64_t oracle_count = 0;
    for (const auto &entry : read_json(root / "exact_real_bound_oracle.json")) {
        const std::array source{hex_double(entry["source_hex"])};
        const std::array restored{hex_double(entry["decoded_hex"])};
        const auto result = Codec::verify_accuracy(source, restored, hex_double(entry["tolerance_hex"]));
        check(bool(result) == entry["accept"].get<bool>(), "independent exact-real oracle");
        if (result)
            check(result->bitwise_equal == entry["bitwise_equal"].get<bool>(), "oracle bit equality");
        else
            check(result.error().code == StatusCode::verification_failure, "oracle rejection phase");
        ++oracle_count;
    }
    if (extra_oracles) {
        for (const auto &entry : read_json(extra_oracles)) {
            const std::array source{bit_double(entry["source_bits"])};
            const std::array restored{bit_double(entry["decoded_bits"])};
            const auto result = Codec::verify_accuracy(source, restored, bit_double(entry["tau_bits"]));
            check(bool(result) == entry["accept"].get<bool>(), "fraction-derived adversarial oracle");
            if (result) {
                check(std::bit_cast<std::uint64_t>(result->max_abs_error) ==
                    std::bit_cast<std::uint64_t>(bit_double(entry["max_bits"])), "upward maximum oracle");
                check(std::bit_cast<std::uint64_t>(result->rms_error) ==
                    std::bit_cast<std::uint64_t>(bit_double(entry["rms_bits"])), "nearest single RMS oracle");
            }
            ++oracle_count;
        }
    }
    const std::array one{1.}, negative_half_ulp{-0x1p-53};
    const double next_one = std::nextafter(1., 2.);
    const auto up = Codec::verify_accuracy(one, negative_half_ulp, next_one);
    check(up && up->max_abs_error == next_one && up->rms_error == 1., "upward versus nearest reporting");
    const std::array negative_zero{-0.}, positive_zero{0.};
    const auto sign = Codec::verify_values(negative_zero, positive_zero, *fixed);
    check(sign && !sign->bitwise_equal && sign->max_abs_error == 0 && sign->rms_error == 0,
          "signed-zero real error distinct from bitwise equality");
    check(!Codec::verify_values(negative_zero, positive_zero, exact), "exact signed-zero mismatch");
    const std::array huge{std::numeric_limits<double>::max()};
    const auto extreme = Codec::verify_accuracy(huge, positive_zero, huge[0]);
    check(extreme && extreme->max_abs_error == huge[0] && extreme->rms_error == huge[0],
          "finite RMS maximum without squared overflow");
    const std::array tiny{0x1p-1074};
    const auto subnormal = Codec::verify_accuracy(tiny, positive_zero, 0x1p-1074);
    check(subnormal && subnormal->rms_error == 0x1p-1074, "subnormal error without square underflow");
    const std::array nonfinite{std::numeric_limits<double>::quiet_NaN()};
    check(!Codec::verify_accuracy(nonfinite, positive_zero, 1.) &&
          !Codec::verify_accuracy(positive_zero, nonfinite, 1.), "nonfinite verifier input");
    const auto injected = CodecTestAccess::accept(codec, one, positive_zero, 16, *fixed);
    check(!injected && injected.error().code == StatusCode::verification_failure,
          "injected candidate failure before raw fallback");
    const auto valid_raw = CodecTestAccess::accept(codec, negative_zero, positive_zero, 16, *fixed);
    std::uint64_t stored_bits = 0;
    if (valid_raw) std::memcpy(&stored_bits, valid_raw->bytes.data(), 8);
    check(valid_raw && valid_raw->encoding == Encoding::raw && valid_raw->max_abs_error == 0. &&
          valid_raw->rms_error == 0. && valid_raw->bitwise_equal && valid_raw->verified_count == 1 &&
          stored_bits == std::bit_cast<std::uint64_t>(-0.), "verified raw fallback resets statistics and source bits");
    const std::array almost_one{1. + 0x1p-14};
    const auto lossy_raw = CodecTestAccess::accept(codec, almost_one, one, 16, *fixed);
    check(lossy_raw && lossy_raw->max_abs_error == 0 && lossy_raw->rms_error == 0 &&
          lossy_raw->bitwise_equal && lossy_raw->verified_count == 1, "raw nonzero error reset");
    std::uint64_t fixture_frames = 0, fixture_values = 0, nonexact_frames = 0;
    const auto manifest = read_json(root / "manifest.json");
    for (const auto &item : manifest["positive"]) {
        const auto description = read_json(root / item["expected"].get<std::string>());
        for (const auto &run : description["runs"]) {
            if (!run.contains("fixed_accuracy")) continue;
            const auto profile = CodecProfile::fixed_accuracy(hex_double(run["fixed_accuracy"]["requested_tolerance_hex"]));
            check(bool(profile), "fixture profile factory");
            const auto prefix = root / run["sidecar"].get<std::string>();
            const auto ix = read(prefix.string() + ".index.bin");
            const auto payload = read(prefix.string() + ".payload.bin");
            const auto source_bytes = read(prefix.string() + ".expected.bin");
            const auto inner = run["shape"][1].get<std::size_t>();
            for (std::size_t off = 0; off < ix.size(); off += 77) {
                const auto record = decode_record(std::span(ix).subspan(off, 77));
                check(record && record->count <= 8, "fixed fixture index");
                const auto data = std::span(payload).subspan(record->byte_offset, record->byte_count);
                check(sha256(data) == record->digest, "fixed fixture digest");
                check(bool(codec.decode(record->encoding, record->count, data, decoded, *profile)),
                      "independent compressed fixed fixture decode");
                bool same = true;
                for (std::size_t j = 0; j < record->count; ++j) {
                    const auto index = record->outer_index * inner + record->inner_begin + j;
                    const auto expected = bit_double(run["fixed_accuracy"]["decoded_bits_hex"][index]);
                    check(std::bit_cast<std::uint64_t>(decoded[j]) == std::bit_cast<std::uint64_t>(expected),
                          "independent fixed decoded bits");
                    double original;
                    std::memcpy(&original, source_bytes.data() + index * 8, 8);
                    same &= std::bit_cast<std::uint64_t>(original) == std::bit_cast<std::uint64_t>(decoded[j]);
                }
                nonexact_frames += !same;
                ++fixture_frames;
                fixture_values += record->count;
            }
        }
    }
    check(nonexact_frames >= 8, "real nonexact compressed corpus exercised");
    std::array<double, 8> source{1.125, -2.25, 3.5, 0.125, 1. / 3., 4., -0., 8.};
    const auto workspace = codec.workspace_bytes();
    allocation_count = 0;
    count_allocations = true;
    for (unsigned j = 0; j < 2000; ++j) {
        const auto output = codec.encode(source, scratch, *fixed);
        if (!output || !codec.decode(output->encoding, source.size(), output->bytes, decoded, *fixed) ||
            CodecTestAccess::accept(codec, one, positive_zero, 16, *fixed)) {
            count_allocations = false;
            throw std::runtime_error("fixed repeated success/failure");
        }
    }
    count_allocations = false;
    check(allocation_count == 0 && codec.workspace_bytes() == workspace, "fixed no active C++ allocations");
    std::cout << "fixed checks passed exponents=1917 oracles=" << oracle_count << " frames=" << fixture_frames
              << " values=" << fixture_values << " nonexact_frames=" << nonexact_frames
              << " repeated_cpp_allocations=" << allocation_count << '\n';
}
