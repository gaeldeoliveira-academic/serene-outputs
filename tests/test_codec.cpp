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
/* Gate for counting global C++ allocation requests only inside selected windows. */ static std::atomic<bool> count_allocations{false};
/* Number of C++ allocation requests made while the gate is enabled. */ static std::atomic<std::uint64_t> allocation_count{0};
/** Allocate n bytes for this test executable; increment the active C++ allocation counter and return owned malloc storage or throw bad_alloc. A zero-size request still obtains one byte. */
void *operator new(std::size_t n) {
    // Count active-window requests, allocate at least one byte, and throw on allocation failure.

    if (count_allocations)
        allocation_count++;
    if (/* Owned malloc block, at least one byte even for zero-size new. */ auto *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
/** Release malloc-backed p from the test global new; null is accepted. Unsized delete uses no size metadata. */
void operator delete(void *p) noexcept {
    // Free the allocation without changing the request counter.

    std::free(p);
}
/** Release malloc-backed p for sized delete; the compiler-provided size is intentionally unused. */
void operator delete(void *p, std::size_t) noexcept {
    // Free the same backing allocation as the unsized overload.

    std::free(p);
}
/** Require b and throw diagnostic m on failure; m is borrowed only for this call. */
void check(bool b, const char *m) {
    // Report failed assertions through the common exception path.

    if (!b)
        throw std::runtime_error(m);
}
/** Read fixture p into an owned byte vector; throws if the file cannot be opened. */
std::vector<std::byte> read(const std::filesystem::path &p) {
    // Read binary bytes through a char buffer, then copy their unchanged representations into byte storage.

    /* Binary fixture input stream. */ std::ifstream f(p, std::ios::binary);
    check(bool(f), "read");
    /* Owned char buffer populated by stream iteration. */ std::vector<char> x{std::istreambuf_iterator<char>(f), {}};
    /* Returned byte-for-byte fixture copy. */ std::vector<std::byte> b(x.size());
    std::memcpy(b.data(), x.data(), x.size());
    return b;
}
/** Return lowercase hexadecimal text for by-value SHA-256 digest d. */
std::string hex(Digest d) {
    // Expand each digest byte into two hexadecimal characters in source order.

    /* Hexadecimal digit alphabet. */ static constexpr char h[] = "0123456789abcdef";
    /* Accumulated lowercase digest spelling. */ std::string out;
    for (/* Current digest byte. */ auto b : d) {
        /* Unsigned digest-byte value used for nibble indexing. */ auto x = std::to_integer<unsigned>(b);
        out += h[x >> 4];
        out += h[x & 15];
    }
    return out;
}
#include "codec_fixed_checks.hpp"
/** Run reversible and fixed codec qualification. argv[1] is the independent fixture directory and optional argv[2] adds exact-real oracles; returns zero or one on failure. */
int main(int argc, char **argv) {
    // Prepare codec/buffers and check SHA-256, reversible tails, raw fallback and malformed headers.
    // Measure repeated codec calls, fuzz bounded headers, then decode independently authored frames.
    // Run fixed-accuracy checks and report frame/value counts and requested workspace.

    try {
        /* Prepared codec with capacity for the largest test frame. */ auto c = Codec::create(4096);
        check(bool(c), "create");
        /* Owned source, codec scratch and decoded-value buffers. */ std::vector<double> source(4096), scratch(4096), decoded(4096);
        /* Workspace byte count before repeated calls. */ auto cap = c->workspace_bytes();
        /* Decoded independent fixture frame/value totals. */ std::uint64_t frames = 0, values = 0;
        /* Published three-byte SHA-256 test vector. */ auto abc = std::as_bytes(std::span("abc", 3));
        check(hex(sha256(abc)) ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA256 vector");
        /* Incremental digest state tested against one-shot hashing. */ Sha256 incremental;
        incremental.update(abc.first(1));
        incremental.update(abc.subspan(1));
        check(incremental.finish() == sha256(abc), "incremental SHA");
        check(!Codec::create(0) && !Codec::create(maximum_frame_values + 1), "capacity limits");
        /* Deterministic xorshift state for finite values and header fuzzing. */ std::uint64_t rng = 73;
        for (/* Current source value populated from deterministic bits. */ auto &x : source) {
            rng ^= rng << 13;
            rng ^= rng >> 7;
            rng ^= rng << 17;
            x = std::bit_cast<double>((rng & 0x800fffffffffffffull) | 0x3fe0000000000000ull);
        }
        source[0] = -0.;
        source[1] = std::numeric_limits<double>::denorm_min();
        for (/* Frame tail length under test. */ std::size_t n : {1, 2, 3, 4, 5, 63, 64, 1027, 4096}) {
            /* Encode result borrowing the codec's current encoded storage. */ auto e = c->encode(std::span(source).first(n), scratch);
            check(bool(e), "encode finite tails");
            check(bool(c->decode(e->encoding, n, e->bytes, decoded)), "decode");
            check(std::memcmp(source.data(), decoded.data(), n * 8) == 0, "exact source bits");
        }
        std::fill(source.begin(), source.end(), 0.);
        /* Frame whose raw and compressed sizes tie. */ auto tie = c->encode(std::span(source).first(2), scratch);
        check(tie && tie->encoding == Encoding::raw && tie->bytes.size() == 16, "raw tie");
        /* Frame whose compressed representation would expand. */ auto expand = c->encode(std::span(source).first(1), scratch);
        check(expand && expand->encoding == Encoding::raw, "raw expansion");
        /* Compressible zero frame used as a header mutation seed. */ auto z = c->encode(std::span(source).first(4), scratch);
        check(z && z->encoding == Encoding::zfp, "compressible");
        /* Owned encoded bytes allowing mutations without changing codec storage. */ auto bytes = std::vector<std::byte>(z->bytes.begin(), z->bytes.end());
        check(!c->decode(Encoding::zfp, 3, bytes, decoded), "header count mismatch");
        check(!c->decode(Encoding::zfp, 4, std::span(bytes).first(8), decoded), "truncation");
        /* Unmodified header/payload copy restored between mutations. */ auto original = bytes;
        bytes[11] ^= std::byte{0x01};
        check(!c->decode(Encoding::zfp, 4, bytes, decoded), "bad mode");
        bytes = original;
        bytes[4] &= std::byte{0xfc};
        check(!c->decode(Encoding::zfp, 4, bytes, decoded), "wrong scalar type");
        /* Nonfinite scalar used to test source and raw-frame admission. */ double inf = std::numeric_limits<double>::infinity();
        check(!c->decode(Encoding::raw, 1, std::as_bytes(std::span(&inf, 1)), decoded),
              "nonfinite raw");
        check(!c->encode(std::span(&inf, 1), scratch), "nonfinite source");
        check(!c->encode(std::span(source).first(4), std::span(source).first(4)),
              "overlap rejected");
        allocation_count = 0;
        count_allocations = true;
        for (/* Repeated allocation-free round-trip index. */ int j = 0; j < 2000; j++) {
            /* Encode result borrowing the codec's current encoded storage. */ auto e = c->encode(std::span(source).first(64), scratch);
            if (!e || !c->decode(e->encoding, 64, e->bytes, decoded)) {
                count_allocations = false;
                throw std::runtime_error("repeated frame");
            }
        }
        count_allocations = false;
        check(allocation_count == 0 && c->workspace_bytes() == cap,
              "bounded repeated C++ allocation/workspace");
        /* Bounded random header probe buffer. */ std::array<std::byte, 128> fuzz{};
        for (/* Header fuzz case index. */ unsigned j = 0; j < 20000; j++) {
            for (/* Byte being populated in the fuzz buffer. */ auto &b : fuzz) {
                rng ^= rng << 13;
                rng ^= rng >> 7;
                rng ^= rng << 17;
                b = std::byte(rng);
            }
            /* Inspection result intentionally ignored; bounded failure must be safe. */ auto status =
                Codec::inspect_header(std::span(fuzz).first(8 * (1 + j % 16)), 1 + j % 4096);
            (void)status;
        }
        check(argc > 1, "fixture directory");
        for (/* Fixture directory entry selected by its shape sidecar. */ auto &e : std::filesystem::directory_iterator(argv[1])) {
            /* Sidecar basename after removing the shape suffix. */ auto path = e.path().string();
            if (!path.ends_with(".shape.txt"))
                continue;
            path.resize(path.size() - 10);
            /* Frame-index bytes; payload and expected own the encoded and original arrays. */ auto ix = read(path + ".index.bin"), payload = read(path + ".payload.bin"),
                 expected = read(path + ".expected.bin");
            /* Text logical-shape input stream. */ std::ifstream dims(path + ".shape.txt");
            /* Logical matrix dimensions parsed from the sidecar. */ std::uint64_t outer, inner;
            dims >> outer >> inner;
            for (/* Byte offset of the next canonical 77-byte frame-index record. */ std::size_t off = 0; off < ix.size(); off += 77) {
                /* Decoded canonical frame-index record. */ auto r = decode_record(std::span(ix).subspan(off, 77));
                check(bool(r), "canonical parse");
                /* Re-encoded record bytes for canonical-layout equality. */ auto canonical = encode_record(*r);
                check(std::equal(canonical.begin(), canonical.end(), ix.begin() + off),
                      "canonical reencode");
                /* Borrowed encoded payload extent for one frame. */ auto data = std::span(payload).subspan(r->byte_offset, r->byte_count);
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
    } catch (/* First codec assertion or I/O failure. */ const std::exception &e) {
        count_allocations = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}

