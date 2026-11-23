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
#include <serene/outputs/modal.hpp>
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
/// Owns no dynamic message; ModalArchiveSession entry points translate it back to Status.
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
/// Fixed-capacity JSON emitter borrowed by ModalArchiveSession metadata routines.
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
/// Owned single-run modal file state; borrows prepared scratch and codec through publication.
/// Generic section helpers use the same bounded algorithms as the preserved scalar writer.
struct ModalArchiveSession::Impl {
    PreparedModalExport *prepared{}; ///< Borrowed exclusive preparation and workspace.
    Codec *codec{}; ///< Borrowed serial reversible codec.
    FaultHook hook{}; ///< Borrowed synchronous fault hooks.
    std::filesystem::path destination, temp; ///< Owned final and exclusive unpublished paths.
    std::FILE *file{}; ///< Owned stream, null after close.
    SessionState state{SessionState::opened}; ///< Current publication lifecycle.
    SaveStatistics stats{}; ///< Constant-size I/O and frame counters.
    std::array<std::uint64_t,7> calls{}; ///< Per-operation fault ordinals.
    std::uint64_t next{128}, position{}, sections{}, finished{}, root_id{}; ///< Checked file/directory cursors.
    std::uint64_t source_bytes{}; ///< Charged aggregate copied document/log bytes.
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
    void generated(Json &j, const char *n, Type type, std::uint64_t N, Get get, std::span<const std::uint64_t> shape = {}) {
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
        if(shape.empty()) descriptor(j, n, s, N); else descriptor(j, n, s, shape);
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
    /// Emit the pinned reversible profile and independent mode-row assignments into values object v.
    void profiles(Json &v, std::uint64_t outer) {
        // Generate row-profile indices, exact profile fields and pinned identity strings without inspecting compressed data.

        /* Pinned reversible profile value; no source ownership is retained. */ const auto profile=CodecProfile::reversible();
        /* Number of prepared profile definitions. */ const std::uint64_t M=1;
        generated<std::uint32_t>(v, "row_profile_index", Type::u32, outer, [&](auto row) {
            // Map every independent mode row to reversible profile zero.
            return 0;
        });
        /* Explicit-close profile catalog descriptor. */ auto p = group(v, "profiles");
        generated<std::uint32_t>(p, "profile_index", Type::u32, M, [](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return k; });
        generated<std::uint8_t>(p, "mode", Type::u8, M, [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return static_cast<unsigned>(profile.mode()); });
        generated<std::uint32_t>(p, "codec_version", Type::u32, M, [](auto) { /* Project this requested catalog/column cell; no input ownership transfers. */ return 5; });
        generated<std::uint32_t>(p, "stream_word_bits", Type::u32, M, [](auto) { /* Project this requested catalog/column cell; no input ownership transfers. */ return 64; });
        generated<std::int8_t>(p, "rounding", Type::i8, M, [](auto) { /* Project this requested catalog/column cell; no input ownership transfers. */ return 0; });
        generated<std::uint64_t>(p, "mode_code", Type::u64, M, [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return profile.mode_code(); });
        generated<std::uint8_t>(p, "tolerance_present", Type::u8, M, [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return profile.tolerance_present(); });
        generated<double>(p, "requested_tolerance", Type::f64, M, [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return profile.requested_tolerance(); });
        generated<double>(p, "effective_tolerance", Type::f64, M, [&](auto k) { /* Project this requested catalog/column cell; no input ownership transfers. */ return profile.effective_tolerance(); });
        for (auto pair /* Fixed metadata key/value pair for codec identity. */ : std::array<std::pair<const char *, std::string_view>, 5>{
                 {{"codec", "zfp"},
                  {"library_version", "1.0.1"},
                  {"library_revision", "f40868a6a1c190c802e7d8b5987064f044bf7812"},
                  {"byte_order", "little"},
                  {"execution", "serial"}}})
            strings(p, pair.first, M, [&](auto) { /* Project this requested catalog/column cell; no input ownership transfers. */ return pair.second; });
        p.finish();
    }

    /// Encode borrowed committed source values into framed payload/index/statistics sections under run.
    void values(Json &run, const ValueView &source) {
        // Describe layout/profiles, reserve index/statistic sections, gather and verify each frame, tile index writes, then finish all descriptors.

        /* Explicit-close framed value descriptor. */ auto g = group(run, "values");
        /* Logical row count/width; B below is the admitted frame capacity. */ auto outer = source.outer, inner = source.inner,
             B = prepared->limits().frame_values;
        g.json.array("shape");
        g.json.number(nullptr, outer);
        g.json.number(nullptr, inner);
        g.json.end(false);
        text(g, "dtype", "float64");
        g.json.array("axis_names");
        /* Modal rows retain one independent direction each. */
        g.json.text(nullptr, "mode");
        g.json.text(nullptr, "observation_component");
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
        for (std::uint64_t row /* Logical independent mode row. */ = 0; row < outer; row++)
            for (std::uint64_t begin /* Inner-axis frame start, advanced by nominal frame capacity. */ = 0; begin < inner; begin += B) {
                /* Actual values in this possibly short final frame. */ auto n = std::min(B, inner - begin);
                /* Borrowed frame-sized source gather destination. */ auto gather = prepared->gather_buffer().first(n);
                fault(IoOperation::source);
                /* Source gather status. */ auto status = source.fill(row, begin, gather);
                if (!status)
                    throw Failure{status};
                fault(IoOperation::codec);
                /* Modal shape coefficients always use the reversible profile. */
                /* Expected profile slot for this logical row. */ const std::uint32_t profile_index=0;
                /* Verified codec result borrowing the codec payload buffer. */ auto encoded = codec->encode(gather, prepared->decode_buffer(),CodecProfile::reversible());
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
    template<class T>
    /// Emit optional typed scalar evidence; absent values use canonical positive zero.
    void optional(Json& j,const char* name,const std::optional<T>& value) {
        // Write presence and the original scalar without changing available evidence.
        auto g=group(j,name); // Fixed evidence object in bounded serialization scratch.
        u8(g,"present",value.has_value());
        if constexpr(std::is_same_v<T,double>)f64(g,"value",value.value_or(0.));
        else u64(g,"value",value.value_or(0));
        g.finish();
    }
    /// Emit the typed source report and independently available variable-length arrays.
    void report(Json& j,const ModalSolveReportView& r) {
        // Write scalar facts then stream the dependent-row and shift columns.
        auto g=group(j,"report"); // Owned report object scope borrowing fixed JSON scratch.
        u64(g,"requested_elastic_modes",r.requested_elastic_modes);
        u64(g,"searched_candidates",r.searched_candidates);
        u64(g,"retained_elastic_modes",r.retained_elastic_modes);
        u8(g,"exhausted_complete_spectrum",r.exhausted_complete_spectrum);
        u8(g,"used_sparse_path",r.used_sparse_path);
        u8(g,"used_dense_fallback",r.used_dense_fallback);
        text(g,"dense_fallback_reason",r.dense_fallback_reason);
        optional(g,"physical_coordinate_count",r.physical_coordinate_count);
        optional(g,"fixed_coordinate_count",r.fixed_coordinate_count);
        optional(g,"constraint_rows",r.constraint_rows);
        optional(g,"constraint_rank",r.constraint_rank);
        optional(g,"reduced_dimension",r.reduced_dimension);
        optional(g,"reduced_mass_nonzeros",r.reduced_mass_nonzeros);
        optional(g,"reduced_stiffness_nonzeros",r.reduced_stiffness_nonzeros);
        optional(g,"lift_constraint_error",r.lift_constraint_error);
        optional(g,"mass_symmetry_error",r.mass_symmetry_error);
        optional(g,"stiffness_symmetry_error",r.stiffness_symmetry_error);
        optional(g,"maximum_mass_orthogonality_error",r.maximum_mass_orthogonality_error);
        optional(g,"maximum_cluster_subspace_sine_bound",r.maximum_cluster_subspace_sine_bound);
        optional(g,"maximum_projected_relative_residual",r.maximum_projected_relative_residual);
        optional(g,"dense_counted_eigenvalues",r.dense_counted_eigenvalues);
        optional(g,"cutoff_cluster_size",r.cutoff_cluster_size);
        optional(g,"certified_lower_eigenvalue_per_s2",r.certified_lower_eigenvalue_per_s2);
        optional(g,"certified_upper_eigenvalue_per_s2",r.certified_upper_eigenvalue_per_s2);
        optional(g,"upper_separator_eigenvalue_per_s2",r.upper_separator_eigenvalue_per_s2);
        optional(g,"dense_absolute_uncertainty_per_s2",r.dense_absolute_uncertainty_per_s2);
        optional(g,"sparse_requested_candidates",r.sparse_requested_candidates);
        optional(g,"sparse_converged_candidates",r.sparse_converged_candidates);
        optional(g,"sparse_iterations",r.sparse_iterations);
        optional(g,"krylov_basis_size",r.krylov_basis_size);
        optional(g,"constraint_workspace_bytes",r.constraint_workspace_bytes);
        optional(g,"operator_workspace_bytes",r.operator_workspace_bytes);
        optional(g,"dense_workspace_bytes",r.dense_workspace_bytes);
        optional(g,"dense_work_bound",r.dense_work_bound);
        optional(g,"factor_workspace_bytes",r.factor_workspace_bytes);
        optional(g,"basis_workspace_bytes",r.basis_workspace_bytes);
        optional(g,"result_payload_bytes",r.result_payload_bytes);
        optional(g,"reduction_seconds",r.reduction_seconds);
        optional(g,"dense_count_seconds",r.dense_count_seconds);
        optional(g,"sparse_seconds",r.sparse_seconds);
        optional(g,"dense_fallback_seconds",r.dense_fallback_seconds);
        auto d=group(g,"dependent_constraint_rows"); // Independent included-empty/unavailable row evidence.
        u8(d,"state",code(r.dependent_constraint_rows_state));
        if(r.dependent_constraint_rows_state==SectionState::included)
            generated<std::uint64_t>(d,"values",Type::u64,r.dependent_constraint_rows.size(),[&](auto k){/* Copy one exact original row identity. */return r.dependent_constraint_rows[k];});
        d.finish();
        auto s=group(g,"effective_shifts_per_s2"); // Independent included-empty/unavailable shift evidence.
        u8(s,"state",code(r.effective_shifts_state));
        if(r.effective_shifts_state==SectionState::included)
            generated<double>(s,"values",Type::f64,r.effective_shifts_per_s2.size(),[&](auto k){/* Copy one original signed shift. */return r.effective_shifts_per_s2[k];});
        s.finish();g.finish();
    }
    /// Emit source-authored interval, classification, grouping and subspace thresholds.
    void criteria(Json& j,const ModalCriteriaView& c) {
        // Write finite admitted scalars in fixed contract order.
        auto g=group(j,"criteria"); // Explicit-close criteria scope.
        f64(g,"minimum_eigenvalue_per_s2",c.minimum_eigenvalue_per_s2);
        f64(g,"maximum_eigenvalue_per_s2",c.maximum_eigenvalue_per_s2);
        f64(g,"zero_eigenvalue_tolerance_per_s2",c.zero_eigenvalue_tolerance_per_s2);
        f64(g,"negative_eigenvalue_tolerance_per_s2",c.negative_eigenvalue_tolerance_per_s2);
        f64(g,"relative_residual_tolerance",c.relative_residual_tolerance);
        f64(g,"constraint_tolerance",c.constraint_tolerance);
        f64(g,"mass_orthogonality_tolerance",c.mass_orthogonality_tolerance);
        f64(g,"cluster_relative_tolerance",c.cluster_relative_tolerance);
        f64(g,"cluster_absolute_tolerance_per_s2",c.cluster_absolute_tolerance_per_s2);
        f64(g,"cluster_subspace_tolerance",c.cluster_subspace_tolerance);
        g.finish();
    }
    /// Emit run/reference/selection outcomes and exact optional document evidence.
    void info(Json& j,const ModalRunResultView& v) {
        // Copy prepared identities, source statuses/request flags and borrowed documents.
        const auto& d=prepared->description(); // Immutable copied identity and requested policy.
        auto g=group(j,"info"); // Bounded metadata object scope.
        text(g,"run_id",d.run_id);u8(g,"kind",2);text(g,"assembly_ref",d.assembly_ref);
        text(g,"run_name",d.run_name);u8(g,"run_name_present",d.run_name_present);
        u8(g,"result_kind",static_cast<unsigned>(v.result_kind));
        u8(g,"search_status",static_cast<unsigned>(v.search_status));
        u8(g,"certification_status",static_cast<unsigned>(v.certification_status));
        text(g,"solve_code",v.solve_code);text(g,"diagnostic",v.diagnostic);
        u8(g,"reference_state",code(d.reference_state));text(g,"physical_id",d.physical_id);text(g,"binding_id",d.binding_id);
        auto q=group(g,"requested"); // Authoritative flags, independent of required signed lambda identity.
        u8(q,"eigenvalues",d.requested.eigenvalues);u8(q,"frequencies_hz",d.requested.frequencies_hz);
        u8(q,"mode_shapes",d.requested.mode_shapes);u8(q,"mode_diagnostics",d.requested.mode_diagnostics);q.finish();
        u8(g,"frequencies_state",code(v.frequencies_state));u8(g,"mode_diagnostics_state",code(v.mode_diagnostics_state));
        document(g,"solver_configuration",v.solver_configuration);document(g,"execution",v.execution);g.finish();
    }
    template<class Get>
    /// Stream one optional per-mode evidence pair, preserving absent-positive-zero convention.
    void mode_optional(Json& j,const char* name,const char* presence,std::uint64_t n,Get get) {
        // Write values and masks in distinct sections without retaining a duplicate column.
        generated<double>(j,name,Type::f64,n,[&](auto k){/* Preserve present evidence or canonical missing zero. */return get(k).value_or(0.);});
        generated<std::uint8_t>(j,presence,Type::u8,n,[&](auto k){/* Preserve optional evidence presence. */return get(k).has_value();});
    }
    /// Stream complete certified mode and cluster tables without changing signs or order.
    void modes(Json& j,const ModalRunResultView& v) {
        // Write mandatory identities/classes, requested optional pairs, then contiguous clusters.
        const auto n=v.modes.size(); // Actual retained independent direction count.
        auto m=group(j,"modes"); // Typed mode-table scope.
        u64(m,"count",n);
        generated<std::uint64_t>(m,"mode_index",Type::u64,n,[](auto k){/* Emit exact row ordinal. */return k;});
        generated<std::uint64_t>(m,"mode_id",Type::u64,n,[&](auto k){/* Copy stable source ID. */return v.modes[k].mode_id;});
        generated<std::uint64_t>(m,"cluster_index",Type::u64,n,[&](auto k){/* Copy cluster membership ordinal. */return v.modes[k].cluster_index;});
        generated<double>(m,"eigenvalue_per_s2",Type::f64,n,[&](auto k){/* Preserve signed eigenvalue bits. */return v.modes[k].eigenvalue_per_s2;});
        generated<std::uint8_t>(m,"classification",Type::u8,n,[&](auto k){/* Copy the admitted physical class. */return static_cast<unsigned>(v.modes[k].classification);});
        if(v.frequencies_state==SectionState::included)
            mode_optional(m,"frequency_hz","frequency_hz_present",n,[&](auto k){/* Borrow optional real Hz evidence. */return v.modes[k].frequency_hz;});
        if(v.mode_diagnostics_state==SectionState::included) {
            mode_optional(m,"projected_relative_residual","projected_relative_residual_present",n,[&](auto k){/* Borrow projected residual evidence. */return v.modes[k].projected_relative_residual;});
            mode_optional(m,"constraint_relative_error","constraint_relative_error_present",n,[&](auto k){/* Borrow constraint evidence. */return v.modes[k].constraint_relative_error;});
            mode_optional(m,"modal_mass","modal_mass_present",n,[&](auto k){/* Borrow mass norm evidence. */return v.modes[k].modal_mass;});
        }
        mode_optional(m,"display_scale","display_scale_present",n,[&](auto k){/* Borrow independent display-only scale. */return v.modes[k].display_scale;});m.finish();
        auto c=group(j,"clusters"); // Complete partition scope; no eigenvectors are merged.
        text(c,"grouping_rule","adjacent_gap_absolute_plus_relative_plus_twice_count_uncertainty");
        text(c,"grouping_basis","same_run_dense_count");
        const auto g=v.clusters.size(); // Actual cluster population.
        u64(c,"count",g);
        generated<std::uint64_t>(c,"cluster_index",Type::u64,g,[](auto k){/* Emit exact cluster ordinal. */return k;});
        generated<std::uint64_t>(c,"cluster_id",Type::u64,g,[&](auto k){/* Copy stable cluster ID. */return v.clusters[k].cluster_id;});
        generated<std::uint64_t>(c,"first_mode_index",Type::u64,g,[&](auto k){/* Copy first member ordinal. */return v.clusters[k].first_mode_index;});
        generated<std::uint64_t>(c,"mode_count",Type::u64,g,[&](auto k){/* Copy independent member count. */return v.clusters[k].mode_count;});c.finish();
    }
    /// Stream physical observation catalog, exact world frame and explicit reference triad.
    void observations(Json& j) {
        // Write identity/address columns and flatten each reference-frame matrix in row order.
        const auto& rows=prepared->description().observations; // Immutable owned physical catalog.
        const auto n=rows.size(); // Ordered observation count, possibly known empty.
        auto g=group(j,"observations"); // Explicit-close observation-table scope.
        u64(g,"count",n);
        generated<std::uint64_t>(g,"observation_index",Type::u64,n,[](auto k){/* Emit authored row ordinal. */return k;});
        strings(g,"request_id",n,[&](auto k){/* Borrow copied request identity. */return std::string_view(rows[k].request_id);});
        strings(g,"occurrence_id",n,[&](auto k){/* Borrow copied occurrence identity. */return std::string_view(rows[k].occurrence_id);});
        strings(g,"port_id",n,[&](auto k){/* Borrow copied physical port identity. */return std::string_view(rows[k].port_id);});
        strings(g,"reference_triad",n,[&](auto k){/* Borrow explicit source reference-basis identity. */return std::string_view(rows[k].reference_triad);});
        generated<std::uint8_t>(g,"kind",Type::u8,n,[&](auto k){/* Copy physical address vocabulary. */return static_cast<unsigned>(rows[k].kind);});
        generated<double>(g,"material_s_m",Type::f64,n,[&](auto k){/* Preserve resolved material coordinate. */return rows[k].material_s_m;});
        generated<std::uint8_t>(g,"material_s_present",Type::u8,n,[&](auto k){/* Preserve material-backed port versus absent coordinate. */return rows[k].material_s_present;});
        generated<std::uint8_t>(g,"side",Type::u8,n,[&](auto k){/* Copy sided material limit. */return static_cast<unsigned>(rows[k].side);});
        const std::array<std::uint64_t,2> pshape{n,3},qshape{n,4}; // Exact world-reference matrix extents.
        generated<double>(g,"position_world_m",Type::f64,n*3,[&](auto k){/* Preserve one reference-point component. */return rows[k/3].position_world_m[k%3];},pshape);
        generated<double>(g,"orientation_world_wxyz",Type::f64,n*4,[&](auto k){/* Preserve one scalar-first reference quaternion component. */return rows[k/4].orientation_world_wxyz[k%4];},qshape);g.finish();
    }
    /// Write one mode-indexed run and return its owning metadata section identity.
    std::uint64_t run(const ModalRunResultView& v) {
        // Emit fixed result metadata and tables, then stream requested shape frames and freeze JSON.
        Json j(prepared->serialization_buffer()); // Fixed preallocated JSON workspace.
        j.object();info(j,v);criteria(j,v.criteria);report(j,v.report);modes(j,v);observations(j);
        auto s=group(j,"shapes"); // Explicit included/unrequested/unavailable shape envelope.
        u8(s,"state",code(v.shapes_state));
        if(v.shapes_state==SectionState::included) {
            s.json.array("logical_shape");s.json.number(nullptr,v.modes.size());s.json.number(nullptr,prepared->description().observations.size());s.json.number(nullptr,6);s.json.end(false);
            s.json.array("component_names");
            for(auto name: {"translation_x","translation_y","translation_z","rotation_x","rotation_y","rotation_z"})s.json.text(nullptr,name); // Fixed physical component order.
            s.json.end(false);s.json.array("component_units");
            for(auto unit: {"m","m","m","rad","rad","rad"})s.json.text(nullptr,unit); // Physical units before multiplication by modal amplitude.
            s.json.end(false);text(s,"frame","world");text(s,"normalization","unit_generalized_mass");text(s,"coefficient_convention","per_unit_modal_coordinate");
            values(s,v.shapes);
        }
        s.finish();j.end();
        auto section=bytes(Type::json,j.bytes()); // Finished bounded strict-JSON metadata.
        return section.id;
    }
    /// Emit the one-row modal root and retained legacy provenance branches.
    void root(std::uint64_t run_section,const ProvenanceView& v) {
        // Declare exact version/features, write global catalog and provenance, then freeze root JSON.
        const auto& d=prepared->description(); // Immutable copied archive/run identity.
        Json j(prepared->serialization_buffer()); // Reused bounded JSON scratch after run is written.
        j.object();auto f=group(j,"format"); // Root format declaration scope.
        text(f,"name","serene.outputs");u32(f,"version_major",1);u32(f,"version_minor",2);
        text(f,"file_id",d.file_id);text(f,"created_utc",d.created_utc);text(f,"writer_name",d.writer_name);text(f,"writer_version",d.writer_version);text(f,"write_state","complete");
        f.json.array("required_features");
        for(auto token: {"serene-container-le-1","json-descriptors-1","utf8-pools-1","frames-77-le-1","zfp-5-le64-serial-1","modal-results-1"})f.json.text(nullptr,token); // Exact admitted single-run profile.
        f.json.end(false);auto st=group(f,"sections"); // Retention state remains independent of solve status.
        u8(st,"inputs",code(v.inputs.state));u8(st,"logs",code(v.logs.state));u8(st,"context",code(v.context.state));st.finish();f.finish();
        auto results=group(j,"results");auto c=group(results,"index"); // One-row authoritative run catalog.
        generated<std::uint64_t>(c,"run_index",Type::u64,1,[](auto){/* Single-run ordinal. */return 0;});
        strings(c,"run_id",1,[&](auto){/* Borrow copied run ID. */return std::string_view(d.run_id);});
        strings(c,"run_name",1,[&](auto){/* Preserve present-empty optional display name. */return std::string_view(d.run_name);},true,d.run_name_present);
        generated<std::uint8_t>(c,"kind",Type::u8,1,[](auto){/* Modal wire kind. */return 2;});
        generated<std::uint64_t>(c,"run_metadata_section",Type::u64,1,[&](auto){/* Link the distinct owned run metadata section. */return run_section;});c.finish();results.finish();
        provenance(j,v);j.end();auto section=bytes(Type::json,j.bytes()); // Finished root owns its catalog/provenance only.
        root_id=section.id;
    }
    /// Write the final directory and backfill the integrity-hashed bootstrap once every section is complete.
    void complete() {
        // Require complete reservations, append directory bytes, assemble header hashes/offsets, then seek to final extent.

        require(sections == finished && sections > 0);
        /* Borrowed completed canonical directory prefix. */ auto directory = prepared->directory_buffer().first(sections * 64);
        /* Checked complete file extent including directory. */ std::uint64_t total;
        require(checked_add(next, directory.size(), total));
        write(next, directory);
        /* Owned zero-initialized integrity-hashed bootstrap. */ std::array<std::byte, 128> header{};
        /* Fixed native-container magic bytes. */ const char magic[8] = {'S', 'E', 'R', 'E', 'N', 'E', 0, 0};
        std::memcpy(header.data(), magic, 8);
        put(std::span(header).subspan(8, 2), 1);
        put(std::span(header).subspan(10,2),2);
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
ModalArchiveSession::ModalArchiveSession() noexcept = default;
ModalArchiveSession::~ModalArchiveSession() = default;
ModalArchiveSession::ModalArchiveSession(ModalArchiveSession &&) noexcept = default;
ModalArchiveSession &ModalArchiveSession::operator=(ModalArchiveSession &&) noexcept = default;
std::expected<ModalArchiveSession, Status> ModalArchiveSession::open(PreparedModalExport &p, Codec &c,
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
        /* Private candidate session owning partial setup until success. */ ModalArchiveSession s;
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
Status ModalArchiveSession::append_run(const ModalRunResultView& v,const ProvenanceView& provenance) noexcept {
    // Admit one final immutable result, serialize it and provenance, and latch any failure.
    if(!impl_||impl_->state!=SessionState::opened)return error(StatusCode::invalid_state,132);
    auto& i=*impl_; // Borrowed sole mutable publication state.
    try {
        auto status=i.prepared->validate(v); // Scientific/result consistency admission.
        if(!status)throw Failure{status};
        i.state=SessionState::writing;
        auto id=i.run(v); // Finished mode-indexed run metadata section.
        i.root(id,provenance);
        i.state=SessionState::written;return {};
    } catch(const Failure& f){i.state=SessionState::failed;return f.status;}
      catch(const std::bad_alloc&){i.state=SessionState::failed;return error(StatusCode::resource_limit,132);}
      catch(...){i.state=SessionState::failed;return error(StatusCode::io_failure,132);}
}
Status ModalArchiveSession::finalize() noexcept {
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
SessionState ModalArchiveSession::state() const noexcept {
    // Read retained lifecycle state, using failed for an empty handle.
    return impl_ ? impl_->state : SessionState::failed;
}
SaveStatistics ModalArchiveSession::statistics() const noexcept {
    // Copy retained counters, using zero values for an empty handle.
    return impl_ ? impl_->stats : SaveStatistics{};
}
const std::filesystem::path &ModalArchiveSession::temporary_path() const noexcept {
    // Return the retained scratch path, or a process-lifetime empty path for an empty handle.
    /* Process-lifetime empty reference for moved-from handles. */ static const std::filesystem::path empty;
    return impl_ ? impl_->temp : empty;
}
} // namespace serene::outputs
