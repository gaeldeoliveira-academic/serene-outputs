#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <serene/outputs/archive.hpp>
#include <system_error>
#include <type_traits>
#include <utility>
#ifdef _WIN32
#define NOMINMAX
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace serene::outputs {
namespace {
/// Private exception envelope carrying fixed Status through writer helpers.
/// Owns no dynamic message; ArchiveSession entry points translate it back to Status.
struct Failure {
    Status status; ///< Exact failure domain/context to propagate.
};
/// Throw the private Failure with code/op when ok is false; otherwise do nothing.
void require(bool ok, StatusCode code = StatusCode::invalid_contract, std::uint32_t op = 30) {
    // Turn an invariant/admission failure into the fixed status envelope.

    if (!ok)
        throw Failure{error(code, op)};
}
/// Throw an I/O Failure with operation and current errno when ok is false.
void io(bool ok, std::uint32_t operation) {
    // Capture native error context only on failed I/O and propagate it to the public boundary.

    if (!ok) {
        /* Fixed I/O status carrying current native error. */ auto s = error(StatusCode::io_failure, operation);
        s.native_code = errno;
        throw Failure{s};
    }
}
/// Return the wire ordinal of section state s; callers separately admit the enum.
std::uint8_t code(SectionState s) {
    // Convert the already-admitted state without allocating.

    return static_cast<std::uint8_t>(s);
}
/// Return whether s has retained bytes: included or partial.
bool included(SectionState s) {
    // Distinguish retained content from disabled/unavailable roles.

    return s == SectionState::included || s == SectionState::partial;
}
/// Write value little-endian into borrowed bytes; caller supplies a width of at most eight.
void put(std::span<std::byte> bytes, std::uint64_t value) {
    // Emit each low-order byte into the fixed destination span.

    for (std::size_t k /* Little-endian output byte position. */ = 0; k < bytes.size(); k++)
        bytes[k] = std::byte(value >> (8 * k));
}
/// Return the owned eight-byte little-endian representation of x.
std::array<std::byte, 8> le64(std::uint64_t x) {
    // Encode one unsigned 64-bit field into local fixed storage.

    /* Owned canonical integer bytes. */ std::array<std::byte, 8> b;
    put(b, x);
    return b;
}
/// Directory dtype IDs; physical widths and metadata spellings are defined below.
enum class Type : std::uint32_t {
    json = 1,
    u8 = 2,
    u32 = 3,
    u64 = 4,
    f64 = 5,
    frames = 6,
    i8 = 7,
    i32 = 8
};
/// Return static metadata spelling for typed-array t; JSON is not an array descriptor.
/// Throw Failure for a kind without a supported array spelling.
const char *dtype(Type t) {
    // Select the fixed wire vocabulary rather than derive it from host C++ types.

    switch (t) {
    case Type::u8:
        return "u8";
    case Type::u32:
        return "u32";
    case Type::u64:
        return "u64";
    case Type::f64:
        return "f64";
    case Type::frames:
        return "frames77";
    case Type::i8:
        return "i8";
    case Type::i32:
        return "i32";
    default:
        throw Failure{error(StatusCode::invalid_contract, 30)};
    }
}
/// Return fixed wire element width of t; byte/JSON fallback has width one.
std::uint64_t width(Type t) {
    // Map admitted typed columns to canonical little-endian storage widths.

    switch (t) {
    case Type::u8:
    case Type::i8:
        return 1;
    case Type::u32:
    case Type::i32:
        return 4;
    case Type::u64:
    case Type::f64:
        return 8;
    case Type::frames:
        return 77;
    default:
        return 1;
    }
}
/// Fixed-capacity JSON emitter borrowed by ArchiveSession metadata routines.
/// Owns counters/depth flags but borrows a prepared output span. Generated keys are
/// known once, so no DOM/key table is allocated. The caller balances object/array
/// scopes and consumes bytes before the same preparation buffer is reused.
/// Failure throws the private fixed Status envelope; partial output is not published.
class Json {
    std::span<std::byte> out_; ///< Borrowed prepared serialization buffer.
    std::size_t used_{}; ///< Bytes emitted into out_.
    std::array<bool, 16> first_{}; ///< Per-open-container first-item flags for comma insertion.
    std::size_t depth_{}; ///< Active object/array nesting depth, at most sixteen.
    std::uint64_t values_{}, ///< Emitted JSON value count for complexity admission.
        members_{}; ///< Emitted object-member count for complexity admission.
    /// Append exact borrowed syntax bytes s, failing if the prepared buffer cannot hold them.
    void raw(std::string_view s) {
        // Check remaining capacity, copy bytes, then advance the emitted extent.

        require(s.size() <= out_.size() - used_, StatusCode::resource_limit);
        std::memcpy(out_.data() + used_, s.data(), s.size());
        used_ += s.size();
    }
    /// Append borrowed UTF-8 text s as one JSON string, escaping quotes/backslashes/control bytes.
    void quoted(std::string_view s) {
        // Validate UTF-8, write delimiters, escape only required bytes, and close the string.

        require(valid_utf8(s));
        raw("\"");
        for (unsigned char /* Unsigned UTF-8 byte considered for JSON escaping. */ c : s) {
            if (c == '"')
                raw("\\\"");
            else if (c == '\\')
                raw("\\\\");
            else if (c < 32) {
                /* JSON Unicode escape buffer for one ASCII control byte. */ char b[6] = {
                    '\\', 'u', '0', '0', "0123456789abcdef"[c >> 4], "0123456789abcdef"[c & 15]};
                raw({b, 6});
            } else {
                /* One unchanged UTF-8 byte. */ char b = static_cast<char>(c);
                raw({&b, 1});
            }
        }
        raw("\"");
    }
    /// Start one value, using optional borrowed key for an object member; null means array/root.
    void prefix(const char *key) {
        // Insert the container comma, optionally emit the key/colon, and charge bounded complexity.

        if (depth_) {
            if (!first_[depth_ - 1])
                raw(",");
            first_[depth_ - 1] = false;
        }
        if (key) {
            require(++members_ <= 16384, StatusCode::resource_limit);
            quoted(key);
            raw(":");
        }
        require(++values_ <= 16384, StatusCode::resource_limit);
    }

  public:
    /// Bind prepared writable buffer b with an empty serialization state; retain no ownership.
    explicit Json(std::span<std::byte> b) : out_(b) {
        // Keep the borrowed output span; all counters use their zero defaults.

    }
    /// Begin an object value under optional key; caller must later call end(true).
    void object(const char *key = nullptr) {
        // Emit the value prefix, bound nesting, open the object and reset its comma state.

        prefix(key);
        require(depth_ < 16, StatusCode::resource_limit);
        raw("{");
        first_[depth_++] = true;
    }
    /// Begin an array value under key; null is allowed for an unnamed value.
    void array(const char *key) {
        // Emit the value prefix, bound nesting, open the array and reset its comma state.

        prefix(key);
        require(depth_ < 16, StatusCode::resource_limit);
        raw("[");
        first_[depth_++] = true;
    }
    /// Close the current container as object or array; require a matching open scope.
    void end(bool object = true) {
        // Decrement depth and emit the selected closing delimiter.

        require(depth_ > 0);
        depth_--;
        raw(object ? "}" : "]");
    }
    /// Emit borrowed string s under optional key, with no retained string allocation.
    void text(const char *key, std::string_view s) {
        // Prefix the value and encode its quoted UTF-8 bytes.

        prefix(key);
        quoted(s);
    }
    /// Emit exact unsigned n under optional key using locale-independent decimal text.
    void number(const char *key, std::uint64_t n) {
        // Prefix the value, format into fixed stack storage, then append the produced bytes.

        prefix(key);
        /* Stack decimal integer buffer. */ char b[32];
        /* Locale-independent formatting result/end pointer. */ auto x = std::to_chars(b, b + sizeof b, n);
        require(x.ec == std::errc{});
        raw({b, static_cast<std::size_t>(x.ptr - b)});
    }
    /// Emit finite binary64 n under optional key with round-trip decimal precision.
    void real(const char *key, double n) {
        // Reject nonfinite input, format locally, then append .0 when needed to preserve JSON real type.

        require(std::isfinite(n));
        prefix(key);
        /* Stack round-trip real buffer. */ char b[64];
        /* Locale-independent formatting result/end pointer. */ auto x = std::to_chars(b, b + sizeof b, n, std::chars_format::general,
                               std::numeric_limits<double>::max_digits10);
        require(x.ec == std::errc{});
        /* Borrowed formatted real text. */ std::string_view s{b, static_cast<std::size_t>(x.ptr - b)};
        raw(s);
        if (s.find_first_of(".eE") == s.npos)
            raw(".0");
    }
    /// Return the borrowed emitted prefix only when all containers have closed.
    std::span<const std::byte> bytes() const {
        // Check balanced nesting before exposing the bounded serialized byte span.

        require(depth_ == 0);
        return out_.first(used_);
    }
};
/// Explicit object-scope helper used by writer metadata routines.
/// Borrows Json and records whether finish was called; destruction deliberately does
/// not emit data or throw. Callers balance scopes explicitly before consuming bytes.
struct Group {
    Json &json; ///< Borrowed fixed-capacity emitter.
    bool open = true; ///< Whether this helper still owns one unmatched object close.
    /// Borrow the underlying emitter so existing typed metadata helpers accept this group.
    operator Json &() {
        // Return the same emitter reference without changing scope state.

        return json;
    }
    /// Close this object exactly once; throw Failure on a repeated close.
    void finish() {
        // Check the open flag, emit the object close, then mark the helper finished.

        require(open);
        json.end();
        open = false;
    }
};
/// Begin object n in borrowed emitter j and return its explicit-close helper.
Group group(Json &j, const char *n) {
    // Open the named object and bind a nonowning Group to the same emitter.

    j.object(n);
    return {j};
}
/// Write borrowed UTF-8 text under borrowed key n to emitter j; no input is retained.
void text(Json &j, const char *n, std::string_view s) {
    // Delegate to the emitter's canonical string serializer.

    j.text(n, s);
}
/// Write unsigned 8-bit value under borrowed key n to emitter j; no input is retained.
void u8(Json &j, const char *n, std::uint8_t x) {
    // Delegate to the emitter's canonical integer serializer.

    j.number(n, x);
}
/// Write unsigned 32-bit value under borrowed key n to emitter j; no input is retained.
void u32(Json &j, const char *n, std::uint32_t x) {
    // Delegate to the emitter's canonical integer serializer.

    j.number(n, x);
}
/// Write unsigned 64-bit value under borrowed key n to emitter j; no input is retained.
void u64(Json &j, const char *n, std::uint64_t x) {
    // Delegate to the emitter's canonical integer serializer.

    j.number(n, x);
}
/// Write finite binary64 value under borrowed key n to emitter j; no input is retained.
void f64(Json &j, const char *n, double x) {
    // Delegate to the emitter's canonical real serializer.

    j.real(n, x);
}
/// One in-progress section reservation owned by the current writer call.
/// Tracks offsets/extents and an incremental digest while borrowing no source payload.
/// reserve creates it, append advances it, and finish commits its directory record once.
struct Section {
    std::uint64_t id{}, ///< Reserved zero-based directory slot.
        offset{}, ///< Absolute file offset of section bytes.
        size{}, ///< Reserved/final byte extent; grows only for the terminal variable payload.
        written{}; ///< Bytes appended and hashed so far.
    Type type{}; ///< Canonical directory element kind.
    bool variable{}; ///< Whether this last section may extend the current file frontier.
    Sha256 hash; ///< Owned incremental digest over appended bytes only.
};
} // namespace
/// Single-run writer state owned uniquely by ArchiveSession.
/// Borrows preparation/codec and their reusable buffers, owns output FILE/path state,
/// and tracks fixed-size directory/source/trace indices. Helpers may throw Failure;
/// public boundaries latch failure. Destruction closes and removes unpublished scratch.
struct ArchiveSession::Impl {
    PreparedExport *prepared{}; ///< Borrowed immutable catalog plus exclusive reusable buffers.
    Codec *codec{}; ///< Borrowed prepared codec, used serially during result framing.
    FaultHook hook{}; ///< Copied callbacks borrowing caller-owned test state.
    std::filesystem::path destination, ///< Owned final no-clobber path.
        temp; ///< Owned exclusive unpublished scratch path.
    std::FILE *file{}; ///< Owned open scratch stream; null after checked close.
    SessionState state{SessionState::opened}; ///< Publicly observable attempt lifecycle.
    SaveStatistics stats{}; ///< Owned constant-size work/reservation counters.
    std::array<std::uint64_t, 7> calls{}; ///< Per-IoOperation deterministic fault-call ordinals.
    std::uint64_t next{128}, ///< Next unreserved file offset after bootstrap.
        position{}, ///< Tracked stdio cursor, including positional backfills.
        sections{}, ///< Number of reserved directory slots.
        finished{}, ///< Number of sections whose directory record is complete.
        root_id{}; ///< Final root JSON section ID.
    std::uint64_t source_bytes{}; ///< Cumulative copied provenance bytes charged against the prepared bound.
    std::array<std::uint64_t,5> trace_ids{}; ///< Trace metadata IDs in prepared authored order.
    std::array<std::uint64_t,69> source_ids{}; ///< Indexed source metadata IDs: five documents plus at most 64 logs.
    std::uint64_t source_count{}; ///< Actual indexed source catalog population.
    DiagnosticStatus diagnostic_status{DiagnosticStatus::disabled}; ///< Validated aggregate capture outcome.
    bool selected_catalog_included{}; ///< Whether result catalogs/values are retained for this run.
    std::array<SectionState,3> indexed_states{}; ///< Aggregate input/log/context source availability.
    /// Return whether an included selected catalog actually uses a fixed-accuracy profile.
    bool fixed_accuracy() const noexcept {
        // Ignore unavailable catalogs, then scan immutable profiles for the fixed-accuracy tag.

        if(!selected_catalog_included)return false;
        for(auto p /* Resolved profile inspected for actual fixed-accuracy use. */:prepared->description().run.profiles)
            if(p.mode()==CodecMode::fixed_accuracy)return true;
        return false;
    }
    /// Return whether any supported extension requires a 1.1 container.
    bool extended() const noexcept {
        // Combine indexed provenance, requested diagnostics and actual fixed-profile use.

        return prepared->description().indexed_provenance ||
               !prepared->description().run.diagnostics.empty() || fixed_accuracy();
    }
    /// Close owned I/O and remove unpublished scratch; never publish from destruction.
    ~Impl() {
        // Close any surviving stream, then best-effort remove the still-owned temporary path.

        if (file)
            std::fclose(file);
        if (!temp.empty()) {
            /* Best-effort filesystem cleanup/publication status. */ std::error_code ec;
            std::filesystem::remove(temp, ec);
        }
    }
    /// Advance op's ordinal and invoke the optional synchronous fault hook.
    void fault(IoOperation op) {
        // Count the operation, call borrowed fault state, and throw its Status on failure.

        /* Per-kind fault invocation ordinal. */ auto n = calls[static_cast<unsigned>(op)]++;
        if (hook.function) {
            auto s = hook.function(hook.context, op, n); // Borrowed hook result propagated unchanged on failure.
            if (!s)
                throw Failure{s};
        }
    }
    /// Move the owned stream to absolute byte offset when necessary; count/check native seeks.
    void seek(std::uint64_t offset) {
        // Bound the target, skip redundant seeks, inject faults, then perform the platform 64-bit seek.

        require(offset <= maximum_extent);
        if (position == offset)
            return;
        fault(IoOperation::seek);
        stats.seek_calls++;
#ifdef _WIN32
        io(_fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0, 34);
#else
        io(fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0, 34);
#endif
        position = offset;
    }
    /// Write borrowed bytes b at offset, enforcing final-file bound and complete physical writes.
    void write(std::uint64_t offset, std::span<const std::byte> b) {
        // Check extent, seek, invoke faults/write cap, write bytes, account actual progress, then reject short writes.

        if (b.empty())
            return;
        /* Checked exclusive byte extent. */ std::uint64_t end;
        require(checked_add(offset, b.size(), end));
        require(end<=prepared->plan().maximum_archive_bytes,StatusCode::resource_limit);
        seek(offset);
        fault(IoOperation::write);
        /* Physical write ordinal for the short-write seam. */ auto ordinal = stats.write_calls++;
        /* Attempted physical byte count after optional fault cap. */ auto count = b.size();
        if (hook.write_limit)
            count = std::min(count, hook.write_limit(hook.context, ordinal, count));
        /* Actual bytes written before complete-write verification. */ auto done = std::fwrite(b.data(), 1, count, file);
        position += done;
        stats.written_bytes += done;
        io(done == b.size(), 35);
    }
    /// Reserve size bytes of type t; variable allows only the current terminal payload to grow.
    /// Return an owned in-progress section, failing before directory/extent capacity is exceeded.
    Section reserve(Type t, std::uint64_t size, bool variable = false) {
        // Bound the directory slot and file extent, assign identity/location, then advance the reservation frontier.

        require(sections < prepared->directory_buffer().size() / 64, StatusCode::resource_limit);
        /* Checked exclusive byte extent. */ std::uint64_t end;
        require(checked_add(next, size, end));
        /* Owned pending section reservation. */ Section s;
        s.id = sections++;
        s.offset = next;
        s.size = size;
        s.type = t;
        s.variable = variable;
        next = end;
        return s;
    }
    /// Append borrowed bytes b to in/out section s and update its digest; no source bytes are retained.
    void append(Section &s, std::span<const std::byte> b) {
        // Check the new extent, extend only a terminal variable section, write bytes, then advance hash/progress.

        /* Checked cumulative section bytes. */ std::uint64_t count;
        require(checked_add(s.written, b.size(), count));
        if (s.variable) {
            require(next == s.offset + s.size);
            /* Checked exclusive byte extent. */ std::uint64_t end;
            require(checked_add(s.offset, count, end));
            s.size = count;
            next = end;
        } else
            require(count <= s.size);
        write(s.offset + s.written, b);
        s.hash.update(b);
        s.written = count;
    }
    /// Require complete section s, finalize its digest and fill its reserved canonical directory slot.
    void finish(Section &s) {
        // Encode fixed directory fields, copy the finalized hash, then count the finished section.

        require(s.written == s.size);
        /* Borrowed canonical slot for the completed section. */ auto d = prepared->directory_buffer().subspan(s.id * 64, 64);
        std::fill(d.begin(), d.end(), std::byte{});
        put(d.first(8), s.id);
        put(d.subspan(8, 4), static_cast<std::uint32_t>(s.type));
        put(d.subspan(16, 8), s.offset);
        put(d.subspan(24, 8), s.size);
        /* Final section digest. */ auto h = s.hash.finish();
        std::copy(h.begin(), h.end(), d.begin() + 32);
        finished++;
    }
    /// Emit named typed descriptor n in j for section s and borrowed multidimensional shape.
    void descriptor(Json &j, const char *n, const Section &s,
                    std::span<const std::uint64_t> shape) {
        // Write section/dtype identity, serialize shape extents in order, then close the descriptor.

        j.object(n);
        j.number("section", s.id);
        j.text("dtype", dtype(s.type));
        j.array("shape");
        for (auto x /* Logical dimension written in wire shape order. */ : shape)
            j.number(nullptr, x);
        j.end(false);
        j.end();
    }
    /// Emit a one-dimensional count descriptor; choose shape overload for rank-two arrays.
    void descriptor(Json &j, const char *n, const Section &s, std::uint64_t count) {
        // Delegate with a temporary one-element borrowed shape span.

        descriptor(j, n, s, std::span(&count, 1));
    }
    /// Copy borrowed byte span b in prepared-size tiles and return its finished section.
    Section bytes(Type type, std::span<const std::byte> b) {
        // Reserve exact extent, copy/hash bounded slices, then finalize the directory entry.

        /* Owned pending section for this exact extent. */ auto s = reserve(type, b.size());
        /* Borrowed prepared copy tile or its byte capacity. */ auto tile = prepared->copy_buffer().size();
        for (std::size_t k /* Absolute byte offset in the borrowed payload. */ = 0; k < b.size(); k += tile)
            append(s, b.subspan(k, std::min(tile, b.size() - k)));
        finish(s);
        return s;
    }
    /// Copy/hash borrowed source src into a byte section, enforcing per-source and total provenance bounds.
    Section source(const ByteSource &src) {
        // Charge source extent, read complete bounded tiles with fault checks, append them, then finish the section.

        require(src.size <= 4294967296ull, StatusCode::resource_limit);
        require(checked_add(source_bytes,src.size,source_bytes) &&
                source_bytes<=prepared->limits().provenance_bytes_bound,StatusCode::resource_limit);
        /* Owned pending section for this exact extent. */ auto s = reserve(Type::u8, src.size);
        /* Borrowed prepared copy tile or its byte capacity. */ auto tile = prepared->copy_buffer();
        for (std::uint64_t k /* Next byte offset in the callback/direct source. */ = 0; k < src.size;) {
            /* Bounded bytes in this source tile. */ auto n = std::min<std::uint64_t>(tile.size(), src.size - k);
            fault(IoOperation::source);
            /* Complete-tile callback/direct read status. */ auto status = src.read(k, tile.first(n));
            if (!status)
                throw Failure{status};
            append(s, tile.first(n));
            k += n;
        }
        finish(s);
        return s;
    }
    template <class T, class Get>
    /// Generate N elements of wire type from get(index), using T for exact numeric interpretation.
    /// Emit descriptor n into j; callback results are consumed synchronously and never retained.
    void generated(Json &j, const char *n, Type type, std::uint64_t N, Get get) {
        // Check byte extent, gather canonical values into the copy tile, flush full/final tiles, then emit the descriptor.

        /* Checked section byte extent. */ std::uint64_t size;
        require(checked_multiply(N, width(type), size));
        /* Owned pending section for this exact extent. */ auto s = reserve(type, size);
        /* Borrowed prepared copy tile or its byte capacity. */ auto tile = prepared->copy_buffer();
        /* Canonical element byte width. */ auto w = width(type);
        /* Filled bytes in the current copy tile. */ std::size_t used = 0;
        for (std::uint64_t k /* Logical element ordinal passed to the synchronous generator. */ = 0; k < N; k++) {
            /* Projected scalar converted to its intended wire value type. */ T v = static_cast<T>(get(k));
            /* Canonical integer bits, including bit-cast binary64 values. */ std::uint64_t value;
            if constexpr (std::is_same_v<T, double>) {
                require(std::isfinite(v));
                value = std::bit_cast<std::uint64_t>(v);
            } else
                value = static_cast<std::uint64_t>(v);
            if (used + w > tile.size()) {
                append(s, tile.first(used));
                used = 0;
            }
            put(tile.subspan(used, w), value);
            used += w;
        }
        append(s, tile.first(used));
        finish(s);
        descriptor(j, n, s, N);
    }
    template <class Get>
    /// Write N borrowed strings from get(index) as one UTF-8 byte pool plus offsets under n.
    /// When optional, emit a uniform present flag from present without changing stored strings.
    void strings(Json &j, const char *n, std::size_t N, Get get, bool optional = false,
                 bool present = false) {
        // Validate/sum strings, copy pooled bytes in tiles, generate cumulative offsets, and optionally write presence flags.

        /* Checked total UTF-8 pool byte length. */ std::uint64_t total = 0;
        for (std::size_t k /* Pool string ordinal during byte admission. */ = 0; k < N; k++) {
            /* Borrowed current string from immutable source metadata. */ std::string_view s = get(k);
            require(valid_utf8(s));
            require(checked_add(total, s.size(), total), StatusCode::resource_limit);
        }
        require(total <= 268435456, StatusCode::resource_limit);
        /* Owned pooled-byte section reservation. */ auto b = reserve(Type::u8, total);
        for (std::size_t k /* Pool string ordinal during exact byte copying. */ = 0; k < N; k++) {
            /* Borrowed current string from immutable source metadata. */ std::string_view s = get(k);
            /* Borrowed exact bytes of the current string. */ auto raw = std::as_bytes(std::span(s.data(), s.size()));
            /* Borrowed prepared copy tile or its byte capacity. */ auto tile = prepared->copy_buffer().size();
            for (std::size_t x /* Byte offset within this string, advanced by a copy tile. */ = 0; x < raw.size(); x += tile)
                append(b, raw.subspan(x, std::min(tile, raw.size() - x)));
        }
        finish(b);
        /* Explicit-close metadata object for this descriptor. */ auto g = group(j, n);
        u64(g, "count", N);
        descriptor(g, "bytes", b, total);
        /* Cumulative UTF-8 pool offset. */ std::uint64_t off = 0;
        generated<std::uint64_t>(g, "offsets", Type::u64, N + 1, [&](auto k) {
            // Produce offset k by extending the cumulative byte count with string k-1.

            if (k)
                off += std::string_view(get(k - 1)).size();
            return off;
        });
        if (optional)
            generated<std::uint8_t>(g, "present", Type::u8, N,
                                    [&](auto) { /* Project this requested catalog/column cell; no input ownership transfers. */ return present ? 1 : 0; });
        g.finish();
    }
    /// Serialize named legacy document view v into j; retained application/json bytes are copied exactly.
    void document(Json &j, const char *n, const DocumentView &v) {
        // Admit state, emit it, and only for retained states write media type plus a source-byte descriptor.

        require(static_cast<unsigned>(v.state) <= 3);
        /* Explicit-close metadata object for this descriptor. */ auto g = group(j, n);
        u8(g, "state", code(v.state));
        if (included(v.state)) {
            require(v.media_type == "application/json");
            text(g, "media_type", v.media_type);
            /* Finished retained document byte section. */ auto s = source(v.bytes);
            descriptor(g, "bytes", s, v.bytes.size);
        }
        g.finish();
    }
    /// Validate aggregate retained branch_state against borrowed member documents docs.
    void validate_branch(SectionState branch_state, std::span<const DocumentView> docs) {
        // For retained branches, detect any retained/missing member and require the corresponding aggregate state.

        require(static_cast<unsigned>(branch_state) <= 3);
        if (!included(branch_state))
            return;
        /* Aggregate retained/missing document latches. */ bool available = false, missing = false;
        for (auto &d /* Borrowed document participating in this aggregate state. */ : docs) {
            available |= included(d.state);
            missing |= d.state == SectionState::partial || d.state == SectionState::unavailable;
        }
        require(available &&
                branch_state == (missing ? SectionState::partial : SectionState::included));
    }
    /// Serialize retained JSONL source v into named group name, or current object when name is null.
    /// Preserve exact bytes/trailing fragment; caller already validates complete JSON object records.
    void logs(Json &j, const LogsView &v, const char* name="logs") {
        // Scan bounded tiles for LF boundaries, copy raw bytes, rescan to generate offsets, then emit capture facts/info.

        require(v.bytes.size <= 4294967296ull, StatusCode::resource_limit);
        require(v.dropped_count_present || v.dropped_count == 0);
        /* Borrowed prepared copy tile or its byte capacity. */ auto tile = prepared->copy_buffer();
        /* Complete LF record count, last complete offset and current line bytes. */ std::uint64_t records = 0, last = 0, line = 0;
        for (/* Next byte offset in the exact JSONL source. */ std::uint64_t off = 0; off < v.bytes.size;) {
            /* Bounded bytes in this source tile. */ auto n = std::min<std::uint64_t>(tile.size(), v.bytes.size - off);
            fault(IoOperation::source);
            /* Complete source-tile read status. */ auto status = v.bytes.read(off, tile.first(n));
            if (!status)
                throw Failure{status};
            for (std::uint64_t k /* Byte position within the current JSONL tile. */ = 0; k < n; k++) {
                line++;
                if (tile[k] == std::byte{'\n'}) {
                    require(line <= 1048576, StatusCode::resource_limit);
                    records++;
                    last = off + k + 1;
                    line = 0;
                }
            }
            off += n;
        }
        require(!(line || (v.dropped_count_present && v.dropped_count)) ||
                v.state == SectionState::partial);
        if(name)j.object(name);
        /* Borrowed emitter for named or inline log metadata. */ auto& g=j;
        /* Finished exact JSONL byte section including trailing fragment. */ auto raw = source(v.bytes);
        descriptor(g, "records_jsonl", raw, v.bytes.size);
        /* Checked section byte extent. */ std::uint64_t size;
        require(checked_multiply(records + 1, 8, size));
        /* Owned complete-record offset section. */ auto offsets = reserve(Type::u64, size);
        /* Initial mandatory zero record offset. */ auto zero = le64(0);
        append(offsets, zero);
        for (/* Next byte offset in the exact JSONL source. */ std::uint64_t off = 0; off < v.bytes.size;) {
            /* Bounded bytes in this source tile. */ auto n = std::min<std::uint64_t>(tile.size(), v.bytes.size - off);
            fault(IoOperation::source);
            /* Complete source-tile read status. */ auto status = v.bytes.read(off, tile.first(n));
            if (!status)
                throw Failure{status};
            for (std::uint64_t k /* Byte position tested for a complete LF record boundary. */ = 0; k < n; k++)
                if (tile[k] == std::byte{'\n'}) {
                    /* Canonical exclusive offset after this LF record. */ auto b = le64(off + k + 1);
                    append(offsets, b);
                }
            off += n;
        }
        finish(offsets);
        descriptor(g, "record_offsets", offsets, records + 1);
        u64(g, "complete_record_count", records);
        u64(g, "trailing_fragment_bytes", v.bytes.size - last);
        u8(g, "dropped_count_present", v.dropped_count_present);
        u64(g, "dropped_count", v.dropped_count);
        text(g, "schema", v.schema);
        document(g, "info_json", v.info_json);
        if(name)j.end();
    }
    /// Emit legacy input/log/context roles from borrowed v under the current root object j.
    void provenance(Json &j, const ProvenanceView &v) {
        // Validate aggregate document branches, then serialize only retained branches in fixed role order.

        /* Borrowed legacy input-role views in fixed order. */ std::array<DocumentView, 4> in{v.inputs.source_json, v.inputs.effective_json,
                                       v.inputs.overrides_json, v.inputs.resources_json};
        validate_branch(v.inputs.state, in);
        /* Borrowed execution-context role. */ std::array<DocumentView, 1> ctx{v.context.execution_json};
        validate_branch(v.context.state, ctx);
        require(static_cast<unsigned>(v.logs.state) <= 3);
        if (included(v.inputs.state)) {
            /* Explicit-close legacy input branch. */ auto g = group(j, "inputs");
            document(g, "source_json", in[0]);
            document(g, "effective_json", in[1]);
            document(g, "overrides_json", in[2]);
            document(g, "resources_json", in[3]);
            g.finish();
        }
        if (included(v.context.state)) {
            /* Explicit-close legacy context branch. */ auto g = group(j, "context");
            document(g, "execution_json", ctx[0]);
            g.finish();
        }
        if (included(v.logs.state))
            logs(j, v.logs);
    }
    template<class T,class Get>
    /// Write one lossless typed diagnostic column named name from v using get(record).
    /// get receives a complete local copied row; no row history or callback result escapes.
    void diagnostic_column(Json& j,const char* name,Type type,const DiagnosticTraceView& v,Get get) {
        // Delegate tiled numeric generation; read each source row under fault injection and project its requested member.

        generated<T>(j,name,type,v.records.count,[&](auto k) {
            // Read complete retained row k and project the selected typed member.

            /* Local complete source row projected synchronously. */ DiagnosticRecord row;
            fault(IoOperation::source);
            /* Source row read status. */ auto status=v.records.read(k,row);if(!status)throw Failure{status};
            return get(row);
        });
    }
    /// Write immutable physical criteria c under j.criteria without recomputation or unit conversion.
    void criteria(Json& j,const StaticCriteria& c) {
        // Open criteria, emit the fixed norm and source scalar limits in wire order, then close it.

        /* Explicit-close physical criteria object. */ auto g=group(j,"criteria");text(g,"norm","block_max_abs_component");
        f64(g,"evaluation_time_s",c.evaluation_time_s);f64(g,"force_absolute_N",c.force_absolute_N);
        f64(g,"moment_absolute_Nm",c.moment_absolute_Nm);f64(g,"relative_tolerance",c.relative_tolerance);
        f64(g,"force_scale_N",c.force_scale_N);f64(g,"moment_scale_Nm",c.moment_scale_Nm);
        f64(g,"force_limit_N",c.force_limit_N);f64(g,"moment_limit_Nm",c.moment_limit_Nm);
        f64(g,"constraint_translation_limit_m",c.constraint_translation_limit_m);
        f64(g,"constraint_rotation_limit_rad",c.constraint_rotation_limit_rad);
        f64(g,"increment_translation_limit_m",c.increment_translation_limit_m);
        f64(g,"increment_rotation_limit_rad",c.increment_rotation_limit_rad);g.finish();
    }
    /// Serialize only fixed columns belonging to policy d.kind from finalized trace v.
    void diagnostic_columns(Json& j,const DiagnosticTraceDescription& d,const DiagnosticTraceView& v) {
        // Write record ordinals, dispatch the trace schema, preserve dictionaries/masks and emit exact typed columns.

        /* Explicit-close typed trace-column object. */ auto g=group(j,"columns");
        // Serialize named unsigned64 member column n from each retained diagnostic row.
        // member is a typed field pointer; the nested getter only borrows the current row.
        auto u=[&](const char* n,auto member){/* Project the member and write its typed lossless column. */diagnostic_column<std::uint64_t>(g,n,Type::u64,v,[=](const auto&r){ /* Project this requested catalog/column cell; no input ownership transfers. */return r.*member;});};
        // Serialize named unsigned8 member column n from each retained diagnostic row.
        // member is a typed field pointer; the nested getter only borrows the current row.
        auto b=[&](const char* n,auto member){/* Project the member and write its typed lossless column. */diagnostic_column<std::uint8_t>(g,n,Type::u8,v,[=](const auto&r){ /* Project this requested catalog/column cell; no input ownership transfers. */return r.*member;});};
        // Serialize named binary64 member column n from each retained diagnostic row.
        // member is a typed field pointer; the nested getter only borrows the current row.
        auto real=[&](const char* n,auto member){/* Project the member and write its typed lossless column. */diagnostic_column<double>(g,n,Type::f64,v,[=](const auto&r){ /* Project this requested catalog/column cell; no input ownership transfers. */return r.*member;});};
        /* Closed trace schema ordinal. */ auto k=static_cast<unsigned>(d.kind);
        u("record_index",&DiagnosticRecord::record_index);
        if(k==0) {
            u("accepted_step",&DiagnosticRecord::accepted_step);real("time_s",&DiagnosticRecord::time_s);
            u("nonlinear_iterations",&DiagnosticRecord::nonlinear_iterations);
        } else if(k<=3) {
            u("attempt_id",&DiagnosticRecord::attempt_id);real("factor",&DiagnosticRecord::factor);
            if(k==2)u("accepted_level_id",&DiagnosticRecord::accepted_level_id);
            else {
                b("certified",&DiagnosticRecord::certified);b("rolled_back",&DiagnosticRecord::rolled_back);
                strings(g,"status_names",d.status_names.size(),[&](auto i)->std::string_view{ /* Project this requested catalog/column cell; no input ownership transfers. */return d.status_names[i];});
                diagnostic_column<std::uint32_t>(g,"status_index",Type::u32,v,[](const auto&r){ /* Project this requested catalog/column cell; no input ownership transfers. */return r.status_index;});
                if(k==1) {
                    b("accepted_level_present",&DiagnosticRecord::accepted_level_present);
                    u("accepted_level_id",&DiagnosticRecord::accepted_level_id);u("newton_record_count",&DiagnosticRecord::newton_record_count);
                } else {u("iteration_id",&DiagnosticRecord::iteration_id);b("row_kind",&DiagnosticRecord::row_kind);}
            }
            b("metric_valid",&DiagnosticRecord::metric_valid);
            for(unsigned m /* Metric slot in force/moment/constraint/increment order. */=0;m<6;++m)
                diagnostic_column<double>(g,diagnostic_measure_name(static_cast<DiagnosticMeasure>(m+2)),Type::f64,v,[=](const auto&r){ /* Project this requested catalog/column cell; no input ownership transfers. */return r.metrics[m];});
        } else {
            u("sequence",&DiagnosticRecord::sequence);
            strings(g,"event_names",d.event_names.size(),[&](auto i)->std::string_view{ /* Project this requested catalog/column cell; no input ownership transfers. */return d.event_names[i];});
            diagnostic_column<std::uint32_t>(g,"event_name_index",Type::u32,v,[](const auto&r){ /* Project this requested catalog/column cell; no input ownership transfers. */return r.event_name_index;});
            b("severity",&DiagnosticRecord::severity);b("identity_valid",&DiagnosticRecord::identity_valid);
            real("time_s",&DiagnosticRecord::time_s);u("accepted_step",&DiagnosticRecord::accepted_step);
            u("accepted_level",&DiagnosticRecord::accepted_level_id);u("attempt_id",&DiagnosticRecord::attempt_id);
        }
        g.finish();
    }
    /// Validate and write ordered trace metadata/columns from result, retaining their section IDs.
    void diagnostic_traces(const RunResultView& result) {
        // Validate aggregate capture facts, serialize each trace, include columns/criteria/summaries only when available, then store IDs.

        /* Borrowed prepared trace descriptions in authored order. */ const auto& descriptions=prepared->description().run.diagnostics;
        /* Validated aggregate capture status. */ auto status=validate_diagnostics(descriptions,result.diagnostics,result.info.accepted_count);
        if(!status)throw Failure{status.error()};diagnostic_status=*status;
        for(std::size_t i /* Prepared trace ordinal paired with its finalized view. */=0;i<descriptions.size();++i) {
            /* Borrowed policy for the current trace. */ const auto& d=descriptions[i];/* Borrowed finalized capture for the same trace. */ const auto&v=result.diagnostics[i];
            /* Emitter borrowing the reusable preparation buffer. */ Json j(prepared->serialization_buffer());j.object();
            u64(j,"trace_schema",1);u8(j,"kind",static_cast<std::uint8_t>(d.kind));u64(j,"run_index",0);
            u8(j,"requirement",static_cast<std::uint8_t>(d.requirement));u8(j,"availability",static_cast<std::uint8_t>(v.availability));
            u8(j,"capture_status",static_cast<std::uint8_t>(v.capture_status));u64(j,"record_capacity",d.record_capacity);u64(j,"record_count",v.records.count);
            u8(j,"eligible_count_present",v.counts_present);u64(j,"eligible_count",v.eligible_count);
            u8(j,"omitted_count_present",v.counts_present);u64(j,"omitted_count",v.omitted_count);
            u64(j,"capture_error_count",v.capture_error_count);u8(j,"first_missing_ordinal_present",v.first_missing_ordinal_present);
            u64(j,"first_missing_ordinal",v.first_missing_ordinal);text(j,"failure_code",v.failure_code);
            if(v.availability!=DiagnosticAvailability::unavailable) {
                diagnostic_columns(j,d,v);if(d.criteria)criteria(j,*d.criteria);
                if(!v.summaries.empty()) {
                    /* Summary wrapper over retained valid records. */ auto s=group(j,"summaries");text(s,"population","retained_valid_records");/* Explicit-close permitted-summary field map. */ auto fields=group(s,"fields");
                    for(const auto& summary /* Borrowed producer summary assertion for one admitted field. */:v.summaries) {
                        /* Current summary field object. */ auto field=group(fields,diagnostic_measure_name(summary.measure));u64(field,"count",summary.count);
                        if(static_cast<unsigned>(summary.measure)<2) {u64(field,"min",summary.min_integer);u64(field,"max",summary.max_integer);}
                        else {f64(field,"min",summary.min);f64(field,"max",summary.max);}
                        f64(field,"mean",summary.mean);u8(field,"median_present",summary.median_present);f64(field,"median",summary.median);field.finish();
                    }
                    fields.finish();s.finish();
                }
            }
            j.end();trace_ids[i]=bytes(Type::json,j.bytes()).id;
        }
    }
    /// Write normalized document/log sources from v and verify retained original bytes against its authored digest.
    void indexed_sources(const IndexedProvenanceView& v) {
        // Bound source count, derive aggregate states, serialize distinct source descriptors, and bind included original bytes by hash.

        require(v.logs.size()<=prepared->description().maximum_log_sources && v.logs.size()<=64);
        require(v.documents[0].state!=SectionState::partial);
        source_count=5+v.logs.size();
        auto aggregate=[](auto count,auto get) {
            // Inspect count source states via get(index), preserving retained empty versus unavailable.

            /* Aggregate source-state latches; included empty remains retained. */ bool all_disabled=true,all_included=true,retained=false,unavailable=false;
            for(std::size_t i /* Source ordinal in the selected aggregate category. */=0;i<count;++i) {
                /* Current source-role state. */ auto state=get(i);require(static_cast<unsigned>(state)<=3);
                all_disabled&=state==SectionState::disabled;all_included&=state==SectionState::included;
                retained|=included(state);unavailable|=state==SectionState::unavailable;
            }
            return !count||all_disabled?SectionState::disabled:all_included?SectionState::included:!retained&&unavailable?SectionState::unavailable:SectionState::partial;
        };
        indexed_states[0]=aggregate(4,[&](auto i){ /* Project this requested catalog/column cell; no input ownership transfers. */return v.documents[i].state;});
        indexed_states[1]=aggregate(v.logs.size(),[&](auto i){ /* Project this requested catalog/column cell; no input ownership transfers. */return v.logs[i].state;});
        indexed_states[2]=v.documents[4].state;
        for(std::uint64_t i /* Normalized source identity, documents first then distinct logs. */=0;i<source_count;++i) {
            /* Emitter borrowing the reusable preparation buffer. */ Json j(prepared->serialization_buffer());j.object();u64(j,"source_schema",1);u64(j,"source_index",i);u8(j,"kind",i<5?0:1);
            if(i<5) {
                document(j,"document",v.documents[i]);
                if(i==0&&v.documents[0].state==SectionState::included) {
                    /* Borrowed digest from the just-finished original byte section. */ const auto hash=prepared->directory_buffer().subspan((sections-1)*64+32,32);
                    require(std::equal(hash.begin(),hash.end(),v.authored_input_sha256.begin()),StatusCode::verification_failure);
                }
            } else {
                /* Borrowed original log stream, kept distinct from other logs. */ const auto& log=v.logs[i-5];u8(j,"state",code(log.state));
                if(included(log.state))logs(j,log,nullptr);
            }
            j.end();source_ids[i]=bytes(Type::json,j.bytes()).id;
        }
    }
    /// Emit source outcome/configuration facts from borrowed v into run.info.
    void info(Json &run, const RunResultView &v) {
        // Write identity/status/presence axes in wire order, then copy the three source configuration documents.

        /* Explicit-close source run-info object. */ auto g = group(run, "info");
        /* Borrowed producer outcome/configuration facts. */ const auto &i = v.info;
        /* Borrowed immutable resolved run description. */ const auto &r = prepared->description().run;
        text(g, "run_id", r.run_id);
        text(g, "assembly_ref", r.assembly_ref);
        u8(g, "kind", static_cast<std::uint8_t>(r.kind));
        u8(g, "run_outcome", static_cast<std::uint8_t>(i.outcome));
        u8(g, "numerical_status", static_cast<std::uint8_t>(i.numerical));
        u8(g, "observation_status", static_cast<std::uint8_t>(i.observation));
        u8(g, "logging_status", static_cast<std::uint8_t>(i.logging));
        text(g, "failure_stage", i.failure_stage);
        text(g, "failure_code", i.failure_code);
        text(g, "termination_reason", i.termination_reason);
        u8(g, "catalog_state", code(i.catalog_state));
        u64(g, "committed_count", v.axis.size());
        u64(g, "accepted_count", i.accepted_count);
        u8(g, "last_accepted_present", i.last_accepted_present);
        u8(g, "last_recorded_present", !v.axis.empty());
        f64(g, "last_accepted_value", i.last_accepted_value);
        f64(g, "last_recorded_value", v.axis.empty() ? 0 : v.axis.back());
        u8(g, "requested_bounds_present", i.requested_bounds_present);
        f64(g, "requested_begin", i.requested_begin);
        f64(g, "requested_end", i.requested_end);
        u8(g, "elapsed_seconds_present", i.elapsed_seconds_present);
        f64(g, "elapsed_seconds", i.elapsed_seconds);
        text(g, "elapsed_seconds_scope", i.elapsed_seconds_scope);
        u8(g, "identity_axis_present", v.identity_axis_present);
        text(g, "identity_axis_convention", i.identity_axis_convention);
        text(g, "axis_name", i.axis_name);
        text(g, "axis_unit", i.axis_unit);
        document(g, "solver_configuration", i.solver_configuration);
        document(g, "sampling_configuration", i.sampling_configuration);
        document(g, "execution", i.execution);
        g.finish();
    }
    /// Emit immutable prepared codec profiles and outer row assignments into values object v.
    void profiles(Json &v, std::uint64_t outer) {
        // Generate row-profile indices, exact profile fields and pinned identity strings without inspecting compressed data.

        /* Borrowed immutable profile catalog. */ const auto& run=prepared->description().run;
        /* Number of prepared profile definitions. */ const auto M=run.profiles.size();
        generated<std::uint32_t>(v, "row_profile_index", Type::u32, outer, [&](auto row) {
            // Return the prepared transient row profile, or reversible profile zero for static rows.
            return run.kind==RunKind::timeseries&&!run.row_profile_index.empty()?run.row_profile_index[row]:0;
        });
        /* Explicit-close profile catalog descriptor. */ auto p = group(v, "profiles");
        generated<std::uint32_t>(p, "profile_index", Type::u32, M, [](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return k; });
        generated<std::uint8_t>(p, "mode", Type::u8, M, [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return static_cast<unsigned>(run.profiles[k].mode()); });
        generated<std::uint32_t>(p, "codec_version", Type::u32, M, [](auto) { /* Project this requested catalog/column cell; no input ownership transfers. */ return 5; });
        generated<std::uint32_t>(p, "stream_word_bits", Type::u32, M, [](auto) { /* Project this requested catalog/column cell; no input ownership transfers. */ return 64; });
        generated<std::int8_t>(p, "rounding", Type::i8, M, [](auto) { /* Project this requested catalog/column cell; no input ownership transfers. */ return 0; });
        generated<std::uint64_t>(p, "mode_code", Type::u64, M, [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return run.profiles[k].mode_code(); });
        generated<std::uint8_t>(p, "tolerance_present", Type::u8, M, [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return run.profiles[k].tolerance_present(); });
        generated<double>(p, "requested_tolerance", Type::f64, M, [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return run.profiles[k].requested_tolerance(); });
        generated<double>(p, "effective_tolerance", Type::f64, M, [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return run.profiles[k].effective_tolerance(); });
        for (auto pair /* Fixed metadata key/value pair for codec identity. */ : std::array<std::pair<const char *, std::string_view>, 5>{
                 {{"codec", "zfp"},
                  {"library_version", "1.0.1"},
                  {"library_revision", "f40868a6a1c190c802e7d8b5987064f044bf7812"},
                  {"byte_order", "little"},
                  {"execution", "serial"}}})
            strings(p, pair.first, M, [&](auto) { /* Project this requested catalog/column cell; no input ownership transfers. */ return pair.second; });
        p.finish();
    }

    /// Emit prepared resolved scalar/request catalogs into run without compiling scientific selectors.
    void catalogs(Json &run) {
        // Write ordered scalar/request pools and linkage, then retain effective JSON/digests only when enabled.

        /* Borrowed immutable resolved run description. */ const auto &r = prepared->description().run;
        /* Actual resolved scalar and request populations. */ auto C = r.channels.size(), R = r.requests.size();
        /* Explicit-close scalar catalog object. */ auto c = group(run, "channels");
        generated<std::uint64_t>(c, "channel_index", Type::u64, C, [](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return k; });
        generated<std::uint64_t>(c, "request_index", Type::u64, C,
                                 [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return r.channels[k].request_index; });
        strings(c, "scalar_id", C,
                [&](auto k) -> std::string_view { /* Project this requested catalog/column cell; no input ownership transfers. */ return r.channels[k].scalar_id; });
        strings(c, "group_id", C,
                [&](auto k) -> std::string_view { /* Project this requested catalog/column cell; no input ownership transfers. */ return r.channels[k].group_id; });
        strings(c, "request_id", C,
                [&](auto k) -> std::string_view { /* Project this requested catalog/column cell; no input ownership transfers. */ return r.channels[k].request_id; });
        strings(c, "component_path", C,
                [&](auto k) -> std::string_view { /* Project this requested catalog/column cell; no input ownership transfers. */ return r.channels[k].component_path; });
        strings(c, "unit", C, [&](auto k) -> std::string_view { /* Project this requested catalog/column cell; no input ownership transfers. */ return r.channels[k].unit; });
        c.finish();
        /* Explicit-close effective-request catalog object. */ auto q = group(run, "requests");
        strings(q, "group_id", R,
                [&](auto k) -> std::string_view { /* Project this requested catalog/column cell; no input ownership transfers. */ return r.requests[k].group_id; });
        strings(q, "request_id", R,
                [&](auto k) -> std::string_view { /* Project this requested catalog/column cell; no input ownership transfers. */ return r.requests[k].request_id; });
        strings(q, "schema_version", R,
                [&](auto k) -> std::string_view { /* Project this requested catalog/column cell; no input ownership transfers. */ return r.requests[k].schema_version; });
        strings(q, "construction_profile", R,
                [&](auto k) -> std::string_view { /* Project this requested catalog/column cell; no input ownership transfers. */ return r.requests[k].construction_profile; });
        u8(q, "effective_state", code(r.effective_requests));
        if (r.effective_requests == SectionState::included) {
            strings(q, "effective_json", R,
                    [&](auto k) -> std::string_view { /* Project this requested catalog/column cell; no input ownership transfers. */ return r.requests[k].effective_json; });
            /* Owned byte matrix of exact effective-request JSON digests. */ auto hashes = reserve(Type::u8, R * 32);
            for (auto &request /* Borrowed effective request whose exact JSON bytes are hashed. */ : r.requests) {
                /* Borrowed exact effective-request JSON text. */ std::string_view s = request.effective_json;
                /* Exact digest of unchanged effective JSON bytes. */ auto h = sha256(std::as_bytes(std::span(s.data(), s.size())));
                append(hashes, h);
            }
            finish(hashes);
            /* Logical request-count by SHA-byte shape. */ std::array<std::uint64_t, 2> shape{R, 32};
            descriptor(q, "effective_sha256", hashes, shape);
        }
        q.finish();
    }
    /// Encode borrowed committed source values into framed payload/index/statistics sections under run.
    void values(Json &run, const RunResultView &source) {
        // Describe layout/profiles, reserve index/statistic sections, gather and verify each frame, tile index writes, then finish all descriptors.

        /* Explicit-close framed value descriptor. */ auto g = group(run, "values");
        /* Logical row count/width; B below is the admitted frame capacity. */ auto outer = source.values.outer, inner = source.values.inner,
             B = prepared->limits().frame_values;
        g.json.array("shape");
        g.json.number(nullptr, outer);
        g.json.number(nullptr, inner);
        g.json.end(false);
        text(g, "dtype", "float64");
        g.json.array("axis_names");
        /* Whether rows represent transient scalar channels. */ bool ts = prepared->description().run.kind == RunKind::timeseries;
        g.json.text(nullptr, ts ? "channel" : "level");
        g.json.text(nullptr, ts ? "sample" : "channel");
        g.json.end(false);
        u32(g, "frame_schema", 1);
        u32(g, "nominal_frame_values", static_cast<std::uint32_t>(B));
        profiles(g, outer);
        // Reserve fixed index/statistics sections before the variable terminal payload.
        /* Checked total frame population. */ std::uint64_t F;
        require(checked_multiply(outer, (inner + B - 1) / B, F) && F <= (1ull << 32),
                StatusCode::resource_limit);
        /* Checked canonical index byte extent. */ std::uint64_t isize;
        require(checked_multiply(F, 77, isize));
        /* Owned fixed frame-index section reservation. */ auto index = reserve(Type::frames, isize);
        /* Owned fixed per-frame error/count/equality section reservations. */ auto maxerr = reserve(Type::f64, F * 8), rms = reserve(Type::f64, F * 8),
             verified = reserve(Type::u64, F * 8), equal = reserve(Type::u8, F);
        /* Terminal variable-length encoded payload section. */ auto payload = reserve(Type::u8, 0, true);
        /* Number of fully encoded/recorded frames so far. */ std::uint64_t frame = 0;
        /* Borrowed prepared frame-index tile. */ auto idx = prepared->index_buffer();
        /* Occupied index-tile bytes. */ std::size_t idx_used = 0;
        // Gather each logical row/frame, verify the resolved codec policy, then write exact encoded bytes.
        for (std::uint64_t row /* Logical scalar row for transient data, or accepted-level row for static data. */ = 0; row < outer; row++)
            for (std::uint64_t begin /* Inner-axis frame start, advanced by nominal frame capacity. */ = 0; begin < inner; begin += B) {
                /* Actual values in this possibly short final frame. */ auto n = std::min(B, inner - begin);
                /* Borrowed frame-sized source gather destination. */ auto gather = prepared->gather_buffer().first(n);
                fault(IoOperation::source);
                /* Source gather status. */ auto status = source.values.fill(row, begin, gather);
                if (!status)
                    throw Failure{status};
                fault(IoOperation::codec);
                /* Borrowed authoritative profile mapping. */ const auto& desc=prepared->description().run;
                /* Expected profile slot for this logical row. */ const auto profile_index=ts&&!desc.row_profile_index.empty()?desc.row_profile_index[row]:0;
                /* Verified codec result borrowing the codec payload buffer. */ auto encoded = codec->encode(gather, prepared->decode_buffer(),desc.profiles[profile_index]);
                if (!encoded)
                    throw Failure{encoded.error()};
                /* Logical canonical index entry for this exact encoded frame. */ FrameRecord record{
                    row, begin,          n, payload.size, encoded->bytes.size(), encoded->encoding,
                    profile_index,   encoded->digest};
                append(payload, encoded->bytes);
                /* Owned 77-byte canonical frame-index record. */ auto canonical = encode_record(record);
                if (idx_used + 77 > idx.size()) {
                    append(index, idx.first(idx_used));
                    idx_used = 0;
                }
                std::copy(canonical.begin(), canonical.end(), idx.begin() + idx_used);
                idx_used += 77;
                // Record codec verification facts independently of size-based raw/ZFP selection.
                /* Canonical maximum-error binary64 bits. */ auto maximum = le64(std::bit_cast<std::uint64_t>(encoded->max_abs_error));
                /* Canonical RMS reporting-statistic bits. */ auto rms_value=le64(std::bit_cast<std::uint64_t>(encoded->rms_error));
                /* Canonical independently verified value count. */ auto count = le64(encoded->verified_count);
                /* Canonical bitwise-equality byte. */ std::array<std::byte, 1> one{std::byte{static_cast<unsigned char>(encoded->bitwise_equal)}};
                append(maxerr, maximum);
                append(rms, rms_value);
                append(verified, count);
                append(equal, one);
                frame++;
            }
        // Flush the last index tile and finalize every reserved section before exposing descriptors.
        append(index, idx.first(idx_used));
        require(frame == F);
        finish(index);
        finish(maxerr);
        finish(rms);
        finish(verified);
        finish(equal);
        finish(payload);
        stats.frames = F;
        stats.payload_bytes = payload.size;
        descriptor(g, "frame_index", index, F);
        descriptor(g, "payload", payload, payload.size);
        /* Explicit-close per-frame verification descriptor. */ auto v = group(g, "verification");
        descriptor(v, "max_abs_error", maxerr, F);
        descriptor(v, "rms_error", rms, F);
        descriptor(v, "verified_count", verified, F);
        descriptor(v, "bitwise_equal", equal, F);
        v.finish();
        g.finish();
    }
    /// Write one complete run descriptor from borrowed v and return its JSON section ID.
    std::uint64_t run(const RunResultView &v) {
        // Emit run info and trace/source references; for an available catalog add axes/catalogs/values, then finish metadata.

        /* Emitter borrowing the reusable preparation buffer. */ Json j(prepared->serialization_buffer());
        j.object();
        info(j, v);
        if(!prepared->description().run.diagnostics.empty()) {
            /* Explicit-close run diagnostic references. */ auto d=group(j,"diagnostics");u8(d,"status",static_cast<std::uint8_t>(diagnostic_status));
            d.json.array("traces");
            for(std::size_t k /* Prepared trace-index entry linked to its written metadata. */=0;k<prepared->description().run.diagnostics.size();++k) {
                d.json.object();u8(d,"kind",static_cast<std::uint8_t>(prepared->description().run.diagnostics[k].kind));
                u64(d,"metadata_section",trace_ids[k]);d.json.end();
            }
            d.json.end(false);d.finish();
        }
        if(prepared->description().indexed_provenance) {
            /* Explicit-close indexed provenance object. */ auto p=group(j,"provenance");/* Explicit-close run source-association arrays. */ auto refs=group(p,"references");
            generated<std::uint8_t>(refs,"role",Type::u8,source_count,[](auto k){ /* Project this requested catalog/column cell; no input ownership transfers. */return k<5?k:5;});
            generated<std::uint64_t>(refs,"source_index",Type::u64,source_count,[](auto k){ /* Project this requested catalog/column cell; no input ownership transfers. */return k;});
            generated<std::uint64_t>(refs,"order",Type::u64,source_count,[](auto k){ /* Project this requested catalog/column cell; no input ownership transfers. */return k<5?0:k-5;});
            refs.finish();p.finish();
        }
        if (v.info.catalog_state == SectionState::included) {
            catalogs(j);
            /* Whether rows represent transient scalar channels. */ bool ts = prepared->description().run.kind == RunKind::timeseries;
            generated<double>(j, ts ? "time" : "load_levels", Type::f64, v.axis.size(),
                              [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return v.axis[k]; });
            if (v.identity_axis_present)
                generated<std::uint64_t>(j, ts ? "step_index" : "level_index", Type::u64,
                                         v.accepted_indices.size(),
                                         [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return v.accepted_indices[k]; });
            values(j, v);
        }
        j.end();
        /* Finished immutable metadata section. */ auto s = bytes(Type::json, j.bytes());
        return s.id;
    }
    /// Write root metadata pointing to run_id using legacy v or nonnull indexed provenance.
    void root(std::uint64_t run_id, const ProvenanceView &v,const IndexedProvenanceView* indexed=nullptr) {
        // Emit exact feature/version/state declarations, single-run catalog, then selected provenance representation and root ID.

        /* Emitter borrowing the reusable preparation buffer. */ Json j(prepared->serialization_buffer());
        j.object();
        /* Borrowed immutable archive identity/catalog. */ const auto &d = prepared->description();
        /* Explicit-close format/version/features object. */ auto f = group(j, "format");
        text(f, "name", "serene.outputs");
        u32(f, "version_major", 1);
        u32(f, "version_minor", extended()?1:0);
        text(f, "file_id", d.file_id);
        text(f, "created_utc", d.created_utc);
        text(f, "writer_name", d.writer_name);
        text(f, "writer_version", d.writer_version);
        text(f, "write_state", "complete");
        f.json.array("required_features");
        for (auto token /* Mandatory base-feature spelling, preserved in deterministic order. */ : {"serene-container-le-1", "json-descriptors-1", "utf8-pools-1",
                           "frames-77-le-1", "zfp-5-le64-serial-1"})
            f.json.text(nullptr, token);
        if(!d.run.diagnostics.empty())f.json.text(nullptr,"run-diagnostics-1");
        if(d.indexed_provenance)f.json.text(nullptr,"provenance-sources-1");
        if(fixed_accuracy())f.json.text(nullptr,"timeseries-zfp-fixed-accuracy-1");
        f.json.end(false);
        /* Explicit-close aggregate source availability object. */ auto states = group(f, "sections");
        u8(states, "inputs", code(indexed?indexed_states[0]:v.inputs.state));
        u8(states, "logs", code(indexed?indexed_states[1]:v.logs.state));
        u8(states, "context", code(indexed?indexed_states[2]:v.context.state));
        states.finish();
        f.finish();
        /* Explicit-close root results object. */ auto results = group(j, "results");
        /* Explicit-close authoritative one-run catalog. */ auto c = group(results, "index");
        generated<std::uint64_t>(c, "run_index", Type::u64, 1, [](auto) { /* Project this requested catalog/column cell; no input ownership transfers. */ return 0; });
        strings(c, "run_id", 1, [&](auto) -> std::string_view { /* Project this requested catalog/column cell; no input ownership transfers. */ return d.run.run_id; });
        strings(
            c, "run_name", 1, [&](auto) -> std::string_view { /* Project this requested catalog/column cell; no input ownership transfers. */ return d.run.run_name; }, true,
            d.run.run_name_present);
        generated<std::uint8_t>(c, "kind", Type::u8, 1,
                                [&](auto) { /* Project this requested catalog/column cell; no input ownership transfers. */ return static_cast<unsigned>(d.run.kind); });
        generated<std::uint64_t>(c, "run_metadata_section", Type::u64, 1,
                                 [&](auto) { /* Project this requested catalog/column cell; no input ownership transfers. */ return run_id; });
        c.finish();
        results.finish();
        if(indexed) {
            /* Explicit-close indexed provenance object. */ auto p=group(j,"provenance");u64(p,"schema",1);u64(p,"original_source_index",0);
            /* Retained pre-solve authored-input digest section. */ auto digest=bytes(Type::u8,indexed->authored_input_sha256);descriptor(p,"authored_input_sha256",digest,32);
            /* Explicit-close normalized source catalog arrays. */ auto sources=group(p,"sources");
            generated<std::uint64_t>(sources,"source_index",Type::u64,source_count,[](auto k){ /* Project this requested catalog/column cell; no input ownership transfers. */return k;});
            generated<std::uint8_t>(sources,"kind",Type::u8,source_count,[](auto k){ /* Project this requested catalog/column cell; no input ownership transfers. */return k<5?0:1;});
            generated<std::uint64_t>(sources,"metadata_section",Type::u64,source_count,[&](auto k){ /* Project this requested catalog/column cell; no input ownership transfers. */return source_ids[k];});
            sources.finish();p.finish();
        } else provenance(j, v);
        j.end();
        /* Finished immutable metadata section. */ auto s = bytes(Type::json, j.bytes());
        root_id = s.id;
    }
    /// Write the final directory and backfill the authenticated bootstrap once every section is complete.
    void complete() {
        // Require complete reservations, append directory bytes, assemble header hashes/offsets, then seek to final extent.

        require(sections == finished && sections > 0);
        /* Borrowed completed canonical directory prefix. */ auto directory = prepared->directory_buffer().first(sections * 64);
        /* Checked complete file extent including directory. */ std::uint64_t total;
        require(checked_add(next, directory.size(), total));
        write(next, directory);
        /* Owned zero-initialized authenticated bootstrap. */ std::array<std::byte, 128> header{};
        /* Fixed native-container magic bytes. */ const char magic[8] = {'S', 'E', 'R', 'E', 'N', 'E', 0, 0};
        std::memcpy(header.data(), magic, 8);
        put(std::span(header).subspan(8, 2), 1);
        put(std::span(header).subspan(10,2),extended()?1:0);
        put(std::span(header).subspan(12, 4), 0x01020304);
        put(std::span(header).subspan(16, 4), 128);
        put(std::span(header).subspan(20, 4), 64);
        put(std::span(header).subspan(24, 8), total);
        put(std::span(header).subspan(32, 8), next);
        put(std::span(header).subspan(40, 8), sections);
        put(std::span(header).subspan(48, 8), root_id);
        put(std::span(header).subspan(56, 8), 1);
        /* Digest of canonical directory bytes. */ auto d = sha256(directory);
        std::copy(d.begin(), d.end(), header.begin() + 64);
        /* Digest of the bootstrap prefix including directory digest. */ auto h = sha256(std::span(header).first(96));
        std::copy(h.begin(), h.end(), header.begin() + 96);
        write(0, header);
        seek(total);
    }
};
ArchiveSession::ArchiveSession() noexcept = default;
ArchiveSession::~ArchiveSession() = default;
ArchiveSession::ArchiveSession(ArchiveSession &&) noexcept = default;
ArchiveSession &ArchiveSession::operator=(ArchiveSession &&) noexcept = default;
std::expected<ArchiveSession, Status> ArchiveSession::open(PreparedExport &p, Codec &c,
                                                           const std::filesystem::path &destination,
                                                           FaultHook hook) {
    // Validate preparation/codec/destination, borrow them into a private owner, exclusively
    // create one sibling scratch file, then write an incomplete zero bootstrap.
    try {
        require(p.valid() && c.max_values() >= p.limits().frame_values &&
                c.workspace_bytes() <= p.plan().codec_bytes);
        require(std::endian::native == std::endian::little, StatusCode::unsupported_feature);
        require(destination.extension() == ".serene");
        require(!std::filesystem::exists(destination), StatusCode::publication_failure);
        /* Destination parent used for same-filesystem scratch creation. */ auto parent = destination.parent_path();
        if (parent.empty())
            parent = ".";
        require(std::filesystem::is_directory(parent), StatusCode::io_failure);
        /* Private candidate session owning partial setup until success. */ ArchiveSession s;
        s.impl_ = std::make_unique<Impl>();
        /* Candidate-owned implementation borrowing preparation/codec. */ auto &i = *s.impl_;
        i.prepared = &p;
        i.codec = &c;
        i.hook = hook;
        i.destination = destination;
        i.stats.application_reserved_bytes = p.plan().total_bytes;
        std::fill(p.directory_buffer().begin(), p.directory_buffer().end(), std::byte{});
        /* Process-local uniqueness counter for exclusive scratch creation. */ static std::atomic<std::uint64_t> seq{};
        /* Steady-clock value used only as a temporary-name discriminator. */ auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt /* Exclusive temporary-name collision attempt. */ = 0; attempt < 32; attempt++) {
            /* Owned platform-native scratch filename under the final parent. */ auto name = destination.filename().native();
            name.insert(name.begin(), static_cast<std::filesystem::path::value_type>('.'));
            name +=
                std::filesystem::path(".tmp-" + std::to_string(stamp) + "-" + std::to_string(seq++))
                    .native();
            i.temp = parent / name;
            /* Native descriptor acquired exclusively before wrapping in stdio. */ int fd = -1;
#ifdef _WIN32
            /* Windows exclusive-open result. */ auto rc = _wsopen_s(&fd, i.temp.c_str(), _O_CREAT | _O_EXCL | _O_BINARY | _O_RDWR,
                                _SH_DENYRW, _S_IREAD | _S_IWRITE);
            if (rc == 0)
                i.file = _wfdopen(fd, L"w+b");
#else
            fd = ::open(i.temp.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
            if (fd >= 0)
                i.file = fdopen(fd, "w+b");
#endif
            if (i.file)
                break;
            if (fd >= 0) {
#ifdef _WIN32
                _close(fd);
#else
                ::close(fd);
#endif
                /* Best-effort filesystem cleanup/publication status. */ std::error_code ec;
                std::filesystem::remove(i.temp, ec);
            }
            i.temp.clear();
        }
        require(i.file != nullptr, StatusCode::io_failure);
        /* Incomplete bootstrap that cannot claim a finished archive. */ std::array<std::byte, 128> zero{};
        i.write(0, zero);
        return s;
    } catch (const Failure &f) {
        return std::unexpected(f.status);
    } catch (const std::bad_alloc &) {
        return std::unexpected(error(StatusCode::resource_limit, 31));
    } catch (...) {
        return std::unexpected(error(StatusCode::io_failure, 31));
    }
}
Status ArchiveSession::append_run(const RunResultView &v,
                                  const ProvenanceView &provenance) noexcept {
    // Admit opened legacy mode, validate results, serialize diagnostics/run/root, then
    // mark written; any exception latches failed and returns a fixed Status.
    if (!impl_ || impl_->state != SessionState::opened)
        return error(StatusCode::invalid_state, 32);
    /* Borrowed sole session state for this operation. */ auto &i = *impl_;
    try {
        require(!i.prepared->description().indexed_provenance);
        /* Committed result admission status. */ auto status = i.prepared->validate(v);
        if (!status)
            throw Failure{status};
        i.state = SessionState::writing;
        i.selected_catalog_included=v.info.catalog_state==SectionState::included;
        i.diagnostic_traces(v);
        /* Finished single-run metadata section ID. */ auto id = i.run(v);
        i.root(id, provenance);
        i.state = SessionState::written;
        return {};
    } catch (const Failure &f) {
        i.state = SessionState::failed;
        return f.status;
    } catch (const std::bad_alloc &) {
        i.state = SessionState::failed;
        return error(StatusCode::resource_limit, 32);
    } catch (...) {
        i.state = SessionState::failed;
        return error(StatusCode::io_failure, 32);
    }
}
Status ArchiveSession::append_run(const RunResultView& v,const IndexedProvenanceView& provenance) noexcept {
    // Admit opened indexed mode, validate results and sources, write diagnostics/run/root,
    // then mark written; failures poison only this publication attempt.
    if(!impl_||impl_->state!=SessionState::opened)return error(StatusCode::invalid_state,32);
    /* Borrowed sole session state for indexed append. */ auto& i=*impl_;
    try {
        require(i.prepared->description().indexed_provenance);
        /* Committed result admission status. */ auto status=i.prepared->validate(v);if(!status)throw Failure{status};
        i.state=SessionState::writing;
        i.selected_catalog_included=v.info.catalog_state==SectionState::included;
        i.indexed_sources(provenance);i.diagnostic_traces(v);
        /* Finished single-run metadata section ID. */ auto id=i.run(v);i.root(id,{},&provenance);i.state=SessionState::written;return {};
    } catch(const Failure& f) {i.state=SessionState::failed;return f.status;}
      catch(const std::bad_alloc&) {i.state=SessionState::failed;return error(StatusCode::resource_limit,32);}
      catch(...) {i.state=SessionState::failed;return error(StatusCode::io_failure,32);}
}
Status ArchiveSession::finalize() noexcept {
    // Require a written run, complete directory/bootstrap, check flush and close, then
    // publish without replacing an existing destination and release scratch ownership.
    if (!impl_ || impl_->state != SessionState::written)
        return error(StatusCode::invalid_state, 33);
    /* Borrowed sole session state for this operation. */ auto &i = *impl_;
    try {
        i.state = SessionState::finalizing;
        i.complete();
        i.fault(IoOperation::flush);
        io(std::fflush(i.file) == 0, 36);
        i.fault(IoOperation::close);
        /* Transferred stream pointer, ensuring one checked close. */ auto *f = std::exchange(i.file, nullptr);
        io(std::fclose(f) == 0, 37);
        i.fault(IoOperation::publish);
#ifdef _WIN32
        require(MoveFileExW(i.temp.c_str(), i.destination.c_str(), 0) != 0,
                StatusCode::publication_failure);
#else
        /* Best-effort filesystem cleanup/publication status. */ std::error_code ec;
        std::filesystem::create_hard_link(i.temp, i.destination, ec);
        require(!ec, StatusCode::publication_failure);
        std::filesystem::remove(i.temp, ec);
#endif
        i.temp.clear();
        i.state = SessionState::published;
        return {};
    } catch (const Failure &f) {
        i.state = SessionState::failed;
        return f.status;
    } catch (...) {
        i.state = SessionState::failed;
        return error(StatusCode::publication_failure, 33);
    }
}
SessionState ArchiveSession::state() const noexcept {
    // Read retained lifecycle state, using failed for an empty handle.
    return impl_ ? impl_->state : SessionState::failed;
}
SaveStatistics ArchiveSession::statistics() const noexcept {
    // Copy retained counters, using zero values for an empty handle.
    return impl_ ? impl_->stats : SaveStatistics{};
}
const std::filesystem::path &ArchiveSession::temporary_path() const noexcept {
    // Return the retained scratch path, or a process-lifetime empty path for an empty handle.
    /* Process-lifetime empty reference for moved-from handles. */ static const std::filesystem::path empty;
    return impl_ ? impl_->temp : empty;
}
} // namespace serene::outputs
