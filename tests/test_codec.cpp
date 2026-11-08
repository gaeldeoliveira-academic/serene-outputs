#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <serene/outputs/codec.hpp>
#include <stdexcept>
#include <vector>
using namespace serene::outputs;
static std::atomic<bool> count_allocations{false};
static std::atomic<std::uint64_t> allocation_count{0};
void *operator new(std::size_t n) {
    if (count_allocations)
        allocation_count++;
    if (auto *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept {
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept {
    std::free(p);
}
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
std::vector<std::byte> read(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    check(bool(f), "read");
    std::vector<char> x{std::istreambuf_iterator<char>(f), {}};
    std::vector<std::byte> b(x.size());
    std::memcpy(b.data(), x.data(), x.size());
    return b;
}
std::string hex(Digest d) {
    static constexpr char h[] = "0123456789abcdef";
    std::string out;
    for (auto b : d) {
        auto x = std::to_integer<unsigned>(b);
        out += h[x >> 4];
        out += h[x & 15];
    }
    return out;
}
#include "codec_fixed_checks.hpp"
int main(int argc, char **argv) {
    try {
        auto c = Codec::create(4096);
        check(bool(c), "create");
        std::vector<double> source(4096), scratch(4096), decoded(4096);
        auto cap = c->workspace_bytes();
        std::uint64_t frames = 0, values = 0;
        auto abc = std::as_bytes(std::span("abc", 3));
        check(hex(sha256(abc)) ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA256 vector");
        Sha256 incremental;
        incremental.update(abc.first(1));
        incremental.update(abc.subspan(1));
        check(incremental.finish() == sha256(abc), "incremental SHA");
        check(!Codec::create(0) && !Codec::create(maximum_frame_values + 1), "capacity limits");
        std::uint64_t rng = 73;
        for (auto &x : source) {
            rng ^= rng << 13;
            rng ^= rng >> 7;
            rng ^= rng << 17;
            x = std::bit_cast<double>((rng & 0x800fffffffffffffull) | 0x3fe0000000000000ull);
        }
        source[0] = -0.;
        source[1] = std::numeric_limits<double>::denorm_min();
        for (std::size_t n : {1, 2, 3, 4, 5, 63, 64, 1027, 4096}) {
            auto e = c->encode(std::span(source).first(n), scratch);
            check(bool(e), "encode finite tails");
            check(bool(c->decode(e->encoding, n, e->bytes, decoded)), "decode");
            check(std::memcmp(source.data(), decoded.data(), n * 8) == 0, "exact source bits");
        }
        std::fill(source.begin(), source.end(), 0.);
        auto tie = c->encode(std::span(source).first(2), scratch);
        check(tie && tie->encoding == Encoding::raw && tie->bytes.size() == 16, "raw tie");
        auto expand = c->encode(std::span(source).first(1), scratch);
        check(expand && expand->encoding == Encoding::raw, "raw expansion");
        auto z = c->encode(std::span(source).first(4), scratch);
        check(z && z->encoding == Encoding::zfp, "compressible");
        auto bytes = std::vector<std::byte>(z->bytes.begin(), z->bytes.end());
        check(!c->decode(Encoding::zfp, 3, bytes, decoded), "header count mismatch");
        check(!c->decode(Encoding::zfp, 4, std::span(bytes).first(8), decoded), "truncation");
        auto original = bytes;
        bytes[11] ^= std::byte{0x01};
        check(!c->decode(Encoding::zfp, 4, bytes, decoded), "bad mode");
        bytes = original;
        bytes[4] &= std::byte{0xfc};
        check(!c->decode(Encoding::zfp, 4, bytes, decoded), "wrong scalar type");
        double inf = std::numeric_limits<double>::infinity();
        check(!c->decode(Encoding::raw, 1, std::as_bytes(std::span(&inf, 1)), decoded),
              "nonfinite raw");
        check(!c->encode(std::span(&inf, 1), scratch), "nonfinite source");
        check(!c->encode(std::span(source).first(4), std::span(source).first(4)),
              "overlap rejected");
        allocation_count = 0;
        count_allocations = true;
        for (int j = 0; j < 2000; j++) {
            auto e = c->encode(std::span(source).first(64), scratch);
            if (!e || !c->decode(e->encoding, 64, e->bytes, decoded)) {
                count_allocations = false;
                throw std::runtime_error("repeated frame");
            }
        }
        count_allocations = false;
        check(allocation_count == 0 && c->workspace_bytes() == cap,
              "bounded repeated C++ allocation/workspace");
        std::array<std::byte, 128> fuzz{};
        for (unsigned j = 0; j < 20000; j++) {
            for (auto &b : fuzz) {
                rng ^= rng << 13;
                rng ^= rng >> 7;
                rng ^= rng << 17;
                b = std::byte(rng);
            }
            auto status =
                Codec::inspect_header(std::span(fuzz).first(8 * (1 + j % 16)), 1 + j % 4096);
            (void)status;
        }
        check(argc > 1, "fixture directory");
        for (auto &e : std::filesystem::directory_iterator(argv[1])) {
            auto path = e.path().string();
            if (!path.ends_with(".shape.txt"))
                continue;
            path.resize(path.size() - 10);
            auto ix = read(path + ".index.bin"), payload = read(path + ".payload.bin"),
                 expected = read(path + ".expected.bin");
            std::ifstream dims(path + ".shape.txt");
            std::uint64_t outer, inner;
            dims >> outer >> inner;
            for (std::size_t off = 0; off < ix.size(); off += 77) {
                auto r = decode_record(std::span(ix).subspan(off, 77));
                check(bool(r), "canonical parse");
                auto canonical = encode_record(*r);
                check(std::equal(canonical.begin(), canonical.end(), ix.begin() + off),
                      "canonical reencode");
                auto data = std::span(payload).subspan(r->byte_offset, r->byte_count);
                check(sha256(data) == r->digest, "fixture digest");
                check(bool(c->decode(r->encoding, r->count, data, decoded)), "fixture decode");
                check(!std::memcmp(decoded.data(),
                                   expected.data() + (r->outer_index * inner + r->inner_begin) * 8,
                                   r->count * 8),
                      "independent fixture values");
                frames++;
                values += r->count;
            }
        }
        test_fixed_accuracy(*c, argc > 2 ? argv[2] : nullptr);
        std::cout << "codec checks passed frames=" << frames << " values=" << values
                  << " repeated_cpp_allocations=" << allocation_count << " workspace=" << cap
                  << " header_fuzz_cases=20000\n";
        return 0;
    } catch (const std::exception &e) {
        count_allocations = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}

