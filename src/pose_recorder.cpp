#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <serene/outputs/pose_recorder.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
namespace serene::outputs::pose {
namespace {
/// Absolute v1 catalog text admission ceiling, independent of caller Options.
constexpr std::uint64_t catalog_limit = 16 * 1024 * 1024;
/// Absolute raw chunk workspace ceiling in bytes.
constexpr std::uint64_t chunk_limit = 64 * 1024 * 1024;
/// Separate hard ceiling on raw batches, retained reference capacity and active scratch.
constexpr std::uint64_t workspace_limit = 128 * 1024 * 1024;
/// Fixed charged allowance for active encoding, headers and linked-index scratch.
constexpr std::uint64_t encoding_workspace = 2048;
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
/// Retain only immutable scalar F admission facts after setup catalog validation.
/// Core owns the actual station bindings; the recorder needs count/generation for each borrowed group.
struct FluidCatalog {
    std::uint64_t generation{}; ///< Positive owning Core generation, zero only when the descriptor is absent.
    std::uint64_t samples{}; ///< Dense fluid sample population, separate from base physical samples.
};
/// Validate all descriptive values in borrowed j under the F metadata rules.
/// Parsing already checked UTF-8 and depth; reject empty/NUL strings and nonfinite numeric metadata.
bool fluid_metadata_valid(const Json& j) {
    // Check scalar metadata, then recursively inspect bounded arrays/object values and their keys.
    if (j.is_string()) {
        const auto& value /* Borrowed decoded UTF-8 spelling. */ = j.get_ref<const std::string&>();
        return !value.empty() && value.find('\0') == std::string::npos;
    }
    if (j.is_number_float())
        return std::isfinite(j.get<double>());
    if (j.is_object()) {
        for (auto it /* One bounded object member and its exact key. */ = j.begin(); it != j.end(); ++it)
            if (it.key().empty() || it.key().find('\0') != std::string::npos || !fluid_metadata_valid(it.value()))
                return false;
    } else if (j.is_array()) {
        for (const auto& value /* Next bounded descriptive array value. */ : j)
            if (!fluid_metadata_valid(value))
                return false;
    }
    return true;
}
/// Admit fluid_poses in already validated base catalog j; commit scalar facts to out only on success.
/// Temporary sets/counts/membership arrays are inactive setup storage and are not retained.
/// Malformed field accesses throw to Recorder::open's invalid_contract boundary.
bool fluid_catalog_valid(const Json& j, FluidCatalog& out) {
    // Validate exact family/generation and line ownership, then sided sample addresses.
    // Check explicit increasing segments and complete sample coverage without inventing topology.
    if (!j.contains("fluid_poses"))
        return true;
    const auto& f /* Borrowed optional F descriptor. */ = j.at("fluid_poses");
    if (!f.is_object() || !fluid_metadata_valid(f) || id(f.at("kind")) != 5 ||
        id(f.at("stream_id")) != 3 || id(f.at("layout_version")) != 1 || f.at("encoding") != "raw_f64_le")
        return false;
    const auto generation /* Exact prepared Core identity. */ = id(f.at("source_preparation_generation"));
    const auto& lines /* Dense immutable F line catalog. */ = f.at("lines");
    const auto& samples /* Dense independent sided F samples. */ = f.at("samples");
    const auto& segments /* Explicit fluid continuity intervals. */ = f.at("segments");
    if (!generation || !lines.is_array() || lines.empty() || lines.size() > sample_limit ||
        !samples.is_array() || samples.empty() || samples.size() > sample_limit ||
        !segments.is_array() || segments.empty() || segments.size() > sample_limit)
        return false;
    std::unordered_map<std::uint64_t, const Json*> occurrences; // Setup-only base occurrence lookup.
    for (const auto& occurrence /* Existing physical owner; no new occurrence is manufactured. */ : j.at("occurrences"))
        occurrences.emplace(id(occurrence.at("id")), &occurrence);
    std::set<std::pair<std::uint64_t, std::string>> line_ids; // Unique occurrence/source-spanline pairs.
    for (std::size_t i /* Dense line ordinal. */ = 0; i < lines.size(); ++i) {
        const auto& line /* Current exact line descriptor. */ = lines[i];
        const auto owner /* Existing occurrence identity. */ = id(line.at("occurrence_id"));
        const auto found /* Setup-only owner lookup result. */ = occurrences.find(owner);
        if (id(line.at("id")) != i || found == occurrences.end() || found->second->at("kind") != "flexible")
            return false;
        for (const auto* field /* Required original/effective identity and admission-policy labels. */ :
             {"spanline_id", "source_geometry_fingerprint", "effective_geometry_fingerprint", "approximation_policy"})
            if (!text(line.at(field)))
                return false;
        const auto native_count /* Positive checked native address bound for this line. */ = id(line.at("native_element_count"));
        if (!native_count || native_count > sample_limit ||
            !line_ids.emplace(owner, line.at("spanline_id").get<std::string>()).second)
            return false;
    }
    std::vector<std::uint64_t> line_counts(lines.size(), 0); // Setup-only count proving each line has two samples.
    std::set<std::tuple<std::uint64_t, double, std::string>> addresses; // Exact material/side identities; signed zero coordinates compare equal.
    for (std::size_t i /* Dense F sample ordinal. */ = 0; i < samples.size(); ++i) {
        const auto& sample /* Borrowed exact sided F address. */ = samples[i];
        const auto line /* Checked dense owner line. */ = id(sample.at("line_id"));
        if (id(sample.at("id")) != i || line >= lines.size() || !sample.at("material_coordinate_m").is_number())
            return false;
        const auto coordinate /* Nonnegative original material arc in metres. */ = sample.at("material_coordinate_m").get<double>();
        const auto& side /* Resolved limiting side; none is not supported by this layout. */ = sample.at("side");
        if (!std::isfinite(coordinate) || coordinate < 0 || (side != "left" && side != "right") ||
            id(sample.at("native_element")) >= id(lines[line].at("native_element_count")) ||
            !addresses.emplace(line, coordinate, side.get<std::string>()).second)
            return false;
        ++line_counts[line];
    }
    if (std::any_of(line_counts.begin(), line_counts.end(), [](std::uint64_t count) {
            // Require the minimum explicit polyline population for every admitted line.
            return count < 2;
        }))
        return false;
    std::vector<unsigned char> used(samples.size(), 0); // Setup-only explicit segment coverage, never duration storage.
    for (std::size_t i /* Dense fluid segment ordinal. */ = 0; i < segments.size(); ++i) {
        const auto& segment /* Borrowed explicit continuous fluid interval. */ = segments[i];
        const auto line /* Its exact owner line. */ = id(segment.at("line_id"));
        const auto& refs /* Ordered independent fluid sample references. */ = segment.at("sample_ids");
        if (id(segment.at("id")) != i || line >= lines.size() || !refs.is_array() || refs.size() < 2 || refs.size() > samples.size())
            return false;
        double previous = -1; // Preceding nonnegative material coordinate, establishing strict increasing order.
        for (const auto& reference /* Next exact integer sample ID within this interval. */ : refs) {
            const auto sample /* Dense fluid sample index, independent of physical pose sample IDs. */ = id(reference);
            if (sample >= samples.size() || id(samples[sample].at("line_id")) != line)
                return false;
            const auto coordinate /* Current material arc, already checked finite/nonnegative. */ = samples[sample].at("material_coordinate_m").get<double>();
            if (coordinate <= previous)
                return false;
            previous = coordinate;
            used[sample] = 1;
        }
    }
    if (std::find(used.begin(), used.end(), 0) != used.end())
        return false;
    out = {generation, static_cast<std::uint64_t>(samples.size())};
    return true;
}
/// Scalar moment admission facts owned by the recorder after inactive catalog validation.
/// Core owns native provider bindings; all cut, break and display maps below are setup-only.
struct MomentCatalog {
    std::uint64_t generation{}; ///< Positive owning Core generation, zero when moments are absent.
    std::uint64_t cuts{}; ///< Dense signed-triple population, independent of physical and F sample counts.
};
/// Return borrowed numeric value as finite binary64 or throw invalid_argument on invalid input.
/// Used only for moment catalog coordinates/eta, never for exact integer identities.
/// Integer-valued coordinates intentionally round into the same binary64 comparison domain as Core.
double moment_number(const Json& value) {
    // Reject booleans/non-numbers before conversion, then require a finite binary64 result.
    if (!value.is_number())
        throw std::invalid_argument("moment numeric value");
    const auto number /* Original coordinate represented in the producer's binary64 domain. */ = value.get<double>();
    if (!std::isfinite(number))
        throw std::invalid_argument("moment finite value");
    return number;
}
/// Admit elastic_moments in already validated base catalog j; assign out only on complete success.
/// Validate the independent sided native addresses and explicit scalar-display mapping without
/// claiming physical continuity or provider capability. All lookup/coverage storage is setup-only.
/// Return true for absence or valid admission, false for contract defects; malformed JSON accesses
/// and numeric-domain errors throw to Recorder::open's invalid_contract boundary. No input changes.
bool moment_catalog_valid(const Json& j, MomentCatalog& out) {
    // Admit the exact family and finite/string metadata; resolve independent structural line identities.
    // Validate native address bounds, sided cuts and the complete declared break pairing.
    // Check explicit base-edge intervals, break endpoints, nonoverlap and complete cut coverage.
    if (!j.contains("elastic_moments"))
        return true;
    const auto& m /* Borrowed immutable moment descriptor. */ = j.at("elastic_moments");
    if (!m.is_object() || !fluid_metadata_valid(m) || id(m.at("kind")) != 7 ||
        id(m.at("stream_id")) != 5 || id(m.at("layout_version")) != 1 ||
        m.at("encoding") != "raw_f64_le" || m.at("quantity") != "elastic_section_moment" ||
        m.at("unit") != "N.m" || m.at("expression_frame") != "current_structural_N" ||
        m.at("moment_point") != "current_structural_N" ||
        m.at("action_convention") != "positive_span_on_negative_span" ||
        m.at("display_interpolation") != "linear_scalar_on_declared_base_edge")
        return false;
    const auto& components /* Closed signed component order, never a norm-only product. */ = m.at("components");
    if (!components.is_array() || components.size() != 3 || components[0] != "Mx" ||
        components[1] != "My" || components[2] != "Mz")
        return false;
    const auto generation /* Common prepared source lifetime. */ = id(m.at("source_preparation_generation"));
    const auto& lines /* Dense independent structural material line declarations. */ = m.at("lines");
    const auto& providers /* Dense selected native element/provider addresses. */ = m.at("provider_addresses");
    const auto& cuts /* Dense payload order, including independent opposite sides. */ = m.at("cuts");
    const auto& pairs /* Explicit physical/display discontinuity pairs; may be empty. */ = m.at("sided_breaks");
    const auto& intervals /* Explicit scalar mappings onto existing structural edges. */ = m.at("display_intervals");
    if (!generation || !lines.is_array() || lines.empty() || lines.size() > sample_limit ||
        !providers.is_array() || providers.empty() || providers.size() > sample_limit ||
        !cuts.is_array() || cuts.size() < 2 || cuts.size() > sample_limit ||
        !pairs.is_array() || pairs.size() > sample_limit ||
        !intervals.is_array() || intervals.empty() || intervals.size() > sample_limit)
        return false;
    const auto& samples /* Already admitted base physical population. */ = j.at("samples");
    std::unordered_map<std::uint64_t, const Json*> owners; // Setup-only exact base occurrence lookup.
    for (const auto& owner /* Existing occurrence, never an inferred new mechanical owner. */ : j.at("occurrences"))
        owners.emplace(id(owner.at("id")), &owner);
    std::map<std::pair<std::uint64_t, std::string>, const Json*> structural; // Optional exact source-line keys; null marks an ambiguous duplicate.
    const bool have_structural /* Whether Main's optional-table identity predicate applies. */ = j.contains("structural_lines");
    if (have_structural) {
        const auto& rows /* Optional producer metadata; unrelated row details remain descriptive. */ = j.at("structural_lines");
        if (!rows.is_array())
            return false;
        for (const auto& row /* Candidate natural-key row, whose detailed fields matter only when matched. */ : rows) {
            if (!row.is_object() || !row.contains("occurrence_id") || !uint(row.at("occurrence_id")) ||
                !row.contains("local_id") || !row.at("local_id").is_string())
                continue;
            const auto [entry, inserted] /* Existing or newly indexed exact natural key. */ = structural.emplace(
                std::make_pair(id(row.at("occurrence_id")), row.at("local_id").get<std::string>()), &row);
            if (!inserted)
                entry->second = nullptr;
        }
    }
    std::set<std::pair<std::uint64_t, std::string>> line_keys; // Independent moment-line natural keys must be unique.
    std::vector<const Json*> matched_lines(lines.size(), nullptr); // Borrowed matched structural metadata, never retained after setup.
    for (std::size_t i /* Dense moment-line ordinal, independent of base structural line IDs. */ = 0; i < lines.size(); ++i) {
        const auto& line /* Exact declared material line and geometry provenance. */ = lines[i];
        const auto owner /* Existing flexible occurrence identity. */ = id(line.at("occurrence_id"));
        const auto found /* Setup-only owner lookup. */ = owners.find(owner);
        if (id(line.at("id")) != i || found == owners.end() || found->second->at("kind") != "flexible")
            return false;
        for (const auto* field /* Required actual line, geometry and realization provenance strings. */ :
             {"spanline_id", "source_geometry_fingerprint", "effective_geometry_fingerprint", "approximation_policy"})
            if (!text(line.at(field)))
                return false;
        const auto native_count /* Actual positive native element count under the moment count ceiling. */ = id(line.at("native_element_count"));
        const auto key /* Exact occurrence/local structural identity, without normalization. */ =
            std::make_pair(owner, line.at("spanline_id").get<std::string>());
        if (!native_count || native_count > sample_limit || !line_keys.insert(key).second)
            return false;
        if (have_structural) {
            const auto match /* Unique optional source row; numeric ID belongs to that separate table. */ = structural.find(key);
            if (match == structural.end() || !match->second)
                return false;
            const auto& row /* Only this matching row's detailed shape is normative for this join. */ = *match->second;
            const auto row_id /* Checked original structural row identity, not required dense here. */ = id(row.at("id"));
            const auto begin /* First physical sample in the source line's contiguous range. */ = id(row.at("sample_begin"));
            const auto count /* Number of samples selected by the optional structural row. */ = id(row.at("sample_count"));
            if (row_id > std::numeric_limits<std::size_t>::max() || begin > samples.size() || count > samples.size() - begin)
                return false;
            matched_lines[i] = &row;
        }
    }
    // Validate each selected actual native element without inventing coverage or an eta formula.
    std::set<std::pair<std::uint64_t, std::uint64_t>> native_addresses; // A line/native element has one prepared provider address.
    for (std::size_t i /* Dense native provider-address ordinal. */ = 0; i < providers.size(); ++i) {
        const auto& provider /* Original native material interval and provider provenance. */ = providers[i];
        const auto line /* Independent moment-line reference. */ = id(provider.at("line_id"));
        const auto native /* Original native element owner within that line. */ = id(provider.at("native_element"));
        if (id(provider.at("id")) != i || line >= lines.size() ||
            native >= id(lines[line].at("native_element_count")) || !text(provider.at("provider_id")) ||
            !native_addresses.emplace(line, native).second)
            return false;
        const auto begin /* Lower material bound in the common binary64 comparison domain. */ = moment_number(provider.at("material_begin_m"));
        const auto end /* Upper material bound; selected native elements need not cover the whole line. */ = moment_number(provider.at("material_end_m"));
        if (begin < 0 || begin >= end || !std::isfinite(end - begin))
            return false;
    }
    std::map<std::tuple<std::uint64_t, double, std::string>, std::uint64_t> addresses; // Exact sided identities; signed-zero coordinates compare equal.
    for (std::size_t i /* Dense payload cut ordinal, unrelated to base sample ordinals. */ = 0; i < cuts.size(); ++i) {
        const auto& cut /* Independent sided material/native address. */ = cuts[i];
        const auto line /* Owning moment-line reference. */ = id(cut.at("line_id"));
        const auto provider_id /* Exact prepared provider table reference. */ = id(cut.at("provider_address_id"));
        if (id(cut.at("id")) != i || line >= lines.size() || provider_id >= providers.size())
            return false;
        const auto& provider /* Bounds and native ownership authority for this cut. */ = providers[provider_id];
        const auto coordinate /* Authoritative placement coordinate, explicitly converted to binary64. */ = moment_number(cut.at("material_coordinate_m"));
        const auto eta /* Actual provider provenance, checked rather than recomputed. */ = moment_number(cut.at("native_eta"));
        const auto begin /* Exact provider lower endpoint. */ = moment_number(provider.at("material_begin_m"));
        const auto end /* Exact provider upper endpoint. */ = moment_number(provider.at("material_end_m"));
        const auto& side /* Explicit resolved physical N limit; never none. */ = cut.at("side");
        if (id(provider.at("line_id")) != line || id(cut.at("native_element")) != id(provider.at("native_element")) ||
            coordinate < begin || coordinate > end || (side != "left" && side != "right") ||
            (coordinate == begin && (side != "right" || eta != -1)) ||
            (coordinate == end && (side != "left" || eta != 1)) ||
            (coordinate > begin && coordinate < end && (eta <= -1 || eta >= 1)) ||
            !addresses.emplace(std::make_tuple(line, coordinate, side.get<std::string>()), i).second)
            return false;
    }
    // Prove an exact one-to-one declared pairing for every opposite-sided coordinate.
    std::map<std::pair<std::uint64_t, double>, std::array<std::uint64_t, 2>> breaks; // Sorted line/coordinate to left/right cut IDs.
    std::vector<unsigned char> paired(cuts.size(), 0); // Each cut may participate in at most one break pair.
    for (const auto& pair /* Explicit display separation contract, including same-provider interior breaks. */ : pairs) {
        if (!pair.is_object() || pair.size() != 2)
            return false;
        const auto left /* Exact left-limit cut identity. */ = id(pair.at("left_cut_id"));
        const auto right /* Exact right-limit cut identity. */ = id(pair.at("right_cut_id"));
        if (left >= cuts.size() || right >= cuts.size() || left == right || paired[left] || paired[right])
            return false;
        const auto line /* Shared independent moment line, proved below. */ = id(cuts[left].at("line_id"));
        const auto coordinate /* Exact common material coordinate, not a proximity tolerance. */ = moment_number(cuts[left].at("material_coordinate_m"));
        if (cuts[left].at("side") != "left" || cuts[right].at("side") != "right" ||
            id(cuts[right].at("line_id")) != line || moment_number(cuts[right].at("material_coordinate_m")) != coordinate ||
            !breaks.emplace(std::make_pair(line, coordinate), std::array<std::uint64_t, 2>{left, right}).second)
            return false;
        paired[left] = paired[right] = 1;
    }
    for (const auto& [address, cut] /* Every distinct sided identity and its dense payload index. */ : addresses) {
        if (std::get<2>(address) != "left")
            continue;
        const auto opposite /* A separately declared right limit at exactly this coordinate, if any. */ =
            addresses.find(std::make_tuple(std::get<0>(address), std::get<1>(address), "right"));
        if (opposite != addresses.end() && (!paired[cut] || !paired[opposite->second]))
            return false;
    }
    // Resolve stable base segment IDs and validate each explicit edge mapping without nearest-node inference.
    std::unordered_map<std::uint64_t, const Json*> segments; // Base segment IDs are stable values, not array positions.
    for (const auto& segment /* Previously admitted base structural continuity declaration. */ : j.at("segments"))
        segments.emplace(id(segment.at("id")), &segment);
    std::vector<unsigned char> used(cuts.size(), 0); // Every declared cut must participate in at least one display interval.
    std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, double, double>> ranges; // Setup-only line/segment/edge/a/b ranges for nonoverlap proof.
    ranges.reserve(intervals.size());
    for (std::size_t i /* Dense display interval ordinal. */ = 0; i < intervals.size(); ++i) {
        const auto& interval /* Explicit scalar continuity range; no provider evaluation between cuts is implied. */ = intervals[i];
        const auto line /* Independent moment-line identity. */ = id(interval.at("line_id"));
        const auto segment_id /* Existing stable base segment identity. */ = id(interval.at("base_segment_id"));
        const auto edge /* Consecutive base sample positions selected within that one segment. */ = id(interval.at("edge_index"));
        const auto segment /* Exact segment lookup result, never an array-index assumption. */ = segments.find(segment_id);
        const auto& refs /* Exactly the lower and upper material cut identities. */ = interval.at("cut_ids");
        if (id(interval.at("id")) != i || line >= lines.size() || segment == segments.end() ||
            !refs.is_array() || refs.size() != 2)
            return false;
        const auto lower /* Interval's explicitly chosen lower cut, possibly off-node. */ = id(refs[0]);
        const auto upper /* Interval's explicitly chosen upper cut, possibly independently sided. */ = id(refs[1]);
        const auto owner /* Flexible occurrence shared by this line and the base segment. */ = id(lines[line].at("occurrence_id"));
        const auto& members /* Existing segment connectivity, never rewritten to accommodate cuts. */ = segment->second->at("sample_ids");
        if (lower >= cuts.size() || upper >= cuts.size() || lower == upper ||
            id(segment->second->at("occurrence_id")) != owner || members.size() < 2 || edge >= members.size() - 1 ||
            id(cuts[lower].at("line_id")) != line || id(cuts[upper].at("line_id")) != line ||
            id(cuts[lower].at("provider_address_id")) != id(cuts[upper].at("provider_address_id")))
            return false;
        const auto first /* Physical sample ID at the selected base-edge start. */ = id(members[edge]);
        const auto second /* Physical sample ID at the immediately following base-edge end. */ = id(members[edge + 1]);
        if (first >= samples.size() || second >= samples.size() ||
            samples[first].at("kind") != "structural_node" || samples[second].at("kind") != "structural_node" ||
            id(samples[first].at("occurrence_id")) != owner || id(samples[second].at("occurrence_id")) != owner)
            return false;
        const auto A /* Lower base-edge coordinate in the same binary64 domain as cuts/providers. */ = moment_number(samples[first].at("material_coordinate_m"));
        const auto B /* Upper base-edge coordinate; reversed or degenerate edges are inadmissible. */ = moment_number(samples[second].at("material_coordinate_m"));
        const auto a /* Lower exact cut coordinate; display never extrapolates beyond this bound. */ = moment_number(cuts[lower].at("material_coordinate_m"));
        const auto b /* Upper exact cut coordinate. */ = moment_number(cuts[upper].at("material_coordinate_m"));
        if (A >= B || !std::isfinite(B - A) || a < A || a >= b || b > B)
            return false;
        if (const auto* structural_line /* Matched optional source row, null when the table is absent. */ = matched_lines[line]) {
            const auto structural_id /* Original structural row ID, independent of line. */ = id(structural_line->at("id"));
            const auto begin /* Already checked contiguous physical sample range origin. */ = id(structural_line->at("sample_begin"));
            const auto end /* Safe exclusive range end; admission proved it fits samples.size(). */ = begin + id(structural_line->at("sample_count"));
            if (first < begin || first >= end || second < begin || second >= end)
                return false;
            for (const auto* entry /* Optional base segment/endpoint metadata must name the matched structural row. */ :
                 {segment->second, &samples[first], &samples[second]})
                if (entry->contains("line_id") && id(entry->at("line_id")) != structural_id)
                    return false;
        }
        auto boundary /* First declared break at or above this interval's lower coordinate. */ = breaks.lower_bound(std::make_pair(line, a));
        if (boundary != breaks.end() && boundary->first.first == line && boundary->first.second == a) {
            if (boundary->second[1] != lower)
                return false;
            ++boundary;
        }
        if (boundary != breaks.end() && boundary->first.first == line && boundary->first.second <= b &&
            (boundary->first.second != b || boundary->second[0] != upper))
            return false;
        ranges.emplace_back(line, segment_id, edge, a, b);
        used[lower] = used[upper] = 1;
    }
    // Sorting proves edge-local nonoverlap while preserving touching and independently sided endpoints.
    std::sort(ranges.begin(), ranges.end());
    for (std::size_t i /* Adjacent sorted intervals on this or the next independent base edge. */ = 1; i < ranges.size(); ++i) {
        const auto& previous /* Range with the immediately preceding lower bound. */ = ranges[i - 1];
        const auto& current /* Next range, whose nonzero overlap must be rejected on the same edge. */ = ranges[i];
        if (std::get<0>(previous) == std::get<0>(current) && std::get<1>(previous) == std::get<1>(current) &&
            std::get<2>(previous) == std::get<2>(current) && std::get<4>(previous) > std::get<3>(current))
            return false;
    }
    if (std::find(used.begin(), used.end(), 0) != used.end())
        return false;
    out = {generation, static_cast<std::uint64_t>(cuts.size())};
    return true;
}
/// Minimal runtime attachment extent owned by an admitted applied-load recorder.
/// Catalog topology remains setup-only; append uses these two values to bound each borrowed row.
struct AppliedAttachmentLayout {
    std::uint64_t station_count{}; ///< Logical rows in this attachment's complete selected range.
    std::uint64_t block_offset{}; ///< Byte offset of its interval prefix within the applied record.
};
/// Applied-load admission facts and fixed runtime layout, never duration storage.
/// Scalar facts are validated before the attachment vector is allocated under the workspace limit.
struct AppliedCatalog {
    std::uint64_t generation{}; ///< Positive prepared lifetime, zero when the descriptor is absent.
    std::uint64_t attachment_count{}; ///< Dense attachment population, validated during inactive setup.
    std::uint64_t frame_bytes{}; ///< Complete common prefix plus all packed attachment blocks.
    std::vector<AppliedAttachmentLayout> attachments; ///< Owned count/offset table; actual capacity is charged.
};
/// Require a borrowed normative load string to be nonempty and NUL-free; JSON parsing checks UTF-8.
/// Return true only for an admitted string; leave the caller's value unchanged.
bool applied_text(const Json& value) {
    // Inspect the parsed required string without copying or normalizing its original spelling.
    return text(value) && value.get_ref<const std::string&>().find('\0') == std::string::npos;
}
/// Check all numeric descriptive metadata in value; optional strings may be empty or contain NUL.
/// Return false for nonfinite floating metadata. Parsed integer values are finite already;
/// this helper does not impose the stricter normative ID/count checks supplied by the caller.
bool applied_numbers_finite(const Json& value) {
    // Walk setup-only metadata recursively, preserving the loads-specific required-string policy.
    if (value.is_number_float())
        return std::isfinite(value.get<double>());
    if (value.is_structured())
        for (const auto& child /* One parsed array element or object value. */ : value)
            if (!applied_numbers_finite(child))
                return false;
    return true;
}
/// Admit the self-contained applied descriptor in base catalog j and return scalar layout facts in out.
/// Throws malformed JSON accesses to open's contract boundary. All validation tables are setup-only;
/// no retained attachment vector is allocated until open preflights the whole-group payload budget.
/// Return true on omission or complete admission, false for an explicit contract failure.
/// Commit out's scalar facts only after full admission; failed checks leave out unchanged.
bool applied_catalog_valid(const Json& j, AppliedCatalog& out) {
    // Validate the exact stream/product and run-scoped model identities.
    // Prove attachment station partitions and model ranges, then logical/consumer addresses.
    // Compute the complete raw extent without retaining catalog JSON or runtime topology.
    if (!j.contains("applied_loads"))
        return true;
    const auto& a /* Borrowed self-contained physical-load descriptor. */ = j.at("applied_loads");
    if (!a.is_object() || !applied_numbers_finite(a) || id(a.at("kind")) != 6 ||
        id(a.at("stream_id")) != 4 || id(a.at("layout_version")) != 1 ||
        a.at("encoding") != "raw_f64_le" || a.at("product") != "completed_weak_force_density" ||
        a.at("force_unit") != "N/m" || a.at("position_unit") != "m" || a.at("vector_frame") != "world")
        return false;
    const auto generation /* Common source/step lifetime. */ = id(a.at("source_preparation_generation"));
    const auto& coordinators /* The qualified synchronous run coordinator. */ = a.at("coordinators");
    const auto& models /* Original source-model identities, independent of target occurrence. */ = a.at("models");
    const auto& attachments /* Complete selected attachment populations. */ = a.at("attachments");
    const auto& stations /* Dense independent logical physical station catalog. */ = a.at("stations");
    if (!generation || !coordinators.is_array() || coordinators.size() != 1 ||
        !models.is_array() || models.empty() || models.size() > sample_limit ||
        !attachments.is_array() || attachments.empty() || attachments.size() > sample_limit ||
        !stations.is_array() || stations.empty() || stations.size() > sample_limit)
        return false;
    const auto& coordinator /* Only source namespace admitted by this producer profile. */ = coordinators[0];
    const auto& scope /* Actual model definition scope, never inferred from its target. */ = coordinator.at("scope");
    if (id(coordinator.at("id")) != 0 || coordinator.at("coupling") != "fixed_step_weak_held" ||
        !scope.is_object() || scope.at("kind") != "run" || !applied_text(scope.at("run_id")) || scope.at("run_id") != j.at("run_id"))
        return false;
    std::set<std::uint64_t> instances; // Unique original model identities within the one coordinator.
    std::set<std::pair<std::string, std::string>> names; // Unique run/model source spellings.
    for (std::size_t i /* Dense model-catalog ordinal. */ = 0; i < models.size(); ++i) {
        const auto& model /* Borrowed original manifest metadata. */ = models[i];
        if (id(model.at("id")) != i || id(model.at("coordinator_id")) != 0 ||
            !id(model.at("instance")) || !id(model.at("layout_revision")) || !id(model.at("logical_node_count")) ||
            id(model.at("logical_node_count")) > std::numeric_limits<std::size_t>::max())
            return false;
        for (const auto* field /* Required source identity/version spellings. */ : {"model_id", "run_id", "model_type", "model_version"})
            if (!applied_text(model.at(field)))
                return false;
        if (model.at("run_id") != j.at("run_id") || !instances.insert(id(model.at("instance"))).second ||
            !names.emplace(model.at("run_id").get<std::string>(), model.at("model_id").get<std::string>()).second)
            return false;
    }
    std::unordered_map<std::uint64_t, const Json*> owners; // Setup-only physical occurrence lookup.
    for (const auto& owner /* Existing physical occurrence. */ : j.at("occurrences"))
        owners.emplace(id(owner.at("id")), &owner);
    std::set<std::pair<std::uint64_t, std::uint64_t>> attachment_indices; // Original attachment ordinal identity per model.
    std::set<std::pair<std::uint64_t, std::string>> attachment_names; // Original attachment spelling per model.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> station_ranges; // Setup-only partition proof, sorted independently of attachment order.
    std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> model_ranges; // Setup-only selected model intervals.
    std::uint64_t width = 80; // Checked complete record extent, including its common prefix.
    for (std::size_t i /* Dense attachment-catalog ordinal. */ = 0; i < attachments.size(); ++i) {
        const auto& attachment /* Borrowed target and prepared-geometry binding. */ = attachments[i];
        const auto model /* Dense source model reference. */ = id(attachment.at("model_catalog_id"));
        const auto owner /* Exact physical target occurrence. */ = owners.find(id(attachment.at("target_occurrence_id")));
        const auto begin /* Beginning in the global station table; order may be permuted. */ = id(attachment.at("station_begin"));
        const auto count /* Complete logical population of this selected attachment. */ = id(attachment.at("station_count"));
        const auto node_begin /* Original model-wide logical range origin. */ = id(attachment.at("model_node_begin"));
        if (id(attachment.at("id")) != i || model >= models.size() || owner == owners.end() ||
            owner->second->at("kind") != "flexible" || !count || begin > stations.size() || count > stations.size() - begin ||
            !id(attachment.at("native_element_count")) || id(attachment.at("native_element_count")) > std::numeric_limits<std::size_t>::max())
            return false;
        for (const auto* field /* Required original/effective geometry and attachment spellings. */ :
             {"attachment_id", "spanline_id", "source_geometry_fingerprint", "effective_geometry_fingerprint", "model_nodes_fingerprint", "approximation_policy"})
            if (!applied_text(attachment.at(field)))
                return false;
        const auto model_count /* Actual model domain extent, not an output-sized allocation. */ = id(models[model].at("logical_node_count"));
        if (node_begin > model_count || count > model_count - node_begin ||
            !attachment_indices.emplace(model, id(attachment.at("model_attachment_index"))).second ||
            !attachment_names.emplace(model, attachment.at("attachment_id").get<std::string>()).second)
            return false;
        station_ranges.emplace_back(begin, begin + count);
        model_ranges.emplace_back(model, node_begin, node_begin + count);
        const auto block /* Prefix plus packed mask plus six binary64 scalars per station. */ = 96 + (count + 7) / 8 + count * 48;
        if (width > INT64_MAX - block)
            return false;
        width += block;
    }
    std::sort(station_ranges.begin(), station_ranges.end());
    std::uint64_t cursor{}; // Expected start of the next partition interval.
    for (const auto& range /* One disjoint complete interval in global station order. */ : station_ranges) {
        if (range.first != cursor)
            return false;
        cursor = range.second;
    }
    if (cursor != stations.size())
        return false;
    std::sort(model_ranges.begin(), model_ranges.end());
    for (std::size_t i /* Adjacent selected ranges after model/order sorting. */ = 1; i < model_ranges.size(); ++i)
        if (std::get<0>(model_ranges[i]) == std::get<0>(model_ranges[i - 1]) &&
            std::get<1>(model_ranges[i]) < std::get<2>(model_ranges[i - 1]))
            return false;
    std::uint64_t total_consumers{}; // Aggregate catalog consumer ceiling, not a retained runtime table.
    for (std::size_t attachment_index /* Attachment used to check its complete logical and consumer domains. */ = 0; attachment_index < attachments.size(); ++attachment_index) {
        const auto& attachment /* Already admitted target range and native domain. */ = attachments[attachment_index];
        const auto begin /* This attachment's global station-table range. */ = id(attachment.at("station_begin"));
        const auto count /* Number of logical rows, independent of native consumer multiplicity. */ = id(attachment.at("station_count"));
        std::set<std::pair<double, std::uint64_t>> addresses; // Exact physical addresses within this attachment; signed zero compares equal.
        std::set<std::uint64_t> consumers; // Unique original consumer indices; density proved after all rows.
        for (std::uint64_t local /* Original logical ordinal within the attachment. */ = 0; local < count; ++local) {
            const auto& station /* Dense catalog station carrying original logical provenance. */ = stations[begin + local];
            if (id(station.at("id")) != begin + local || id(station.at("attachment_catalog_id")) != attachment_index ||
                id(station.at("logical_node_index")) != id(attachment.at("model_node_begin")) + local ||
                !station.at("material_coordinate_m").is_number())
                return false;
            const auto coordinate /* Root-zero material arc, finite and nonnegative. */ = station.at("material_coordinate_m").get<double>();
            const auto& side /* Physical continuity side, not the native limiting side. */ = station.at("physical_side");
            if (!std::isfinite(coordinate) || coordinate < 0 || (side != "continuous" && side != "left" && side != "right") ||
                !addresses.emplace(coordinate, id(station.at("physical_side_token"))).second)
                return false;
            const auto& native /* All original mechanical consumers of this logical point. */ = station.at("native_consumers");
            if (!native.is_array() || native.empty() || native.size() > sample_limit - total_consumers)
                return false;
            total_consumers += native.size();
            for (const auto& consumer /* Original consumer index and actual native owner/limit. */ : native) {
                if (!consumers.insert(id(consumer.at("consumer_index"))).second ||
                    id(consumer.at("native_element")) >= id(attachment.at("native_element_count")) ||
                    (consumer.at("limit_side") != "left" && consumer.at("limit_side") != "right"))
                    return false;
            }
        }
        if (consumers.empty() || *consumers.begin() != 0 || *consumers.rbegin() != consumers.size() - 1)
            return false;
    }
    out.generation = generation;
    out.attachment_count = attachments.size();
    out.frame_bytes = width;
    return true;
}
/// Fixed per-plane payload extent owned by an admitted recorder, never geometry or provider state.
/// Dense vector position is the plane ID; append uses only these two checked integer extents.
struct PlaneLayout {
    std::uint64_t sample_count{}; ///< Realized nu*nv point population in u-fastest order.
    std::uint64_t block_offset{}; ///< This plane's mask offset within the common-prefix record.
};
/// Inflow-plane admission facts retained for synchronous bounded raw recording.
/// Catalog geometry, source manifests and setup lookup tables are not retained here.
/// The layout vector is reserved only after minimum whole-group workspace admission.
struct PlaneCatalog {
    std::uint64_t generation{}; ///< Positive common prepared lifetime; zero when no descriptor exists.
    std::uint64_t plane_count{}; ///< Dense immutable plane count, known before layout allocation.
    std::uint64_t frame_bytes{}; ///< One common80 prefix plus every plane's mask and velocity triples.
    std::vector<PlaneLayout> planes; ///< Owned per-plane extents; actual capacity is charged with applied layouts.
};
/// Return whether borrowed value is an exact plane integer in [0, INT64_MAX].
/// Accept signed integer zero, including JSON -0, without accepting floating zero or booleans.
/// This plane-only rule preserves earlier family admission and leaves original metadata unchanged.
bool plane_uint(const Json& value) {
    // Bound unsigned integers first; then admit the nonnegative signed-integer representation.
    if (value.is_number_unsigned())
        return value.get<std::uint64_t>() <= INT64_MAX;
    return value.is_number_integer() && value.get<std::int64_t>() >= 0;
}
/// Return the admitted exact plane index/count value, or throw invalid_argument to open.
/// Convert integer -0 to its numeric zero only for validation/addressing; recorded JSON bytes
/// remain the caller's original text. Normative identity/count rules apply after this conversion.
std::uint64_t plane_id(const Json& value) {
    // Reject negative, floating, Boolean and oversized tokens before extracting the exact integer.
    if (!plane_uint(value))
        throw std::invalid_argument("plane unsigned id");
    return value.get<std::uint64_t>();
}
/// Check borrowed plane/source metadata recursively without imposing normative identity rules.
/// Return false for NUL strings/keys or nonfinite floating values. JSON parsing already validates
/// UTF8 and depth; empty descriptive strings and signed integer metadata remain unchanged/valid.
bool plane_metadata_valid(const Json& value) {
    // Preserve open descriptive metadata while rejecting nonfinite numbers and embedded NULs.
    if (value.is_string())
        return value.get_ref<const std::string&>().find('\0') == std::string::npos;
    if (value.is_number_float())
        return std::isfinite(value.get<double>());
    if (value.is_object()) {
        for (auto entry /* One original source/object key and its borrowed child value. */ = value.begin(); entry != value.end(); ++entry)
            if (entry.key().find('\0') != std::string::npos || !plane_metadata_valid(entry.value()))
                return false;
    } else if (value.is_array()) {
        for (const auto& child /* Next bounded descriptive array value. */ : value)
            if (!plane_metadata_valid(child))
                return false;
    }
    return true;
}
/// Return borrowed plane geometry scalar as finite binary64, or throw invalid_argument.
/// Numeric integer coordinates may round to binary64; booleans and non-numbers are invalid.
/// This setup-only conversion never applies to exact IDs/counts and never rewrites catalog JSON.
double plane_number(const Json& value) {
    // Check the numeric domain, convert once and reject any nonfinite representation.
    if (!value.is_number())
        throw std::invalid_argument("plane numeric value");
    const auto number /* Geometry scalar represented in the recorded binary64 domain. */ = value.get<double>();
    if (!std::isfinite(number))
        throw std::invalid_argument("plane finite value");
    return number;
}
/// Return an owned three-value geometry vector converted from borrowed JSON value.
/// Require exactly three finite numeric components; invalid shape/domain throws to open.
std::array<double, 3> plane_vector(const Json& value) {
    // Check the exact xyz shape and convert each unchanged scalar into bounded stack storage.
    if (!value.is_array() || value.size() != 3)
        throw std::invalid_argument("plane vector shape");
    return {plane_number(value[0]), plane_number(value[1]), plane_number(value[2])};
}
/// Validate the minimum copied-provider manifest in source against recording run_id.
/// The descriptor's general metadata policy has already passed. Return false on inconsistent
/// recorded identity, preparation or known native provenance; missing fields throw to open.
/// Unknown nonempty source kinds remain descriptive. Never reopen assets, recompute hashes,
/// rewrite provider stage facts or claim a second provider/basis/scientific validation.
bool plane_manifest_valid(const Json& source, const Json& run_id) {
    // Join actual source/run identities and the exact successful adapter-stage facts.
    // Validate native provenance syntax or the absence of a fabricated mean-only asset.
    if (!applied_text(source.at("inflow_id")) || !applied_text(source.at("run_id")) || source.at("run_id") != run_id)
        return false;
    const auto& manifest /* Copied actual prepared provider object, kept semantically unchanged. */ = source.at("initialization_manifest");
    if (!manifest.is_object() || manifest.at("format") != "serene-inflow.initialization.v1")
        return false;
    const auto& identity /* Provider-emitted source/run identity facts. */ = manifest.at("identity");
    const auto& validation /* Historical adapter-stage preparation facts, not Core's later binding result. */ = manifest.at("validation");
    const auto& runtime /* Open runtime profile whose source kind selects only the minimum known checks. */ = manifest.at("runtime_profile");
    if (!identity.is_object() || !applied_text(identity.at("inflow_id")) || !applied_text(identity.at("run_id")) ||
        identity.at("inflow_id") != source.at("inflow_id") || identity.at("run_id") != source.at("run_id") ||
        !validation.is_object() || !validation.at("standalone_provider_prepared").is_boolean() ||
        !validation.at("standalone_provider_prepared").get<bool>() || validation.at("core_binding") != "not_performed" ||
        !runtime.is_object() || !applied_text(runtime.at("source_kind")))
        return false;
    if (runtime.at("source_kind") == "none")
        return !manifest.contains("source");
    if (runtime.at("source_kind") != "native_general_kaimal_wnd")
        return true;
    const auto& asset /* Recorded native file provenance; the file is never read by this recorder. */ = manifest.at("source");
    if (!asset.is_object() || !applied_text(asset.at("path")) || !plane_uint(asset.at("source_bytes")) ||
        runtime.at("transverse") != "turbulence_only_clamp")
        return false;
    const auto& fingerprint /* Original noncryptographic algorithm-labelled file fingerprint. */ = asset.at("fingerprint");
    if (!fingerprint.is_object() || fingerprint.at("algorithm") != "fnv1a64" ||
        fingerprint.at("encoding") != "hex_lowercase_16" || fingerprint.at("scope") != "entire_source_file" ||
        !fingerprint.at("cryptographic").is_boolean() || fingerprint.at("cryptographic").get<bool>() ||
        !fingerprint.at("value").is_string())
        return false;
    const auto& hexadecimal /* Exact original spelling; uppercase/prefixes/other digest widths are inadmissible. */ =
        fingerprint.at("value").get_ref<const std::string&>();
    return hexadecimal.size() == 16 && std::all_of(hexadecimal.begin(), hexadecimal.end(), [](char digit) {
        // Admit only the literal lowercase hexadecimal alphabet, independent of locale.
        return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
    });
}
/// Admit borrowed realized affine plane geometry and return its point count through count.
/// Return false on dimension, basis, spacing or finite-arithmetic defects; malformed numeric
/// shapes throw to open. No points are allocated, no basis is normalized and no JSON is changed.
/// Four corner extrema bound all interior u/v multiplications and sequential affine additions.
bool plane_geometry_valid(const Json& plane, std::uint64_t& count) {
    // Bound exact dimensions before products, then check realized basis/spacings and finite extents.
    // Validate corner multiplication/addition intermediates without retaining a point population.
    const auto nu /* Endpoint-inclusive fast-axis sample count. */ = plane_id(plane.at("nu"));
    const auto nv /* Endpoint-inclusive slow-axis sample count. */ = plane_id(plane.at("nv"));
    if (nu < 2 || nv < 2 || nu > sample_limit / nv)
        return false;
    const auto origin /* Original realized lower world corner in metres. */ = plane_vector(plane.at("lower_corner_world_m"));
    const auto e1 /* Original fast-axis world direction; retained without normalization. */ = plane_vector(plane.at("e1_world"));
    const auto e2 /* Original slow-axis world direction; e1 cross e2 defines the displayed normal. */ = plane_vector(plane.at("e2_world"));
    const auto du /* Strictly positive realized fast-axis spacing in metres. */ = plane_number(plane.at("du_m"));
    const auto dv /* Strictly positive realized slow-axis spacing in metres. */ = plane_number(plane.at("dv_m"));
    if (du <= 0 || dv <= 0 || plane.at("sample_order") != "u_fastest")
        return false;
    double norm1 = 0; // Squared e1 length under the fixed representation tolerance.
    double norm2 = 0; // Squared e2 length, checked independently without normalization.
    double dot = 0; // Orthogonality residual in the original world basis.
    for (std::size_t axis /* World xyz scalar used by the basis checks. */ = 0; axis < 3; ++axis) {
        norm1 += e1[axis] * e1[axis];
        norm2 += e2[axis] * e2[axis];
        dot += e1[axis] * e2[axis];
    }
    if (!std::isfinite(norm1) || !std::isfinite(norm2) || !std::isfinite(dot) ||
        std::abs(norm1 - 1) > 1e-12 || std::abs(norm2 - 1) > 1e-12 || std::abs(dot) > 1e-12)
        return false;
    const auto width /* Maximum u*du; admitted dimensions keep the integer-to-double conversion exact. */ = static_cast<double>(nu - 1) * du;
    const auto height /* Maximum v*dv; every smaller nonnegative multiplier is bounded by this extent. */ = static_cast<double>(nv - 1) * dv;
    if (!std::isfinite(width) || !std::isfinite(height))
        return false;
    for (const auto u /* Fast-axis extent at each affine corner. */ : {0.0, width})
        for (const auto v /* Slow-axis extent at each affine corner. */ : {0.0, height})
            for (std::size_t axis /* One coordinate and its sequential affine arithmetic. */ = 0; axis < 3; ++axis) {
                const auto first_term /* Finite fast-axis world displacement component. */ = u * e1[axis];
                const auto second_term /* Finite slow-axis world displacement component. */ = v * e2[axis];
                const auto first_sum /* Intermediate origin plus fast displacement; cancellation later cannot excuse overflow. */ = origin[axis] + first_term;
                const auto point /* Final corner coordinate after the slow displacement. */ = first_sum + second_term;
                if (!std::isfinite(first_term) || !std::isfinite(second_term) ||
                    !std::isfinite(first_sum) || !std::isfinite(point))
                    return false;
            }
    count = nu * nv;
    return true;
}
/// Admit inflow_planes in base catalog j, returning scalar layout facts through out on success.
/// Source manifests remain open preserved metadata under the minimum known syntax predicate.
/// All planes have finite immutable affine geometry; aggregate point/count/byte bounds are checked
/// before retained layout allocation. Temporary sets/coverage arrays are setup-only.
/// Return true on omission/admission, false on explicit defects; malformed JSON throws to open.
bool plane_catalog_valid(const Json& j, PlaneCatalog& out) {
    // Admit descriptor identity and open metadata, then validate every copied source manifest.
    // Prove dense unique planes, finite geometry, aggregate extents and complete source use.
    if (!j.contains("inflow_planes"))
        return true;
    const auto& descriptor /* Original borrowed immutable plane descriptor. */ = j.at("inflow_planes");
    if (!descriptor.is_object() || !plane_metadata_valid(descriptor) || plane_id(descriptor.at("kind")) != 8 ||
        plane_id(descriptor.at("stream_id")) != 6 || plane_id(descriptor.at("layout_version")) != 1 ||
        descriptor.at("encoding") != "raw_f64_le" || descriptor.at("quantity") != "absolute_unperturbed_world_velocity" ||
        descriptor.at("units") != "m/s")
        return false;
    const auto generation /* Exact positive prepared source lifetime shared with every enabled stream. */ = plane_id(descriptor.at("source_preparation_generation"));
    const auto& sources /* Dense immutable actual source manifests; first Core route uses one, wire tables remain generic. */ = descriptor.at("sources");
    const auto& planes /* Dense original realized plane descriptors determining wire order. */ = descriptor.at("planes");
    if (!generation || !sources.is_array() || sources.empty() || !planes.is_array() || planes.empty() ||
        planes.size() > sample_limit || sources.size() > planes.size())
        return false;
    for (std::size_t i /* Dense source ordinal, independent of provider diagnostic IDs. */ = 0; i < sources.size(); ++i)
        if (plane_id(sources[i].at("id")) != i || !plane_manifest_valid(sources[i], j.at("run_id")))
            return false;
    std::vector<unsigned char> used(sources.size(), 0); // No orphan source can survive complete plane admission.
    std::set<std::string> identities; // Unique exact user/request plane IDs, without normalization.
    std::uint64_t total = 0; // Aggregate realized point count, never a point-sized retained allocation.
    std::uint64_t frame_bytes = 80; // Single common prefix, followed by separate plane mask/value blocks.
    for (std::size_t i /* Dense plane ordinal and eventual runtime layout-vector position. */ = 0; i < planes.size(); ++i) {
        const auto& plane /* Borrowed geometry and request identity for one fixed plane. */ = planes[i];
        const auto source /* Exact dense source table reference. */ = plane_id(plane.at("source_id"));
        if (plane_id(plane.at("id")) != i || !applied_text(plane.at("plane_id")) || source >= sources.size() ||
            !identities.insert(plane.at("plane_id").get<std::string>()).second)
            return false;
        std::uint64_t count = 0; // Point count assigned only after complete finite-geometry admission.
        if (!plane_geometry_valid(plane, count) || count > sample_limit - total)
            return false;
        const auto mask /* Checked ceil(count/8), avoiding an addition before division. */ = count / 8 + (count % 8 != 0);
        if (count > (INT64_MAX - mask) / 24)
            return false;
        const auto block /* This plane's packed mask and point-major world triples. */ = mask + 24 * count;
        if (frame_bytes > INT64_MAX - block || frame_bytes + block > std::numeric_limits<std::size_t>::max())
            return false;
        frame_bytes += block;
        total += count;
        used[source] = 1;
    }
    if (std::find(used.begin(), used.end(), 0) != used.end())
        return false;
    out.generation = generation;
    out.plane_count = planes.size();
    out.frame_bytes = frame_bytes;
    return true;
}
/// Check one borrowed F sample without changing any pose or quaternion representation.
/// Invalid rows require canonical unavailable bits; valid rows require finite unit poses.
bool fluid_pose_valid(const FluidPose& sample) noexcept {
    // Admit every finite scalar, then distinguish exact unavailable bytes from a valid unit quaternion.
    for (std::size_t k /* Pose scalar index: xyz then wxyz. */ = 0; k < sample.world_pose.size(); ++k) {
        const auto value /* Borrowed original binary64 scalar. */ = sample.world_pose[k];
        if (!std::isfinite(value))
            return false;
        if (!sample.valid && std::bit_cast<std::uint64_t>(value) != std::bit_cast<std::uint64_t>(k == 3 ? 1.0 : 0.0))
            return false;
    }
    if (!sample.valid)
        return true;
    double norm = 0; // Squared quaternion norm is checked without normalization or sign adjustment.
    for (std::size_t k /* Quaternion scalar position within the original pose. */ = 3; k < 7; ++k)
        norm += sample.world_pose[k] * sample.world_pose[k];
    return std::isfinite(norm) && std::abs(norm - 1.0) <= 1e-6;
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
/// Encode borrowed interval v into exactly 96 bytes at p, without padding or alignment assumptions.
/// p must name writable storage of that extent. Preserve input field bits and write reserved zero;
/// no validation is performed, allowing the caller to test an unavailable prefix for canonical zeros.
void encode_applied_interval(std::byte* p, const AppliedInterval& v) noexcept {
    // Serialize explicit fields; opaque tokens are preserved, never reconstructed or ordered.
    u32(p, static_cast<std::uint32_t>(v.status));
    u32(p + 4, 0);
    u64(p + 8, v.source_accepted_id);
    f64(p + 16, v.source.time_s);
    u64(p + 24, v.end_accepted_id);
    f64(p + 32, v.end_time_s);
    u64(p + 40, v.source.publication);
    u64(p + 48, v.source.mechanical_revision);
    u64(p + 56, v.source.state_revision);
    u64(p + 64, v.source.geometry_revision);
    u64(p + 72, v.source.environment_revision);
    u64(p + 80, v.prepared_step_sequence);
    u64(p + 88, v.packet);
}
/// Validate borrowed interval v against current endpoint s and optional previous saved stamp.
/// phase is already admitted against s; prepared display time never bounds a source interval.
/// Return true only for a canonical unavailable interval or a fully joined completed interval.
/// No argument is mutated and no borrowed stamp or interval is retained.
bool applied_interval_valid(const AppliedInterval& v, SourcePhase phase, const Stamp& s,
                            const Stamp* previous) noexcept {
    // Require an exact zero unavailable prefix outside the positive transient phase.
    // Otherwise check positive opaque identities and actual source/endpoint facts.
    // Join the source to the previous saved transient state without requiring adjacent IDs.
    if (phase != SourcePhase::transient_accepted) {
        std::array<std::byte, 96> prefix{}; // Fixed scratch charged within the encoding allowance.
        encode_applied_interval(prefix.data(), v);
        return std::all_of(prefix.begin(), prefix.end(), [](std::byte value) {
            // Unavailable interval fields, including signed-zero times, must encode to zero bytes.
            return value == std::byte{};
        });
    }
    if (v.status != AppliedIntervalStatus::completed || v.source_accepted_id > INT64_MAX ||
        v.source_accepted_id >= v.end_accepted_id || v.end_accepted_id != s.state_id ||
        !std::isfinite(v.source.time_s) || !std::isfinite(v.end_time_s) || v.source.time_s >= v.end_time_s ||
        std::bit_cast<std::uint64_t>(v.end_time_s) != std::bit_cast<std::uint64_t>(s.time))
        return false;
    for (const auto identity /* Positive exact opaque token or completed endpoint; no inter-token relation. */ :
         {v.end_accepted_id, v.source.publication, v.source.mechanical_revision, v.source.state_revision,
          v.source.geometry_revision, v.source.environment_revision, v.prepared_step_sequence, v.packet})
        if (!identity || identity > INT64_MAX)
            return false;
    if (previous && previous->analysis == Analysis::transient) {
        if (v.source_accepted_id < previous->state_id || v.source.time_s < previous->time)
            return false;
        if (v.source_accepted_id == previous->state_id) {
            if (std::bit_cast<std::uint64_t>(v.source.time_s) != std::bit_cast<std::uint64_t>(previous->time))
                return false;
        } else if (v.source.time_s <= previous->time)
            return false;
    }
    return true;
}
/// Validate sample's six source-point/density values; unavailable_interval forbids valid rows.
/// Return false on nonfinite values, a forbidden valid row or noncanonical unavailable bits.
/// This read-only helper does not change source positions, signed zeros or physical densities.
bool applied_sample_valid(const AppliedLoadSample& sample, bool unavailable_interval) noexcept {
    // Preserve valid finite signed zeros; unavailable values require bitwise positive zero.
    if (unavailable_interval && sample.valid)
        return false;
    for (std::size_t k /* World xyz component shared by position and force density. */ = 0; k < 3; ++k)
        for (const auto value /* Original binary64 scalar, never replaced or normalized. */ :
             {sample.source_position_world_m[k], sample.force_density_world_N_per_m[k]})
            if (!std::isfinite(value) || (!sample.valid && std::bit_cast<std::uint64_t>(value) != 0))
                return false;
    return true;
}
/// Return whether borrowed moment is a finite signed triple or canonical unavailable triple.
/// Preserve valid signed zeros and all caller values; a clear mask requires positive-zero bits.
/// This allocation-free check neither computes a resultant nor transforms the current-N basis.
bool moment_sample_valid(const ElasticMoment& moment) noexcept {
    // Inspect every unchanged component; distinguish finite valid zero from unavailable +0 bits.
    for (const auto value /* Original signed Mx, My or Mz scalar in N.m. */ : moment.moment_N_Nm)
        if (!std::isfinite(value) || (!moment.valid && std::bit_cast<std::uint64_t>(value) != 0))
            return false;
    return true;
}
/// Return whether borrowed sample is finite and has the exact representation required by its mask.
/// Valid calm/signed-zero velocities remain unchanged; unavailable requires three positive-zero bits.
/// This allocation-free check neither classifies provider coverage nor replaces failed source sampling.
bool plane_sample_valid(const InflowVelocity& sample) noexcept {
    // Inspect unchanged world velocity components and enforce only the wire availability distinction.
    for (const auto value /* Original absolute world ux, uy or uz in metres per second. */ : sample.world_velocity_mps)
        if (!std::isfinite(value) || (!sample.valid && std::bit_cast<std::uint64_t>(value) != 0))
            return false;
    return true;
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
    std::vector<std::byte> fluid_batch; ///< Optional fixed-capacity packed F batch, including masks and unaligned scalar bytes.
    std::vector<std::byte> applied_batch; ///< Optional fixed-capacity applied physical point/density batch.
    std::vector<std::byte> moment_batch; ///< Optional fixed-capacity packed signed triples, including unaligned mask/row bytes.
    std::vector<std::byte> plane_batch; ///< Optional fixed-capacity per-plane masks/world triples under one common source prefix.
    WinchCatalog winches; ///< Immutable Core generation and canonical A/B/D sample references, empty when disabled.
    FluidCatalog fluid; ///< Scalar F count/generation only; all line/topology admission tables are setup-only.
    AppliedCatalog applied; ///< Source generation and actual-capacity-charged attachment extents; no runtime JSON.
    MomentCatalog moments; ///< Scalar cut count/generation only; native and display topology is setup-only.
    PlaneCatalog planes; ///< Source generation and charged per-plane extents; no runtime geometry, manifests or provider callbacks.
    std::uint64_t samples{}, ///< Fixed catalog sample count per frame.
        frame_bytes{}, ///< Complete raw frame bytes, including 64-byte identity prefix.
        feature_frame_bytes{}, ///< Optional raw feature frame bytes, zero when the stream is absent.
        fluid_frame_bytes{}, ///< Optional F prefix/mask/pose frame extent, zero when absent.
        fluid_mask_bytes{}, ///< Packed LSB-first mask width for the immutable F sample population.
        moment_frame_bytes{}, ///< Optional common prefix/mask/signed-triple raw extent, zero when absent.
        moment_mask_bytes{}, ///< Packed LSB-first validity width for the immutable cut population.
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
        u16(h.data() + 6, kind >= 4 && kind <= 8 ? 0 : 1);
        u32(h.data() + 8, 80);
        u32(h.data() + 16, kind >= 4 && kind <= 8 ? kind - 2 : (kind == 1 ? 1 : 0));
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
    /// Persist buffered frames as F, loads, moments, planes, optional adjacent winch, then linked pose.
    /// A failed or truncated group does not advance persisted_frames; empty buffers are a no-op.
    bool flush_batch() noexcept {
        // Write F, loads, moments and planes before the optional winch/pose suffix. Advance the structural
        // links and persisted count only after every enabled chunk and flush succeeds.

        if (!stats.buffered_frames)
            return true;
        if (fluid_frame_bytes &&
            !chunk(5, stats.buffered_frames, stats.persisted_frames,
                   std::span<const std::byte>(fluid_batch.data(),
                       static_cast<std::size_t>(stats.buffered_frames * fluid_frame_bytes))))
            return false;
        if (applied.frame_bytes &&
            !chunk(6, stats.buffered_frames, stats.persisted_frames,
                   std::span<const std::byte>(applied_batch.data(),
                       static_cast<std::size_t>(stats.buffered_frames * applied.frame_bytes))))
            return false;
        if (moment_frame_bytes &&
            !chunk(7, stats.buffered_frames, stats.persisted_frames,
                   std::span<const std::byte>(moment_batch.data(),
                       static_cast<std::size_t>(stats.buffered_frames * moment_frame_bytes))))
            return false;
        if (planes.frame_bytes &&
            !chunk(8, stats.buffered_frames, stats.persisted_frames,
                   std::span<const std::byte>(plane_batch.data(),
                       static_cast<std::size_t>(stats.buffered_frames * planes.frame_bytes))))
            return false;
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
    /// Optional views are null only for absent respective descriptors; no caller storage survives this call.
    /// s and values supply the structural stamp and P*7 poses; feature/fluid_view/applied_view/moment_view/plane_view
    /// supply their complete catalog populations. Return latched validation or I/O failure on error.
    Status append(const Stamp &s, std::span<const double> values, const WinchFrameView *feature,
                  const FluidFrameView* fluid_view = nullptr, const AppliedLoadsFrameView* applied_view = nullptr,
                  const ElasticMomentFrameView* moment_view = nullptr,
                  const InflowPlanesFrameView* plane_view = nullptr) noexcept {
        // Validate lifecycle, full structural population and optional exact source binding.
        // Check every finite optional value/status and A/B/D consistency before copying any population.
        // Encode unchanged input bits and packed masks, advance accepted counters, then flush a full group.
        if (state != State::open)
            return state == State::failed ? result : error(StatusCode::invalid_state, 11);
        if (bool(feature) != !winches.ports.empty() || bool(fluid_view) != (fluid.samples != 0) ||
            bool(applied_view) != (applied.generation != 0) || bool(moment_view) != (moments.cuts != 0) ||
            bool(plane_view) != (planes.generation != 0) ||
            values.size() != samples * 7 ||
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
        if (fluid_view) {
            if (fluid_view->poses.size() != fluid.samples ||
                fluid_view->preparation_generation != fluid.generation || !same_stamp(s, fluid_view->stamp) ||
                !phase_valid(fluid_view->phase, s) || (feature && feature->phase != fluid_view->phase))
                return stop(StatusCode::invalid_contract, 11);
            for (const auto& sample /* Complete caller-owned F row admitted before copying any group bytes. */ : fluid_view->poses)
                if (!fluid_pose_valid(sample))
                    return stop(StatusCode::invalid_contract, 11);
        }
        if (stats.accepted_frames >= INT64_MAX)
            return stop(StatusCode::resource_limit, 11);
        // Admit every completed interval and logical row before any group population is serialized.
        if (applied_view) {
            if (applied_view->attachments.size() != applied.attachments.size() ||
                applied_view->preparation_generation != applied.generation || !same_stamp(s, applied_view->stamp) ||
                !phase_valid(applied_view->phase, s) || (feature && feature->phase != applied_view->phase) ||
                (fluid_view && fluid_view->phase != applied_view->phase))
                return stop(StatusCode::invalid_contract, 11);
            std::array<std::byte, 96> common{}, prefix{}; // Bounded identity scratch; compare encoded fields, never C++ padding.
            for (std::size_t i /* Complete attachment ordinal; all validate before any population is copied. */ = 0; i < applied_view->attachments.size(); ++i) {
                const auto& attachment /* Borrowed exact completed/unavailable interval and logical rows. */ = applied_view->attachments[i];
                if (attachment.samples.size() != applied.attachments[i].station_count ||
                    !applied_interval_valid(attachment.interval, applied_view->phase, s, have_stamp ? &last_stamp : nullptr))
                    return stop(StatusCode::invalid_contract, 11);
                encode_applied_interval(prefix.data(), attachment.interval);
                if (i == 0)
                    common = prefix;
                else if (common != prefix)
                    return stop(StatusCode::invalid_contract, 11);
                for (const auto& sample /* Actual source point and pre-transfer physical density. */ : attachment.samples)
                    if (!applied_sample_valid(sample, attachment.interval.status == AppliedIntervalStatus::unavailable))
                        return stop(StatusCode::invalid_contract, 11);
            }
        }
        // Admit the complete current-state moment population before copying any group population.
        if (moment_view) {
            if (moment_view->moments.size() != moments.cuts ||
                moment_view->preparation_generation != moments.generation || !same_stamp(s, moment_view->stamp) ||
                !phase_valid(moment_view->phase, s) || (feature && feature->phase != moment_view->phase) ||
                (fluid_view && fluid_view->phase != moment_view->phase) ||
                (applied_view && applied_view->phase != moment_view->phase))
                return stop(StatusCode::invalid_contract, 11);
            for (const auto& moment /* Every signed cut triple is borrowed and checked before any raw copy. */ : moment_view->moments)
                if (!moment_sample_valid(moment))
                    return stop(StatusCode::invalid_contract, 11);
        }
        // Admit every plane's identity, fixed extent and velocity before copying any group population.
        if (plane_view) {
            if (plane_view->planes.size() != planes.planes.size() ||
                plane_view->preparation_generation != planes.generation || !same_stamp(s, plane_view->stamp) ||
                !phase_valid(plane_view->phase, s) || (feature && feature->phase != plane_view->phase) ||
                (fluid_view && fluid_view->phase != plane_view->phase) ||
                (applied_view && applied_view->phase != plane_view->phase) ||
                (moment_view && moment_view->phase != plane_view->phase))
                return stop(StatusCode::invalid_contract, 11);
            for (std::size_t i /* Dense plane ID and immutable runtime extent position. */ = 0; i < plane_view->planes.size(); ++i) {
                const auto& plane /* Complete caller-owned plane population, borrowed only through this append. */ = plane_view->planes[i];
                if (plane.plane_id != i || plane.samples.size() != planes.planes[i].sample_count)
                    return stop(StatusCode::invalid_contract, 11);
                for (const auto& sample /* Original finite world triple and explicit availability bit. */ : plane.samples)
                    if (!plane_sample_valid(sample))
                        return stop(StatusCode::invalid_contract, 11);
            }
        }
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
        if (fluid_view) {
            auto* f /* Bounded F slot; following binary64 rows may be unaligned. */ = fluid_batch.data() + stats.buffered_frames * fluid_frame_bytes;
            std::copy_n(b, 64, f);
            u64(f + 64, fluid_view->preparation_generation);
            u32(f + 72, static_cast<std::uint32_t>(fluid_view->phase));
            u32(f + 76, 0);
            std::fill_n(f + 80, static_cast<std::size_t>(fluid_mask_bytes), std::byte{});
            for (std::size_t row /* Dense independent F sample ordinal. */ = 0; row < fluid_view->poses.size(); ++row) {
                const auto& sample /* Original valid or canonical unavailable row. */ = fluid_view->poses[row];
                if (sample.valid)
                    f[80 + row / 8] |= std::byte(1U << (row % 8));
                for (std::size_t k /* Seven scalar components copied by bytewise encoder. */ = 0; k < 7; ++k)
                    f64(f + 80 + fluid_mask_bytes + row * 56 + k * 8, sample.world_pose[k]);
            }
        }
        // Serialize admitted load intervals and masks into their preallocated unaligned blocks.
        if (applied_view) {
            auto* record /* Bounded raw record, whose nested prefixes and scalars may be unaligned. */ = applied_batch.data() + stats.buffered_frames * applied.frame_bytes;
            std::copy_n(b, 64, record);
            u64(record + 64, applied_view->preparation_generation);
            u32(record + 72, static_cast<std::uint32_t>(applied_view->phase));
            u32(record + 76, 0);
            for (std::size_t i /* Payload attachment order, independent of global station-range ordering. */ = 0; i < applied_view->attachments.size(); ++i) {
                const auto& attachment /* Already fully admitted borrowed logical population. */ = applied_view->attachments[i];
                const auto& layout /* Immutable setup-sized record extent. */ = applied.attachments[i];
                const auto mask_bytes /* Packed validity bytes; high padding bits remain zero. */ = (layout.station_count + 7) / 8;
                auto* block /* Exact nested interval prefix destination. */ = record + layout.block_offset;
                encode_applied_interval(block, attachment.interval);
                std::fill_n(block + 96, static_cast<std::size_t>(mask_bytes), std::byte{});
                for (std::size_t row /* Original local logical-node index within this attachment. */ = 0; row < attachment.samples.size(); ++row) {
                    const auto& sample /* Original finite/canonical source row, with bits retained. */ = attachment.samples[row];
                    if (sample.valid)
                        block[96 + row / 8] |= std::byte(1U << (row % 8));
                    for (std::size_t k /* World xyz component, position followed by force density. */ = 0; k < 3; ++k) {
                        f64(block + 96 + mask_bytes + row * 48 + k * 8, sample.source_position_world_m[k]);
                        f64(block + 96 + mask_bytes + row * 48 + (k + 3) * 8, sample.force_density_world_N_per_m[k]);
                    }
                }
            }
        }
        // Encode dense signed moment triples bytewise after the potentially unaligned packed mask.
        if (moment_view) {
            auto* record /* Preallocated moment record destination, independent of caller storage alignment. */ =
                moment_batch.data() + stats.buffered_frames * moment_frame_bytes;
            std::copy_n(b, 64, record);
            u64(record + 64, moment_view->preparation_generation);
            u32(record + 72, static_cast<std::uint32_t>(moment_view->phase));
            u32(record + 76, 0);
            std::fill_n(record + 80, static_cast<std::size_t>(moment_mask_bytes), std::byte{});
            for (std::size_t row /* Dense independent cut ordinal, not a structural sample ID. */ = 0; row < moment_view->moments.size(); ++row) {
                const auto& moment /* Original admitted signed triple and authoritative availability. */ = moment_view->moments[row];
                if (moment.valid)
                    record[80 + row / 8] |= std::byte(1U << (row % 8));
                for (std::size_t component /* Current-N Mx/My/Mz order; no permutation or normalization. */ = 0; component < 3; ++component)
                    f64(record + 80 + moment_mask_bytes + row * 24 + component * 8, moment.moment_N_Nm[component]);
            }
        }
        // Pack each admitted plane's independent mask followed by unaligned point-major velocity triples.
        if (plane_view) {
            auto* record /* Setup-sized raw destination, with a common prefix only once per frame. */ =
                plane_batch.data() + stats.buffered_frames * planes.frame_bytes;
            std::copy_n(b, 64, record);
            u64(record + 64, plane_view->preparation_generation);
            u32(record + 72, static_cast<std::uint32_t>(plane_view->phase));
            u32(record + 76, 0);
            for (std::size_t i /* Dense catalog plane order, unchanged from the fully admitted borrowed views. */ = 0; i < plane_view->planes.size(); ++i) {
                const auto& plane /* Original point population in u-fastest order. */ = plane_view->planes[i];
                const auto& layout /* Fixed retained sample extent and mask offset. */ = planes.planes[i];
                const auto mask_bytes /* Independent ceil(N/8), with high padding bits kept zero. */ =
                    layout.sample_count / 8 + (layout.sample_count % 8 != 0);
                auto* block /* Mask destination; subsequent doubles need not be aligned. */ = record + layout.block_offset;
                std::fill_n(block, static_cast<std::size_t>(mask_bytes), std::byte{});
                for (std::size_t row /* Original j=u+nu*v index; no transposition or coordinate reconstruction. */ = 0; row < plane.samples.size(); ++row) {
                    const auto& sample /* Original admitted world velocity and authoritative validity. */ = plane.samples[row];
                    if (sample.valid)
                        block[row / 8] |= std::byte(1U << (row % 8));
                    for (std::size_t component /* World ux,uy,uz order, copied without magnitude/normalization tests. */ = 0; component < 3; ++component)
                        f64(block + mask_bytes + row * 24 + component * 8, sample.world_velocity_mps[component]);
                }
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
    // Admit catalog and whole-group limits before retaining payload storage. Charge actual
    // physical-reference/overlay-layout capacities and fixed scratch, derive bounded raw buffers, then open
    // an exclusive partial stream and write/flush the header and unchanged catalog bytes.

    try {
        if (path.empty() || catalog.empty() || catalog.size() > catalog_limit ||
            catalog.size() > opts.max_catalog_bytes || opts.max_batch_frames == 0 ||
            opts.max_batch_frames > INT64_MAX || opts.max_batch_bytes < 64 ||
            opts.max_batch_bytes > chunk_limit || opts.max_workspace_bytes > workspace_limit ||
            opts.max_workspace_bytes < encoding_workspace)
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
        FluidCatalog fluid; // Scalar F facts; topology admission has no retained reference table.
        if (!fluid_catalog_valid(j, fluid))
            return std::unexpected(error(StatusCode::invalid_contract, 10));
        AppliedCatalog applied; // Scalar admission facts first; retained layout allocation follows aggregate preflight.
        if (!applied_catalog_valid(j, applied))
            return std::unexpected(error(StatusCode::invalid_contract, 10));
        MomentCatalog moments; // Only scalar cut count/generation survives complete setup topology admission.
        if (!moment_catalog_valid(j, moments))
            return std::unexpected(error(StatusCode::invalid_contract, 10));
        PlaneCatalog planes; // Scalar plane facts first; retained extents allocate only after whole-group preflight.
        if (!plane_catalog_valid(j, planes))
            return std::unexpected(error(StatusCode::invalid_contract, 10));
        // Preflight the minimum whole group before the optional physical-reference reserve.
        std::uint64_t feature_count{}; // Advertised winch count, subsequently checked by full topology admission.
        if (j.contains("winch_geometry")) {
            const auto& arrangements /* Borrowed population controlling fixed reference and record extents. */ = j.at("winch_geometry").at("arrangements");
            if (!arrangements.is_array() || arrangements.empty() || arrangements.size() > sample_limit)
                return std::unexpected(error(StatusCode::invalid_contract, 10));
            feature_count = arrangements.size();
        }
        if (samples > (INT64_MAX - 64) / 56 || feature_count > (INT64_MAX - 80) / 104 ||
            fluid.samples > (INT64_MAX - 87) / 57 || moments.cuts > (INT64_MAX - 87) / 25)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        /* Complete physical frame: copied identity prefix plus seven values per sample. */ const auto frame_bytes = 64 + samples * 56;
        /* Optional feature frame: full structural stamp, source binding, then complete arrangements. */ const auto feature_frame_bytes = feature_count ? 80 + feature_count * 104 : 0;
        const auto fluid_mask_bytes /* Number of packed validity bytes, including zero high padding bits. */ = (fluid.samples + 7) / 8;
        const auto fluid_frame_bytes /* Optional common prefix, mask and seven binary64 values per F sample. */ = fluid.samples ? 80 + fluid_mask_bytes + fluid.samples * 56 : 0;
        const auto moment_mask_bytes /* Packed validity bytes; unused high bits encode as zero. */ = (moments.cuts + 7) / 8;
        const auto moment_frame_bytes /* Optional common prefix plus dense signed cut triples, with no alignment padding. */ =
            moments.cuts ? 80 + moment_mask_bytes + moments.cuts * 24 : 0;
        if (frame_bytes > INT64_MAX - feature_frame_bytes ||
            frame_bytes + feature_frame_bytes > INT64_MAX - fluid_frame_bytes ||
            frame_bytes + feature_frame_bytes + fluid_frame_bytes > INT64_MAX - applied.frame_bytes ||
            frame_bytes + feature_frame_bytes + fluid_frame_bytes + applied.frame_bytes > INT64_MAX - moment_frame_bytes ||
            frame_bytes + feature_frame_bytes + fluid_frame_bytes + applied.frame_bytes + moment_frame_bytes > INT64_MAX - planes.frame_bytes)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        const auto group_frame_bytes /* One complete group shares the aggregate raw limit. */ =
            frame_bytes + feature_frame_bytes + fluid_frame_bytes + applied.frame_bytes + moment_frame_bytes + planes.frame_bytes;
        constexpr auto reference_row_bytes /* Actual stored A/B/D representation, including any implementation padding. */ = sizeof(std::array<std::uint64_t, 3>);
        const auto requested_reference_bytes /* Minimum retained reference allocation under the advertised count. */ = feature_count * reference_row_bytes;
        const auto requested_applied_bytes /* Minimum retained load layout; admitted count bounds make this product safe. */ =
            applied.attachment_count * sizeof(AppliedAttachmentLayout);
        const auto requested_plane_bytes /* Minimum retained plane layout; no geometry/point population is retained. */ =
            planes.plane_count * sizeof(PlaneLayout);
        if (requested_applied_bytes > INT64_MAX - requested_plane_bytes)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        const auto requested_overlay_bytes /* Additive minimum retained layouts, never a replacement for existing applied references. */ =
            requested_applied_bytes + requested_plane_bytes;
        if (requested_reference_bytes > opts.max_workspace_bytes - encoding_workspace ||
            requested_overlay_bytes > opts.max_workspace_bytes - encoding_workspace - requested_reference_bytes ||
            group_frame_bytes > opts.max_batch_bytes ||
            group_frame_bytes > opts.max_workspace_bytes - encoding_workspace - requested_reference_bytes - requested_overlay_bytes)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        WinchCatalog winches; // Full immutable bindings, empty on the original recorder path.
        if (!winch_catalog_valid(j, winches) ||
            (fluid.samples && feature_count && fluid.generation != winches.generation) ||
            (applied.generation && ((fluid.samples && applied.generation != fluid.generation) ||
                                   (feature_count && applied.generation != winches.generation))) ||
            (moments.cuts && ((fluid.samples && moments.generation != fluid.generation) ||
                             (applied.generation && moments.generation != applied.generation) ||
                             (feature_count && moments.generation != winches.generation))) ||
            (planes.generation && ((fluid.samples && planes.generation != fluid.generation) ||
                                  (applied.generation && planes.generation != applied.generation) ||
                                  (moments.cuts && planes.generation != moments.generation) ||
                                  (feature_count && planes.generation != winches.generation))))
            return std::unexpected(error(StatusCode::invalid_contract, 10));
        if (winches.ports.capacity() > (opts.max_workspace_bytes - encoding_workspace) / reference_row_bytes)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        const auto reference_bytes /* Charged retained capacity rather than logical size. */ = winches.ports.capacity() * reference_row_bytes;
        if (requested_overlay_bytes > opts.max_workspace_bytes - encoding_workspace - reference_bytes ||
            group_frame_bytes > opts.max_workspace_bytes - encoding_workspace - reference_bytes - requested_overlay_bytes ||
            applied.attachment_count > applied.attachments.max_size() || planes.plane_count > planes.planes.max_size())
            return std::unexpected(error(StatusCode::resource_limit, 10));
        // Allocate the admitted applied layout, then recheck the whole minimum group before plane reserve.
        applied.attachments.reserve(static_cast<std::size_t>(applied.attachment_count));
        if (applied.attachments.capacity() > (opts.max_workspace_bytes - encoding_workspace - reference_bytes) / sizeof(AppliedAttachmentLayout))
            return std::unexpected(error(StatusCode::resource_limit, 10));
        const auto applied_reference_bytes /* Actual applied capacity remains charged alongside all plane entries. */ =
            applied.attachments.capacity() * sizeof(AppliedAttachmentLayout);
        if (requested_plane_bytes > opts.max_workspace_bytes - encoding_workspace - reference_bytes - applied_reference_bytes ||
            group_frame_bytes > opts.max_workspace_bytes - encoding_workspace - reference_bytes - applied_reference_bytes - requested_plane_bytes)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        planes.planes.reserve(static_cast<std::size_t>(planes.plane_count));
        if (planes.planes.capacity() > (opts.max_workspace_bytes - encoding_workspace - reference_bytes - applied_reference_bytes) / sizeof(PlaneLayout))
            return std::unexpected(error(StatusCode::resource_limit, 10));
        const auto plane_reference_bytes /* Actual per-plane vector capacity, including any implementation surplus. */ =
            planes.planes.capacity() * sizeof(PlaneLayout);
        const auto overlay_bytes /* Both retained layout capacities, distinct from physical winch references. */ =
            applied_reference_bytes + plane_reference_bytes;
        const auto effective_raw_budget /* Scratch and reference charges reduce the available raw group budget. */ =
            std::min<std::uint64_t>(opts.max_batch_bytes, opts.max_workspace_bytes - encoding_workspace - reference_bytes - overlay_bytes);
        /* Effective whole-group capacity under count, raw and workspace limits; zero rejects before any file. */ const auto frames =
            std::min(opts.max_batch_frames, effective_raw_budget / group_frame_bytes);
        if (!frames)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        if (applied.generation) {
            std::uint64_t offset = 80; // First attachment follows the copied common source prefix.
            for (const auto& attachment /* Already validated catalog attachment in payload order. */ : j.at("applied_loads").at("attachments")) {
                const auto count /* Complete local station population; no runtime JSON lookup remains. */ = id(attachment.at("station_count"));
                applied.attachments.push_back({count, offset});
                offset += 96 + (count + 7) / 8 + count * 48;
            }
        }
        if (planes.generation) {
            std::uint64_t offset = 80; // Only one common source prefix precedes the ordered plane blocks.
            for (const auto& plane /* Admitted dense plane descriptor, read only during inactive setup. */ : j.at("inflow_planes").at("planes")) {
                const auto count /* Exact dimension product already bounded by complete catalog admission. */ = plane_id(plane.at("nu")) * plane_id(plane.at("nv"));
                const auto mask /* Per-plane packed validity width, with no alignment padding. */ = count / 8 + (count % 8 != 0);
                planes.planes.push_back({count, offset});
                offset += mask + count * 24;
            }
        }
        // Check every raw allocation and the aggregate before evaluating products or creating files.
        /* Host vector extents and wire signed extents share this admitted byte ceiling. */ const auto storage_limit = std::min<std::uint64_t>(effective_raw_budget, std::numeric_limits<std::size_t>::max());
        if (frame_bytes > storage_limit / frames ||
            (feature_frame_bytes && feature_frame_bytes > storage_limit / frames) ||
            (fluid_frame_bytes && fluid_frame_bytes > storage_limit / frames) ||
            (applied.frame_bytes && applied.frame_bytes > storage_limit / frames) ||
            (moment_frame_bytes && moment_frame_bytes > storage_limit / frames) ||
            (planes.frame_bytes && planes.frame_bytes > storage_limit / frames) ||
            group_frame_bytes > storage_limit / frames)
            return std::unexpected(error(StatusCode::resource_limit, 10));
        /* Checked structural allocation request, never duration-sized. */ const auto pose_bytes = frames * frame_bytes;
        /* Checked feature allocation request, zero for feature-free catalogs. */ const auto feature_bytes = frames * feature_frame_bytes;
        const auto fluid_bytes /* Checked mask/pose allocation request, zero without an F descriptor. */ = frames * fluid_frame_bytes;
        const auto applied_bytes /* Checked physical point/density allocation request, zero without loads. */ = frames * applied.frame_bytes;
        const auto moment_bytes /* Checked signed-triple allocation request, zero without a moment descriptor. */ = frames * moment_frame_bytes;
        const auto plane_bytes /* Checked complete plane-batch allocation request, zero without its descriptor. */ = frames * planes.frame_bytes;
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
        p.fluid_frame_bytes = fluid_frame_bytes;
        p.fluid_mask_bytes = fluid_mask_bytes;
        p.moment_frame_bytes = moment_frame_bytes;
        p.moment_mask_bytes = moment_mask_bytes;
        p.winches = std::move(winches);
        p.fluid = fluid;
        p.applied = std::move(applied);
        p.moments = moments;
        p.planes = std::move(planes);
        if (pose_bytes > p.batch.max_size() || feature_bytes > p.feature_batch.max_size() ||
            fluid_bytes > p.fluid_batch.max_size() || applied_bytes > p.applied_batch.max_size() ||
            moment_bytes > p.moment_batch.max_size() || plane_bytes > p.plane_batch.max_size())
            return std::unexpected(error(StatusCode::resource_limit, 10));
        p.batch.resize(static_cast<std::size_t>(pose_bytes));
        p.feature_batch.resize(static_cast<std::size_t>(feature_bytes));
        p.fluid_batch.resize(static_cast<std::size_t>(fluid_bytes));
        p.applied_batch.resize(static_cast<std::size_t>(applied_bytes));
        p.moment_batch.resize(static_cast<std::size_t>(moment_bytes));
        p.plane_batch.resize(static_cast<std::size_t>(plane_bytes));
        // Recheck actual implementation capacities before creating the partial file. New vectors
        // normally have exactly requested capacity; no surplus allocation is hidden by accounting.
        if (p.batch.capacity() > effective_raw_budget ||
            p.feature_batch.capacity() > effective_raw_budget - p.batch.capacity() ||
            p.fluid_batch.capacity() > effective_raw_budget - p.batch.capacity() - p.feature_batch.capacity() ||
            p.applied_batch.capacity() > effective_raw_budget - p.batch.capacity() - p.feature_batch.capacity() - p.fluid_batch.capacity() ||
            p.moment_batch.capacity() > effective_raw_budget - p.batch.capacity() - p.feature_batch.capacity() - p.fluid_batch.capacity() - p.applied_batch.capacity() ||
            p.plane_batch.capacity() > effective_raw_budget - p.batch.capacity() - p.feature_batch.capacity() - p.fluid_batch.capacity() - p.applied_batch.capacity() - p.moment_batch.capacity())
            return std::unexpected(error(StatusCode::resource_limit, 10));
        p.stats.batch_capacity_bytes = p.batch.capacity() + p.feature_batch.capacity() + p.fluid_batch.capacity() + p.applied_batch.capacity() + p.moment_batch.capacity() + p.plane_batch.capacity();
        p.stats.effective_batch_frames = frames;
        p.stats.index_workspace_bytes = 256;
        p.stats.physical_reference_bytes = reference_bytes;
        p.stats.encoding_workspace_bytes = encoding_workspace;
        p.stats.overlay_reference_bytes = overlay_bytes;
        p.stats.workspace_capacity_bytes = p.stats.batch_capacity_bytes + reference_bytes + overlay_bytes + encoding_workspace;
        p.stats.effective_raw_budget_bytes = effective_raw_budget;
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
Status Recorder::append_frame(const FrameGroupView& group) noexcept {
    // Admit/copy all borrowed populations synchronously through the common atomic group path.
    if (!impl_)
        return error(StatusCode::invalid_state, 11);
    return impl_->append(group.stamp, group.structural_poses, group.winches, group.fluid_poses,
                         group.applied_loads, group.elastic_moments, group.inflow_planes);
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
