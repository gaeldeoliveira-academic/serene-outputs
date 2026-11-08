// G0C trusted tiny-fixture inspector. No Serene implementation or HDF5 dependency.
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <zfp.h>
using Bytes = std::vector<unsigned char>;
using Json = nlohmann::json;
namespace fs = std::filesystem;
void need(bool yes, const char *what) {
    if (!yes)
        throw std::runtime_error(what);
}
Bytes read(const fs::path &p) {
    need(fs::file_size(p) <= 16777216, "fixture-only file limit");
    std::ifstream f(p, std::ios::binary);
    need(bool(f), "open");
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
std::uint64_t le(const unsigned char *p, unsigned n) {
    std::uint64_t x = 0;
    for (unsigned k = 0; k < n; k++)
        x |= std::uint64_t(p[k]) << (k * 8);
    return x;
}
void put(Bytes &b, std::uint64_t x, unsigned n) {
    for (unsigned k = 0; k < n; k++)
        b.push_back(static_cast<unsigned char>(x >> (k * 8)));
}
std::array<unsigned char, 32> sha(std::span<const unsigned char> b) {
    std::array<unsigned char, 32> x{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(b.data()),
                    static_cast<ULONG>(b.size()), x.data(), 32) >= 0,
         "sha");
    return x;
}
std::string hex(std::span<const unsigned char> b) {
    std::string s;
    for (auto x : b) {
        s += "0123456789abcdef"[x >> 4];
        s += "0123456789abcdef"[x & 15];
    }
    return s;
}
void same(std::span<const unsigned char> a, std::span<const unsigned char> b, const char *m) {
    need(a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin()), m);
}
struct Entry {
    std::uint32_t kind{};
    std::uint64_t offset{}, size{};
};
struct File {
    Bytes bytes;
    std::vector<Entry> entries;
    std::vector<bool> owned;
    std::uint64_t root{};
    explicit File(const fs::path &path) : bytes(read(path)) {
        need(bytes.size() >= 128, "bootstrap length");
        auto *p = bytes.data();
        const unsigned char magic[8] = {'S', 'E', 'R', 'E', 'N', 'E', 0, 0};
        need(std::memcmp(p, magic, 8) == 0, "magic");
        need(le(p + 8, 2) == 1 && le(p + 10, 2) == 0 && le(p + 12, 4) == 0x01020304,
             "version/endian");
        need(le(p + 16, 4) == 128 && le(p + 20, 4) == 64 && le(p + 24, 8) == bytes.size() &&
                 le(p + 56, 8) == 1,
             "bootstrap fields");
        auto d = le(p + 32, 8), n = le(p + 40, 8);
        root = le(p + 48, 8);
        need(n > 0 && n <= 1048576 && d >= 128 && d <= bytes.size() &&
                 n <= (bytes.size() - d) / 64 && n * 64 == bytes.size() - d && root < n,
             "directory bounds");
        auto h = sha(std::span(p, 96));
        need(std::memcmp(h.data(), p + 96, 32) == 0, "bootstrap digest");
        h = sha(std::span(p + d, static_cast<std::size_t>(n * 64)));
        need(std::memcmp(h.data(), p + 64, 32) == 0, "directory digest");
        std::uint64_t next = 128;
        entries.reserve(n);
        owned.resize(n, false);
        for (std::uint64_t i = 0; i < n; i++) {
            auto *q = p + d + i * 64;
            auto kind = le(q + 8, 4), off = le(q + 16, 8), len = le(q + 24, 8);
            need(le(q, 8) == i && kind >= 1 && kind <= 8 && le(q + 12, 4) == 0,
                 "directory ID/kind/flags");
            need(off == next && off <= d && len <= d - off, "section contiguous bounds");
            h = sha(std::span(p + off, static_cast<std::size_t>(len)));
            need(std::memcmp(h.data(), q + 32, 32) == 0, "section digest");
            entries.push_back({static_cast<std::uint32_t>(kind), off, len});
            next = off + len;
        }
        need(next == d, "section coverage");
    }
    std::span<const unsigned char> section(std::uint64_t id) const {
        need(id < entries.size(), "section ID");
        const auto &e = entries[id];
        return std::span(bytes.data() + e.offset, static_cast<std::size_t>(e.size));
    }
    Json metadata(std::uint64_t id) {
        need(id < entries.size() && entries[id].kind == 1 && !owned[id], "metadata reference");
        owned[id] = true;
        auto b = section(id);
        need(b.size() <= 1048576, "JSON metadata length");
        auto j = Json::parse(b.begin(), b.end());
        need(j.is_object(), "metadata object");
        return j;
    }
    std::span<const unsigned char> array(const Json &d) const {
        auto id = d.at("section").get<std::uint64_t>();
        need(id < entries.size(), "array ID");
        auto dtype = d.at("dtype").get<std::string>();
        std::uint32_t kind = 0;
        std::uint64_t width = 0;
        if (dtype == "u8") {
            kind = 2;
            width = 1;
        } else if (dtype == "u32") {
            kind = 3;
            width = 4;
        } else if (dtype == "u64") {
            kind = 4;
            width = 8;
        } else if (dtype == "f64") {
            kind = 5;
            width = 8;
        } else if (dtype == "frames77") {
            kind = 6;
            width = 77;
        } else if (dtype == "i8") {
            kind = 7;
            width = 1;
        } else if (dtype == "i32") {
            kind = 8;
            width = 4;
        }
        need(kind && kind == entries[id].kind, "descriptor dtype");
        auto shape = d.at("shape");
        need(shape.is_array() && !shape.empty() && shape.size() <= 2, "descriptor rank");
        std::uint64_t count = 1;
        for (auto &v : shape) {
            auto n = v.get<std::uint64_t>();
            need(n <= 1099511627776ull && (n == 0 || count <= 9223372036854775807ull / n),
                 "shape bounds");
            count *= n;
        }
        need(count <= 9223372036854775807ull / width && count * width == entries[id].size,
             "descriptor extent");
        return section(id);
    }
    void claim(const Json &j) {
        if (j.is_object() && j.contains("section") && j.contains("dtype") && j.contains("shape")) {
            array(j);
            auto id = j.at("section").get<std::uint64_t>();
            need(!owned[id], "aliased section");
            owned[id] = true;
            return;
        }
        if (j.is_object() || j.is_array())
            for (const auto &v : j)
                claim(v);
    }
    std::string pool(const Json &j, std::size_t i) const {
        need(i < j.at("count").get<std::size_t>(), "pool index");
        auto b = array(j.at("bytes")), o = array(j.at("offsets"));
        need((i + 2) * 8 <= o.size(), "pool offset size");
        auto lo = le(o.data() + i * 8, 8), hi = le(o.data() + (i + 1) * 8, 8);
        need(lo <= hi && hi <= b.size(), "pool bounds");
        return std::string(reinterpret_cast<const char *>(b.data() + lo),
                           static_cast<std::size_t>(hi - lo));
    }
};
struct Summary {
    std::uint64_t files{}, runs{}, frames{}, raw{}, zfp{}, values{}, sections{};
};
void frames(std::span<const unsigned char> idx, std::span<const unsigned char> payload,
            std::span<const unsigned char> gold, std::uint64_t outer, std::uint64_t inner,
            Summary &s) {
    need(idx.size() % 77 == 0 && gold.size() == outer * inner * 8, "frame/expected shape");
    std::uint64_t r = 0, b = 0, off = 0;
    Bytes canonical;
    for (std::size_t k = 0; k < idx.size() / 77; k++) {
        auto *p = idx.data() + k * 77;
        auto row = le(p, 8), begin = le(p + 8, 8), count = le(p + 16, 8), at = le(p + 24, 8),
             size = le(p + 32, 8), enc = le(p + 40, 1), profile = le(p + 41, 4);
        need(row == r && begin == b && at == off && count > 0 && count <= 4 && begin <= inner &&
                 count <= inner - begin && at <= payload.size() && size <= payload.size() - at &&
                 profile == 0,
             "frame partition");
        for (unsigned q = 0; q < 5; q++)
            put(canonical, le(p + q * 8, 8), 8);
        put(canonical, enc, 1);
        put(canonical, profile, 4);
        canonical.insert(canonical.end(), p + 45, p + 77);
        auto encoded = payload.subspan(at, size);
        auto h = sha(encoded);
        need(std::memcmp(h.data(), p + 45, 32) == 0, "frame digest");
        auto expected = gold.subspan((row * inner + begin) * 8, count * 8);
        if (enc == 0) {
            need(size == count * 8, "raw length");
            same(encoded, expected, "actual raw bits");
            s.raw++;
        } else {
            need(enc == 1 && size >= 12, "encoding");
            need(((le(encoded.data() + 8, 4) >> 20) & 4095) == 2176, "independent short mode bits");
            auto *stream = stream_open(const_cast<unsigned char *>(encoded.data()), encoded.size());
            auto *z = zfp_stream_open(stream);
            auto *field = zfp_field_alloc();
            need(stream && z && field, "public codec state");
            need(zfp_read_header(z, field, ZFP_HEADER_FULL) == 96, "header bits");
            need(zfp_field_type(field) == zfp_type_double && zfp_field_dimensionality(field) == 1 &&
                     zfp_field_size(field, nullptr) == count && zfp_stream_mode(z) == 2176,
                 "header count/type/mode");
            uint mn, mx, prec;
            int exp;
            zfp_stream_params(z, &mn, &mx, &prec, &exp);
            need(mn == 1 && mx == 16658 && prec == 64 && exp == -1075, "reversible tuple");
            std::vector<double> values(count);
            zfp_field_set_pointer(field, values.data());
            need(zfp_decompress(z, field) == size, "consumed bytes");
            same(std::span(reinterpret_cast<unsigned char *>(values.data()), count * 8), expected,
                 "actual ZFP decoded bits");
            zfp_field_free(field);
            zfp_stream_close(z);
            stream_close(stream);
            s.zfp++;
        }
        off += size;
        b += count;
        if (b == inner) {
            r++;
            b = 0;
        }
        s.frames++;
        s.values += count;
    }
    same(canonical, idx, "canonical77 roundtrip");
    need(off == payload.size() &&
             ((outer == 0 || inner == 0) ? idx.empty() : (r == outer && b == 0)),
         "terminal coverage");
}
int main(int argc, char **argv) {
    try {
        need(argc == 2 && std::endian::native == std::endian::little && zfp_codec_version == 5,
             "arguments/host/codec");
        fs::path dir = argv[1];
        auto mb = read(dir / "manifest.json");
        auto manifest = Json::parse(mb.begin(), mb.end());
        Summary total;
        for (auto it = manifest.at("files").begin(); it != manifest.at("files").end(); ++it) {
            auto b = read(dir / it.key());
            need(b.size() == it.value().at("bytes").get<std::size_t>() &&
                     hex(sha(b)) == it.value().at("sha256").get<std::string>(),
                 "manifest artifact identity");
        }
        for (auto &item : manifest.at("positive")) {
            File file(dir / item.at("file").get<std::string>());
            auto root = file.metadata(file.root);
            file.claim(root);
            auto eb = read(dir / item.at("expected").get<std::string>());
            auto expected = Json::parse(eb.begin(), eb.end());
            auto &cat = root.at("results").at("index");
            auto ids = file.array(cat.at("run_metadata_section"));
            need(ids.size() == expected.at("runs").size() * 8, "run count");
            for (std::size_t i = 0; i < expected.at("runs").size(); i++) {
                auto id = le(ids.data() + i * 8, 8);
                need(id != file.root, "root/run alias");
                auto run = file.metadata(id);
                file.claim(run);
                auto &x = expected.at("runs")[i];
                need(file.pool(cat.at("run_id"), i) == x.at("run_id").get<std::string>() &&
                         run.at("info").at("run_id") == x.at("run_id"),
                     "run ID");
                if (x.contains("catalog_state")) {
                    need(run.size() == 1 && run.at("info").at("catalog_state") == 2,
                         "unavailable info-only");
                    continue;
                }
                auto prefix = dir / x.at("sidecar").get<std::string>();
                auto index = file.array(run.at("values").at("frame_index")),
                     payload = file.array(run.at("values").at("payload"));
                same(index, read(prefix.string() + ".index.bin"), "actual index sidecar");
                same(payload, read(prefix.string() + ".payload.bin"), "actual payload sidecar");
                bool transient = x.at("kind") == 0;
                same(file.array(run.at(transient ? "time" : "load_levels")),
                     read(prefix.string() + ".axis.bin"), "actual axis bits");
                same(file.array(run.at(transient ? "step_index" : "level_index")),
                     read(prefix.string() + ".identity.bin"), "actual identity bytes");
                auto sh = run.at("values").at("shape");
                need(sh == x.at("shape"), "logical shape");
                auto gold = read(prefix.string() + ".expected.bin");
                frames(index, payload, gold, sh[0].get<std::uint64_t>(), sh[1].get<std::uint64_t>(),
                       total);
                total.runs++;
            }
            for (bool marked : file.owned)
                need(marked, "orphan section");
            total.files++;
            total.sections += file.entries.size();
        }
        std::cout
            << Json{{"positive_files", total.files},
                    {"prepared_runs", total.runs},
                    {"sections", total.sections},
                    {"frames", total.frames},
                    {"raw", total.raw},
                    {"zfp", total.zfp},
                    {"exact_values", total.values},
                    {"actual_container_bytes", "pass"},
                    {"scope",
                     "independent trusted tiny bootstrap/directory/descriptor/ownership/section "
                     "hashes/axes/frames; not production hostile-input qualification"}}
                   .dump()
            << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
