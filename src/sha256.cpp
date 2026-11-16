#include <serene/outputs/sha256.hpp>
extern "C" {
#include "sha256.h"
}
#include <cstring>
#include <memory>
namespace serene::outputs {
static_assert(sizeof(SHA256_CTX) <= 128);
static_assert(alignof(SHA256_CTX) <= alignof(std::uint64_t));
static_assert(sizeof(WORD) == 4);
Sha256::Sha256() noexcept {
    // Start the native context lifetime in aligned inline storage and initialize SHA-256.
    auto *p /* Native state borrowed from this object's inline storage. */ = std::construct_at(reinterpret_cast<SHA256_CTX *>(state_.data()));
    sha256_init(p);
}
void Sha256::update(std::span<const std::byte> b) noexcept {
    // Forward exact borrowed bytes to the already-initialized native state.
    sha256_update(reinterpret_cast<SHA256_CTX *>(state_.data()),
                  reinterpret_cast<const BYTE *>(b.data()), b.size());
}
Digest Sha256::finish() noexcept {
    // Finalize native state directly into the returned owned digest bytes.
    Digest d; // Output digest, fully written by the native finalizer.
    sha256_final(reinterpret_cast<SHA256_CTX *>(state_.data()), reinterpret_cast<BYTE *>(d.data()));
    return d;
}
Digest sha256(std::span<const std::byte> b) noexcept {
    // Initialize, consume the single borrowed span, and finalize once.
    Sha256 s; // Local incremental state for the one-shot helper.
    s.update(b);
    return s.finish();
}
std::array<std::byte, 77> encode_record(const FrameRecord &r) noexcept {
    // Serialize fixed-width integers in declared little-endian offsets, then copy
    // digest bytes; native structure layout and padding are never consulted.
    std::array<std::byte, 77> b{}; // Owned canonical wire record.
    // Write x at byte offset off with width n into the owned wire record.
    auto put = [&](std::uint64_t x, unsigned off, unsigned n) {
        // Emit least-significant bytes first.
        for (unsigned i /* Byte position within this integer field. */ = 0; i < n; i++)
            b[off + i] = std::byte(x >> (8 * i));
    };
    // Fixed offsets are the public 77-byte frame-record layout.
    put(r.outer_index, 0, 8);
    put(r.inner_begin, 8, 8);
    put(r.count, 16, 8);
    put(r.byte_offset, 24, 8);
    put(r.byte_count, 32, 8);
    put(static_cast<unsigned>(r.encoding), 40, 1);
    put(r.profile_index, 41, 4);
    std::copy(r.digest.begin(), r.digest.end(), b.begin() + 45);
    return b;
}
std::expected<FrameRecord, Status> decode_record(std::span<const std::byte> b) noexcept {
    // Require the exact wire width, reconstruct little-endian integer fields, and
    // copy the digest. Semantic encoding/profile/range checks belong to consumers.
    if (b.size() != 77)
        return std::unexpected(error(StatusCode::invalid_contract, 20));
    // Read width n at offset off from the already width-checked borrowed record.
    auto get = [&](unsigned off, unsigned n) {
        // Accumulate bytes in their declared little-endian significance.
        std::uint64_t x = 0; // Reconstructed unsigned field value.
        for (unsigned i /* Byte position within this integer field. */ = 0; i < n; i++)
            x |= std::uint64_t(std::to_integer<unsigned>(b[off + i])) << (8 * i);
        return x;
    };
    FrameRecord r{ // Owned logical record populated from fixed wire offsets.
get(0, 8),
                  get(8, 8),
                  get(16, 8),
                  get(24, 8),
                  get(32, 8),
                  static_cast<Encoding>(get(40, 1)),
                  static_cast<std::uint32_t>(get(41, 4)),
                  {}};
    std::copy(b.begin() + 45, b.end(), r.digest.begin());
    return r;
}
} // namespace serene::outputs
