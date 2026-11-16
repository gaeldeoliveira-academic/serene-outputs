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
/** Require yes or throw runtime_error carrying borrowed diagnostic what. Used only inside this trusted-fixture CLI; returns no value. */
void need(bool yes, const char *what) {
    // Forward an invariant failure to the top-level exception handler.

    if (!yes)
        throw std::runtime_error(what);
}
/** Return owned complete bytes from fixture path p, limited to 16 MiB. Throws on excess size/open failure; intentionally not a streaming production reader. */
Bytes read(const fs::path &p) {
    // Check the fixture-only file-size ceiling, open binary input and materialize its bytes.

    need(fs::file_size(p) <= 16777216, "fixture-only file limit");
    /* Owned binary input stream for the bounded whole-file fixture read. */ std::ifstream f(p, std::ios::binary);
    need(bool(f), "open");
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
/** Decode n little-endian bytes at p; internal callers ensure n<=8 and a valid borrowed extent. Returns uint64 without retaining p. */
std::uint64_t le(const unsigned char *p, unsigned n) {
    // Accumulate unsigned byte contributions in wire order.

    /* Unsigned little-endian accumulator. */ std::uint64_t x = 0;
    for (/* Byte position in the field. */ unsigned k = 0; k < n; k++)
        x |= std::uint64_t(p[k]) << (k * 8);
    return x;
}
/** Append low n bytes of x to in-out b in little-endian order; internal n<=8. Used to rebuild canonical records independently. */
void put(Bytes &b, std::uint64_t x, unsigned n) {
    // Emit the requested low-order bytes into the owned accumulator.

    for (/* Low-order byte position to append. */ unsigned k = 0; k < n; k++)
        b.push_back(static_cast<unsigned char>(x >> (k * 8)));
}
/** Return owned SHA-256 of tiny borrowed span b through Windows BCrypt; throws on hashing failure. */
std::array<unsigned char, 32> sha(std::span<const unsigned char> b) {
    // Hash the exact source extent into a fixed-size digest.

    /* Owned digest result buffer. */ std::array<unsigned char, 32> x{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(b.data()),
                    static_cast<ULONG>(b.size()), x.data(), 32) >= 0,
         "sha");
    return x;
}
/** Return owned lowercase hexadecimal text for borrowed bytes b. Used for exact manifest digest comparison. */
std::string hex(std::span<const unsigned char> b) {
    // Append high then low nibble characters for every input byte.

    /* Owned lowercase hexadecimal accumulator. */ std::string s;
    for (/* Current digest byte split into two hexadecimal digits. */ auto x : b) {
        s += "0123456789abcdef"[x >> 4];
        s += "0123456789abcdef"[x & 15];
    }
    return s;
}
/** Require byte spans a/b to have equal size/content or throw message m. Inputs are borrowed and unchanged; returns void. */
void same(std::span<const unsigned char> a, std::span<const unsigned char> b, const char *m) {
    // Compare extent first, then all bytes, and report the caller-labelled mismatch.

    need(a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin()), m);
}
/** Owned section extent retained by File after envelope/hash checks. Points by offset into File::bytes, never into an external source buffer. */
struct Entry {
    std::uint32_t kind{}; ///< Canonical directory kind code, one through eight.
    std::uint64_t offset{}, ///< Absolute byte offset in the owning File bytes.
        size{}; ///< Exact payload byte extent inside the owning File.
};
/** Trusted tiny fixture container with owned full bytes, directory and ownership bitmap. The CLI uses metadata/array/pool views only while this File remains alive; claim() rejects section aliasing and callers check for orphans. Not a complete hostile-input or semantic-format validator. */
struct File {
    Bytes bytes; ///< Owned complete archive bytes, limited by read() to 16 MiB.
    std::vector<Entry> entries; ///< Canonical dense directory descriptors into bytes.
    std::vector<bool> owned; ///< Section-use bitmap; metadata and array claims must be unique.
    std::uint64_t root{}; ///< Root metadata section ID from the verified bootstrap.
    /** Load fixture path into owned bytes and validate its complete bootstrap, directory, contiguous extents and all section hashes. Throws before a malformed File is usable. */
explicit File(const fs::path &path) : bytes(read(path)) {
    // Validate fixed bootstrap/version/endian fields and directory bounds.
    // Verify bootstrap/directory hashes, then every contiguous section descriptor and digest.
    // Retain directory entries and a clear ownership bitmap for subsequent graph traversal.

        need(bytes.size() >= 128, "bootstrap length");
        /* Borrowed base address of the owned archive bytes. */ auto *p = bytes.data();
        /* Expected eight-byte container discriminator. */ const unsigned char magic[8] = {'S', 'E', 'R', 'E', 'N', 'E', 0, 0};
        need(std::memcmp(p, magic, 8) == 0, "magic");
        need(le(p + 8, 2) == 1 && le(p + 10, 2) == 0 && le(p + 12, 4) == 0x01020304,
             "version/endian");
        need(le(p + 16, 4) == 128 && le(p + 20, 4) == 64 && le(p + 24, 8) == bytes.size() &&
                 le(p + 56, 8) == 1,
             "bootstrap fields");
        /* Directory byte offset and record population decoded from the bootstrap. */ auto d = le(p + 32, 8), n = le(p + 40, 8);
        root = le(p + 48, 8);
        need(n > 0 && n <= 1048576 && d >= 128 && d <= bytes.size() &&
                 n <= (bytes.size() - d) / 64 && n * 64 == bytes.size() - d && root < n,
             "directory bounds");
        /* Independent digest reused for bootstrap, directory and section checks. */ auto h = sha(std::span(p, 96));
        need(std::memcmp(h.data(), p + 96, 32) == 0, "bootstrap digest");
        h = sha(std::span(p + d, static_cast<std::size_t>(n * 64)));
        need(std::memcmp(h.data(), p + 64, 32) == 0, "directory digest");
        /* Next expected contiguous section byte offset. */ std::uint64_t next = 128;
        entries.reserve(n);
        owned.resize(n, false);
        for (/* Canonical directory row/section ID. */ std::uint64_t i = 0; i < n; i++) {
            /* Borrowed pointer to the current 64-byte directory row. */ auto *q = p + d + i * 64;
            /* Decoded section kind, absolute offset and byte extent. */ auto kind = le(q + 8, 4), off = le(q + 16, 8), len = le(q + 24, 8);
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
    /** Return borrowed bytes of admitted section id, rejecting an out-of-range index. The view is valid only while this File and its bytes remain unchanged/alive. */
std::span<const unsigned char> section(std::uint64_t id) const {
    // Validate the directory index and project its byte extent from owned storage.

        need(id < entries.size(), "section ID");
        /* Already-admitted directory extent for the requested section. */ const auto &e = entries[id];
        return std::span(bytes.data() + e.offset, static_cast<std::size_t>(e.size));
    }
    /** Claim unique metadata section id and return an owned parsed JSON object. Limits metadata to 1 MiB; this fixture parser is not the production strict-JSON admission path. */
Json metadata(std::uint64_t id) {
    // Require a unique JSON-kind section, claim it, then parse its bounded bytes as an object.

        need(id < entries.size() && entries[id].kind == 1 && !owned[id], "metadata reference");
        owned[id] = true;
        /* Borrowed exact metadata section bytes. */ auto b = section(id);
        need(b.size() <= 1048576, "JSON metadata length");
        /* Owned parsed JSON object returned to the caller. */ auto j = Json::parse(b.begin(), b.end());
        need(j.is_object(), "metadata object");
        return j;
    }
    /** Validate typed array descriptor d and return its borrowed section bytes. Checks dtype/kind, one/two-dimensional shape and checked extent; does not itself claim ownership. */
std::span<const unsigned char> array(const Json &d) const {
    // Map dtype to wire kind/width and validate the descriptor section.
    // Bound rank/dimension products and require exact section byte extent.
    // Return the view whose lifetime is tied to this File.

        /* Directory section ID named by the descriptor. */ auto id = d.at("section").get<std::uint64_t>();
        need(id < entries.size(), "array ID");
        /* Owned declared dtype spelling. */ auto dtype = d.at("dtype").get<std::string>();
        /* Expected directory kind derived from dtype. */ std::uint32_t kind = 0;
        /* Bytes per scalar or canonical frame record. */ std::uint64_t width = 0;
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
        /* Owned shape array used for bounded product checks. */ auto shape = d.at("shape");
        need(shape.is_array() && !shape.empty() && shape.size() <= 2, "descriptor rank");
        /* Checked product of logical dimensions. */ std::uint64_t count = 1;
        for (/* Current dimension value. */ auto &v : shape) {
            /* Current unsigned dimension before overflow admission. */ auto n = v.get<std::uint64_t>();
            need(n <= 1099511627776ull && (n == 0 || count <= 9223372036854775807ull / n),
                 "shape bounds");
            count *= n;
        }
        need(count <= 9223372036854775807ull / width && count * width == entries[id].size,
             "descriptor extent");
        return section(id);
    }
    /** Recursively claim every array descriptor in borrowed metadata j. Rejects aliases; callers use only bounded trusted fixture JSON and check all bitmap entries afterward. */
void claim(const Json &j) {
    // Recognize typed array objects, validate and claim their section once.
    // Otherwise recurse through object/array children without copying metadata.

        if (j.is_object() && j.contains("section") && j.contains("dtype") && j.contains("shape")) {
            array(j);
            /* Referenced data-section identity to claim once. */ auto id = j.at("section").get<std::uint64_t>();
            need(!owned[id], "aliased section");
            owned[id] = true;
            return;
        }
        if (j.is_object() || j.is_array())
            for (/* Borrowed child metadata visited recursively. */ const auto &v : j)
                claim(v);
    }
    /** Return an owned string copied from entry i in pool descriptor j. Validates index/offset bounds; this trusted fixture helper does not independently prove every UTF-8/presence rule. */
std::string pool(const Json &j, std::size_t i) const {
    // Read bytes/offset descriptors, bound the selected range and copy its exact bytes.

        need(i < j.at("count").get<std::size_t>(), "pool index");
        /* Borrowed pool byte and u64 offset sections. */ auto b = array(j.at("bytes")), o = array(j.at("offsets"));
        need((i + 2) * 8 <= o.size(), "pool offset size");
        /* Half-open byte interval for the selected string. */ auto lo = le(o.data() + i * 8, 8), hi = le(o.data() + (i + 1) * 8, 8);
        need(lo <= hi && hi <= b.size(), "pool bounds");
        return std::string(reinterpret_cast<const char *>(b.data() + lo),
                           static_cast<std::size_t>(hi - lo));
    }
};
/** Owned aggregate CLI counters for fully inspected trusted fixtures or writer cases. No graph or numeric history is stored in this result. */
struct Summary {
    std::uint64_t files{}, ///< Completely inspected archive files.
        runs{}, ///< Available runs with exact frame/axis comparisons.
        frames{}, ///< Canonical frames whose payload and value checks passed.
        raw{}, ///< Frames compared as exact raw binary64 bytes.
        zfp{}, ///< Frames independently decoded by public reversible ZFP.
        values{}, ///< Exact binary64 values compared across frames.
        sections{}; ///< Directory entries proved to have unique graph ownership.
};
/** Validate frame index idx and payload against exact raw gold for shape outer x inner; add successful counters to in-out s. All spans borrow tiny owned caller buffers. Uses public ZFP directly and the pinned reversible profile, with no Serene codec dependency. */
void frames(std::span<const unsigned char> idx, std::span<const unsigned char> payload,
            std::span<const unsigned char> gold, std::uint64_t outer, std::uint64_t inner,
            Summary &s) {
    // Check shape and iterate canonical row/frame/payload coverage.
    // Rebuild record bytes, verify payload hashes and compare raw or public-ZFP decoded values exactly.
    // Require final payload/matrix coverage and canonical record roundtrip.

    need(idx.size() % 77 == 0 && gold.size() == outer * inner * 8, "frame/expected shape");
    /* Next required row, inner-value offset and payload byte offset. */ std::uint64_t r = 0, b = 0, off = 0;
    /* Independent canonical-record repacking accumulator. */ Bytes canonical;
    for (/* Frame ordinal within the supplied index. */ std::size_t k = 0; k < idx.size() / 77; k++) {
        /* Borrowed pointer to one complete 77-byte record. */ auto *p = idx.data() + k * 77;
        /* Decoded logical/payload extents, encoding and profile index. */ auto row = le(p, 8), begin = le(p + 8, 8), count = le(p + 16, 8), at = le(p + 24, 8),
             size = le(p + 32, 8), enc = le(p + 40, 1), profile = le(p + 41, 4);
        need(row == r && begin == b && at == off && count > 0 && count <= 4 && begin <= inner &&
                 count <= inner - begin && at <= payload.size() && size <= payload.size() - at &&
                 profile == 0,
             "frame partition");
        for (/* One of the five leading u64 record fields. */ unsigned q = 0; q < 5; q++)
            put(canonical, le(p + q * 8, 8), 8);
        put(canonical, enc, 1);
        put(canonical, profile, 4);
        canonical.insert(canonical.end(), p + 45, p + 77);
        /* Borrowed frame payload slice. */ auto encoded = payload.subspan(at, size);
        /* Independent exact-byte digest for that payload. */ auto h = sha(encoded);
        need(std::memcmp(h.data(), p + 45, 32) == 0, "frame digest");
        /* Borrowed exact-value oracle slice corresponding to this frame. */ auto expected = gold.subspan((row * inner + begin) * 8, count * 8);
        if (enc == 0) {
            need(size == count * 8, "raw length");
            same(encoded, expected, "actual raw bits");
            s.raw++;
        } else {
            need(enc == 1 && size >= 12, "encoding");
            need(((le(encoded.data() + 8, 4) >> 20) & 4095) == 2176, "independent short mode bits");
            /* Public ZFP bitstream state borrowing encoded bytes. */ auto *stream = stream_open(const_cast<unsigned char *>(encoded.data()), encoded.size());
            /* Public ZFP stream state released after successful frame inspection. */ auto *z = zfp_stream_open(stream);
            /* Public ZFP field description for the decoded output. */ auto *field = zfp_field_alloc();
            need(stream && z && field, "public codec state");
            need(zfp_read_header(z, field, ZFP_HEADER_FULL) == 96, "header bits");
            need(zfp_field_type(field) == zfp_type_double && zfp_field_dimensionality(field) == 1 &&
                     zfp_field_size(field, nullptr) == count && zfp_stream_mode(z) == 2176,
                 "header count/type/mode");
            /* Decoded minimum/maximum bits and precision parameters. */ uint mn, mx, prec;
            /* Decoded minimum exponent of the pinned reversible profile. */ int exp;
            zfp_stream_params(z, &mn, &mx, &prec, &exp);
            need(mn == 1 && mx == 16658 && prec == 64 && exp == -1075, "reversible tuple");
            /* Owned tiny decoded-frame buffer used for exact bit comparison. */ std::vector<double> values(count);
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

/** Inspect every .serene in dir against its small writer sidecars and return counters. Claims complete section ownership and compares run identity, frame bytes and exact axes; throws on mismatch. */
Summary inspect_writer_directory(const fs::path &dir) {
    // Load each trusted writer archive and claim root/run metadata.
    // For available runs, compare shape, encoded frames and exact axis oracle bytes.
    // Require no unowned sections and accumulate complete-file totals.

    /* Owned counters accumulated across complete writer cases. */ Summary total;
    for (/* Current writer-output directory entry. */ const auto &entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".serene")
            continue;
        /* Owned verified envelope bytes and section-ownership tracking. */ File file(entry.path());
        /* Owned root metadata parsed independently. */ auto root = file.metadata(file.root);
        file.claim(root);
        /* Borrowed results index inside root metadata. */ auto &catalog = root.at("results").at("index");
        /* Borrowed one-run metadata-section ID column. */ auto ids = file.array(catalog.at("run_metadata_section"));
        need(ids.size() == 8, "one writer run");
        /* Owned single-run metadata with its data sections claimed. */ auto run = file.metadata(le(ids.data(), 8));
        file.claim(run);
        need(file.pool(catalog.at("run_id"), 0) == run.at("info").at("run_id").get<std::string>(),
             "writer run ID");
        if (run.at("info").at("catalog_state") == 0) {
            /* Writer sidecar stem derived from the archive filename. */ auto base = entry.path();
            base.replace_extension();
            /* Text shape oracle input stream. */ std::ifstream input(base.string() + ".shape.txt");
            /* Expected matrix dimensions from the sidecar. */ std::uint64_t outer{}, inner{};
            input >> outer >> inner;
            need(bool(input), "writer expected shape");
            /* Declared matrix shape copied for comparison. */ auto shape = run.at("values").at("shape");
            need(shape[0] == outer && shape[1] == inner, "writer actual shape");
            /* Borrowed canonical frame index from the archive. */ auto index = file.array(run.at("values").at("frame_index"));
            /* Borrowed encoded payload from the archive. */ auto payload = file.array(run.at("values").at("payload"));
            /* Owned exact-value writer oracle bytes. */ auto expected = read(base.string() + ".expected.raw");
            frames(index, payload, expected, outer, inner, total);
            /* Whether this run uses time rather than load-level axis semantics. */ auto transient = run.at("info").at("kind") == 0;
            same(file.array(run.at(transient ? "time" : "load_levels")),
                 read(base.string() + ".axis.raw"), "writer exact axis");
            total.runs++;
        } else
            need(run.size() == 1, "writer unavailable info-only");
        for (/* Claim bit proving one directory section has a graph owner. */ bool marked : file.owned)
            need(marked, "writer orphan section");
        total.sections += file.entries.size();
        total.files++;
    }
    return total;
}
/** Inspect fixture directory argv[1], optionally in --writer mode argv[2]. argc must be two/three on little-endian host with codec version five. Prints JSON totals, returning zero or one after caught failures. */
int main(int argc, char **argv) {
    // Admit CLI/host/codec assumptions and dispatch writer-sidecar mode when requested.
    // Otherwise verify every manifest artifact hash and inspect each positive fixture/run.
    // Prove section ownership and exact frame/axis sidecars, then print totals or an error.

    try {
        need((argc == 2 || argc == 3) && std::endian::native == std::endian::little &&
                 zfp_codec_version == 5,
             "arguments/host/codec");
        /* Input directory selected by the CLI. */ fs::path dir = argv[1];
        if (argc == 3) {
            need(std::string_view(argv[2]) == "--writer", "inspection mode");
            /* Completed writer-mode inspection counters. */ auto result = inspect_writer_directory(dir);
            std::cout << Json{{"writer_files", result.files},
                              {"prepared_runs", result.runs},
                              {"sections", result.sections},
                              {"frames", result.frames},
                              {"raw", result.raw},
                              {"zfp", result.zfp},
                              {"exact_values", result.values},
                              {"scope",
                               "independent actual writer bootstrap/directory/ownership/section "
                               "and frame hashes/public ZFP decode/expected raw values and axes; "
                               "trusted bounded test archives"}}
                             .dump()
                      << '\n';
            return 0;
        }

        /* Owned manifest bytes. */ auto mb = read(dir / "manifest.json");
        /* Owned parsed artifact/positive-case manifest. */ auto manifest = Json::parse(mb.begin(), mb.end());
        /* Aggregate counters across all positive fixture cases. */ Summary total;
        for (/* Manifest artifact entry with expected size and SHA-256. */ auto it = manifest.at("files").begin(); it != manifest.at("files").end(); ++it) {
            /* Owned artifact bytes being identity-checked. */ auto b = read(dir / it.key());
            need(b.size() == it.value().at("bytes").get<std::size_t>() &&
                     hex(sha(b)) == it.value().at("sha256").get<std::string>(),
                 "manifest artifact identity");
        }
        for (/* Current positive-case manifest entry. */ auto &item : manifest.at("positive")) {
            /* Owned fixture archive bytes, admitted envelope and ownership bitmap. */ File file(dir / item.at("file").get<std::string>());
            /* Owned parsed root metadata. */ auto root = file.metadata(file.root);
            file.claim(root);
            /* Owned expected-oracle JSON bytes. */ auto eb = read(dir / item.at("expected").get<std::string>());
            /* Owned parsed expected run/value descriptions. */ auto expected = Json::parse(eb.begin(), eb.end());
            /* Borrowed results index in the root metadata. */ auto &cat = root.at("results").at("index");
            /* Borrowed run metadata-section ID column. */ auto ids = file.array(cat.at("run_metadata_section"));
            need(ids.size() == expected.at("runs").size() * 8, "run count");
            for (/* Run ordinal in the expected/catalog order. */ std::size_t i = 0; i < expected.at("runs").size(); i++) {
                /* Canonical run-metadata section ID. */ auto id = le(ids.data() + i * 8, 8);
                need(id != file.root, "root/run alias");
                /* Owned parsed run metadata with array sections claimed. */ auto run = file.metadata(id);
                file.claim(run);
                /* Expected description for the current run. */ auto &x = expected.at("runs")[i];
                need(file.pool(cat.at("run_id"), i) == x.at("run_id").get<std::string>() &&
                         run.at("info").at("run_id") == x.at("run_id"),
                     "run ID");
                if (x.contains("catalog_state")) {
                    need(run.size() == 1 && run.at("info").at("catalog_state") == 2,
                         "unavailable info-only");
                    continue;
                }
                /* Filesystem stem of this run's independent sidecars. */ auto prefix = dir / x.at("sidecar").get<std::string>();
                /* Borrowed encoded frame index and payload extents. */ auto index = file.array(run.at("values").at("frame_index")),
                     payload = file.array(run.at("values").at("payload"));
                same(index, read(prefix.string() + ".index.bin"), "actual index sidecar");
                same(payload, read(prefix.string() + ".payload.bin"), "actual payload sidecar");
                /* Whether the expected run uses time/step rather than load/level axes. */ bool transient = x.at("kind") == 0;
                same(file.array(run.at(transient ? "time" : "load_levels")),
                     read(prefix.string() + ".axis.bin"), "actual axis bits");
                same(file.array(run.at(transient ? "step_index" : "level_index")),
                     read(prefix.string() + ".identity.bin"), "actual identity bytes");
                /* Declared matrix shape compared with the independent oracle. */ auto sh = run.at("values").at("shape");
                need(sh == x.at("shape"), "logical shape");
                /* Owned exact binary64 value oracle bytes. */ auto gold = read(prefix.string() + ".expected.bin");
                frames(index, payload, gold, sh[0].get<std::uint64_t>(), sh[1].get<std::uint64_t>(),
                       total);
                total.runs++;
            }
            for (/* Claim bit proving a section is not an orphan. */ bool marked : file.owned)
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
    } catch (/* Inspection exception reported with failing process status. */ const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
