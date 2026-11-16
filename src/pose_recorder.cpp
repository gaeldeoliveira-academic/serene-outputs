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
/// Owns a FILE, raw batch and path strings, with only constant-size linked-chunk index
/// state. Catalog parse trees are setup-only. Borrowed fault context must outlive it.
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
    std::uint64_t samples{}, ///< Fixed catalog sample count per frame.
        frame_bytes{}, ///< Complete raw frame bytes, including 64-byte identity prefix.
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

        /* Canonical zero-initialized chunk header, including reserved fields. */ std::array<std::byte, 80> h{};
        h[0] = std::byte{'P'};
        h[1] = std::byte{'C'};
        h[2] = std::byte{'H'};
        h[3] = std::byte{'K'};
        u16(h.data() + 4, kind);
        u16(h.data() + 6, 1);
        u32(h.data() + 8, 80);
        u32(h.data() + 16, kind == 1 ? 1 : 0);
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
    /// Persist currently buffered frames as one backward-linked raw pose chunk; no-op if empty.
    bool flush_batch() noexcept {
        // Snapshot the file frontier, write/flush the batch, update first/last links and persisted counters, then empty the batch.

        if (!stats.buffered_frames)
            return true;
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
        /* Complete raw frame size: identity prefix plus seven values per sample. */ const auto frame_bytes = 64 + samples * 56;
        /* Effective whole-frame batch capacity under both caller limits. */ const auto frames = std::min(opts.max_batch_frames, opts.max_batch_bytes / frame_bytes);
        if (!frames)
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
        p.batch.resize(static_cast<std::size_t>(frames * frame_bytes));
        p.stats.batch_capacity_bytes = p.batch.size();
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
    // Require open lifecycle and valid source stamp, check complete finite poses/unit quaternion
    // norms, encode one raw frame into the batch, advance accepted counters, and flush a full batch.

    if (!impl_)
        return error(StatusCode::invalid_state, 11);
    /* Exclusively borrowed recorder state for this operation. */ auto &p = *impl_;
    if (p.state != State::open)
        return p.state == State::failed ? p.result : error(StatusCode::invalid_state, 11);
    if (values.size() != p.samples * 7 || !stamp_valid(s, p.have_stamp ? &p.last_stamp : nullptr))
        return p.stop(StatusCode::invalid_contract, 11);
    for (std::size_t i /* Start of the next seven-value position/quaternion block. */ = 0; i < values.size(); i += 7) {
        for (std::size_t k /* Component offset checked for finite input. */ = 0; k < 7; ++k)
            if (!std::isfinite(values[i + k]))
                return p.stop(StatusCode::invalid_contract, 11);
        /* Quaternion squared norm accumulated without normalization. */ double norm = 0;
        for (std::size_t k /* Quaternion component offset accumulated into squared norm. */ = 3; k < 7; ++k)
            norm += values[i + k] * values[i + k];
        if (!std::isfinite(norm) || std::abs(norm - 1) > 1e-6)
            return p.stop(StatusCode::invalid_contract, 11);
    }
    if (p.stats.accepted_frames >= INT64_MAX)
        return p.stop(StatusCode::resource_limit, 11);
    // Serialize only after the complete frame passes admission; never normalize its values.
    /* Destination slot for the next complete canonical raw frame. */ auto *b = p.batch.data() + p.stats.buffered_frames * p.frame_bytes;
    std::fill_n(b, 64, std::byte{});
    u64(b, p.stats.accepted_frames);
    u32(b + 8, static_cast<std::uint32_t>(s.analysis));
    u32(b + 12, s.events);
    f64(b + 16, s.time);
    u64(b + 24, s.state_id);
    f64(b + 32, s.load_factor);
    u64(b + 40, p.samples);
    for (std::size_t k /* Frame value ordinal encoded as exact binary64 bits. */ = 0; k < values.size(); ++k)
        f64(b + 64 + k * 8, values[k]);
    ++p.stats.accepted_frames;
    ++p.stats.buffered_frames;
    p.last_stamp = s;
    p.have_stamp = true;
    if (p.stats.buffered_frames == p.stats.effective_batch_frames && !p.flush_batch())
        return p.result;
    return {};
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
