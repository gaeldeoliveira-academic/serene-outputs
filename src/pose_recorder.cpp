#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <limits>
#include <nlohmann/json.hpp>
#include <serene/outputs/pose_recorder.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
namespace serene::outputs::pose {
namespace {
/// Absolute v1 catalog text admission ceiling, independent of caller Options.
constexpr std::uint64_t catalog_limit = 16 * 1024 * 1024;
/// Absolute raw chunk workspace ceiling in bytes.
constexpr std::uint64_t chunk_limit = 64 * 1024 * 1024;
/// Maximum catalog sample population; each pose contributes seven binary64 values.
constexpr std::uint64_t sample_limit = 1000000;
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
using Json = nlohmann::json;
/// Write unsigned x into borrowed p[0..2) in canonical little-endian order.
void u16(std::byte *p, std::uint16_t x) noexcept {
    // Emit the 2 bytes without host-layout or alignment assumptions.

    for (unsigned i /* Little-endian16-bit byte position. */ = 0; i < 2; ++i)
        p[i] = std::byte((x >> (8 * i)) & 255);
}
/// Write unsigned x into borrowed p[0..4) in canonical little-endian order.
void u32(std::byte *p, std::uint32_t x) noexcept {
    // Emit the 4 bytes without host-layout or alignment assumptions.

    for (unsigned i /* Little-endian32-bit byte position. */ = 0; i < 4; ++i)
        p[i] = std::byte((x >> (8 * i)) & 255);
}
/// Write unsigned x into borrowed p[0..8) in canonical little-endian order.
void u64(std::byte *p, std::uint64_t x) noexcept {
    // Emit the 8 bytes without host-layout or alignment assumptions.

    for (unsigned i /* Little-endian64-bit byte position. */ = 0; i < 8; ++i)
        p[i] = std::byte((x >> (8 * i)) & 255);
}
/// Write binary64 x bit-for-bit into borrowed eight-byte destination p.
void f64(std::byte *p, double x) noexcept {
    // Bit-cast the floating representation and reuse canonical integer encoding.

    u64(p, std::bit_cast<std::uint64_t>(x));
}
/// Return the owned compile-time reflected CRC-32 lookup table used by sidecar checksums.
constexpr auto crc_table() {
    // For each byte seed, apply the reflected polynomial over eight bit shifts.

    /* Owned CRC lookup table under construction. */ std::array<std::uint32_t, 256> a{};
    for (std::uint32_t i /* CRC lookup byte seed. */ = 0; i < 256; ++i) {
        /* Current reflected CRC remainder for this byte seed. */ auto c = i;
        for (unsigned k /* Reflected polynomial bit iteration. */ = 0; k < 8; ++k)
            c = (c >> 1) ^ ((c & 1) ? 0xedb88320U : 0U);
        a[i] = c;
    }
    return a;
}
/// Immutable lookup table shared by catalog/chunk/footer CRC calculations.
constexpr auto crc_values = crc_table();
/// Compute CRC-32 over borrowed data, returning the complemented checksum without retaining input.
std::uint32_t crc(std::span<const std::byte> data) noexcept {
    // Start with all bits set, update through the lookup table for each byte, then complement.

    /* Incremental complemented CRC state. */ std::uint32_t c = 0xffffffffU;
    for (auto b /* Current raw byte contributing to CRC. */ : data)
        c = crc_values[(c ^ std::to_integer<unsigned>(b)) & 255] ^ (c >> 8);
    return c ^ 0xffffffffU;
}
/// Return whether JSON j is an unsigned integer admitted by the signed-extent wire ceiling.
bool uint(const Json &j) {
    // Check JSON numeric type before extracting and bounding the u64 value.

    return j.is_number_unsigned() && j.get<std::uint64_t>() <= INT64_MAX;
}
/// Read admitted unsigned ID j or throw invalid_argument; callers translate setup failures to Status.
std::uint64_t id(const Json &j) {
    // Reuse unsigned admission before extracting the exact integer identity.

    if (!uint(j))
        throw std::invalid_argument("unsigned id");
    return j.get<std::uint64_t>();
}
/// Return whether JSON j is a nonempty string; parsing already established valid JSON text.
bool text(const Json &j) {
    // Inspect string type and borrowed decoded length without copying.

    return j.is_string() && !j.get_ref<const std::string &>().empty();
}
/// Validate decoded catalog j and set output count to its sample population only on success.
/// Used during Recorder::open; temporary graph/maps belong to setup and are never retained.
bool catalog_valid(const Json &j, std::uint64_t &count) {
    // Check wire identity/frame convention, index unique occurrences, detect parent cycles,
    // validate samples and segment membership, then resolve aliases/connections and return sample count.

    if (!j.is_object() || !uint(j.at("catalog_version")) || j.at("catalog_version") != 1)
        return false;
    for (auto k /* Required catalog identity key. */ : {"recording_id", "run_id", "preparation_id"})
        if (!text(j.at(k)))
            return false;
    /* Borrowed declared global coordinate convention. */ const auto &w = j.at("world");
    if (w.at("axes") != "right_handed_xyz" || w.at("length_unit") != "m" ||
        w.at("orientation") != "hamilton_wxyz_local_to_world")
        return false;
    /* Borrowed occurrence graph array. */ const auto &occ = j.at("occurrences");
    /* Borrowed authoritative sample order. */ const auto &samples = j.at("samples");
    if (!occ.is_array() || !samples.is_array() || samples.size() > sample_limit)
        return false;
    /* Setup-only ID-to-occurrence-position index. */ std::unordered_map<std::uint64_t, std::size_t> owners;
    /* Setup-only unique occurrence path spellings. */ std::set<std::string> paths;
    for (std::size_t i /* Occurrence-catalog ordinal for unique ID/parent indexing. */ = 0; i < occ.size(); ++i) {
        /* Current borrowed occurrence descriptor. */ const auto &o = occ[i];
        /* Exact source occurrence ID. */ const auto key = id(o.at("id"));
        if (!owners.emplace(key, i).second || !text(o.at("path")) ||
            !paths.insert(o.at("path").get<std::string>()).second)
            return false;
        /* Declared occurrence family. */ const auto &kind = o.at("kind");
        if (kind != "assembly" && kind != "flexible" && kind != "rigid" && kind != "anchor")
            return false;
    }
    // Validate every parent chain before samples can refer to the occurrence map.
    // Linear-time cycle validation; temporary setup-only storage.
    /* Setup graph colors: unseen, active parent chain, completed. */ std::vector<unsigned char> colors(occ.size());
    for (std::size_t start /* Starting occurrence for an independent parent-chain cycle check. */ = 0; start < occ.size(); ++start) {
        /* Current occurrence in this parent-chain traversal. */ auto current = start;
        while (colors[current] == 0) {
            colors[current] = 1;
            /* Borrowed nullable parent ID. */ const auto &p = occ[current].at("parent_id");
            if (p.is_null())
                break;
            /* Parent lookup in the validated unique ID map. */ auto it = owners.find(id(p));
            if (it == owners.end())
                return false;
            current = it->second;
            if (colors[current] == 1)
                return false;
        }
        current = start;
        while (colors[current] == 1) {
            colors[current] = 2;
            /* Borrowed nullable parent ID. */ const auto &p = occ[current].at("parent_id");
            if (p.is_null())
                break;
            current = owners.at(id(p));
        }
    }
    /* Seen occurrence/port-name pairs. */ std::set<std::pair<std::uint64_t, std::string>> ports;
    for (std::size_t i /* Sample-catalog ordinal, matching every frame pose block. */ = 0; i < samples.size(); ++i) {
        /* Borrowed sample descriptor in authoritative order. */ const auto &s = samples[i];
        /* Exact owning occurrence ID. */ const auto owner = id(s.at("occurrence_id"));
        if (id(s.at("id")) != i || !owners.contains(owner))
            return false;
        /* Structural-node versus physical-port sample family. */ const auto &kind = s.at("kind");
        if (kind == "structural_node") {
            if (!s.at("material_coordinate_m").is_number() ||
                !std::isfinite(s.at("material_coordinate_m").get<double>()))
                return false;
            /* Explicit material station side at a discontinuity. */ const auto &side = s.at("side");
            if (side != "none" && side != "left" && side != "right")
                return false;
        } else if (kind == "body_port" || kind == "anchor_port") {
            if (!text(s.at("port_id")) ||
                !ports.emplace(owner, s.at("port_id").get<std::string>()).second)
                return false;
            /* Resolved sample owner family for anchor-port consistency. */ const auto &owner_kind = occ[owners.at(owner)].at("kind");
            if ((kind == "anchor_port") != (owner_kind == "anchor"))
                return false;
        } else
            return false;
    }
    // Validate visual segment membership without creating physical/mechanical connectivity.
    /* Borrowed visual structural segment descriptors. */ const auto &seg = j.at("segments");
    if (!seg.is_array())
        return false;
    /* Seen segment IDs. */ std::set<std::uint64_t> segments;
    for (const auto &s /* Borrowed visual segment under validation. */ : seg) {
        if (!segments.insert(id(s.at("id"))).second)
            return false;
        /* Exact owning occurrence ID. */ const auto owner = id(s.at("occurrence_id"));
        if (!owners.contains(owner))
            return false;
        /* Borrowed ordered samples joined by this visual segment. */ const auto &ids = s.at("sample_ids");
        if (!ids.is_array())
            return false;
        /* Sample IDs already used within this segment. */ std::set<std::uint64_t> seen;
        for (const auto &n /* Borrowed sample ID in this segment membership list. */ : ids) {
            /* Exact segment sample reference. */ const auto v = id(n);
            if (v >= samples.size() || !seen.insert(v).second ||
                samples[v].at("kind") != "structural_node" ||
                id(samples[v].at("occurrence_id")) != owner)
                return false;
        }
    }
    // Return whether borrowed JSON v names an existing non-structural port sample.
    auto port_ref = [&](const Json &v) {
        // Admit its exact ID, then check sample range/family.

        /* Exact sample reference checked as a physical port. */ const auto n = id(v);
        return n < samples.size() && samples[n].at("kind") != "structural_node";
    };
    // Validate aliases and connection endpoints against the admitted physical-port samples.
    /* Borrowed public-interface alias descriptors. */ const auto &aliases = j.at("port_aliases");
    if (!aliases.is_array())
        return false;
    /* Seen scope/alias pairs. */ std::set<std::pair<std::uint64_t, std::string>> alias_names;
    for (const auto &a /* Borrowed scoped alias pointing to a physical port sample. */ : aliases) {
        /* Exact alias owner occurrence ID. */ const auto scope = id(a.at("scope_id"));
        if (!owners.contains(scope) || !text(a.at("alias")) || !port_ref(a.at("sample_id")) ||
            !alias_names.emplace(scope, a.at("alias").get<std::string>()).second)
            return false;
    }
    /* Borrowed visual connection descriptors. */ const auto &conn = j.at("connections");
    if (!conn.is_array())
        return false;
    /* Seen connection IDs. */ std::set<std::uint64_t> connections;
    for (const auto &c /* Borrowed visual connection and its endpoint references. */ : conn) {
        if (!connections.insert(id(c.at("id"))).second || !owners.contains(id(c.at("scope_id"))) ||
            !text(c.at("kind")) || !text(c.at("first_endpoint_id")) ||
            !text(c.at("second_endpoint_id")) || !port_ref(c.at("first_sample_id")) ||
            !port_ref(c.at("second_sample_id")) ||
            !(c.at("junction_id").is_null() || text(c.at("junction_id"))))
            return false;
    }
    count = samples.size();
    return true;
}
/// Setup-derived immutable winch binding owned by the recorder after catalog admission.
/// Keeps only the Core generation and canonical A/B/D sample indices; JSON and strings
/// remain setup-only. One three-index row is retained per admitted arrangement.
struct WinchCatalog {
    std::uint64_t generation{}; ///< Owning Core context generation, distinct from capture preparation_id.
    std::vector<std::array<std::uint64_t, 3>> ports; ///< Dense arrangement order: physical samples A, B and D.
};
/// Validate optional winch topology in already admitted catalog j; assign out only on success.
/// Throws on malformed JSON accesses; Recorder::open maps those setup failures to invalid_contract.
bool winch_catalog_valid(const Json &j, WinchCatalog &out) {
    // Omitted descriptor preserves the feature-free path. Otherwise validate identifiers,
    // canonical owner/force keys and role/edge topology, then retain only the physical bindings.
    if (!j.contains("winch_geometry"))
        return true;
    /* Borrowed extension descriptor; required fields carry exact integer/string semantics. */ const auto &w = j.at("winch_geometry");
    if (!w.is_object() || id(w.at("layout_version")) != 1 || id(w.at("kind")) != 4 ||
        id(w.at("stream_id")) != 2 || w.at("encoding") != "raw_f64_le")
        return false;
    /* Setup candidate committed to the caller only after all arrangements validate. */ WinchCatalog candidate;
    candidate.generation = id(w.at("source_preparation_generation"));
    /* Borrowed dense arrangement population. */ const auto &arrangements = w.at("arrangements");
    if (!candidate.generation || !arrangements.is_array() || arrangements.empty() || arrangements.size() > sample_limit)
        return false;
    /* Borrowed physical catalog previously validated by catalog_valid. */ const auto &samples = j.at("samples");
    /* Setup-only exact occurrence-ID map used to resolve owner paths without per-frame strings. */ std::unordered_map<std::uint64_t, const Json *> owners;
    for (const auto &o /* Existing occurrence under its exact integer ID. */ : j.at("occurrences"))
        owners.emplace(id(o.at("id")), &o);
    /* Unique owner/force identities; repeated local force IDs in different occurrences are valid. */ std::set<std::pair<std::uint64_t, std::string>> identities;
    if (arrangements.size() > candidate.ports.max_size())
        return false;
    candidate.ports.reserve(arrangements.size());
    for (std::size_t ordinal /* Dense immutable payload index. */ = 0; ordinal < arrangements.size(); ++ordinal) {
        /* Borrowed arrangement descriptor being checked. */ const auto &a = arrangements[ordinal];
        /* Exact owning assembly occurrence. */ const auto owner = id(a.at("owner_occurrence_id"));
        /* Setup lookup, never retained after open. */ const auto found = owners.find(owner);
        if (id(a.at("index")) != ordinal || found == owners.end() || found->second->at("kind") != "assembly" ||
            a.at("feature_type") != "winch_with_saddle" || !text(a.at("force_id")) || !text(a.at("key")))
            return false;
        /* Exact UTF-8 bytes from the already parsed strings; size() is byte count. */ const auto &path = found->second->at("path").get_ref<const std::string &>();
        /* Local force spelling is not normalized or escaped by the identity encoder. */ const auto &force = a.at("force_id").get_ref<const std::string &>();
        if (path.find('\0') != std::string::npos || force.find('\0') != std::string::npos ||
            !identities.emplace(owner, force).second)
            return false;
        /* Redundant canonical key, computed only during inactive admission. */ const auto key =
            "winch_with_saddle.v1.o" + std::to_string(path.size()) + "_" + path + ".f" + std::to_string(force.size()) + "_" + force;
        if (a.at("key") != key)
            return false;
        /* Four fixed roles and three visual edges, separate from structural connections. */ const auto &points = a.at("points");
        /* Borrowed ordered visual connectivity. */ const auto &edges = a.at("edges");
        if (!points.is_array() || points.size() != 4 || !edges.is_array() || edges.size() != 3)
            return false;
        /* Stable role names and their physical-source positions. */ constexpr std::array<const char *, 4> roles{"A", "B", "C", "D"};
        /* Exact physical sample references retained in the bounded setup table. */ std::array<std::uint64_t, 3> refs{};
        for (std::size_t role /* Point-role ordinal, including algebraic C. */ = 0; role < 4; ++role) {
            /* Current role descriptor; C deliberately has no structural identity. */ const auto &point = points[role];
            if (point.at("role") != roles[role])
                return false;
            if (role == 2) {
                if (point.size() != 3 || point.at("kind") != "algebraic" || point.at("feature") != "pulley")
                    return false;
                continue;
            }
            /* Canonical A/B/D source index; this is not a new physical identity. */ const auto sample = id(point.at("sample_id"));
            if (point.at("kind") != "physical_port" || sample >= samples.size() ||
                samples[sample].at("kind") != (role == 3 ? "anchor_port" : "body_port"))
                return false;
            refs[role == 3 ? 2 : role] = sample;
        }
        if (refs[0] == refs[1] || samples[refs[0]].at("occurrence_id") != samples[refs[1]].at("occurrence_id"))
            return false;
        /* Exact local point connectivity A-C, C-B and C-D. */ constexpr std::array<std::array<std::uint64_t, 2>, 3> ends{{{0, 2}, {2, 1}, {2, 3}}};
        for (std::size_t edge /* Ordered edge ordinal and expected visual family. */ = 0; edge < 3; ++edge) {
            /* Borrowed endpoint indices, never interpreted as structural samples. */ const auto &pair = edges[edge].at("points");
            if (!pair.is_array() || pair.size() != 2 || id(pair[0]) != ends[edge][0] || id(pair[1]) != ends[edge][1] ||
                edges[edge].at("kind") != (edge == 2 ? "main_cable" : "bridle"))
                return false;
        }
        candidate.ports.push_back(refs);
    }
    out = std::move(candidate);
    return true;
}
/// Compare the exact translated wire stamp fields, preserving binary64 sign/representation.
bool same_stamp(const Stamp &a, const Stamp &b) noexcept {
    // Check enum/integer identities directly and time/factor through their exact encoded bits.
    return a.analysis == b.analysis && a.events == b.events && a.state_id == b.state_id &&
           std::bit_cast<std::uint64_t>(a.time) == std::bit_cast<std::uint64_t>(b.time) &&
           std::bit_cast<std::uint64_t>(a.load_factor) == std::bit_cast<std::uint64_t>(b.load_factor);
}
/// Return whether source phase maps to translated wire stamp s under the accepted lifecycle.
bool phase_valid(WinchSourcePhase phase, const Stamp &s) noexcept {
    // Separate initialized diagnostics, static certification and the two transient packet phases.
    switch (phase) {
    case WinchSourcePhase::initialized:
        return s.analysis == Analysis::prepared && s.events == Event::prepared && s.state_id == 0 && s.load_factor == 0;
    case WinchSourcePhase::static_certified:
        return s.analysis == Analysis::static_equilibrium && s.events == Event::target;
    case WinchSourcePhase::transient_initial:
        return s.analysis == Analysis::transient && (s.events & Event::initial) && s.state_id == 0 && s.load_factor == 0;
    case WinchSourcePhase::transient_accepted:
        return s.analysis == Analysis::transient && !(s.events & Event::initial) && s.state_id > 0 && s.load_factor == 0;
    }
    return false;
}
/// Check finite point f against finite physical point p without changing either representation.
/// The fixed 64-epsilon bound is representation consistency, never solver tolerance or an error guarantee.
bool point_consistent(const double *f, const double *p) noexcept {
    // Derive a finite infinity-norm scale in metres, then compare each unchanged component.
    /* Scale has a one-metre floor and cannot overflow for finite components. */ double scale = 1.0;
    for (std::size_t axis /* Cartesian component used to form the common point scale. */ = 0; axis < 3; ++axis)
        scale = std::max({scale, std::abs(f[axis]), std::abs(p[axis])});
    /* 64*2^-52 is exact binary64; multiplication remains finite even at the largest finite coordinate. */ const double bound = 0x1p-46 * scale;
    for (std::size_t axis /* Cartesian component tested with inclusive consistency semantics. */ = 0; axis < 3; ++axis)
        if (std::abs(f[axis] - p[axis]) > bound)
            return false;
    return true;
}
/// Admit candidate Stamp s relative to optional borrowed previous stamp; return false on invalid lifecycle/order.
/// A prepared snapshot is not accepted dynamics or equilibrium; target terminates further frames.
bool stamp_valid(const Stamp &s, const Stamp *previous) noexcept {
    // Check finite scalar identity and terminal target, distinguish prepared/transient/static states,
    // then enforce event combinations and strictly increasing transient accepted identity/time.

    if (!std::isfinite(s.time) || !std::isfinite(s.load_factor) || s.state_id > INT64_MAX)
        return false;
    if (previous && (previous->events & Event::target))
        return false;
    if (s.analysis == Analysis::prepared)
        return !previous && s.events == Event::prepared && s.state_id == 0 && s.load_factor == 0;
    if (s.analysis == Analysis::transient) {
        if (!s.events || (s.events & ~14U) || s.load_factor != 0)
            return false;
        if (previous && previous->analysis == Analysis::static_equilibrium)
            return false;
        if (s.events & Event::initial)
            if (s.state_id != 0 || (previous && previous->analysis == Analysis::transient))
                return false;
        if (previous && previous->analysis == Analysis::transient &&
            (s.state_id <= previous->state_id || s.time <= previous->time))
            return false;
        return true;
    }
    return s.analysis == Analysis::static_equilibrium && s.events == Event::target &&
           (!previous || previous->analysis == Analysis::prepared);
}
} // namespace
/// Unique pose-recorder state owned by Recorder; used synchronously by core capture adapters.
/// Owns a FILE, bounded raw batches and path strings, with only constant-size linked-chunk index
/// state. The winch A/B/D reference table is fixed by admitted catalog size; JSON is setup-only.
/// Borrowed fault context must outlive this owner.
/// Destruction closes any stream but deliberately retains unsuccessful .partial evidence.
struct Recorder::Impl {
    std::filesystem::path destination, ///< Owned no-clobber destination path.
        partial; ///< Owned sibling path retained on unsuccessful recording.
    std::FILE *file{}; ///< Owned unbuffered stdio stream, null after checked close.
    State state{State::open}; ///< Open, published or terminal failed lifecycle.
    Status result{}; ///< First latched failure; default success while open/published.
    FaultHook fault{}; ///< Copied fault callbacks borrowing caller-owned context.
    Statistics stats{}; ///< Constant-size accepted/persisted/buffered accounting.
    std::vector<std::byte> batch; ///< Owned fixed-capacity canonical raw frame batch.
    std::vector<std::byte> feature_batch; ///< Optional fixed-capacity feature batch, charged together with batch.
    WinchCatalog winches; ///< Immutable Core generation and canonical A/B/D sample references, empty when disabled.
    std::uint64_t samples{}, ///< Fixed catalog sample count per frame.
        frame_bytes{}, ///< Complete raw frame bytes, including 64-byte identity prefix.
        feature_frame_bytes{}, ///< Optional raw feature frame bytes, zero when the stream is absent.
        first_pose{}, ///< File offset of first pose chunk, or zero before any chunk.
        last_pose{}, ///< File offset of previous pose chunk for backward linkage.
        sequence{}, ///< Consecutive next chunk ordinal, including terminal metadata chunks.
        io_calls{}; ///< Global deterministic fault-operation ordinal.
    Stamp last_stamp{}; ///< Last accepted source state for lifecycle/order admission.
    bool have_stamp{}; ///< Whether last_stamp is meaningful.
    /// Close any still-owned stream; never publish or erase the retained partial recording.
    ~Impl() {
        // Best-effort close only the stream still owned by this implementation.

        if (file)
            std::fclose(file);
    }
    /// Latch code/op plus current errno on first failure and return the retained Status.
    Status stop(StatusCode code, std::uint32_t op) noexcept {
        // Preserve the first error, transition to failed once, and return it on subsequent failures.

        if (state != State::failed) {
            result = error(code, op);
            result.native_code = errno;
            state = State::failed;
        }
        return result;
    }
    /// Call the optional fault hook for op with the next global ordinal; return whether it requests failure.
    bool injected(IoOperation op) noexcept {
        // Advance operation accounting even without a hook, then invoke borrowed fault state.

        /* Global operation ordinal passed to the fault hook. */ const auto ordinal = io_calls++;
        return fault.fail && fault.fail(fault.context, op, ordinal);
    }
    /// Write the complete borrowed data span or latch failure; no input bytes are retained here.
    bool write(std::span<const std::byte> data) noexcept {
        // Apply the fault/write cap, check total extent, write once, account actual bytes and reject any short write.

        if (injected(IoOperation::write)) {
            stop(StatusCode::io_failure, 1);
            return false;
        }
        /* Physical write count after optional short-write injection. */ auto n = data.size();
        if (fault.write_limit)
            n = std::min(n, fault.write_limit(fault.context, io_calls - 1, n));
        if (stats.written_bytes > INT64_MAX - data.size()) {
            stop(StatusCode::resource_limit, 1);
            return false;
        }
        /* Actual bytes accepted by stdio. */ const auto done = std::fwrite(data.data(), 1, n, file);
        stats.written_bytes += done;
        if (done != data.size()) {
            stop(StatusCode::io_failure, 1);
            return false;
        }
        return true;
    }
    /// Check stdio flush of the owned stream, with deterministic fault injection; return success.
    bool flush() noexcept {
        // Invoke the flush seam before fflush and latch any I/O failure; this is not fsync durability.

        if (injected(IoOperation::flush) || std::fflush(file) != 0) {
            stop(StatusCode::io_failure, 2);
            return false;
        }
        return true;
    }
    /// Write one chunk of kind with records/first identity, borrowed payload and optional previous chunk offset.
    /// Return success only after header/payload and checked stdio flush; increment sequence then.
    bool chunk(std::uint16_t kind, std::uint64_t records, std::uint64_t first,
               std::span<const std::byte> payload, std::uint64_t previous = 0) noexcept {
        // Build the fixed header, CRC payload/header, write both, flush, then advance the chunk ordinal.

        if (sequence > INT64_MAX) {
            stop(StatusCode::resource_limit, 1);
            return false;
        }
        /* Canonical zero-initialized chunk header, including reserved fields. */ std::array<std::byte, 80> h{};
        h[0] = std::byte{'P'};
        h[1] = std::byte{'C'};
        h[2] = std::byte{'H'};
        h[3] = std::byte{'K'};
        u16(h.data() + 4, kind);
        u16(h.data() + 6, kind == 4 ? 0 : 1);
        u32(h.data() + 8, 80);
        u32(h.data() + 16, kind == 4 ? 2 : (kind == 1 ? 1 : 0));
        u32(h.data() + 20, 1);
        u64(h.data() + 24, sequence);
        u64(h.data() + 32, records);
        u64(h.data() + 40, first);
        u64(h.data() + 48, payload.size());
        u64(h.data() + 56, payload.size());
        u32(h.data() + 64, crc(payload));
        u64(h.data() + 72, previous);
        u32(h.data() + 68, crc(h));
        if (!write(h) || !write(payload) || !flush())
            return false;
        ++sequence;
        return true;
    }
    /// Persist buffered frames as an optional feature chunk followed by its matching linked pose chunk.
    /// A failed or truncated pair does not advance persisted_frames; empty buffers are a no-op.
    bool flush_batch() noexcept {
        // Write the optional feature batch first, then its structural partner. Advance the
        // unchanged structural index and paired persisted count only after both flushes succeed.

        if (!stats.buffered_frames)
            return true;
        if (feature_frame_bytes &&
            !chunk(4, stats.buffered_frames, stats.persisted_frames,
                   std::span<const std::byte>(feature_batch.data(),
                       static_cast<std::size_t>(stats.buffered_frames * feature_frame_bytes))))
            return false;
        /* Starting file offset of the next complete pose chunk. */ const auto offset = stats.written_bytes;
        if (!chunk(1, stats.buffered_frames, stats.persisted_frames,
                   std::span<const std::byte>(
                       batch.data(), static_cast<std::size_t>(stats.buffered_frames * frame_bytes)),
                   last_pose))
            return false;
        if (!stats.pose_chunks)
            first_pose = offset;
        last_pose = offset;
        ++stats.pose_chunks;
        stats.persisted_frames += stats.buffered_frames;
        stats.buffered_frames = 0;
        return true;
    }
    /// Admit one complete borrowed packet, then copy it into the setup-sized batches.
    /// feature is null only for a feature-free catalog; no caller storage survives this call.
    Status append(const Stamp &s, std::span<const double> values, const WinchFrameView *feature) noexcept {
        // Validate lifecycle, full structural population and optional exact source binding.
        // Check every finite feature/status and A/B/D representation bound before copying either population.
        // Encode unchanged input bits, advance logical accepted counters, then synchronously flush a full pair.
        if (state != State::open)
            return state == State::failed ? result : error(StatusCode::invalid_state, 11);
        if (bool(feature) != !winches.ports.empty() || values.size() != samples * 7 ||
            !stamp_valid(s, have_stamp ? &last_stamp : nullptr))
            return stop(StatusCode::invalid_contract, 11);
        for (std::size_t i /* Beginning of the current physical position/quaternion block. */ = 0; i < values.size(); i += 7) {
            for (std::size_t k /* Finite scalar component within this physical pose. */ = 0; k < 7; ++k)
                if (!std::isfinite(values[i + k]))
                    return stop(StatusCode::invalid_contract, 11);
            /* Quaternion norm is validated without changing its supplied bits. */ double norm = 0;
            for (std::size_t k /* Quaternion component accumulated into squared norm. */ = 3; k < 7; ++k)
                norm += values[i + k] * values[i + k];
            if (!std::isfinite(norm) || std::abs(norm - 1) > 1e-6)
                return stop(StatusCode::invalid_contract, 11);
        }
        if (feature) {
            if (feature->geometries.size() != winches.ports.size() ||
                feature->preparation_generation != winches.generation || !same_stamp(s, feature->stamp) ||
                !phase_valid(feature->phase, s))
                return stop(StatusCode::invalid_contract, 11);
            for (std::size_t row /* Immutable arrangement ordinal used by the reference table. */ = 0; row < feature->geometries.size(); ++row) {
                /* Borrowed accepted arrangement, never modified to match physical samples. */ const auto &geometry = feature->geometries[row];
                if (static_cast<std::uint32_t>(geometry.geometry) > 1)
                    return stop(StatusCode::invalid_contract, 11);
                for (const double value /* Accepted feature coordinate required to be finite. */ : geometry.world_xyz_m)
                    if (!std::isfinite(value))
                        return stop(StatusCode::invalid_contract, 11);
                for (std::size_t role /* Retained A/B/D reference-table column. */ = 0; role < 3; ++role) {
                    /* A/B occupy point0/1; D occupies point3, leaving algebraic C independent. */ const auto point = role == 2 ? 3 : role;
                    if (!point_consistent(geometry.world_xyz_m.data() + point * 3,
                                          values.data() + winches.ports[row][role] * 7))
                        return stop(StatusCode::invalid_contract, 11);
                }
            }
        }
        if (stats.accepted_frames >= INT64_MAX)
            return stop(StatusCode::resource_limit, 11);
        // Serialize only after all packet populations and exact source facts pass admission.
        /* Next bounded structural slot, including its canonical copied stamp. */ auto *b = batch.data() + stats.buffered_frames * frame_bytes;
        std::fill_n(b, 64, std::byte{});
        u64(b, stats.accepted_frames);
        u32(b + 8, static_cast<std::uint32_t>(s.analysis));
        u32(b + 12, s.events);
        f64(b + 16, s.time);
        u64(b + 24, s.state_id);
        f64(b + 32, s.load_factor);
        u64(b + 40, samples);
        for (std::size_t k /* Physical scalar ordinal encoded without reinterpretation. */ = 0; k < values.size(); ++k)
            f64(b + 64 + k * 8, values[k]);
        if (feature) {
            /* Corresponding bounded feature slot; same stamp, distinct accepted geometry. */ auto *f = feature_batch.data() + stats.buffered_frames * feature_frame_bytes;
            std::copy_n(b, 64, f);
            u64(f + 64, feature->preparation_generation);
            u32(f + 72, static_cast<std::uint32_t>(feature->phase));
            u32(f + 76, 0);
            for (std::size_t row /* Dense arrangement ordinal in the raw feature payload. */ = 0; row < feature->geometries.size(); ++row) {
                /* Borrowed accepted values, copied without averaging or normalizing. */ const auto &geometry = feature->geometries[row];
                /* Output entry's status/reserved prefix, followed by twelve XYZ scalars. */ auto *entry = f + 80 + row * 104;
                u32(entry, static_cast<std::uint32_t>(geometry.geometry));
                u32(entry + 4, 0);
                for (std::size_t scalar /* Axyz/Bxyz/Cxyz/Dxyz component order. */ = 0; scalar < 12; ++scalar)
                    f64(entry + 8 + scalar * 8, geometry.world_xyz_m[scalar]);
            }
        }
        ++stats.accepted_frames;
        ++stats.buffered_frames;
        last_stamp = s;
        have_stamp = true;
        if (stats.buffered_frames == stats.effective_batch_frames && !flush_batch())
            return result;
        return {};
    }
};
Recorder::Recorder() noexcept = default;
Recorder::~Recorder() = default;
Recorder::Recorder(Recorder &&) noexcept = default;
Recorder &Recorder::operator=(Recorder &&) noexcept = default;
std::expected<Recorder, Status> Recorder::open(const std::filesystem::path &path,
                                               std::string_view catalog, Options opts,
                                               FaultHook fault) {
    // Admit options/catalog text, parse and validate its reference graph, derive bounded batch
    // capacity, open an exclusive partial stream, then write/flush header and exact catalog bytes.

    try {
        if (path.empty() || catalog.empty() || catalog.size() > catalog_limit ||
            catalog.size() > opts.max_catalog_bytes || opts.max_batch_frames == 0 ||
            opts.max_batch_frames > INT64_MAX || opts.max_batch_bytes < 64 ||
            opts.max_batch_bytes > chunk_limit)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        if (catalog.size() >= 3 && static_cast<unsigned char>(catalog[0]) == 0xef &&
            static_cast<unsigned char>(catalog[1]) == 0xbb &&
            static_cast<unsigned char>(catalog[2]) == 0xbf)
            return std::unexpected(error(StatusCode::invalid_contract, 10));
        /* Output sample count from catalog admission. */ std::uint64_t samples{};
        /* Setup-only parsed catalog; no JSON owner remains after open. */ const auto j =
            Json::parse(catalog.begin(), catalog.end(), [](int depth, Json::parse_event_t, Json &) {
                // Enforce setup parse depth; event/value are intentionally unused.

                if (depth > 64)
                    throw std::invalid_argument("catalog depth");
                return true;
            });
        if (!catalog_valid(j, samples))
            return std::unexpected(error(StatusCode::invalid_contract, 10));
        /* Setup-derived immutable feature references, empty for the original recorder path. */ WinchCatalog winches;
        if (!winch_catalog_valid(j, winches))
            return std::unexpected(error(StatusCode::invalid_contract, 10));
        /* Arrangement count controls both fixed-width record size and retained reference storage. */ const std::uint64_t feature_count = winches.ports.size();
        if (samples > (INT64_MAX - 64) / 56 || feature_count > (INT64_MAX - 80) / 104)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        /* Complete physical frame: copied identity prefix plus seven values per sample. */ const auto frame_bytes = 64 + samples * 56;
        /* Optional feature frame: full structural stamp, source binding, then complete arrangements. */ const auto feature_frame_bytes = feature_count ? 80 + feature_count * 104 : 0;
        if (frame_bytes > INT64_MAX - feature_frame_bytes)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        /* Both encoded populations consume one common caller byte budget. */ const auto paired_frame_bytes = frame_bytes + feature_frame_bytes;
        /* Effective whole-packet capacity under both caller limits; zero rejects before opening a file. */ const auto frames = std::min(opts.max_batch_frames, opts.max_batch_bytes / paired_frame_bytes);
        if (!frames)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        // Check both products against wire/chunk/host extents before evaluating or converting them.
        /* Host vector extents and wire signed extents share this admitted byte ceiling. */ const auto storage_limit = std::min<std::uint64_t>(opts.max_batch_bytes, std::numeric_limits<std::size_t>::max());
        if (frame_bytes > storage_limit / frames ||
            (feature_frame_bytes && feature_frame_bytes > storage_limit / frames))
            return std::unexpected(error(StatusCode::resource_limit, 10));
        /* Checked structural allocation request, never duration-sized. */ const auto pose_bytes = frames * frame_bytes;
        /* Checked feature allocation request, zero for feature-free catalogs. */ const auto feature_bytes = frames * feature_frame_bytes;
        if (pose_bytes > storage_limit - feature_bytes)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        /* Private recorder candidate, released automatically on setup failure. */ Recorder out;
        out.impl_ = std::make_unique<Impl>();
        /* Candidate-owned stream/batch state. */ auto &p = *out.impl_;
        p.destination = path;
        p.partial = path;
        p.partial += ".partial";
        p.fault = fault;
        if (std::filesystem::exists(p.destination) || std::filesystem::exists(p.partial))
            return std::unexpected(error(StatusCode::publication_failure, 10));
        p.samples = samples;
        p.frame_bytes = frame_bytes;
        p.feature_frame_bytes = feature_frame_bytes;
        p.winches = std::move(winches);
        if (pose_bytes > p.batch.max_size() || feature_bytes > p.feature_batch.max_size())
            return std::unexpected(error(StatusCode::resource_limit, 10));
        p.batch.resize(static_cast<std::size_t>(pose_bytes));
        p.feature_batch.resize(static_cast<std::size_t>(feature_bytes));
        p.stats.batch_capacity_bytes = pose_bytes + feature_bytes;
        p.stats.effective_batch_frames = frames;
        p.stats.index_workspace_bytes = 256;
#ifdef _WIN32
        if (_wfopen_s(&p.file, p.partial.c_str(), L"wbx") != 0)
            return std::unexpected(error(StatusCode::io_failure, 10));
#else
        p.file = std::fopen(p.partial.c_str(), "wbx");
        if (!p.file)
            return std::unexpected(error(StatusCode::io_failure, 10));
#endif
        if (std::setvbuf(p.file, nullptr, _IONBF, 0) != 0)
            return std::unexpected(error(StatusCode::io_failure, 10));
        /* Canonical zero-initialized file header. */ std::array<std::byte, 64> h{};
        /* Fixed wire magic bytes. */ const char magic[8] = {'S', 'R', 'N', 'P', 'O', 'S', 'E', 0};
        std::copy_n(reinterpret_cast<const std::byte *>(magic), 8, h.data());
        u16(h.data() + 8, 1);
        u32(h.data() + 12, 64);
        u64(h.data() + 24, catalog.size());
        u64(h.data() + 32, samples);
        u64(h.data() + 40, opts.max_batch_bytes);
        /* Borrowed original catalog text bytes, copied unchanged. */ const auto bytes = std::as_bytes(std::span(catalog.data(), catalog.size()));
        u32(h.data() + 48, crc(bytes));
        u32(h.data() + 52, crc(h));
        if (!p.write(h) || !p.write(bytes) || !p.flush())
            return std::unexpected(p.result);
        return out;
    } catch (const std::bad_alloc &) {
        return std::unexpected(error(StatusCode::resource_limit, 10));
    } catch (const std::filesystem::filesystem_error &) {
        return std::unexpected(error(StatusCode::io_failure, 10));
    } catch (...) {
        return std::unexpected(error(StatusCode::invalid_contract, 10));
    }
}
Status Recorder::append_frame(const Stamp &s, std::span<const double> values) noexcept {
    // Reject an empty handle, then admit/copy through the common feature-free packet path.

    if (!impl_)
        return error(StatusCode::invalid_state, 11);
    return impl_->append(s, values, nullptr);
}
Status Recorder::append_frame(const Stamp &s, std::span<const double> values,
                              const WinchFrameView &winches) noexcept {
    // Reject an empty handle, then validate/copy one complete borrowed structural/feature packet.

    if (!impl_)
        return error(StatusCode::invalid_state, 11);
    return impl_->append(s, values, &winches);
}
Status Recorder::finalize(Completion result) noexcept {
    // Flush the final batch, emit linked-index summary and external outcomes, write footer,
    // check flush/close, then no-clobber publish by hard link; retain honest counters/failure state.

    if (!impl_)
        return error(StatusCode::invalid_state, 12);
    /* Exclusively borrowed recorder state for this operation. */ auto &p = *impl_;
    if (p.state != State::open)
        return p.state == State::failed ? p.result : error(StatusCode::invalid_state, 12);
    if (static_cast<unsigned>(result.run) > 3 || static_cast<unsigned>(result.capture) > 3)
        return p.stop(StatusCode::invalid_contract, 12);
    if (!p.flush_batch())
        return p.result;
    /* File offset of the terminal linked-index summary chunk. */ const auto index_offset = p.stats.written_bytes;
    /* Constant-size index summary containing first/last chunk links. */ std::array<std::byte, 32> index{};
    u64(index.data(), p.stats.pose_chunks);
    u64(index.data() + 8, p.stats.persisted_frames);
    u64(index.data() + 16, p.first_pose);
    u64(index.data() + 24, p.last_pose);
    if (!p.chunk(2, 1, 0, index))
        return p.result;
    /* File offset of external outcomes/completion facts. */ const auto completion_offset = p.stats.written_bytes;
    /* Canonical completion payload, with last-ordinal sentinel when empty. */ std::array<std::byte, 64> done{};
    u32(done.data(), static_cast<std::uint32_t>(result.run));
    u32(done.data() + 4, static_cast<std::uint32_t>(result.capture));
    u32(done.data() + 8, 1);
    u64(done.data() + 16, p.stats.accepted_frames);
    u64(done.data() + 24, p.stats.persisted_frames);
    u64(done.data() + 32, p.stats.pose_chunks);
    u64(done.data() + 40, p.stats.accepted_frames ? p.stats.accepted_frames - 1 : UINT64_MAX);
    u64(done.data() + 48, index_offset);
    if (!p.chunk(3, 1, 0, done))
        return p.result;
    /* Canonical terminal locator/footer with checksum. */ std::array<std::byte, 32> footer{};
    /* Fixed wire magic bytes. */ const char magic[8] = {'P', 'O', 'S', 'E', 'E', 'N', 'D', 0};
    std::copy_n(reinterpret_cast<const std::byte *>(magic), 8, footer.data());
    u64(footer.data() + 8, index_offset);
    u64(footer.data() + 16, completion_offset);
    u32(footer.data() + 24, crc(footer));
    if (!p.write(footer) || !p.flush())
        return p.result;
    /* Injected close failure remembered while the actual stream is still closed. */ const bool close_fault = p.injected(IoOperation::close);
    /* Native close return code, checked before publication. */ const auto closed = std::fclose(p.file);
    p.file = nullptr;
    if (close_fault || closed != 0)
        return p.stop(StatusCode::io_failure, 3);
    if (p.injected(IoOperation::publish))
        return p.stop(StatusCode::publication_failure, 4);
    try {
        /* No-throw hard-link/cleanup status; destination creation cannot replace a file. */ std::error_code ec;
        std::filesystem::create_hard_link(p.partial, p.destination, ec);
        if (ec)
            return p.stop(StatusCode::publication_failure, 4);
        p.state = State::published;
        std::filesystem::remove(p.partial,
                                ec); // Publication already succeeded; a leftover alias is harmless.
        return {};
    } catch (...) {
        return p.stop(StatusCode::publication_failure, 4);
    }
}
State Recorder::state() const noexcept {
    // Return current lifecycle or failed for an empty/moved-from handle.

    return impl_ ? impl_->state : State::failed;
}
Status Recorder::status() const noexcept {
    // Return retained result or invalid_state for an empty/moved-from handle.

    return impl_ ? impl_->result : error(StatusCode::invalid_state, 13);
}
Statistics Recorder::statistics() const noexcept {
    // Copy constant-size counters, or return zeros for an empty handle.

    return impl_ ? impl_->stats : Statistics{};
}
const std::filesystem::path &Recorder::temporary_path() const noexcept {
    // Borrow the retained partial path, or a static empty path for an empty handle.

    /* Process-lifetime empty reference for empty handles. */ static const std::filesystem::path empty;
    return impl_ ? impl_->partial : empty;
}
} // namespace serene::outputs::pose
