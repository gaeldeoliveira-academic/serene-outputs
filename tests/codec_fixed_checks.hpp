#pragma once
#include <cmath>
#include "../third_party/nlohmann_json/include/nlohmann/json.hpp"
namespace serene::outputs {
/** Stateless friend test adapter used only by test_fixed_accuracy to reach the same private candidate-admission routine as encode; it owns no codec state and retains no spans. */ struct CodecTestAccess {
/** Exercise c's production candidate-admission path using borrowed source/decoded values, candidate byte size and admitted profile. Returns the normal encoded-frame/status result; no runtime fault hook is installed. */
    static std::expected<EncodedFrame, Status> accept(Codec &c, std::span<const double> source,
        std::span<const double> decoded, std::size_t candidate_bytes, const CodecProfile &profile) {
    // Forward the supplied candidate directly into the codec's private verification/fallback decision.

        return c.accept_candidate(source, decoded, candidate_bytes, profile);
    }
};
}
/** Parse fixture path into an owned JSON value; throws if the file cannot be opened or parsed. */
static nlohmann::json read_json(const std::filesystem::path &path) {
    // Open the fixture, check availability, then parse its owned JSON representation.

    /* JSON fixture stream. */ std::ifstream stream(path);
    check(bool(stream), "JSON fixture open");
    return nlohmann::json::parse(stream);
}
/** Parse the JSON string value as one complete hexadecimal binary64 spelling. Returns its double value or fails the test on trailing text. */
static double hex_double(const nlohmann::json &value) {
    // Extract the text, parse through strtod, and require complete consumption.

    /* Owned hexadecimal numeric spelling. */ const auto text = value.get<std::string>();
    /* Parser end pointer used to prove full consumption. */ char *end = nullptr;
    /* Parsed binary64 value. */ const auto x = std::strtod(text.c_str(), &end);
    check(end == text.c_str() + text.size(), "hex binary64 parse");
    return x;
}
/** Interpret a JSON hexadecimal uint64 spelling as the exact IEEE-754 binary64 bits; no numeric conversion of the bit pattern occurs. */
static double bit_double(const nlohmann::json &value) {
    // Parse the 64-bit hexadecimal word and bit-cast it into double.

    return std::bit_cast<double>(std::stoull(value.get<std::string>(), nullptr, 16));
}
/** Exercise fixed-accuracy profile/header/verifier behavior using prepared codec. extra_oracles may name an additional exact-real corpus or be null; existing buffers/codec ownership remain with callers. Throws on the first mismatch. */
static void test_fixed_accuracy(Codec &codec, const char *extra_oracles) {
    // Check wire profiles and every admitted exponent, including neighboring-header rejection.
    // Compare exact-real error decisions/reporting with independent oracles and injected fallback candidates.
    // Decode independent fixed frames bit-for-bit, then repeat success/error paths under allocation counting.

    /* Accepted extension-corpus directory relative to the source checkout. */ const auto root = std::filesystem::path(SERENE_OUTPUTS_SOURCE_DIR) / "fixtures_extensions_draft";
    /* Reversible expected profile for mismatch checks. */ const auto exact = CodecProfile::reversible();
    /* Canonical fixed-accuracy profile with tolerance 2^-12. */ auto fixed = CodecProfile::fixed_accuracy(0x1p-12);
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
    for (/* Invalid or unsupported tolerance candidate. */ auto x : {0., -0., -1., std::numeric_limits<double>::infinity(),
                   std::numeric_limits<double>::quiet_NaN(), 0x1p-1074, 0x1p844,
                   std::numeric_limits<double>::max()})
        check(!CodecProfile::fixed_accuracy(x), "invalid or unsupported tau");
    /* Reusable zero input, encode scratch and decode buffers. */ std::array<double, 8> zeros{}, scratch{}, decoded{};
    for (/* Every supported effective power-of-two tolerance exponent. */ int exponent = -1073; exponent <= 843; ++exponent) {
        /* Exactly representable tolerance at the current exponent. */ const double tau = std::ldexp(1., exponent);
        /* Admitted profile for the current exponent or independent fixture. */ const auto profile = CodecProfile::fixed_accuracy(tau);
        check(profile && profile->minimum_exponent() == exponent &&
              profile->effective_tolerance() == tau, "every admitted exponent");
        /* Largest representable tolerance below the next power of two. */ const auto upper = CodecProfile::fixed_accuracy(std::nextafter(std::ldexp(1., exponent + 1), 0.));
        check(upper && upper->minimum_exponent() == exponent &&
              upper->effective_tolerance() == tau, "bit-derived floor at power boundary");
        /* Encoded zero frame borrowing codec storage until the next encode. */ const /* Encoded frame copied before mutating expert-mode bits. */ auto frame = codec.encode(zeros, scratch, *profile);
        check(frame && frame->encoding == Encoding::zfp && frame->verified_count == zeros.size(),
              "all exponent native setter/header agreement");
        check(bool(Codec::inspect_header(frame->bytes, zeros.size(), *profile)), "expected mode header");
        check(!Codec::inspect_header(frame->bytes, zeros.size(), exact), "fixed header exact mismatch");
        /* Different valid profile used to reject a canonical but unexpected header. */ auto mismatch = CodecProfile::fixed_accuracy(std::ldexp(1., exponent == 843 ? 842 : exponent + 1));
        check(!codec.decode(Encoding::zfp, zeros.size(), frame->bytes, decoded, *mismatch),
              "valid canonical other profile rejected");
    }
    /* Encoded frame copied before mutating expert-mode bits. */ auto frame = codec.encode(zeros, scratch, *fixed);
    check(bool(frame), "header frame");
    /* Owned header/payload mutation buffer. */ std::vector<std::byte> bytes(frame->bytes.begin(), frame->bytes.end());
    bytes[10] |= std::byte{0xf0}; bytes[11] = std::byte{0xff};
    check(!Codec::inspect_header(bytes, zeros.size(), *fixed), "long expert header rejected");
    check(!codec.decode(Encoding::zfp, zeros.size(), bytes, decoded, *fixed), "long header decoder rejected");
    /* Total independent exact-real verifier cases checked. */ std::uint64_t oracle_count = 0;
    for (/* Owned JSON oracle row borrowed within this loop. */ const auto &entry : read_json(root / "exact_real_bound_oracle.json")) {
        /* Exact source values reconstructed from the oracle. */ const std::array source{hex_double(entry["source_hex"])};
        /* Decoded comparison values reconstructed from the oracle. */ const std::array restored{hex_double(entry["decoded_hex"])};
        /* Exact-real verification result under the oracle tolerance. */ const auto result = Codec::verify_accuracy(source, restored, hex_double(entry["tolerance_hex"]));
        check(bool(result) == entry["accept"].get<bool>(), "independent exact-real oracle");
        if (result)
            check(result->bitwise_equal == entry["bitwise_equal"].get<bool>(), "oracle bit equality");
        else
            check(result.error().code == StatusCode::verification_failure, "oracle rejection phase");
        ++oracle_count;
    }
    if (extra_oracles) {
        for (/* Owned JSON oracle row borrowed within this loop. */ const auto &entry : read_json(extra_oracles)) {
            /* Exact source values reconstructed from the oracle. */ const std::array source{bit_double(entry["source_bits"])};
            /* Decoded comparison values reconstructed from the oracle. */ const std::array restored{bit_double(entry["decoded_bits"])};
            /* Exact-real verification result under the oracle tolerance. */ const auto result = Codec::verify_accuracy(source, restored, bit_double(entry["tau_bits"]));
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
    /* Unit source and negative half-ULP operand for rounding-direction checks. */ const std::array one{1.}, negative_half_ulp{-0x1p-53};
    /* Next representable value above one; upward absolute-error bound. */ const double next_one = std::nextafter(1., 2.);
    /* Verifier result distinguishing upward max error from nearest RMS. */ const auto up = Codec::verify_accuracy(one, negative_half_ulp, next_one);
    check(up && up->max_abs_error == next_one && up->rms_error == 1., "upward versus nearest reporting");
    /* Signed-zero probes with equal real values but different bits. */ const std::array negative_zero{-0.}, positive_zero{0.};
    /* Fixed-profile signed-zero comparison result. */ const auto sign = Codec::verify_values(negative_zero, positive_zero, *fixed);
    check(sign && !sign->bitwise_equal && sign->max_abs_error == 0 && sign->rms_error == 0,
          "signed-zero real error distinct from bitwise equality");
    check(!Codec::verify_values(negative_zero, positive_zero, exact), "exact signed-zero mismatch");
    /* Maximum finite input for overflow-safe RMS evaluation. */ const std::array huge{std::numeric_limits<double>::max()};
    /* Verifier result at maximum finite magnitude. */ const auto extreme = Codec::verify_accuracy(huge, positive_zero, huge[0]);
    check(extreme && extreme->max_abs_error == huge[0] && extreme->rms_error == huge[0],
          "finite RMS maximum without squared overflow");
    /* Smallest positive binary64 subnormal. */ const std::array tiny{0x1p-1074};
    /* Verifier result preserving subnormal error without squaring underflow. */ const auto subnormal = Codec::verify_accuracy(tiny, positive_zero, 0x1p-1074);
    check(subnormal && subnormal->rms_error == 0x1p-1074, "subnormal error without square underflow");
    /* NaN source/decoded rejection probe. */ const std::array nonfinite{std::numeric_limits<double>::quiet_NaN()};
    check(!Codec::verify_accuracy(nonfinite, positive_zero, 1.) &&
          !Codec::verify_accuracy(positive_zero, nonfinite, 1.), "nonfinite verifier input");
    /* Invalid candidate that must fail before any raw fallback. */ const auto injected = CodecTestAccess::accept(codec, one, positive_zero, 16, *fixed);
    check(!injected && injected.error().code == StatusCode::verification_failure,
          "injected candidate failure before raw fallback");
    /* Verified candidate whose raw fallback must restore source bits/statistics. */ const auto valid_raw = CodecTestAccess::accept(codec, negative_zero, positive_zero, 16, *fixed);
    /* Raw fallback word copied without aliasing. */ std::uint64_t stored_bits = 0;
    if (valid_raw) std::memcpy(&stored_bits, valid_raw->bytes.data(), 8);
    check(valid_raw && valid_raw->encoding == Encoding::raw && valid_raw->max_abs_error == 0. &&
          valid_raw->rms_error == 0. && valid_raw->bitwise_equal && valid_raw->verified_count == 1 &&
          stored_bits == std::bit_cast<std::uint64_t>(-0.), "verified raw fallback resets statistics and source bits");
    /* Finite source with a small nonzero candidate error. */ const std::array almost_one{1. + 0x1p-14};
    /* Raw fallback result expected to report exact stored source bytes. */ const auto lossy_raw = CodecTestAccess::accept(codec, almost_one, one, 16, *fixed);
    check(lossy_raw && lossy_raw->max_abs_error == 0 && lossy_raw->rms_error == 0 &&
          lossy_raw->bitwise_equal && lossy_raw->verified_count == 1, "raw nonzero error reset");
    /* Frame/value totals and count of genuinely nonexact compressed frames. */ std::uint64_t fixture_frames = 0, fixture_values = 0, nonexact_frames = 0;
    /* Owned positive-corpus manifest. */ const auto manifest = read_json(root / "manifest.json");
    for (/* Positive fixture manifest row. */ const auto &item : manifest["positive"]) {
        /* Owned expected metadata sidecar for one fixture. */ const auto description = read_json(root / item["expected"].get<std::string>());
        for (/* Run expected metadata borrowed while its owner remains alive. */ const auto &run : description["runs"]) {
            if (!run.contains("fixed_accuracy")) continue;
            /* Admitted profile for the current exponent or independent fixture. */ const auto profile = CodecProfile::fixed_accuracy(hex_double(run["fixed_accuracy"]["requested_tolerance_hex"]));
            check(bool(profile), "fixture profile factory");
            /* Basename of binary sidecars for this run. */ const auto prefix = root / run["sidecar"].get<std::string>();
            /* Owned canonical frame-index bytes. */ const auto ix = read(prefix.string() + ".index.bin");
            /* Owned encoded frame payload bytes. */ const auto payload = read(prefix.string() + ".payload.bin");
            /* Owned exact original binary64 source array. */ const auto source_bytes = read(prefix.string() + ".expected.bin");
            /* Logical inner dimension used to map frame offsets into source arrays. */ const auto inner = run["shape"][1].get<std::size_t>();
            for (/* Byte offset of each 77-byte frame-index record. */ std::size_t off = 0; off < ix.size(); off += 77) {
                /* Validated frame-index record. */ const auto record = decode_record(std::span(ix).subspan(off, 77));
                check(record && record->count <= 8, "fixed fixture index");
                /* Encoded payload span for the current frame. */ const auto data = std::span(payload).subspan(record->byte_offset, record->byte_count);
                check(sha256(data) == record->digest, "fixed fixture digest");
                check(bool(codec.decode(record->encoding, record->count, data, decoded, *profile)),
                      "independent compressed fixed fixture decode");
                /* Whether every reconstructed value equals the original bit pattern. */ bool same = true;
                for (/* Value position inside the current frame. */ std::size_t j = 0; j < record->count; ++j) {
                    /* Flat logical index into expected source and decoded metadata. */ const auto index = record->outer_index * inner + record->inner_begin + j;
                    /* Independent decoded binary64 value at that logical index. */ const auto expected = bit_double(run["fixed_accuracy"]["decoded_bits_hex"][index]);
                    check(std::bit_cast<std::uint64_t>(decoded[j]) == std::bit_cast<std::uint64_t>(expected),
                          "independent fixed decoded bits");
                    /* Original source value copied safely from byte storage. */ double original;
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
    /* Fixed heterogeneous input reused during allocation measurement. */ std::array<double, 8> source{1.125, -2.25, 3.5, 0.125, 1. / 3., 4., -0., 8.};
    /* Codec workspace size before repeated success/error calls. */ const auto workspace = codec.workspace_bytes();
    allocation_count = 0;
    count_allocations = true;
    for (/* Repeated fixed-profile call index. */ unsigned j = 0; j < 2000; ++j) {
        /* Current encoded frame, consumed before the next encode. */ const auto output = codec.encode(source, scratch, *fixed);
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
