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
struct Failure {
    Status status;
};
void require(bool ok, StatusCode code = StatusCode::invalid_contract, std::uint32_t op = 30) {
    if (!ok)
        throw Failure{error(code, op)};
}
void io(bool ok, std::uint32_t operation) {
    if (!ok) {
        auto s = error(StatusCode::io_failure, operation);
        s.native_code = errno;
        throw Failure{s};
    }
}
std::uint8_t code(SectionState s) {
    return static_cast<std::uint8_t>(s);
}
bool included(SectionState s) {
    return s == SectionState::included || s == SectionState::partial;
}
void put(std::span<std::byte> bytes, std::uint64_t value) {
    for (std::size_t k = 0; k < bytes.size(); k++)
        bytes[k] = std::byte(value >> (8 * k));
}
std::array<std::byte, 8> le64(std::uint64_t x) {
    std::array<std::byte, 8> b;
    put(b, x);
    return b;
}
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
const char *dtype(Type t) {
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
std::uint64_t width(Type t) {
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
// Fixed-capacity JSON emitter. No DOM/string allocations; each generated key is known once.
class Json {
    std::span<std::byte> out_;
    std::size_t used_{};
    std::array<bool, 16> first_{};
    std::size_t depth_{};
    std::uint64_t values_{}, members_{};
    void raw(std::string_view s) {
        require(s.size() <= out_.size() - used_, StatusCode::resource_limit);
        std::memcpy(out_.data() + used_, s.data(), s.size());
        used_ += s.size();
    }
    void quoted(std::string_view s) {
        require(valid_utf8(s));
        raw("\"");
        for (unsigned char c : s) {
            if (c == '"')
                raw("\\\"");
            else if (c == '\\')
                raw("\\\\");
            else if (c < 32) {
                char b[6] = {
                    '\\', 'u', '0', '0', "0123456789abcdef"[c >> 4], "0123456789abcdef"[c & 15]};
                raw({b, 6});
            } else {
                char b = static_cast<char>(c);
                raw({&b, 1});
            }
        }
        raw("\"");
    }
    void prefix(const char *key) {
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
    explicit Json(std::span<std::byte> b) : out_(b) {
    }
    void object(const char *key = nullptr) {
        prefix(key);
        require(depth_ < 16, StatusCode::resource_limit);
        raw("{");
        first_[depth_++] = true;
    }
    void array(const char *key) {
        prefix(key);
        require(depth_ < 16, StatusCode::resource_limit);
        raw("[");
        first_[depth_++] = true;
    }
    void end(bool object = true) {
        require(depth_ > 0);
        depth_--;
        raw(object ? "}" : "]");
    }
    void text(const char *key, std::string_view s) {
        prefix(key);
        quoted(s);
    }
    void number(const char *key, std::uint64_t n) {
        prefix(key);
        char b[32];
        auto x = std::to_chars(b, b + sizeof b, n);
        require(x.ec == std::errc{});
        raw({b, static_cast<std::size_t>(x.ptr - b)});
    }
    void real(const char *key, double n) {
        require(std::isfinite(n));
        prefix(key);
        char b[64];
        auto x = std::to_chars(b, b + sizeof b, n, std::chars_format::general,
                               std::numeric_limits<double>::max_digits10);
        require(x.ec == std::errc{});
        std::string_view s{b, static_cast<std::size_t>(x.ptr - b)};
        raw(s);
        if (s.find_first_of(".eE") == s.npos)
            raw(".0");
    }
    std::span<const std::byte> bytes() const {
        require(depth_ == 0);
        return out_.first(used_);
    }
};
struct Group {
    Json &json;
    bool open = true;
    operator Json &() {
        return json;
    }
    void finish() {
        require(open);
        json.end();
        open = false;
    }
};
Group group(Json &j, const char *n) {
    j.object(n);
    return {j};
}
void text(Json &j, const char *n, std::string_view s) {
    j.text(n, s);
}
void u8(Json &j, const char *n, std::uint8_t x) {
    j.number(n, x);
}
void u32(Json &j, const char *n, std::uint32_t x) {
    j.number(n, x);
}
void u64(Json &j, const char *n, std::uint64_t x) {
    j.number(n, x);
}
void f64(Json &j, const char *n, double x) {
    j.real(n, x);
}
struct Section {
    std::uint64_t id{}, offset{}, size{}, written{};
    Type type{};
    bool variable{};
    Sha256 hash;
};
} // namespace
struct ArchiveSession::Impl {
    PreparedExport *prepared{};
    Codec *codec{};
    FaultHook hook{};
    std::filesystem::path destination, temp;
    std::FILE *file{};
    SessionState state{SessionState::opened};
    SaveStatistics stats{};
    std::array<std::uint64_t, 7> calls{};
    std::uint64_t next{128}, position{}, sections{}, finished{}, root_id{};
    std::uint64_t source_bytes{};
    std::array<std::uint64_t,5> trace_ids{};
    std::array<std::uint64_t,69> source_ids{};
    std::uint64_t source_count{};
    DiagnosticStatus diagnostic_status{DiagnosticStatus::disabled};
    bool selected_catalog_included{};
    std::array<SectionState,3> indexed_states{};
    bool fixed_accuracy() const noexcept {
        if(!selected_catalog_included)return false;
        for(auto p:prepared->description().run.profiles)
            if(p.mode()==CodecMode::fixed_accuracy)return true;
        return false;
    }
    bool extended() const noexcept {
        return prepared->description().indexed_provenance ||
               !prepared->description().run.diagnostics.empty() || fixed_accuracy();
    }
    ~Impl() {
        if (file)
            std::fclose(file);
        if (!temp.empty()) {
            std::error_code ec;
            std::filesystem::remove(temp, ec);
        }
    }
    void fault(IoOperation op) {
        auto n = calls[static_cast<unsigned>(op)]++;
        if (hook.function) {
            auto s = hook.function(hook.context, op, n);
            if (!s)
                throw Failure{s};
        }
    }
    void seek(std::uint64_t offset) {
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
    void write(std::uint64_t offset, std::span<const std::byte> b) {
        if (b.empty())
            return;
        std::uint64_t end;
        require(checked_add(offset, b.size(), end));
        require(end<=prepared->plan().maximum_archive_bytes,StatusCode::resource_limit);
        seek(offset);
        fault(IoOperation::write);
        auto ordinal = stats.write_calls++;
        auto count = b.size();
        if (hook.write_limit)
            count = std::min(count, hook.write_limit(hook.context, ordinal, count));
        auto done = std::fwrite(b.data(), 1, count, file);
        position += done;
        stats.written_bytes += done;
        io(done == b.size(), 35);
    }
    Section reserve(Type t, std::uint64_t size, bool variable = false) {
        require(sections < prepared->directory_buffer().size() / 64, StatusCode::resource_limit);
        std::uint64_t end;
        require(checked_add(next, size, end));
        Section s;
        s.id = sections++;
        s.offset = next;
        s.size = size;
        s.type = t;
        s.variable = variable;
        next = end;
        return s;
    }
    void append(Section &s, std::span<const std::byte> b) {
        std::uint64_t count;
        require(checked_add(s.written, b.size(), count));
        if (s.variable) {
            require(next == s.offset + s.size);
            std::uint64_t end;
            require(checked_add(s.offset, count, end));
            s.size = count;
            next = end;
        } else
            require(count <= s.size);
        write(s.offset + s.written, b);
        s.hash.update(b);
        s.written = count;
    }
    void finish(Section &s) {
        require(s.written == s.size);
        auto d = prepared->directory_buffer().subspan(s.id * 64, 64);
        std::fill(d.begin(), d.end(), std::byte{});
        put(d.first(8), s.id);
        put(d.subspan(8, 4), static_cast<std::uint32_t>(s.type));
        put(d.subspan(16, 8), s.offset);
        put(d.subspan(24, 8), s.size);
        auto h = s.hash.finish();
        std::copy(h.begin(), h.end(), d.begin() + 32);
        finished++;
    }
    void descriptor(Json &j, const char *n, const Section &s,
                    std::span<const std::uint64_t> shape) {
        j.object(n);
        j.number("section", s.id);
        j.text("dtype", dtype(s.type));
        j.array("shape");
        for (auto x : shape)
            j.number(nullptr, x);
        j.end(false);
        j.end();
    }
    void descriptor(Json &j, const char *n, const Section &s, std::uint64_t count) {
        descriptor(j, n, s, std::span(&count, 1));
    }
    Section bytes(Type type, std::span<const std::byte> b) {
        auto s = reserve(type, b.size());
        auto tile = prepared->copy_buffer().size();
        for (std::size_t k = 0; k < b.size(); k += tile)
            append(s, b.subspan(k, std::min(tile, b.size() - k)));
        finish(s);
        return s;
    }
    Section source(const ByteSource &src) {
        require(src.size <= 4294967296ull, StatusCode::resource_limit);
        require(checked_add(source_bytes,src.size,source_bytes) &&
                source_bytes<=prepared->limits().provenance_bytes_bound,StatusCode::resource_limit);
        auto s = reserve(Type::u8, src.size);
        auto tile = prepared->copy_buffer();
        for (std::uint64_t k = 0; k < src.size;) {
            auto n = std::min<std::uint64_t>(tile.size(), src.size - k);
            fault(IoOperation::source);
            auto status = src.read(k, tile.first(n));
            if (!status)
                throw Failure{status};
            append(s, tile.first(n));
            k += n;
        }
        finish(s);
        return s;
    }
    template <class T, class Get>
    void generated(Json &j, const char *n, Type type, std::uint64_t N, Get get) {
        std::uint64_t size;
        require(checked_multiply(N, width(type), size));
        auto s = reserve(type, size);
        auto tile = prepared->copy_buffer();
        auto w = width(type);
        std::size_t used = 0;
        for (std::uint64_t k = 0; k < N; k++) {
            T v = static_cast<T>(get(k));
            std::uint64_t value;
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
    void strings(Json &j, const char *n, std::size_t N, Get get, bool optional = false,
                 bool present = false) {
        std::uint64_t total = 0;
        for (std::size_t k = 0; k < N; k++) {
            std::string_view s = get(k);
            require(valid_utf8(s));
            require(checked_add(total, s.size(), total), StatusCode::resource_limit);
        }
        require(total <= 268435456, StatusCode::resource_limit);
        auto b = reserve(Type::u8, total);
        for (std::size_t k = 0; k < N; k++) {
            std::string_view s = get(k);
            auto raw = std::as_bytes(std::span(s.data(), s.size()));
            auto tile = prepared->copy_buffer().size();
            for (std::size_t x = 0; x < raw.size(); x += tile)
                append(b, raw.subspan(x, std::min(tile, raw.size() - x)));
        }
        finish(b);
        auto g = group(j, n);
        u64(g, "count", N);
        descriptor(g, "bytes", b, total);
        std::uint64_t off = 0;
        generated<std::uint64_t>(g, "offsets", Type::u64, N + 1, [&](auto k) {
            if (k)
                off += std::string_view(get(k - 1)).size();
            return off;
        });
        if (optional)
            generated<std::uint8_t>(g, "present", Type::u8, N,
                                    [&](auto) { return present ? 1 : 0; });
        g.finish();
    }
    void document(Json &j, const char *n, const DocumentView &v) {
        require(static_cast<unsigned>(v.state) <= 3);
        auto g = group(j, n);
        u8(g, "state", code(v.state));
        if (included(v.state)) {
            require(v.media_type == "application/json");
            text(g, "media_type", v.media_type);
            auto s = source(v.bytes);
            descriptor(g, "bytes", s, v.bytes.size);
        }
        g.finish();
    }
    void validate_branch(SectionState branch_state, std::span<const DocumentView> docs) {
        require(static_cast<unsigned>(branch_state) <= 3);
        if (!included(branch_state))
            return;
        bool available = false, missing = false;
        for (auto &d : docs) {
            available |= included(d.state);
            missing |= d.state == SectionState::partial || d.state == SectionState::unavailable;
        }
        require(available &&
                branch_state == (missing ? SectionState::partial : SectionState::included));
    }
    void logs(Json &j, const LogsView &v, const char* name="logs") {
        require(v.bytes.size <= 4294967296ull, StatusCode::resource_limit);
        require(v.dropped_count_present || v.dropped_count == 0);
        auto tile = prepared->copy_buffer();
        std::uint64_t records = 0, last = 0, line = 0;
        for (std::uint64_t off = 0; off < v.bytes.size;) {
            auto n = std::min<std::uint64_t>(tile.size(), v.bytes.size - off);
            fault(IoOperation::source);
            auto status = v.bytes.read(off, tile.first(n));
            if (!status)
                throw Failure{status};
            for (std::uint64_t k = 0; k < n; k++) {
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
        auto& g=j;
        auto raw = source(v.bytes);
        descriptor(g, "records_jsonl", raw, v.bytes.size);
        std::uint64_t size;
        require(checked_multiply(records + 1, 8, size));
        auto offsets = reserve(Type::u64, size);
        auto zero = le64(0);
        append(offsets, zero);
        for (std::uint64_t off = 0; off < v.bytes.size;) {
            auto n = std::min<std::uint64_t>(tile.size(), v.bytes.size - off);
            fault(IoOperation::source);
            auto status = v.bytes.read(off, tile.first(n));
            if (!status)
                throw Failure{status};
            for (std::uint64_t k = 0; k < n; k++)
                if (tile[k] == std::byte{'\n'}) {
                    auto b = le64(off + k + 1);
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
    void provenance(Json &j, const ProvenanceView &v) {
        std::array<DocumentView, 4> in{v.inputs.source_json, v.inputs.effective_json,
                                       v.inputs.overrides_json, v.inputs.resources_json};
        validate_branch(v.inputs.state, in);
        std::array<DocumentView, 1> ctx{v.context.execution_json};
        validate_branch(v.context.state, ctx);
        require(static_cast<unsigned>(v.logs.state) <= 3);
        if (included(v.inputs.state)) {
            auto g = group(j, "inputs");
            document(g, "source_json", in[0]);
            document(g, "effective_json", in[1]);
            document(g, "overrides_json", in[2]);
            document(g, "resources_json", in[3]);
            g.finish();
        }
        if (included(v.context.state)) {
            auto g = group(j, "context");
            document(g, "execution_json", ctx[0]);
            g.finish();
        }
        if (included(v.logs.state))
            logs(j, v.logs);
    }
    template<class T,class Get>
    void diagnostic_column(Json& j,const char* name,Type type,const DiagnosticTraceView& v,Get get) {
        generated<T>(j,name,type,v.records.count,[&](auto k) {
            DiagnosticRecord row;
            fault(IoOperation::source);
            auto status=v.records.read(k,row);if(!status)throw Failure{status};
            return get(row);
        });
    }
    void criteria(Json& j,const StaticCriteria& c) {
        auto g=group(j,"criteria");text(g,"norm","block_max_abs_component");
        f64(g,"evaluation_time_s",c.evaluation_time_s);f64(g,"force_absolute_N",c.force_absolute_N);
        f64(g,"moment_absolute_Nm",c.moment_absolute_Nm);f64(g,"relative_tolerance",c.relative_tolerance);
        f64(g,"force_scale_N",c.force_scale_N);f64(g,"moment_scale_Nm",c.moment_scale_Nm);
        f64(g,"force_limit_N",c.force_limit_N);f64(g,"moment_limit_Nm",c.moment_limit_Nm);
        f64(g,"constraint_translation_limit_m",c.constraint_translation_limit_m);
        f64(g,"constraint_rotation_limit_rad",c.constraint_rotation_limit_rad);
        f64(g,"increment_translation_limit_m",c.increment_translation_limit_m);
        f64(g,"increment_rotation_limit_rad",c.increment_rotation_limit_rad);g.finish();
    }
    void diagnostic_columns(Json& j,const DiagnosticTraceDescription& d,const DiagnosticTraceView& v) {
        auto g=group(j,"columns");
        auto u=[&](const char* n,auto member){diagnostic_column<std::uint64_t>(g,n,Type::u64,v,[=](const auto&r){return r.*member;});};
        auto b=[&](const char* n,auto member){diagnostic_column<std::uint8_t>(g,n,Type::u8,v,[=](const auto&r){return r.*member;});};
        auto real=[&](const char* n,auto member){diagnostic_column<double>(g,n,Type::f64,v,[=](const auto&r){return r.*member;});};
        auto k=static_cast<unsigned>(d.kind);
        u("record_index",&DiagnosticRecord::record_index);
        if(k==0) {
            u("accepted_step",&DiagnosticRecord::accepted_step);real("time_s",&DiagnosticRecord::time_s);
            u("nonlinear_iterations",&DiagnosticRecord::nonlinear_iterations);
        } else if(k<=3) {
            u("attempt_id",&DiagnosticRecord::attempt_id);real("factor",&DiagnosticRecord::factor);
            if(k==2)u("accepted_level_id",&DiagnosticRecord::accepted_level_id);
            else {
                b("certified",&DiagnosticRecord::certified);b("rolled_back",&DiagnosticRecord::rolled_back);
                strings(g,"status_names",d.status_names.size(),[&](auto i)->std::string_view{return d.status_names[i];});
                diagnostic_column<std::uint32_t>(g,"status_index",Type::u32,v,[](const auto&r){return r.status_index;});
                if(k==1) {
                    b("accepted_level_present",&DiagnosticRecord::accepted_level_present);
                    u("accepted_level_id",&DiagnosticRecord::accepted_level_id);u("newton_record_count",&DiagnosticRecord::newton_record_count);
                } else {u("iteration_id",&DiagnosticRecord::iteration_id);b("row_kind",&DiagnosticRecord::row_kind);}
            }
            b("metric_valid",&DiagnosticRecord::metric_valid);
            for(unsigned m=0;m<6;++m)
                diagnostic_column<double>(g,diagnostic_measure_name(static_cast<DiagnosticMeasure>(m+2)),Type::f64,v,[=](const auto&r){return r.metrics[m];});
        } else {
            u("sequence",&DiagnosticRecord::sequence);
            strings(g,"event_names",d.event_names.size(),[&](auto i)->std::string_view{return d.event_names[i];});
            diagnostic_column<std::uint32_t>(g,"event_name_index",Type::u32,v,[](const auto&r){return r.event_name_index;});
            b("severity",&DiagnosticRecord::severity);b("identity_valid",&DiagnosticRecord::identity_valid);
            real("time_s",&DiagnosticRecord::time_s);u("accepted_step",&DiagnosticRecord::accepted_step);
            u("accepted_level",&DiagnosticRecord::accepted_level_id);u("attempt_id",&DiagnosticRecord::attempt_id);
        }
        g.finish();
    }
    void diagnostic_traces(const RunResultView& result) {
        const auto& descriptions=prepared->description().run.diagnostics;
        auto status=validate_diagnostics(descriptions,result.diagnostics,result.info.accepted_count);
        if(!status)throw Failure{status.error()};diagnostic_status=*status;
        for(std::size_t i=0;i<descriptions.size();++i) {
            const auto& d=descriptions[i];const auto&v=result.diagnostics[i];
            Json j(prepared->serialization_buffer());j.object();
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
                    auto s=group(j,"summaries");text(s,"population","retained_valid_records");auto fields=group(s,"fields");
                    for(const auto& summary:v.summaries) {
                        auto field=group(fields,diagnostic_measure_name(summary.measure));u64(field,"count",summary.count);
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
    void indexed_sources(const IndexedProvenanceView& v) {
        require(v.logs.size()<=prepared->description().maximum_log_sources && v.logs.size()<=64);
        require(v.documents[0].state!=SectionState::partial);
        source_count=5+v.logs.size();
        auto aggregate=[](auto count,auto get) {
            bool all_disabled=true,all_included=true,retained=false,unavailable=false;
            for(std::size_t i=0;i<count;++i) {
                auto state=get(i);require(static_cast<unsigned>(state)<=3);
                all_disabled&=state==SectionState::disabled;all_included&=state==SectionState::included;
                retained|=included(state);unavailable|=state==SectionState::unavailable;
            }
            return !count||all_disabled?SectionState::disabled:all_included?SectionState::included:!retained&&unavailable?SectionState::unavailable:SectionState::partial;
        };
        indexed_states[0]=aggregate(4,[&](auto i){return v.documents[i].state;});
        indexed_states[1]=aggregate(v.logs.size(),[&](auto i){return v.logs[i].state;});
        indexed_states[2]=v.documents[4].state;
        for(std::uint64_t i=0;i<source_count;++i) {
            Json j(prepared->serialization_buffer());j.object();u64(j,"source_schema",1);u64(j,"source_index",i);u8(j,"kind",i<5?0:1);
            if(i<5) {
                document(j,"document",v.documents[i]);
                if(i==0&&v.documents[0].state==SectionState::included) {
                    const auto hash=prepared->directory_buffer().subspan((sections-1)*64+32,32);
                    require(std::equal(hash.begin(),hash.end(),v.authored_input_sha256.begin()),StatusCode::verification_failure);
                }
            } else {
                const auto& log=v.logs[i-5];u8(j,"state",code(log.state));
                if(included(log.state))logs(j,log,nullptr);
            }
            j.end();source_ids[i]=bytes(Type::json,j.bytes()).id;
        }
    }
    void info(Json &run, const RunResultView &v) {
        auto g = group(run, "info");
        const auto &i = v.info;
        const auto &r = prepared->description().run;
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
    void profiles(Json &v, std::uint64_t outer) {
        const auto& run=prepared->description().run;
        const auto M=run.profiles.size();
        generated<std::uint32_t>(v, "row_profile_index", Type::u32, outer, [&](auto row) {
            return run.kind==RunKind::timeseries&&!run.row_profile_index.empty()?run.row_profile_index[row]:0;
        });
        auto p = group(v, "profiles");
        generated<std::uint32_t>(p, "profile_index", Type::u32, M, [](auto k) { return k; });
        generated<std::uint8_t>(p, "mode", Type::u8, M, [&](auto k) { return static_cast<unsigned>(run.profiles[k].mode()); });
        generated<std::uint32_t>(p, "codec_version", Type::u32, M, [](auto) { return 5; });
        generated<std::uint32_t>(p, "stream_word_bits", Type::u32, M, [](auto) { return 64; });
        generated<std::int8_t>(p, "rounding", Type::i8, M, [](auto) { return 0; });
        generated<std::uint64_t>(p, "mode_code", Type::u64, M, [&](auto k) { return run.profiles[k].mode_code(); });
        generated<std::uint8_t>(p, "tolerance_present", Type::u8, M, [&](auto k) { return run.profiles[k].tolerance_present(); });
        generated<double>(p, "requested_tolerance", Type::f64, M, [&](auto k) { return run.profiles[k].requested_tolerance(); });
        generated<double>(p, "effective_tolerance", Type::f64, M, [&](auto k) { return run.profiles[k].effective_tolerance(); });
        for (auto pair : std::array<std::pair<const char *, std::string_view>, 5>{
                 {{"codec", "zfp"},
                  {"library_version", "1.0.1"},
                  {"library_revision", "f40868a6a1c190c802e7d8b5987064f044bf7812"},
                  {"byte_order", "little"},
                  {"execution", "serial"}}})
            strings(p, pair.first, M, [&](auto) { return pair.second; });
        p.finish();
    }

    void catalogs(Json &run) {
        const auto &r = prepared->description().run;
        auto C = r.channels.size(), R = r.requests.size();
        auto c = group(run, "channels");
        generated<std::uint64_t>(c, "channel_index", Type::u64, C, [](auto k) { return k; });
        generated<std::uint64_t>(c, "request_index", Type::u64, C,
                                 [&](auto k) { return r.channels[k].request_index; });
        strings(c, "scalar_id", C,
                [&](auto k) -> std::string_view { return r.channels[k].scalar_id; });
        strings(c, "group_id", C,
                [&](auto k) -> std::string_view { return r.channels[k].group_id; });
        strings(c, "request_id", C,
                [&](auto k) -> std::string_view { return r.channels[k].request_id; });
        strings(c, "component_path", C,
                [&](auto k) -> std::string_view { return r.channels[k].component_path; });
        strings(c, "unit", C, [&](auto k) -> std::string_view { return r.channels[k].unit; });
        c.finish();
        auto q = group(run, "requests");
        strings(q, "group_id", R,
                [&](auto k) -> std::string_view { return r.requests[k].group_id; });
        strings(q, "request_id", R,
                [&](auto k) -> std::string_view { return r.requests[k].request_id; });
        strings(q, "schema_version", R,
                [&](auto k) -> std::string_view { return r.requests[k].schema_version; });
        strings(q, "construction_profile", R,
                [&](auto k) -> std::string_view { return r.requests[k].construction_profile; });
        u8(q, "effective_state", code(r.effective_requests));
        if (r.effective_requests == SectionState::included) {
            strings(q, "effective_json", R,
                    [&](auto k) -> std::string_view { return r.requests[k].effective_json; });
            auto hashes = reserve(Type::u8, R * 32);
            for (auto &request : r.requests) {
                std::string_view s = request.effective_json;
                auto h = sha256(std::as_bytes(std::span(s.data(), s.size())));
                append(hashes, h);
            }
            finish(hashes);
            std::array<std::uint64_t, 2> shape{R, 32};
            descriptor(q, "effective_sha256", hashes, shape);
        }
        q.finish();
    }
    void values(Json &run, const RunResultView &source) {
        auto g = group(run, "values");
        auto outer = source.values.outer, inner = source.values.inner,
             B = prepared->limits().frame_values;
        g.json.array("shape");
        g.json.number(nullptr, outer);
        g.json.number(nullptr, inner);
        g.json.end(false);
        text(g, "dtype", "float64");
        g.json.array("axis_names");
        bool ts = prepared->description().run.kind == RunKind::timeseries;
        g.json.text(nullptr, ts ? "channel" : "level");
        g.json.text(nullptr, ts ? "sample" : "channel");
        g.json.end(false);
        u32(g, "frame_schema", 1);
        u32(g, "nominal_frame_values", static_cast<std::uint32_t>(B));
        profiles(g, outer);
        std::uint64_t F;
        require(checked_multiply(outer, (inner + B - 1) / B, F) && F <= (1ull << 32),
                StatusCode::resource_limit);
        std::uint64_t isize;
        require(checked_multiply(F, 77, isize));
        auto index = reserve(Type::frames, isize);
        auto maxerr = reserve(Type::f64, F * 8), rms = reserve(Type::f64, F * 8),
             verified = reserve(Type::u64, F * 8), equal = reserve(Type::u8, F);
        auto payload = reserve(Type::u8, 0, true);
        std::uint64_t frame = 0;
        auto idx = prepared->index_buffer();
        std::size_t idx_used = 0;
        for (std::uint64_t row = 0; row < outer; row++)
            for (std::uint64_t begin = 0; begin < inner; begin += B) {
                auto n = std::min(B, inner - begin);
                auto gather = prepared->gather_buffer().first(n);
                fault(IoOperation::source);
                auto status = source.values.fill(row, begin, gather);
                if (!status)
                    throw Failure{status};
                fault(IoOperation::codec);
                const auto& desc=prepared->description().run;
                const auto profile_index=ts&&!desc.row_profile_index.empty()?desc.row_profile_index[row]:0;
                auto encoded = codec->encode(gather, prepared->decode_buffer(),desc.profiles[profile_index]);
                if (!encoded)
                    throw Failure{encoded.error()};
                FrameRecord record{
                    row, begin,          n, payload.size, encoded->bytes.size(), encoded->encoding,
                    profile_index,   encoded->digest};
                append(payload, encoded->bytes);
                auto canonical = encode_record(record);
                if (idx_used + 77 > idx.size()) {
                    append(index, idx.first(idx_used));
                    idx_used = 0;
                }
                std::copy(canonical.begin(), canonical.end(), idx.begin() + idx_used);
                idx_used += 77;
                auto maximum = le64(std::bit_cast<std::uint64_t>(encoded->max_abs_error));
                auto rms_value=le64(std::bit_cast<std::uint64_t>(encoded->rms_error));
                auto count = le64(encoded->verified_count);
                std::array<std::byte, 1> one{std::byte{static_cast<unsigned char>(encoded->bitwise_equal)}};
                append(maxerr, maximum);
                append(rms, rms_value);
                append(verified, count);
                append(equal, one);
                frame++;
            }
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
        auto v = group(g, "verification");
        descriptor(v, "max_abs_error", maxerr, F);
        descriptor(v, "rms_error", rms, F);
        descriptor(v, "verified_count", verified, F);
        descriptor(v, "bitwise_equal", equal, F);
        v.finish();
        g.finish();
    }
    std::uint64_t run(const RunResultView &v) {
        Json j(prepared->serialization_buffer());
        j.object();
        info(j, v);
        if(!prepared->description().run.diagnostics.empty()) {
            auto d=group(j,"diagnostics");u8(d,"status",static_cast<std::uint8_t>(diagnostic_status));
            d.json.array("traces");
            for(std::size_t k=0;k<prepared->description().run.diagnostics.size();++k) {
                d.json.object();u8(d,"kind",static_cast<std::uint8_t>(prepared->description().run.diagnostics[k].kind));
                u64(d,"metadata_section",trace_ids[k]);d.json.end();
            }
            d.json.end(false);d.finish();
        }
        if(prepared->description().indexed_provenance) {
            auto p=group(j,"provenance");auto refs=group(p,"references");
            generated<std::uint8_t>(refs,"role",Type::u8,source_count,[](auto k){return k<5?k:5;});
            generated<std::uint64_t>(refs,"source_index",Type::u64,source_count,[](auto k){return k;});
            generated<std::uint64_t>(refs,"order",Type::u64,source_count,[](auto k){return k<5?0:k-5;});
            refs.finish();p.finish();
        }
        if (v.info.catalog_state == SectionState::included) {
            catalogs(j);
            bool ts = prepared->description().run.kind == RunKind::timeseries;
            generated<double>(j, ts ? "time" : "load_levels", Type::f64, v.axis.size(),
                              [&](auto k) { return v.axis[k]; });
            if (v.identity_axis_present)
                generated<std::uint64_t>(j, ts ? "step_index" : "level_index", Type::u64,
                                         v.accepted_indices.size(),
                                         [&](auto k) { return v.accepted_indices[k]; });
            values(j, v);
        }
        j.end();
        auto s = bytes(Type::json, j.bytes());
        return s.id;
    }
    void root(std::uint64_t run_id, const ProvenanceView &v,const IndexedProvenanceView* indexed=nullptr) {
        Json j(prepared->serialization_buffer());
        j.object();
        const auto &d = prepared->description();
        auto f = group(j, "format");
        text(f, "name", "serene.outputs");
        u32(f, "version_major", 1);
        u32(f, "version_minor", extended()?1:0);
        text(f, "file_id", d.file_id);
        text(f, "created_utc", d.created_utc);
        text(f, "writer_name", d.writer_name);
        text(f, "writer_version", d.writer_version);
        text(f, "write_state", "complete");
        f.json.array("required_features");
        for (auto token : {"serene-container-le-1", "json-descriptors-1", "utf8-pools-1",
                           "frames-77-le-1", "zfp-5-le64-serial-1"})
            f.json.text(nullptr, token);
        if(!d.run.diagnostics.empty())f.json.text(nullptr,"run-diagnostics-1");
        if(d.indexed_provenance)f.json.text(nullptr,"provenance-sources-1");
        if(fixed_accuracy())f.json.text(nullptr,"timeseries-zfp-fixed-accuracy-1");
        f.json.end(false);
        auto states = group(f, "sections");
        u8(states, "inputs", code(indexed?indexed_states[0]:v.inputs.state));
        u8(states, "logs", code(indexed?indexed_states[1]:v.logs.state));
        u8(states, "context", code(indexed?indexed_states[2]:v.context.state));
        states.finish();
        f.finish();
        auto results = group(j, "results");
        auto c = group(results, "index");
        generated<std::uint64_t>(c, "run_index", Type::u64, 1, [](auto) { return 0; });
        strings(c, "run_id", 1, [&](auto) -> std::string_view { return d.run.run_id; });
        strings(
            c, "run_name", 1, [&](auto) -> std::string_view { return d.run.run_name; }, true,
            d.run.run_name_present);
        generated<std::uint8_t>(c, "kind", Type::u8, 1,
                                [&](auto) { return static_cast<unsigned>(d.run.kind); });
        generated<std::uint64_t>(c, "run_metadata_section", Type::u64, 1,
                                 [&](auto) { return run_id; });
        c.finish();
        results.finish();
        if(indexed) {
            auto p=group(j,"provenance");u64(p,"schema",1);u64(p,"original_source_index",0);
            auto digest=bytes(Type::u8,indexed->authored_input_sha256);descriptor(p,"authored_input_sha256",digest,32);
            auto sources=group(p,"sources");
            generated<std::uint64_t>(sources,"source_index",Type::u64,source_count,[](auto k){return k;});
            generated<std::uint8_t>(sources,"kind",Type::u8,source_count,[](auto k){return k<5?0:1;});
            generated<std::uint64_t>(sources,"metadata_section",Type::u64,source_count,[&](auto k){return source_ids[k];});
            sources.finish();p.finish();
        } else provenance(j, v);
        j.end();
        auto s = bytes(Type::json, j.bytes());
        root_id = s.id;
    }
    void complete() {
        require(sections == finished && sections > 0);
        auto directory = prepared->directory_buffer().first(sections * 64);
        std::uint64_t total;
        require(checked_add(next, directory.size(), total));
        write(next, directory);
        std::array<std::byte, 128> header{};
        const char magic[8] = {'S', 'E', 'R', 'E', 'N', 'E', 0, 0};
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
        auto d = sha256(directory);
        std::copy(d.begin(), d.end(), header.begin() + 64);
        auto h = sha256(std::span(header).first(96));
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
    try {
        require(p.valid() && c.max_values() >= p.limits().frame_values &&
                c.workspace_bytes() <= p.plan().codec_bytes);
        require(std::endian::native == std::endian::little, StatusCode::unsupported_feature);
        require(destination.extension() == ".serene");
        require(!std::filesystem::exists(destination), StatusCode::publication_failure);
        auto parent = destination.parent_path();
        if (parent.empty())
            parent = ".";
        require(std::filesystem::is_directory(parent), StatusCode::io_failure);
        ArchiveSession s;
        s.impl_ = std::make_unique<Impl>();
        auto &i = *s.impl_;
        i.prepared = &p;
        i.codec = &c;
        i.hook = hook;
        i.destination = destination;
        i.stats.application_reserved_bytes = p.plan().total_bytes;
        std::fill(p.directory_buffer().begin(), p.directory_buffer().end(), std::byte{});
        static std::atomic<std::uint64_t> seq{};
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 32; attempt++) {
            auto name = destination.filename().native();
            name.insert(name.begin(), static_cast<std::filesystem::path::value_type>('.'));
            name +=
                std::filesystem::path(".tmp-" + std::to_string(stamp) + "-" + std::to_string(seq++))
                    .native();
            i.temp = parent / name;
            int fd = -1;
#ifdef _WIN32
            auto rc = _wsopen_s(&fd, i.temp.c_str(), _O_CREAT | _O_EXCL | _O_BINARY | _O_RDWR,
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
                std::error_code ec;
                std::filesystem::remove(i.temp, ec);
            }
            i.temp.clear();
        }
        require(i.file != nullptr, StatusCode::io_failure);
        std::array<std::byte, 128> zero{};
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
    if (!impl_ || impl_->state != SessionState::opened)
        return error(StatusCode::invalid_state, 32);
    auto &i = *impl_;
    try {
        require(!i.prepared->description().indexed_provenance);
        auto status = i.prepared->validate(v);
        if (!status)
            throw Failure{status};
        i.state = SessionState::writing;
        i.selected_catalog_included=v.info.catalog_state==SectionState::included;
        i.diagnostic_traces(v);
        auto id = i.run(v);
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
    if(!impl_||impl_->state!=SessionState::opened)return error(StatusCode::invalid_state,32);
    auto& i=*impl_;
    try {
        require(i.prepared->description().indexed_provenance);
        auto status=i.prepared->validate(v);if(!status)throw Failure{status};
        i.state=SessionState::writing;
        i.selected_catalog_included=v.info.catalog_state==SectionState::included;
        i.indexed_sources(provenance);i.diagnostic_traces(v);
        auto id=i.run(v);i.root(id,{},&provenance);i.state=SessionState::written;return {};
    } catch(const Failure& f) {i.state=SessionState::failed;return f.status;}
      catch(const std::bad_alloc&) {i.state=SessionState::failed;return error(StatusCode::resource_limit,32);}
      catch(...) {i.state=SessionState::failed;return error(StatusCode::io_failure,32);}
}
Status ArchiveSession::finalize() noexcept {
    if (!impl_ || impl_->state != SessionState::written)
        return error(StatusCode::invalid_state, 33);
    auto &i = *impl_;
    try {
        i.state = SessionState::finalizing;
        i.complete();
        i.fault(IoOperation::flush);
        io(std::fflush(i.file) == 0, 36);
        i.fault(IoOperation::close);
        auto *f = std::exchange(i.file, nullptr);
        io(std::fclose(f) == 0, 37);
        i.fault(IoOperation::publish);
#ifdef _WIN32
        require(MoveFileExW(i.temp.c_str(), i.destination.c_str(), 0) != 0,
                StatusCode::publication_failure);
#else
        std::error_code ec;
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
    return impl_ ? impl_->state : SessionState::failed;
}
SaveStatistics ArchiveSession::statistics() const noexcept {
    return impl_ ? impl_->stats : SaveStatistics{};
}
const std::filesystem::path &ArchiveSession::temporary_path() const noexcept {
    static const std::filesystem::path empty;
    return impl_ ? impl_->temp : empty;
}
} // namespace serene::outputs
