#include <serene/outputs/bundle.hpp>
#include "spool_reader.hpp"
#include "json_contract.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <ostream>
#include <streambuf>
#include <system_error>
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
using Json=nlohmann::json;
/// Internal unwinding carrier; public entry points translate its owned Status to expected/Status.
struct Failure {Status status; ///< Original structured error; never a borrowed diagnostic.
};
/// Require boolean value or throw an internal Failure with code and bundle operation identity.
void require(bool value,StatusCode code=StatusCode::invalid_contract){
    // Translate a failed local invariant into structured unwinding.
if(!value)throw Failure{error(code,80)};}
/// Propagate failed input Status unchanged; successful values have no effect.
void status(Status value){
    // Preserve the original source, parser or fault status during unwinding.
if(!value)throw Failure{value};}
/// Write v little-endian into borrowed output b; its caller supplies the intended integer width.
void put(std::span<std::byte> b,std::uint64_t v){
    // Emit successive low bytes without host alignment or representation assumptions.
for(/* Little-endian output byte position. */ std::size_t i=0;i<b.size();++i)b[i]=std::byte(v>>(8*i));}
/// Return a+b within maximum_extent or throw resource_limit; a and b are byte/count extents.
std::uint64_t add(std::uint64_t a,std::uint64_t b){
    // Use the shared checked extent addition before returning the result.
/* Checked arithmetic result, assigned only when admitted. */ std::uint64_t out;require(checked_add(a,b,out),StatusCode::resource_limit);return out;}
/// Return a*b within maximum_extent or throw resource_limit; operands are nonnegative extents.
std::uint64_t mul(std::uint64_t a,std::uint64_t b){
    // Use the shared checked extent multiplication before returning the result.
/* Checked arithmetic result, assigned only when admitted. */ std::uint64_t out;require(checked_multiply(a,b,out),StatusCode::resource_limit);return out;}
/// Return whether state s describes retained bytes, including partial evidence.
bool included(SectionState s){
    // Classify included/partial without treating included-empty as unavailable.
return s==SectionState::included||s==SectionState::partial;}
// Only schema-owned descriptors are visited. An ignored comment cannot authorize a section.
/// Visit one borrowed schema-owned descriptor j with mutable callback fn; retain neither.
template<class Fn>void array(Json& j,Fn& fn){
    // Apply the caller marking/remapping action to this descriptor only.
fn(j);}
/// Visit known byte/offset/optional-presence descriptors in borrowed UTF-8 pool j through fn.
template<class Fn>void pool(Json& j,Fn& fn){
    // Visit mandatory pool storage, then presence storage only when encoded.
array(j.at("bytes"),fn);array(j.at("offsets"),fn);if(j.contains("present"))array(j.at("present"),fn);}
/// Visit retained-byte descriptor of document j through fn; inactive documents have none.
template<class Fn>void document(Json& j,Fn& fn){
    // Check descriptor presence before exposing its section to the action.
if(j.contains("bytes"))array(j.at("bytes"),fn);}
/// Visit only kind-specific byte descriptors of normalized source j through mutable fn.
template<class Fn>void source_fields(Json& j,Fn& fn){
    // Dispatch document versus JSONL record/offset/info ownership without traversing unknown JSON.

    if(j.at("kind").get<unsigned>()==0)document(j.at("document"),fn);
    else if(j.contains("records_jsonl")){array(j.at("records_jsonl"),fn);array(j.at("record_offsets"),fn);document(j.at("info_json"),fn);}
}
/// Visit admitted typed columns/dictionaries of trace j through mutable fn; no bytes are read here.
template<class Fn>void trace_fields(Json& j,Fn& fn){
    // Skip unavailable columns, dispatch by trace kind, then visit the exact owned numeric
    // and dictionary descriptors needed by marking or remapping.

    if(!j.contains("columns"))return;/* Borrowed typed columns of the admitted trace. */ auto& c=j.at("columns");
    /* Trace kind controls exact descriptor ownership. */ const unsigned k=j.at("kind").get<unsigned>();array(c.at("record_index"),fn);
    if(k==0){for(/* Required transient trace column name. */ auto n:{"accepted_step","time_s","nonlinear_iterations"})array(c.at(n),fn);}
    else if(k<=3){
        for(/* Required shared static trace column name. */ auto n:{"attempt_id","factor","metric_valid","force_residual_N","moment_residual_Nm","constraint_translation_m","constraint_rotation_rad","increment_translation_m","increment_rotation_rad"})array(c.at(n),fn);
        if(k==2)array(c.at("accepted_level_id"),fn);
        else {for(/* Attempt/Newton status column name. */ auto n:{"certified","rolled_back","status_index"})array(c.at(n),fn);pool(c.at("status_names"),fn);
            if(k==1){for(/* Attempt-only accepted-level/count column name. */ auto n:{"accepted_level_present","accepted_level_id","newton_record_count"})array(c.at(n),fn);}
            else {array(c.at("iteration_id"),fn);array(c.at("row_kind"),fn);}
        }
    }else{
        for(/* Required event identity/severity column name. */ auto n:{"sequence","event_name_index","severity","identity_valid","time_s","accepted_step","accepted_level","attempt_id"})array(c.at(n),fn);
        pool(c.at("event_names"),fn);
    }
}
/// Visit known info/catalog/axis/value descriptors in normalized run j through mutable fn.
template<class Fn>void run_fields(Json& j,Fn& fn){
    // Visit info documents even for unavailable runs; for retained results visit catalogs,
    // optional axes, resolved profiles, frame bytes and verification statistics in wire ownership order.

    for(/* Known run-info document role. */ auto n:{"solver_configuration","sampling_configuration","execution"})document(j.at("info").at(n),fn);
    if(!j.contains("values"))return;
    /* Borrowed retained scalar channel catalog. */ auto& c=j.at("channels");for(/* Known channel identity-index column. */ auto n:{"channel_index","request_index"})array(c.at(n),fn);
    for(/* Known channel UTF-8 pool name. */ auto n:{"scalar_id","group_id","request_id","component_path","unit"})pool(c.at(n),fn);
    /* Borrowed request catalog paired with those channels. */ auto&q=j.at("requests");for(/* Known request UTF-8 pool name. */ auto n:{"group_id","request_id","schema_version","construction_profile"})pool(q.at(n),fn);
    if(q.contains("effective_json")){pool(q.at("effective_json"),fn);array(q.at("effective_sha256"),fn);}
    for(/* Optional axis/identity descriptor name. */ auto n:{"time","load_levels","step_index","level_index"})if(j.contains(n))array(j.at(n),fn);
    /* Borrowed value layout, encoded frames and profile selection. */ auto&v=j.at("values");for(/* Encoded-value ownership descriptor name. */ auto n:{"row_profile_index","frame_index","payload"})array(v.at(n),fn);
    /* Borrowed resolved codec profile catalog. */ auto&p=v.at("profiles");for(/* Numeric resolved-profile column name. */ auto n:{"profile_index","mode","codec_version","stream_word_bits","rounding","mode_code","tolerance_present","requested_tolerance","effective_tolerance"})array(p.at(n),fn);
    for(/* Profile identity UTF-8 pool name. */ auto n:{"codec","library_version","library_revision","byte_order","execution"})pool(p.at(n),fn);
    for(/* Stored verification column name. */ auto n:{"max_abs_error","rms_error","verified_count","bitwise_equal"})array(v.at("verification").at(n),fn);
}
// A conservative serialization count before dump() allocation; all text remains UTF-8.
/// Return a checked conservative JSON-escaped byte count for borrowed UTF-8 string s.
std::uint64_t quoted_bound(std::string_view s){
    // Include quotes and charge control escapes at six bytes, quote/backslash at two, other bytes at one.
/* Serialized delimiter/escape byte accumulator. */ std::uint64_t n=2;for(/* Unsigned UTF-8 byte charged for JSON escaping. */ unsigned char c:s)n=add(n,c<32?6:c=='"'||c=='\\'?2:1);return n;}
/// Bound compact serialization bytes of decoded j at structured depth; throw beyond depth16/extents.
std::uint64_t json_bound(const Json& j,unsigned depth=1){
    // Bound strings and primitives, then recursively charge aggregate delimiters, keys and children.
    // This admission precedes serialization into the fixed buffer.

    require(depth<=16,StatusCode::resource_limit);
    if(j.is_string())return quoted_bound(j.get_ref<const std::string&>());
    if(j.is_primitive())return 32;
    /* Serialized delimiter/escape byte accumulator. */ std::uint64_t n=2;
    for(/* Decoded JSON child/member iterator for conservative serialization charge. */ auto it=j.begin();it!=j.end();++it){n=add(n,1);if(j.is_object())n=add(n,add(quoted_bound(it.key()),1));n=add(n,json_bound(it.value(),depth+(it.value().is_structured()?1:0)));}
    return n;
}
/// Copy public limits l to the private validator policy without changing any ceiling.
detail::SpoolReadLimits read_limits(const SpoolLimits& l){
    // Project the five parser/copy/workspace limits in their declared order.
return {l.metadata_budget_bytes,l.workspace_budget_bytes,l.copy_tile_bytes,l.document_bytes,l.document_parser_budget_bytes};}
/// Copy immutable public manifest facts b into the validator binding; owned strings are copied.
detail::SpoolBinding binding(const SpoolBinding& b){
    // Preserve exact file/authored digests, extent, identities and explicit trust assertion.
return {b.expected_file_sha256,b.authored_input_sha256,b.expected_file_bytes,b.expected_run_id,b.producer_identity,b.dependency_identity,b.trusted_manifest_binding};}
}
/// Unique verified file/graph owner used by VerifiedSpool and one BundleSession append.
/// Public summary owns its strings; private data keeps the open read-only source alive.
struct VerifiedSpool::Impl {
    detail::VerifiedSpoolData data; ///< Owned file, admitted metadata graph and bounded verification resources.
    SpoolSummary summary; ///< Owned public facts/accounting copied from the validator.
    bool consumed{}; ///< Latches on append attempt, even if that attempt fails.
};
// Default construction leaves the unique owner empty; default special members transfer/release it.
VerifiedSpool::VerifiedSpool() noexcept=default;
VerifiedSpool::~VerifiedSpool()=default;
VerifiedSpool::VerifiedSpool(VerifiedSpool&&) noexcept=default;
VerifiedSpool& VerifiedSpool::operator=(VerifiedSpool&&) noexcept=default;
std::expected<VerifiedSpool,Status> VerifiedSpool::open(const std::filesystem::path& p,const SpoolBinding& b,SpoolLimits l){
    // Admit trusted manifest facts and reserve their copies before invoking the shared validator.
    // Own the admitted file/graph, expose a copied summary, and account wrapper capacity within limits.

    try{
        require(b.trusted_manifest_binding&&b.expected_file_bytes>0&&!b.expected_run_id.empty()&&!b.producer_identity.empty()&&!b.dependency_identity.empty(),StatusCode::unsupported_feature);
        /* Manifest string bytes to reserve before private copies. */ std::uint64_t binding_bytes=0;
        for(/* Borrowed trusted-manifest identity string. */ const auto* s:{&b.expected_run_id,&b.producer_identity,&b.dependency_identity}){require(s->size()<=1048576,StatusCode::resource_limit);binding_bytes=add(binding_bytes,s->size()+1);}
        // Admission precedes the public-to-private binding copy. Account for that temporary
        // alongside the reader's retained copy and the public summary wrapper.
        /* Public/private binding and wrapper reservation. */ auto reserved=add(mul(binding_bytes,2),sizeof(Impl));
        require(reserved<l.workspace_budget_bytes&&reserved<l.metadata_budget_bytes,StatusCode::resource_limit);
        /* Reduced validator limits after reserving public wrapper overhead. */ auto inner=l;inner.workspace_budget_bytes-=reserved;inner.metadata_budget_bytes-=reserved;
        /* Admitted open-file owner returned by the shared validator. */ auto data=detail::VerifiedSpoolData::open(p,binding(b),read_limits(inner));if(!data)return std::unexpected(data.error());
        /* New unique public owner assembled only after validator success. */ VerifiedSpool s;s.impl_=std::make_unique<Impl>();s.impl_->data=std::move(*data);/* Borrowed private summary while copying public facts. */ const auto&v=s.impl_->data.summary;
        s.impl_->summary={v.run_id,v.run_name,v.run_name_present,v.kind,v.diagnostics,v.indexed_provenance,v.fixed_accuracy,v.file_bytes,v.section_count,v.maximum_frame_values,v.retained_metadata_bytes,v.workspace_bytes,v.authored_input_sha256,v.original_state};
        /* Retained wrapper and string-capacity charge. */ auto extra=add(sizeof(Impl),add(s.impl_->summary.run_id.capacity()+1,s.impl_->summary.run_name.capacity()+1));
        s.impl_->summary.workspace_bytes=add(v.workspace_bytes,add(extra,reserved));
        require(s.impl_->summary.workspace_bytes<=l.workspace_budget_bytes,StatusCode::resource_limit);return s;
    }catch(const Failure& f){return std::unexpected(f.status);}catch(const std::bad_alloc&){return std::unexpected(error(StatusCode::resource_limit,80));}catch(...){return std::unexpected(error(StatusCode::invalid_contract,80));}
}
const SpoolSummary& VerifiedSpool::summary()const noexcept {
    // Borrow retained summary or a process-lifetime empty fallback without allocating.
/* Immutable no-owner fallback with process lifetime. */ static const SpoolSummary empty;return impl_?impl_->summary:empty;}
bool VerifiedSpool::valid()const noexcept{
    // Require both owned validator state and an unconsumed append handle.
return impl_&&!impl_->consumed;}
/// Unique preallocated merger plan/workspace, owned by PreparedBundle and borrowed by a session.
/// Atomic active prevents overlapping session borrows, not concurrent mutation through a session.
struct PreparedBundle::Impl {
    /// One emitted normalized source-catalog entry, consumed by final root serialization.
    struct Source {
        std::uint64_t metadata_section{}; ///< Output JSON section ID after source remapping.
        std::uint8_t kind{}; ///< Document0 or structured-log1 source layout.
    };
    BundleDescription description; ///< Owned ordered run plans plus borrowed immutable original bytes.
    BundleLimits limits; ///< Copied admission policy used during preparation/import.
    BundlePlan plan; ///< Checked directory, graph, workspace and final-file reservations.
    std::atomic_bool active{false}; ///< One live session holds the workspace until destruction.
    std::vector<std::byte> directory, ///< Owned canonical64-byte directory slots, sized to capacity.
        copy, ///< Reusable bounded source/write tile, never a whole-spool materialization.
        serialization; ///< Fixed1MiB JSON serialization scratch.
    std::vector<std::uint64_t> remap, ///< Per-import old section ID -> new ID, UINT64_MAX when unretained.
        run_sections; ///< Final run metadata IDs indexed by authoritative catalog order.
    std::vector<std::uint8_t> keep, ///< One byte per current input section selected by known ownership.
        seen; ///< One byte per planned run, set only after successful metadata copy.
    std::vector<Source> sources; ///< Emitted source catalog, reserved once and appended during copy.
};
// Default construction leaves the unique owner empty; default special members transfer/release it.
PreparedBundle::PreparedBundle() noexcept=default;PreparedBundle::~PreparedBundle()=default;
PreparedBundle::PreparedBundle(PreparedBundle&&) noexcept=default;PreparedBundle& PreparedBundle::operator=(PreparedBundle&&) noexcept=default;
std::expected<PreparedBundle,Status> PreparedBundle::prepare(BundleDescription d,BundleLimits l){
    // Admit identities/run reservations and common-original policy; sum conservative section,
    // source, metadata and final-file bounds with checked arithmetic. Allocate all reusable
    // buffers only after those bounds fit the caller limits, then transfer the description.

    try{
        require(!d.runs.empty()&&d.runs.size()<=1024&&l.copy_tile_bytes>=4096&&l.copy_tile_bytes<=1048576&&l.workspace_budget_bytes&&l.metadata_budget_bytes);
        require(d.original_input.state!=SectionState::partial&&static_cast<unsigned>(d.original_input.state)<=2);
        require(d.original_input.bytes.size<=4294967296ull,StatusCode::resource_limit);
        // Reuse identity validation without retaining the temporary export resources.
        /* Temporary identity-only description for shared producer identity admission. */ ArchiveDescription identity;identity.file_id=d.file_id;identity.created_utc=d.created_utc;identity.writer_name=d.writer_name;identity.writer_version=d.writer_version;identity.run.run_id="bundle";
        /* Temporary validation result released before merger allocation. */ auto identity_check=PreparedExport::prepare(std::move(identity));require(bool(identity_check));identity_check={};
        /* Checked conservative reservation under construction. */ BundlePlan p;p.section_capacity=14;p.source_capacity=1;p.maximum_archive_bytes=add(1048576,d.original_input.bytes.size);
        /* Largest donor directory and owned metadata charge, respectively. */ std::uint64_t max_sections=0,metadata=sizeof(Impl);
        /* Closure accumulating owned UTF-8 capacity into metadata. */ auto text_charge=[&](const std::string&s){/* Validate borrowed text and charge retained capacity, including terminator. */require(valid_utf8(s));metadata=add(metadata,s.capacity()+1);};
        for(/* Borrowed bundle identity string whose owned capacity is charged. */ auto*s:{&d.file_id,&d.created_utc,&d.writer_name,&d.writer_version})text_charge(*s);
        metadata=add(metadata,mul(d.runs.capacity(),sizeof(BundleRunPlan)));
        for(/* Authoritative run reservation ordinal. */ std::size_t i=0;i<d.runs.size();++i){/* Current authoritative ordered run reservation. */ const auto&r=d.runs[i];
            require(!r.run_id.empty()&&(!r.run_name.empty()?r.run_name_present:true)&&static_cast<unsigned>(r.kind)<=1);
            require(r.section_capacity>0&&r.section_capacity<=1048576&&r.maximum_archive_bytes>=128&&r.source_capacity>=5&&r.source_capacity<=69);
            for(/* Earlier run ordinal checked for duplicate identity. */ std::size_t j=0;j<i;++j)require(d.runs[j].run_id!=r.run_id);
            text_charge(r.run_id);text_charge(r.run_name);max_sections=std::max(max_sections,r.section_capacity);
            p.section_capacity=add(p.section_capacity,add(r.section_capacity,14));p.source_capacity=add(p.source_capacity,r.source_capacity);
            p.maximum_archive_bytes=add(p.maximum_archive_bytes,add(r.maximum_archive_bytes,1024));
        }
        // Derive separate directory/remap/catalog/import charges before any buffer allocation.
        require(p.section_capacity<=1048576&&p.source_capacity<=1000000,StatusCode::resource_limit);
        p.directory_bytes=mul(p.section_capacity,64);p.remap_bytes=mul(max_sections,9);
        p.catalog_bytes=add(65536,add(mul(metadata,3),add(mul(p.source_capacity,sizeof(Impl::Source)),mul(d.runs.size(),9))));
        p.copy_bytes=l.copy_tile_bytes;p.serialization_bytes=1048576;p.spool_workspace_bytes=l.spool.workspace_budget_bytes;
        p.workspace_bytes=add(add(add(p.directory_bytes,p.remap_bytes),add(p.catalog_bytes,p.copy_bytes)),add(p.serialization_bytes,p.spool_workspace_bytes));
        p.maximum_archive_bytes=add(p.maximum_archive_bytes,add(add(p.directory_bytes,p.catalog_bytes),mul(p.source_capacity,128)));
        require(p.catalog_bytes<=l.metadata_budget_bytes&&p.workspace_bytes<=l.workspace_budget_bytes,StatusCode::resource_limit);
        // Commit the owned description and allocate reusable capacities only after admission.
        /* New owner receiving buffers only after all limits pass. */ PreparedBundle out;out.impl_=std::make_unique<Impl>();/* Borrowed new implementation during initialization. */ auto&x=*out.impl_;x.description=std::move(d);x.limits=l;x.plan=p;
        x.directory.resize(p.directory_bytes);x.copy.resize(p.copy_bytes);x.serialization.resize(p.serialization_bytes);x.remap.resize(max_sections);x.keep.resize(max_sections);
        x.run_sections.resize(x.description.runs.size());x.seen.resize(x.description.runs.size());x.sources.reserve(p.source_capacity);return out;
    }catch(const Failure& f){return std::unexpected(f.status);}catch(const std::bad_alloc&){return std::unexpected(error(StatusCode::resource_limit,81));}catch(...){return std::unexpected(error(StatusCode::invalid_contract,81));}
}
const BundlePlan& PreparedBundle::plan()const noexcept{
    // Borrow retained accounting or a process-lifetime empty fallback.
/* Immutable empty accounting fallback. */ static const BundlePlan empty;return impl_?impl_->plan:empty;}
const BundleDescription& PreparedBundle::description()const noexcept{
    // Borrow the retained ordered plan/policy or a process-lifetime empty fallback.
/* Immutable empty description fallback. */ static const BundleDescription empty;return impl_?impl_->description:empty;}
bool PreparedBundle::valid()const noexcept{
    // Report whether preparation produced owned implementation state.
return bool(impl_);}
namespace {
/// Ostream adapter borrowing fixed serialization storage from PreparedBundle.
/// Used only during synchronous metadata serialization; never reallocates or owns the span.
class FixedBuffer final:public std::streambuf {
public:
    /// Borrow writable b as the full put area; b outlives the stream and this adapter.
    explicit FixedBuffer(std::span<std::byte>b){
        // Point stream cursors at the existing byte span without growing storage.
/* Borrowed beginning of the fixed stream put area. */ auto*p=reinterpret_cast<char*>(b.data());setp(p,p+b.size());}
    /// Return the number of bytes already written into the borrowed put area.
    std::size_t size()const{
        // Subtract the initial cursor from the current output cursor.
return static_cast<std::size_t>(pptr()-pbase());}
protected:
    /// Append exactly n borrowed characters s or throw resource_limit; return n on success.
    std::streamsize xsputn(const char*s,std::streamsize n)override{
        // Admit remaining capacity, copy bytes, then advance the put cursor.

        require(n>=0&&n<=epptr()-pptr(),StatusCode::resource_limit);std::memcpy(pptr(),s,static_cast<std::size_t>(n));pbump(static_cast<int>(n));return n;
    }
    /// Append character ch or throw at capacity; EOF is a successful no-op notification.
    int_type overflow(int_type ch)override{
        // Handle EOF without writing; otherwise require one free byte and advance once.

        if(traits_type::eq_int_type(ch,traits_type::eof()))return traits_type::not_eof(ch);
        require(pptr()<epptr(),StatusCode::resource_limit);*pptr()=traits_type::to_char_type(ch);pbump(1);return ch;
    }
};
}
/// Synchronous bundle publication state borrowing one PreparedBundle workspace.
/// Owns file/path lifecycle and cumulative counters; spool metadata is consumed/remapped
/// during append while encoded data stays on disk. Destruction closes/removes only its
/// unpublished scratch and releases the exclusive prepared-workspace borrow.
struct BundleSession::Impl {
    PreparedBundle::Impl* prepared{}; ///< Borrowed stable preparation, required through destruction.
    BundleFeatures features{}; ///< Caller-declared actual feature union, checked again at completion.
    FaultHook hook{}; ///< Copied callbacks with borrowed external context.
    std::FILE* file{}; ///< Owned output stream; null after checked close.
    std::filesystem::path destination, ///< Owned final no-clobber path.
        temp; ///< Exclusively created sibling scratch, empty after successful publication.
    SessionState state{SessionState::opened}; ///< Open/writing/finalizing/published or terminal failure.
    SaveStatistics stats{}; ///< Actual write/seek/frame counters and prepared reservation charge.
    std::uint64_t next{128}, ///< Append frontier after the reserved bootstrap.
        section_count{}, ///< Number of initialized directory entries.
        position{}; ///< Tracked stdio cursor, avoiding redundant seeks.
    std::array<std::uint64_t,7> calls{}; ///< Deterministic fault ordinal per IoOperation.
    std::array<std::array<std::uint64_t,4>,3> states{}; ///< Input/log/context reference counts by retention state.
    std::array<std::uint64_t,4> original_states{}; ///< Donor original-role states for common-original union policy.
    bool any_diagnostics{}, ///< At least one appended donor advertises diagnostic traces.
        any_fixed{}; ///< At least one appended donor resolves fixed accuracy.
    /// Release owned stream/scratch and the prepared borrow; cleanup is best effort and never publishes.
    ~Impl(){
        // Close any remaining stream, remove only the owned temporary path, then clear active.
if(file)std::fclose(file);if(!temp.empty()){/* Nonthrowing filesystem result for publication/owned-scratch cleanup. */ std::error_code ec;std::filesystem::remove(temp,ec);}if(prepared)prepared->active.store(false);}
    /// Invoke optional borrowed hook for operation op and its per-operation ordinal; propagate failure.
    void fault(IoOperation op){
        // Increment deterministic operation count before invoking the callback.
/* Per-operation callback ordinal before increment. */ auto ordinal=calls[static_cast<unsigned>(op)]++;if(hook.function)status(hook.function(hook.context,op,ordinal));}
    /// Move output cursor to absolute off within maximum_extent or throw I/O failure.
    void seek(std::uint64_t off){
        // Skip a redundant seek; otherwise inject/account it, use native64-bit seek, and update cursor.
require(off<=maximum_extent);if(position==off)return;fault(IoOperation::seek);++stats.seek_calls;
#ifdef _WIN32
        require(_fseeki64(file,static_cast<__int64>(off),SEEK_SET)==0,StatusCode::io_failure);
#else
        require(fseeko(file,static_cast<off_t>(off),SEEK_SET)==0,StatusCode::io_failure);
#endif
        position=off;
    }
    /// Write all borrowed bytes b at absolute off under the final-file bound; throw on any short write.
    void write(std::uint64_t off,std::span<const std::byte>b){
        // Admit the extent, position the stream, apply fault/write-cap seams, then account actual bytes.

        if(b.empty())return;require(add(off,b.size())<=prepared->plan.maximum_archive_bytes,StatusCode::resource_limit);
        seek(off);fault(IoOperation::write);/* Actual write-call ordinal for the optional short-write seam. */ auto ordinal=stats.write_calls++;/* Requested write extent, optionally capped by fault injection. */ auto n=b.size();if(hook.write_limit)n=std::min(n,hook.write_limit(hook.context,ordinal,n));
        /* Actual bytes written, including partial-failure accounting. */ auto done=std::fwrite(b.data(),1,n,file);position+=done;stats.written_bytes+=done;require(done==b.size(),StatusCode::io_failure);
    }
    /// One in-progress output section with owned streaming digest state; begin/append/finish use it.
    struct Section{
        std::uint64_t id{}, ///< Consecutive output directory slot.
            offset{}, ///< Absolute payload start after bootstrap.
            size{}, ///< Exact declared payload extent.
            written{}; ///< Bytes appended so far, never beyond size.
        std::uint32_t kind{}; ///< Canonical section element kind.
        Sha256 hash; ///< Streaming digest over precisely the bytes written.
    };
    /// Reserve the next section of kind with exact size; return owned in-progress state or resource failure.
    Section begin(std::uint32_t kind,std::uint64_t size){
        // Require a directory slot, snapshot the append frontier and advance it with checked addition.

        require(section_count<prepared->plan.section_capacity,StatusCode::resource_limit);/* New directory slot and streaming digest state. */ Section s;s.id=section_count++;s.offset=next;s.size=size;s.kind=kind;next=add(next,size);return s;
    }
    /// Append borrowed bytes b to mutable section s, updating its digest and written count.
    void append(Section&s,std::span<const std::byte>b){
        // Enforce the declared remaining extent before writing and hashing identical bytes.
require(b.size()<=s.size-s.written);write(s.offset+s.written,b);s.hash.update(b);s.written+=b.size();}
    /// Finalize complete section s, optionally requiring expected digest, and fill its reserved directory slot.
    void finish(Section&s,const Digest* expected=nullptr){
        // Require exact extent and digest, then encode canonical ID/kind/offset/size/hash fields.

        require(s.written==s.size);/* Final payload digest used for donor verification and directory. */ auto digest=s.hash.finish();if(expected)require(digest==*expected,StatusCode::verification_failure);
        /* Borrowed canonical directory slot for this section. */ auto bytes=std::span(prepared->directory).subspan(s.id*64,64);std::fill(bytes.begin(),bytes.end(),std::byte{});
        put(bytes.first(8),s.id);put(bytes.subspan(8,4),s.kind);put(bytes.subspan(16,8),s.offset);put(bytes.subspan(24,8),s.size);std::copy(digest.begin(),digest.end(),bytes.begin()+32);
    }
    /// Return an owned rank-one descriptor for section s, dtype label and logical element count.
    Json descriptor(const Section&s,const char*dtype,std::uint64_t count){
        // Build only the section ID, element type and shape needed by the caller metadata.
return Json{{"section",s.id},{"dtype",dtype},{"shape",Json::array({count})}};}
    /// Write borrowed bytes b as a u8 section and return its owned descriptor.
    Json bytes(std::span<const std::byte>b){
        // Copy through bounded slices, finish the digest, then describe the complete byte population.
/* Owned in-progress section with its exact declared extent. */ auto s=begin(2,b.size());for(/* Payload byte offset advanced by the bounded copy tile. */ std::size_t off=0;off<b.size();off+=prepared->copy.size())append(s,b.subspan(off,std::min(prepared->copy.size(),b.size()-off)));finish(s);return descriptor(s,"u8",b.size());}
    /// Generate count integer elements via get(index), encode kind/dtype little-endian, and return descriptor.
    template<class Get>Json integers(std::uint32_t kind,const char* dtype,std::uint64_t count,Get get){
        // Select one/eight-byte width, fill the reusable tile and flush it as needed, then finish the section.

        /* Canonical element width selected by section kind. */ auto width=kind==2?1ull:8ull;/* Owned in-progress section with its exact declared extent. */ auto s=begin(kind,mul(count,width));/* Borrowed reusable bounded transfer/encoding tile. */ auto tile=std::span(prepared->copy);/* Bytes currently filled in the reusable integer tile. */ std::size_t used=0;
        for(/* Generated numeric element ordinal. */ std::uint64_t i=0;i<count;++i){if(used+width>tile.size()){append(s,tile.first(used));used=0;}put(tile.subspan(used,width),get(i));used+=static_cast<std::size_t>(width);}
        append(s,tile.first(used));finish(s);return descriptor(s,dtype,count);
    }
    /// Write count borrowed UTF-8 strings from get(index); optional adds run-name presence bits.
    template<class Get>Json strings(std::uint64_t count,Get get,bool optional=false){
        // Count and bound bytes, stream exact strings, emit prefix offsets, then optional presence.
        // No caller string is retained beyond this synchronous call.

        /* Total UTF-8 bytes across the ordered pool. */ std::uint64_t total=0;for(/* Ordered pool string ordinal during byte counting. */ std::uint64_t i=0;i<count;++i)total=add(total,std::string_view(get(i)).size());require(total<=268435456,StatusCode::resource_limit);
        /* Owned in-progress section with its exact declared extent. */ auto s=begin(2,total);for(/* Ordered pool string ordinal during exact copying. */ std::uint64_t i=0;i<count;++i){/* Borrowed current string; copied before advancing. */ std::string_view value=get(i);/* Borrowed byte representation of the current string. */ auto b=std::as_bytes(std::span(value.data(),value.size()));for(/* Byte offset within the current UTF-8 string. */ std::size_t off=0;off<b.size();off+=prepared->copy.size())append(s,b.subspan(off,std::min(prepared->copy.size(),b.size()-off)));}finish(s);
        /* Prefix-offset accumulator and owned pool descriptor. */ std::uint64_t off=0;Json p{{"count",count},{"bytes",descriptor(s,"u8",total)}};
        p["offsets"]=integers(4,"u64",count+1,[&](auto i){/* Project ordered element i into this generated catalog/column; no borrow escapes. */if(i)off+=std::string_view(get(i-1)).size();return off;});
        if(optional)p["present"]=integers(2,"u8",count,[&](auto i){/* Project ordered element i into this generated catalog/column; no borrow escapes. */return prepared->description.runs[i].run_name_present?1:0;});return p;
    }
    /// Serialize owned/borrowed JSON j into fixed scratch, validate its parser budget, and return new section ID.
    std::uint64_t metadata(Json& j){
        // Bound escaping/depth first, stream compact JSON to fixed storage, run shared preflight, then write/hash it.

        require(json_bound(j)<=1048576,StatusCode::resource_limit);
        /* Non-owning stream adapter over prepared serialization storage. */ FixedBuffer buffer(prepared->serialization);/* Temporary serializer stream borrowing the fixed adapter. */ std::ostream output(&buffer);output.exceptions(std::ios::badbit|std::ios::failbit);output<<j;output.flush();
        /* Borrowed exact used serialization prefix. */ auto b=std::span<const std::byte>(prepared->serialization).first(buffer.size());
        status(json_contract::preflight(std::string_view(reinterpret_cast<const char*>(b.data()),b.size()),prepared->limits.metadata_budget_bytes));
        /* Owned in-progress section with its exact declared extent. */ auto s=begin(1,b.size());append(s,b);finish(s);return s.id;
    }
    /// Write common-original source0 according to retained policy and immutable authored digest.
    void original(){
        // For included bytes, read bounded tiles and require exact authored digest; then emit the source descriptor.

        /* Borrowed immutable bundle identity and ordered reservation. */ const auto&d=prepared->description;/* Borrowed original snapshot/state retained by the caller. */ const auto&v=d.original_input;/* Owned normalized document descriptor under construction. */ Json doc{{"state",static_cast<unsigned>(v.state)}};
        if(v.state==SectionState::included){
            require(!v.media_type.empty()&&valid_utf8(v.media_type));/* Owned in-progress section with its exact declared extent. */ auto s=begin(2,v.bytes.size);/* Borrowed reusable bounded transfer/encoding tile. */ auto tile=std::span(prepared->copy);
            for(/* Original-source byte offset, independent of source backing storage. */ std::uint64_t off=0;off<v.bytes.size;){/* Next bounded original-source read size. */ auto n=std::min<std::uint64_t>(tile.size(),v.bytes.size-off);fault(IoOperation::source);status(v.bytes.read(off,tile.first(n)));append(s,tile.first(n));off+=n;}
            finish(s,&d.authored_input_sha256);doc["media_type"]=v.media_type;doc["bytes"]=descriptor(s,"u8",v.bytes.size);
        }
        /* Owned common-original source0 descriptor. */ Json source{{"source_schema",1},{"source_index",0},{"kind",0},{"document",std::move(doc)}};
        prepared->sources.push_back({metadata(source),0});
    }
    /// Consume/remap validated donor in into planned catalog position global_index without recompressing frames.
    void copy(std::uint64_t global_index,detail::VerifiedSpoolData& in){
        // Check donor identity and reservations, select schema-owned sections and copy/hash encoded bytes.
        // Remap trace/source/run descriptors, replace only original-role references, aggregate retention states,
        // then rehash the complete open donor to detect mutation during this copy.

        /* Immutable reservation for the requested catalog position. */ const auto& plan=prepared->description.runs[global_index];/* Verified donor facts that must fit that reservation. */ const auto& summary=in.summary;
        require(summary.run_id==plan.run_id&&summary.kind==plan.kind&&summary.run_name_present==plan.run_name_present&&summary.run_name==plan.run_name);
        require(summary.authored_input_sha256==prepared->description.authored_input_sha256);
        require(summary.section_count<=plan.section_capacity&&summary.file_bytes<=plan.maximum_archive_bytes&&in.sources.size()<=plan.source_capacity,StatusCode::resource_limit);
        require(summary.workspace_bytes<=prepared->plan.spool_workspace_bytes,StatusCode::resource_limit);
        require(!summary.diagnostics||features.diagnostics);require(!summary.fixed_accuracy||features.fixed_accuracy);
        if(in.run.contains("values")) {
            stats.frames=add(stats.frames,in.run.at("values").at("frame_index").at("shape").at(0).get<std::uint64_t>());
            stats.payload_bytes=add(stats.payload_bytes,in.run.at("values").at("payload").at("shape").at(0).get<std::uint64_t>());
        }
        any_diagnostics|=summary.diagnostics;any_fixed|=summary.fixed_accuracy;
        ++original_states[static_cast<unsigned>(summary.original_state)];
        if(summary.original_state==SectionState::included){
            require(prepared->description.original_input.state==SectionState::included);
            require(in.sources[in.original_source_index].metadata.at("document").at("media_type").get_ref<const std::string&>()==prepared->description.original_input.media_type);
        }
        // A source shared with another role must survive original-role replacement unchanged.
        /* Whether another role requires retaining the original donor source separately. */ const bool retain_local_original=std::any_of(in.references.begin(),in.references.end(),[&](const auto&r){/* Test whether a non-original role shares the donor original source. */return r.role!=0&&r.source_index==in.original_source_index;});
        require(in.directory.size()<=prepared->remap.size(),StatusCode::resource_limit);
        /* Validated donor directory population for fixed remap/mark storage. */ auto n=in.directory.size();std::fill_n(prepared->keep.begin(),n,std::uint8_t{});std::fill_n(prepared->remap.begin(),n,UINT64_MAX);
        /* Known-descriptor visitor that marks selected source sections. */ auto mark=[&](Json& descriptor){/* Mark only this known descriptor section for retention. *//* Old validated section identity referenced by this descriptor. */ auto id=descriptor.at("section").get<std::uint64_t>();require(id<n);prepared->keep[id]=1;};
        run_fields(in.run,mark);for(/* Borrowed donor trace whose known columns need retention. */ auto&t:in.traces)trace_fields(t.metadata,mark);
        for(/* Borrowed donor source considered for retained-role ownership. */ auto&s:in.sources)if(s.source_index!=in.original_source_index||retain_local_original)source_fields(s.metadata,mark);
        // Copy only known owned non-JSON sections; verify original per-section digests in flight.
        /* Borrowed reusable bounded transfer/encoding tile. */ auto tile=std::span(prepared->copy);
        for(/* Donor section ID tested against the ownership mark. */ std::uint64_t id=0;id<n;++id)if(prepared->keep[id]){
            /* Borrowed donor directory record and its exact payload digest. */ const auto& old=in.directory[id];require(old.kind!=1);/* Owned in-progress section with its exact declared extent. */ auto s=begin(old.kind,old.size);prepared->remap[id]=s.id;
            for(/* Byte offset within the current encoded donor section. */ std::uint64_t off=0;off<old.size;){/* Next bounded donor-section read size. */ auto count=std::min<std::uint64_t>(tile.size(),old.size-off);fault(IoOperation::source);status(in.read(id,off,tile.first(count)));append(s,tile.first(count));off+=count;}
            finish(s,&old.digest);
        }
        // Rebuild metadata with new section/run/source IDs; never reinterpret payload values.
        /* Known-descriptor visitor replacing only proven old section identities. */ auto remap=[&](Json& descriptor){/* Require a retained old ID, then substitute its new output section ID. *//* Old validated section identity referenced by this descriptor. */ auto id=descriptor.at("section").get<std::uint64_t>();require(id<n&&prepared->remap[id]!=UINT64_MAX);descriptor["section"]=prepared->remap[id];};
        /* Output trace metadata IDs in donor trace order. */ std::array<std::uint64_t,5> trace_ids{};
        for(/* Donor trace ordinal paired with its rewritten output metadata. */ std::size_t i=0;i<in.traces.size();++i){/* Mutable consumed donor trace whose section/run IDs are rewritten. */ auto&t=in.traces[i];trace_fields(t.metadata,remap);t.metadata["run_index"]=global_index;trace_ids[i]=metadata(t.metadata);}
        /* Fixed admitted local source ID -> output source ID map. */ std::array<std::uint64_t,69> source_map{};
        require(in.sources.size()<=source_map.size(),StatusCode::resource_limit);
        for(/* Mutable consumed donor source descriptor. */ auto&s:in.sources){require(s.source_index<source_map.size());
            if(s.source_index==in.original_source_index&&!retain_local_original){source_map[s.source_index]=0;continue;}
            require(prepared->sources.size()<prepared->plan.source_capacity,StatusCode::resource_limit);
            /* Next normalized global source-catalog identity. */ auto new_index=prepared->sources.size();source_map[s.source_index]=new_index;
            source_fields(s.metadata,remap);s.metadata["source_index"]=new_index;prepared->sources.push_back({metadata(s.metadata),s.kind});
        }
        run_fields(in.run,remap);
        if(features.diagnostics){
            /* Owned diagnostic trace-index list for the rewritten run. */ Json entries=Json::array();for(/* Donor trace ordinal used to rebuild the run trace index. */ std::size_t i=0;i<in.traces.size();++i)entries.push_back({{"kind",in.traces[i].kind},{"metadata_section",trace_ids[i]}});
            if(summary.diagnostics)in.run["diagnostics"]["traces"]=std::move(entries);
            else in.run["diagnostics"]={{"status",3},{"traces",std::move(entries)}};
        }
        // Preserve ordered role associations; only role0 points at the common original.
        /* Ordered provenance-reference population, including explicit inactive states. */ const auto K=in.references.size();/* New typed reference-column descriptors. */ Json refs;
        refs["role"]=integers(2,"u8",K,[&](auto i){/* Project ordered element i into this generated catalog/column; no borrow escapes. */return in.references[i].role;});
        refs["source_index"]=integers(4,"u64",K,[&](auto i){/* Project ordered element i into this generated catalog/column; no borrow escapes. */return in.references[i].role==0?0:source_map[in.references[i].source_index];});
        refs["order"]=integers(4,"u64",K,[&](auto i){/* Project ordered element i into this generated catalog/column; no borrow escapes. */return in.references[i].order;});
        in.run["provenance"]={{"references",std::move(refs)}};
        for(/* Preserved ordered provenance association. */ const auto&ref:in.references){
            /* Effective state after replacing original role with the common source. */ SectionState s;
            if(ref.role==0)s=prepared->description.original_input.state;
            else {/* Borrowed donor source for a preserved non-original association. */ const auto&src=in.sources[ref.source_index];s=static_cast<SectionState>((src.kind?src.metadata.at("state"):src.metadata.at("document").at("state")).get<unsigned>());}
            ++states[ref.role<4?0:ref.role==4?2:1][static_cast<unsigned>(s)];
        }
        // Publish this catalog position only within scratch, then verify donor immutability again.
        prepared->run_sections[global_index]=metadata(in.run);prepared->seen[global_index]=1;
        status(in.verify_unchanged());
    }
    /// Return wire aggregate state for counts s ordered included/disabled/unavailable/partial.
    unsigned aggregate(const std::array<std::uint64_t,4>&s){
        // Distinguish unanimous disabled/included from unavailable-only and mixed/partial populations.
/* Total counted references in this aggregate category. */ auto total=s[0]+s[1]+s[2]+s[3];return !total||s[1]==total?1:s[0]==total?0:!s[0]&&!s[3]&&s[2]?2:3;}
    /// Require every planned run, build ordered catalogs, then write directory and final bootstrap.
    void complete(){
        // Check feature/original union, serialize authoritative run/source catalogs and root metadata.
        // Append directory, hash directory/bootstrap, replace bootstrap and seek to the complete file extent.

        require(std::all_of(prepared->seen.begin(),prepared->seen.end(),[](auto x){/* Require successful append for this planned catalog slot. */return x==1;}));
        require(any_diagnostics==features.diagnostics&&any_fixed==features.fixed_accuracy);
        /* Borrowed immutable bundle identity and ordered reservation. */ const auto&d=prepared->description;
        if(!original_states[0]){
            if(d.original_input.state==SectionState::included)require(d.explicitly_retain_original);
            else require(d.original_input.state==(original_states[2]?SectionState::unavailable:SectionState::disabled));
        }
        /* Exact actual feature union emitted by the completed1.1 bundle. */ Json tokens=Json::array({"serene-container-le-1","json-descriptors-1","utf8-pools-1","frames-77-le-1","zfp-5-le64-serial-1"});
        if(features.diagnostics)tokens.push_back("run-diagnostics-1");tokens.push_back("provenance-sources-1");if(features.fixed_accuracy)tokens.push_back("timeseries-zfp-fixed-accuracy-1");
        /* Owned final format identity and aggregate retention-state metadata. */ Json format{{"name","serene.outputs"},{"version_major",1},{"version_minor",1},{"file_id",d.file_id},{"created_utc",d.created_utc},{"writer_name",d.writer_name},{"writer_version",d.writer_version},{"write_state","complete"},{"required_features",std::move(tokens)},
            {"sections",{{"inputs",aggregate(states[0])},{"logs",aggregate(states[1])},{"context",aggregate(states[2])}}}};
        // Serialize run/source catalogs in authored order regardless of append order.
        /* Owned authoritative run-catalog descriptors. */ Json index;/* Planned run count, all proven present before final serialization. */ auto N=d.runs.size();index["run_index"]=integers(4,"u64",N,[](auto i){/* Project ordered element i into this generated catalog/column; no borrow escapes. */return i;});
        index["run_id"]=strings(N,[&](auto i)->std::string_view{/* Project ordered element i into this generated catalog/column; no borrow escapes. */return d.runs[i].run_id;});
        index["run_name"]=strings(N,[&](auto i)->std::string_view{/* Project ordered element i into this generated catalog/column; no borrow escapes. */return d.runs[i].run_name;},true);
        index["kind"]=integers(2,"u8",N,[&](auto i){/* Project ordered element i into this generated catalog/column; no borrow escapes. */return static_cast<unsigned>(d.runs[i].kind);});
        index["run_metadata_section"]=integers(4,"u64",N,[&](auto i){/* Project ordered element i into this generated catalog/column; no borrow escapes. */return prepared->run_sections[i];});
        /* Actual emitted source count within reserved capacity. */ auto S=prepared->sources.size();/* Owned normalized source-catalog descriptors. */ Json sources;
        sources["source_index"]=integers(4,"u64",S,[](auto i){/* Project ordered element i into this generated catalog/column; no borrow escapes. */return i;});sources["kind"]=integers(2,"u8",S,[&](auto i){/* Project ordered element i into this generated catalog/column; no borrow escapes. */return prepared->sources[i].kind;});
        sources["metadata_section"]=integers(4,"u64",S,[&](auto i){/* Project ordered element i into this generated catalog/column; no borrow escapes. */return prepared->sources[i].metadata_section;});
        /* Owned common original identity/digest and indexed source metadata. */ Json provenance{{"schema",1},{"original_source_index",0},{"authored_input_sha256",bytes(d.authored_input_sha256)},{"sources",std::move(sources)}};
        /* Final root metadata linking format, run catalog and provenance. */ Json root{{"format",std::move(format)},{"results",{{"index",std::move(index)}}},{"provenance",std::move(provenance)}};
        // Finish the complete root/directory before replacing the provisional bootstrap.
        /* New authoritative root JSON section ID. */ auto root_id=metadata(root);/* Borrowed initialized directory prefix only. */ auto directory=std::span<const std::byte>(prepared->directory).first(section_count*64);/* Checked complete extent including final directory. */ auto file_bytes=add(next,directory.size());write(next,directory);
        /* Canonical bootstrap, zero-initialized before fields and digests. */ std::array<std::byte,128> header{};std::memcpy(header.data(),"SERENE\0\0",8);put(std::span(header).subspan(8,2),1);put(std::span(header).subspan(10,2),1);put(std::span(header).subspan(12,4),0x01020304);put(std::span(header).subspan(16,4),128);put(std::span(header).subspan(20,4),64);
        put(std::span(header).subspan(24,8),file_bytes);put(std::span(header).subspan(32,8),next);put(std::span(header).subspan(40,8),section_count);put(std::span(header).subspan(48,8),root_id);put(std::span(header).subspan(56,8),1);
        /* SHA256 of precisely the emitted directory bytes. */ auto directory_hash=sha256(directory);std::copy(directory_hash.begin(),directory_hash.end(),header.begin()+64);/* SHA256 of bootstrap prefix before its own digest. */ auto header_hash=sha256(std::span(header).first(96));std::copy(header_hash.begin(),header_hash.end(),header.begin()+96);write(0,header);seek(file_bytes);
    }
};
// Default construction leaves the unique owner empty; default special members transfer/release it.
BundleSession::BundleSession() noexcept=default;BundleSession::~BundleSession()=default;
BundleSession::BundleSession(BundleSession&&) noexcept=default;BundleSession& BundleSession::operator=(BundleSession&&) noexcept=default;
std::expected<BundleSession,Status> BundleSession::open(PreparedBundle& p,const std::filesystem::path& destination,BundleFeatures features,FaultHook hook){
    // Admit destination and acquire the sole prepared borrow; preflight original JSON before
    // spool working sets exist. Reset reusable storage, create exclusive sibling scratch, then
    // write the empty bootstrap and common-original source, translating errors to Status.

    try{
        require(p.valid());require(std::endian::native==std::endian::little,StatusCode::unsupported_feature);
        require(destination.extension()==".serene");require(!std::filesystem::exists(destination),StatusCode::publication_failure);
        /* Destination parent or current directory for sibling scratch. */ auto parent=destination.parent_path();if(parent.empty())parent=".";require(std::filesystem::is_directory(parent),StatusCode::io_failure);
        /* Unique owner ensuring cleanup on any subsequent setup failure. */ BundleSession session;session.impl_=std::make_unique<Impl>();/* Borrowed newly allocated publication state. */ auto&i=*session.impl_;
        /* Expected atomic value for acquiring exclusive prepared storage. */ bool inactive=false;require(p.impl_->active.compare_exchange_strong(inactive,true),StatusCode::invalid_state);i.prepared=p.impl_.get();
        i.destination=destination;i.features=features;i.hook=hook;i.stats.application_reserved_bytes=p.plan().workspace_bytes;
        /* Shared preflight result before any per-spool workspace is resident. */ auto original_validation=detail::validate_original_document(p.description().original_input,p.description().authored_input_sha256,read_limits(p.impl_->limits.spool));
        if(!original_validation)throw Failure{original_validation.error()};
        std::fill(i.prepared->directory.begin(),i.prepared->directory.end(),std::byte{});std::fill(i.prepared->seen.begin(),i.prepared->seen.end(),std::uint8_t{});i.prepared->sources.clear();
        // Exclusive creation establishes ownership; failed collisions must never be removed.
        /* Process-local collision-resistant suffix counter, not a durable identity. */ static std::atomic<std::uint64_t> sequence{};/* Monotonic-clock suffix combined with the process counter. */ auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        for(/* Exclusive temporary-name collision attempt. */ unsigned attempt=0;attempt<32;++attempt){
            /* Native-character sibling scratch basename. */ auto name=destination.filename().native();name.insert(name.begin(),static_cast<std::filesystem::path::value_type>('.'));name+=std::filesystem::path(".bundle-tmp-"+std::to_string(stamp)+"-"+std::to_string(sequence++)).native();i.temp=parent/name;/* Owned native descriptor only after successful exclusive creation. */ int fd=-1;
#ifdef _WIN32
            /* Windows exclusive-open result, distinct from fdopen success. */ auto rc=_wsopen_s(&fd,i.temp.c_str(),_O_CREAT|_O_EXCL|_O_BINARY|_O_RDWR,_SH_DENYRW,_S_IREAD|_S_IWRITE);if(rc==0)i.file=_wfdopen(fd,L"w+b");
#else
            fd=::open(i.temp.c_str(),O_CREAT|O_EXCL|O_RDWR,0600);if(fd>=0)i.file=fdopen(fd,"w+b");
#endif
            if(i.file)break;
            if(fd>=0){
#ifdef _WIN32
                _close(fd);
#else
                ::close(fd);
#endif
                /* Nonthrowing filesystem result for publication/owned-scratch cleanup. */ std::error_code ec;std::filesystem::remove(i.temp,ec);
            }
            i.temp.clear();
        }
        require(i.file!=nullptr,StatusCode::io_failure);/* Provisional all-zero bootstrap; never a complete published archive. */ std::array<std::byte,128> zero{};i.write(0,zero);i.original();return session;
    }catch(const Failure&f){return std::unexpected(f.status);}catch(const std::bad_alloc&){return std::unexpected(error(StatusCode::resource_limit,82));}catch(...){return std::unexpected(error(StatusCode::io_failure,82));}
}
Status BundleSession::append_verified_spool(std::uint64_t index,VerifiedSpool& spool) noexcept{
    // Check live lifecycle and consume this verified handle before attempting import.
    // Require a unique planned position and bounded donor workspace, then copy; any failure poisons this session.

    if(!impl_||(impl_->state!=SessionState::opened&&impl_->state!=SessionState::writing)||!spool.valid())return error(StatusCode::invalid_state,83);
    /* Borrowed live session for stateful append/finalization. */ auto&i=*impl_;spool.impl_->consumed=true;
    try{
        require(index<i.prepared->description.runs.size()&&!i.prepared->seen[index]);
        require(spool.summary().workspace_bytes<=i.prepared->plan.spool_workspace_bytes,StatusCode::resource_limit);
        i.state=SessionState::writing;i.copy(index,spool.impl_->data);return {};
    }catch(const Failure&f){i.state=SessionState::failed;return f.status;}catch(const std::bad_alloc&){i.state=SessionState::failed;return error(StatusCode::resource_limit,83);}catch(...){i.state=SessionState::failed;return error(StatusCode::invalid_contract,83);}
}
Status BundleSession::finalize() noexcept{
    // Complete catalogs/digests, check flush and close, then publish without replacing a destination.
    // On failure retain terminal state and owned scratch for destructor cleanup; success clears its path.

    if(!impl_||(impl_->state!=SessionState::opened&&impl_->state!=SessionState::writing))return error(StatusCode::invalid_state,84);/* Borrowed live session for stateful append/finalization. */ auto&i=*impl_;
    try{
        i.state=SessionState::finalizing;i.complete();i.fault(IoOperation::flush);require(std::fflush(i.file)==0,StatusCode::io_failure);
        // A checked close precedes publication; callbacks cannot claim an unclosed file succeeded.
        i.fault(IoOperation::close);/* Detached stream closed exactly once even when fclose fails. */ auto*file=std::exchange(i.file,nullptr);require(std::fclose(file)==0,StatusCode::io_failure);i.fault(IoOperation::publish);
#ifdef _WIN32
        require(MoveFileExW(i.temp.c_str(),i.destination.c_str(),0)!=0,StatusCode::publication_failure);
#else
        /* Nonthrowing filesystem result for publication/owned-scratch cleanup. */ std::error_code ec;std::filesystem::create_hard_link(i.temp,i.destination,ec);require(!ec,StatusCode::publication_failure);std::filesystem::remove(i.temp,ec);
#endif
        i.temp.clear();i.state=SessionState::published;return {};
    }catch(const Failure&f){i.state=SessionState::failed;return f.status;}catch(...){i.state=SessionState::failed;return error(StatusCode::publication_failure,84);}
}
SessionState BundleSession::state()const noexcept{
    // Return retained lifecycle, treating an empty owner as failed.
return impl_?impl_->state:SessionState::failed;}
SaveStatistics BundleSession::statistics()const noexcept{
    // Copy current counters or return zero counters for an empty owner.
return impl_?impl_->stats:SaveStatistics{};}
} // namespace serene::outputs
