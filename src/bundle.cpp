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
struct Failure {Status status;};
void require(bool value,StatusCode code=StatusCode::invalid_contract){if(!value)throw Failure{error(code,80)};}
void status(Status value){if(!value)throw Failure{value};}
void put(std::span<std::byte> b,std::uint64_t v){for(std::size_t i=0;i<b.size();++i)b[i]=std::byte(v>>(8*i));}
std::uint64_t add(std::uint64_t a,std::uint64_t b){std::uint64_t out;require(checked_add(a,b,out),StatusCode::resource_limit);return out;}
std::uint64_t mul(std::uint64_t a,std::uint64_t b){std::uint64_t out;require(checked_multiply(a,b,out),StatusCode::resource_limit);return out;}
bool included(SectionState s){return s==SectionState::included||s==SectionState::partial;}
// Only schema-owned descriptors are visited. An ignored comment cannot authorize a section.
template<class Fn>void array(Json& j,Fn& fn){fn(j);}
template<class Fn>void pool(Json& j,Fn& fn){array(j.at("bytes"),fn);array(j.at("offsets"),fn);if(j.contains("present"))array(j.at("present"),fn);}
template<class Fn>void document(Json& j,Fn& fn){if(j.contains("bytes"))array(j.at("bytes"),fn);}
template<class Fn>void source_fields(Json& j,Fn& fn){
    if(j.at("kind").get<unsigned>()==0)document(j.at("document"),fn);
    else if(j.contains("records_jsonl")){array(j.at("records_jsonl"),fn);array(j.at("record_offsets"),fn);document(j.at("info_json"),fn);}
}
template<class Fn>void trace_fields(Json& j,Fn& fn){
    if(!j.contains("columns"))return;auto& c=j.at("columns");
    const unsigned k=j.at("kind").get<unsigned>();array(c.at("record_index"),fn);
    if(k==0){for(auto n:{"accepted_step","time_s","nonlinear_iterations"})array(c.at(n),fn);}
    else if(k<=3){
        for(auto n:{"attempt_id","factor","metric_valid","force_residual_N","moment_residual_Nm","constraint_translation_m","constraint_rotation_rad","increment_translation_m","increment_rotation_rad"})array(c.at(n),fn);
        if(k==2)array(c.at("accepted_level_id"),fn);
        else {for(auto n:{"certified","rolled_back","status_index"})array(c.at(n),fn);pool(c.at("status_names"),fn);
            if(k==1){for(auto n:{"accepted_level_present","accepted_level_id","newton_record_count"})array(c.at(n),fn);}
            else {array(c.at("iteration_id"),fn);array(c.at("row_kind"),fn);}
        }
    }else{
        for(auto n:{"sequence","event_name_index","severity","identity_valid","time_s","accepted_step","accepted_level","attempt_id"})array(c.at(n),fn);
        pool(c.at("event_names"),fn);
    }
}
template<class Fn>void run_fields(Json& j,Fn& fn){
    for(auto n:{"solver_configuration","sampling_configuration","execution"})document(j.at("info").at(n),fn);
    if(!j.contains("values"))return;
    auto& c=j.at("channels");for(auto n:{"channel_index","request_index"})array(c.at(n),fn);
    for(auto n:{"scalar_id","group_id","request_id","component_path","unit"})pool(c.at(n),fn);
    auto&q=j.at("requests");for(auto n:{"group_id","request_id","schema_version","construction_profile"})pool(q.at(n),fn);
    if(q.contains("effective_json")){pool(q.at("effective_json"),fn);array(q.at("effective_sha256"),fn);}
    for(auto n:{"time","load_levels","step_index","level_index"})if(j.contains(n))array(j.at(n),fn);
    auto&v=j.at("values");for(auto n:{"row_profile_index","frame_index","payload"})array(v.at(n),fn);
    auto&p=v.at("profiles");for(auto n:{"profile_index","mode","codec_version","stream_word_bits","rounding","mode_code","tolerance_present","requested_tolerance","effective_tolerance"})array(p.at(n),fn);
    for(auto n:{"codec","library_version","library_revision","byte_order","execution"})pool(p.at(n),fn);
    for(auto n:{"max_abs_error","rms_error","verified_count","bitwise_equal"})array(v.at("verification").at(n),fn);
}
// A conservative serialization count before dump() allocation; all text remains UTF-8.
std::uint64_t quoted_bound(std::string_view s){std::uint64_t n=2;for(unsigned char c:s)n=add(n,c<32?6:c=='"'||c=='\\'?2:1);return n;}
std::uint64_t json_bound(const Json& j,unsigned depth=1){
    require(depth<=16,StatusCode::resource_limit);
    if(j.is_string())return quoted_bound(j.get_ref<const std::string&>());
    if(j.is_primitive())return 32;
    std::uint64_t n=2;
    for(auto it=j.begin();it!=j.end();++it){n=add(n,1);if(j.is_object())n=add(n,add(quoted_bound(it.key()),1));n=add(n,json_bound(it.value(),depth+(it.value().is_structured()?1:0)));}
    return n;
}
detail::SpoolReadLimits read_limits(const SpoolLimits& l){return {l.metadata_budget_bytes,l.workspace_budget_bytes,l.copy_tile_bytes,l.document_bytes,l.document_parser_budget_bytes};}
detail::SpoolBinding binding(const SpoolBinding& b){return {b.expected_file_sha256,b.authored_input_sha256,b.expected_file_bytes,b.expected_run_id,b.producer_identity,b.dependency_identity,b.trusted_manifest_binding};}
}
struct VerifiedSpool::Impl {detail::VerifiedSpoolData data;SpoolSummary summary;bool consumed{};};
VerifiedSpool::VerifiedSpool() noexcept=default;
VerifiedSpool::~VerifiedSpool()=default;
VerifiedSpool::VerifiedSpool(VerifiedSpool&&) noexcept=default;
VerifiedSpool& VerifiedSpool::operator=(VerifiedSpool&&) noexcept=default;
std::expected<VerifiedSpool,Status> VerifiedSpool::open(const std::filesystem::path& p,const SpoolBinding& b,SpoolLimits l){
    try{
        require(b.trusted_manifest_binding&&b.expected_file_bytes>0&&!b.expected_run_id.empty()&&!b.producer_identity.empty()&&!b.dependency_identity.empty(),StatusCode::unsupported_feature);
        std::uint64_t binding_bytes=0;
        for(const auto* s:{&b.expected_run_id,&b.producer_identity,&b.dependency_identity}){require(s->size()<=1048576,StatusCode::resource_limit);binding_bytes=add(binding_bytes,s->size()+1);}
        // Admission precedes the public-to-private binding copy. Account for that temporary
        // alongside the reader's retained copy and the public summary wrapper.
        auto reserved=add(mul(binding_bytes,2),sizeof(Impl));
        require(reserved<l.workspace_budget_bytes&&reserved<l.metadata_budget_bytes,StatusCode::resource_limit);
        auto inner=l;inner.workspace_budget_bytes-=reserved;inner.metadata_budget_bytes-=reserved;
        auto data=detail::VerifiedSpoolData::open(p,binding(b),read_limits(inner));if(!data)return std::unexpected(data.error());
        VerifiedSpool s;s.impl_=std::make_unique<Impl>();s.impl_->data=std::move(*data);const auto&v=s.impl_->data.summary;
        s.impl_->summary={v.run_id,v.run_name,v.run_name_present,v.kind,v.diagnostics,v.indexed_provenance,v.fixed_accuracy,v.file_bytes,v.section_count,v.maximum_frame_values,v.retained_metadata_bytes,v.workspace_bytes,v.authored_input_sha256,v.original_state};
        auto extra=add(sizeof(Impl),add(s.impl_->summary.run_id.capacity()+1,s.impl_->summary.run_name.capacity()+1));
        s.impl_->summary.workspace_bytes=add(v.workspace_bytes,add(extra,reserved));
        require(s.impl_->summary.workspace_bytes<=l.workspace_budget_bytes,StatusCode::resource_limit);return s;
    }catch(const Failure& f){return std::unexpected(f.status);}catch(const std::bad_alloc&){return std::unexpected(error(StatusCode::resource_limit,80));}catch(...){return std::unexpected(error(StatusCode::invalid_contract,80));}
}
const SpoolSummary& VerifiedSpool::summary()const noexcept {static const SpoolSummary empty;return impl_?impl_->summary:empty;}
bool VerifiedSpool::valid()const noexcept{return impl_&&!impl_->consumed;}
struct PreparedBundle::Impl {
    struct Source {std::uint64_t metadata_section{};std::uint8_t kind{};};
    BundleDescription description;BundleLimits limits;BundlePlan plan;std::atomic_bool active{false};
    std::vector<std::byte> directory,copy,serialization;
    std::vector<std::uint64_t> remap,run_sections;
    std::vector<std::uint8_t> keep,seen;
    std::vector<Source> sources;
};
PreparedBundle::PreparedBundle() noexcept=default;PreparedBundle::~PreparedBundle()=default;
PreparedBundle::PreparedBundle(PreparedBundle&&) noexcept=default;PreparedBundle& PreparedBundle::operator=(PreparedBundle&&) noexcept=default;
std::expected<PreparedBundle,Status> PreparedBundle::prepare(BundleDescription d,BundleLimits l){
    try{
        require(!d.runs.empty()&&d.runs.size()<=1024&&l.copy_tile_bytes>=4096&&l.copy_tile_bytes<=1048576&&l.workspace_budget_bytes&&l.metadata_budget_bytes);
        require(d.original_input.state!=SectionState::partial&&static_cast<unsigned>(d.original_input.state)<=2);
        require(d.original_input.bytes.size<=4294967296ull,StatusCode::resource_limit);
        // Reuse identity validation without retaining the temporary export resources.
        ArchiveDescription identity;identity.file_id=d.file_id;identity.created_utc=d.created_utc;identity.writer_name=d.writer_name;identity.writer_version=d.writer_version;identity.run.run_id="bundle";
        auto identity_check=PreparedExport::prepare(std::move(identity));require(bool(identity_check));identity_check={};
        BundlePlan p;p.section_capacity=14;p.source_capacity=1;p.maximum_archive_bytes=add(1048576,d.original_input.bytes.size);
        std::uint64_t max_sections=0,metadata=sizeof(Impl);
        auto text_charge=[&](const std::string&s){require(valid_utf8(s));metadata=add(metadata,s.capacity()+1);};
        for(auto*s:{&d.file_id,&d.created_utc,&d.writer_name,&d.writer_version})text_charge(*s);
        metadata=add(metadata,mul(d.runs.capacity(),sizeof(BundleRunPlan)));
        for(std::size_t i=0;i<d.runs.size();++i){const auto&r=d.runs[i];
            require(!r.run_id.empty()&&(!r.run_name.empty()?r.run_name_present:true)&&static_cast<unsigned>(r.kind)<=1);
            require(r.section_capacity>0&&r.section_capacity<=1048576&&r.maximum_archive_bytes>=128&&r.source_capacity>=5&&r.source_capacity<=69);
            for(std::size_t j=0;j<i;++j)require(d.runs[j].run_id!=r.run_id);
            text_charge(r.run_id);text_charge(r.run_name);max_sections=std::max(max_sections,r.section_capacity);
            p.section_capacity=add(p.section_capacity,add(r.section_capacity,14));p.source_capacity=add(p.source_capacity,r.source_capacity);
            p.maximum_archive_bytes=add(p.maximum_archive_bytes,add(r.maximum_archive_bytes,1024));
        }
        require(p.section_capacity<=1048576&&p.source_capacity<=1000000,StatusCode::resource_limit);
        p.directory_bytes=mul(p.section_capacity,64);p.remap_bytes=mul(max_sections,9);
        p.catalog_bytes=add(65536,add(mul(metadata,3),add(mul(p.source_capacity,sizeof(Impl::Source)),mul(d.runs.size(),9))));
        p.copy_bytes=l.copy_tile_bytes;p.serialization_bytes=1048576;p.spool_workspace_bytes=l.spool.workspace_budget_bytes;
        p.workspace_bytes=add(add(add(p.directory_bytes,p.remap_bytes),add(p.catalog_bytes,p.copy_bytes)),add(p.serialization_bytes,p.spool_workspace_bytes));
        p.maximum_archive_bytes=add(p.maximum_archive_bytes,add(add(p.directory_bytes,p.catalog_bytes),mul(p.source_capacity,128)));
        require(p.catalog_bytes<=l.metadata_budget_bytes&&p.workspace_bytes<=l.workspace_budget_bytes,StatusCode::resource_limit);
        PreparedBundle out;out.impl_=std::make_unique<Impl>();auto&x=*out.impl_;x.description=std::move(d);x.limits=l;x.plan=p;
        x.directory.resize(p.directory_bytes);x.copy.resize(p.copy_bytes);x.serialization.resize(p.serialization_bytes);x.remap.resize(max_sections);x.keep.resize(max_sections);
        x.run_sections.resize(x.description.runs.size());x.seen.resize(x.description.runs.size());x.sources.reserve(p.source_capacity);return out;
    }catch(const Failure& f){return std::unexpected(f.status);}catch(const std::bad_alloc&){return std::unexpected(error(StatusCode::resource_limit,81));}catch(...){return std::unexpected(error(StatusCode::invalid_contract,81));}
}
const BundlePlan& PreparedBundle::plan()const noexcept{static const BundlePlan empty;return impl_?impl_->plan:empty;}
const BundleDescription& PreparedBundle::description()const noexcept{static const BundleDescription empty;return impl_?impl_->description:empty;}
bool PreparedBundle::valid()const noexcept{return bool(impl_);}
namespace {
class FixedBuffer final:public std::streambuf {
public:
    explicit FixedBuffer(std::span<std::byte>b){auto*p=reinterpret_cast<char*>(b.data());setp(p,p+b.size());}
    std::size_t size()const{return static_cast<std::size_t>(pptr()-pbase());}
protected:
    std::streamsize xsputn(const char*s,std::streamsize n)override{
        require(n>=0&&n<=epptr()-pptr(),StatusCode::resource_limit);std::memcpy(pptr(),s,static_cast<std::size_t>(n));pbump(static_cast<int>(n));return n;
    }
    int_type overflow(int_type ch)override{
        if(traits_type::eq_int_type(ch,traits_type::eof()))return traits_type::not_eof(ch);
        require(pptr()<epptr(),StatusCode::resource_limit);*pptr()=traits_type::to_char_type(ch);pbump(1);return ch;
    }
};
}
struct BundleSession::Impl {
    PreparedBundle::Impl* prepared{};BundleFeatures features{};FaultHook hook{};
    std::FILE* file{};std::filesystem::path destination,temp;SessionState state{SessionState::opened};SaveStatistics stats{};
    std::uint64_t next{128},section_count{},position{};std::array<std::uint64_t,7> calls{};
    std::array<std::array<std::uint64_t,4>,3> states{};std::array<std::uint64_t,4> original_states{};
    bool any_diagnostics{},any_fixed{};
    ~Impl(){if(file)std::fclose(file);if(!temp.empty()){std::error_code ec;std::filesystem::remove(temp,ec);}if(prepared)prepared->active.store(false);}
    void fault(IoOperation op){auto ordinal=calls[static_cast<unsigned>(op)]++;if(hook.function)status(hook.function(hook.context,op,ordinal));}
    void seek(std::uint64_t off){require(off<=maximum_extent);if(position==off)return;fault(IoOperation::seek);++stats.seek_calls;
#ifdef _WIN32
        require(_fseeki64(file,static_cast<__int64>(off),SEEK_SET)==0,StatusCode::io_failure);
#else
        require(fseeko(file,static_cast<off_t>(off),SEEK_SET)==0,StatusCode::io_failure);
#endif
        position=off;
    }
    void write(std::uint64_t off,std::span<const std::byte>b){
        if(b.empty())return;require(add(off,b.size())<=prepared->plan.maximum_archive_bytes,StatusCode::resource_limit);
        seek(off);fault(IoOperation::write);auto ordinal=stats.write_calls++;auto n=b.size();if(hook.write_limit)n=std::min(n,hook.write_limit(hook.context,ordinal,n));
        auto done=std::fwrite(b.data(),1,n,file);position+=done;stats.written_bytes+=done;require(done==b.size(),StatusCode::io_failure);
    }
    struct Section{std::uint64_t id{},offset{},size{},written{};std::uint32_t kind{};Sha256 hash;};
    Section begin(std::uint32_t kind,std::uint64_t size){
        require(section_count<prepared->plan.section_capacity,StatusCode::resource_limit);Section s;s.id=section_count++;s.offset=next;s.size=size;s.kind=kind;next=add(next,size);return s;
    }
    void append(Section&s,std::span<const std::byte>b){require(b.size()<=s.size-s.written);write(s.offset+s.written,b);s.hash.update(b);s.written+=b.size();}
    void finish(Section&s,const Digest* expected=nullptr){
        require(s.written==s.size);auto digest=s.hash.finish();if(expected)require(digest==*expected,StatusCode::verification_failure);
        auto bytes=std::span(prepared->directory).subspan(s.id*64,64);std::fill(bytes.begin(),bytes.end(),std::byte{});
        put(bytes.first(8),s.id);put(bytes.subspan(8,4),s.kind);put(bytes.subspan(16,8),s.offset);put(bytes.subspan(24,8),s.size);std::copy(digest.begin(),digest.end(),bytes.begin()+32);
    }
    Json descriptor(const Section&s,const char*dtype,std::uint64_t count){return Json{{"section",s.id},{"dtype",dtype},{"shape",Json::array({count})}};}
    Json bytes(std::span<const std::byte>b){auto s=begin(2,b.size());for(std::size_t off=0;off<b.size();off+=prepared->copy.size())append(s,b.subspan(off,std::min(prepared->copy.size(),b.size()-off)));finish(s);return descriptor(s,"u8",b.size());}
    template<class Get>Json integers(std::uint32_t kind,const char* dtype,std::uint64_t count,Get get){
        auto width=kind==2?1ull:8ull;auto s=begin(kind,mul(count,width));auto tile=std::span(prepared->copy);std::size_t used=0;
        for(std::uint64_t i=0;i<count;++i){if(used+width>tile.size()){append(s,tile.first(used));used=0;}put(tile.subspan(used,width),get(i));used+=static_cast<std::size_t>(width);}
        append(s,tile.first(used));finish(s);return descriptor(s,dtype,count);
    }
    template<class Get>Json strings(std::uint64_t count,Get get,bool optional=false){
        std::uint64_t total=0;for(std::uint64_t i=0;i<count;++i)total=add(total,std::string_view(get(i)).size());require(total<=268435456,StatusCode::resource_limit);
        auto s=begin(2,total);for(std::uint64_t i=0;i<count;++i){std::string_view value=get(i);auto b=std::as_bytes(std::span(value.data(),value.size()));for(std::size_t off=0;off<b.size();off+=prepared->copy.size())append(s,b.subspan(off,std::min(prepared->copy.size(),b.size()-off)));}finish(s);
        std::uint64_t off=0;Json p{{"count",count},{"bytes",descriptor(s,"u8",total)}};
        p["offsets"]=integers(4,"u64",count+1,[&](auto i){if(i)off+=std::string_view(get(i-1)).size();return off;});
        if(optional)p["present"]=integers(2,"u8",count,[&](auto i){return prepared->description.runs[i].run_name_present?1:0;});return p;
    }
    std::uint64_t metadata(Json& j){
        require(json_bound(j)<=1048576,StatusCode::resource_limit);
        FixedBuffer buffer(prepared->serialization);std::ostream output(&buffer);output.exceptions(std::ios::badbit|std::ios::failbit);output<<j;output.flush();
        auto b=std::span<const std::byte>(prepared->serialization).first(buffer.size());
        status(json_contract::preflight(std::string_view(reinterpret_cast<const char*>(b.data()),b.size()),prepared->limits.metadata_budget_bytes));
        auto s=begin(1,b.size());append(s,b);finish(s);return s.id;
    }
    void original(){
        const auto&d=prepared->description;const auto&v=d.original_input;Json doc{{"state",static_cast<unsigned>(v.state)}};
        if(v.state==SectionState::included){
            require(!v.media_type.empty()&&valid_utf8(v.media_type));auto s=begin(2,v.bytes.size);auto tile=std::span(prepared->copy);
            for(std::uint64_t off=0;off<v.bytes.size;){auto n=std::min<std::uint64_t>(tile.size(),v.bytes.size-off);fault(IoOperation::source);status(v.bytes.read(off,tile.first(n)));append(s,tile.first(n));off+=n;}
            finish(s,&d.authored_input_sha256);doc["media_type"]=v.media_type;doc["bytes"]=descriptor(s,"u8",v.bytes.size);
        }
        Json source{{"source_schema",1},{"source_index",0},{"kind",0},{"document",std::move(doc)}};
        prepared->sources.push_back({metadata(source),0});
    }
    void copy(std::uint64_t global_index,detail::VerifiedSpoolData& in){
        const auto& plan=prepared->description.runs[global_index];const auto& summary=in.summary;
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
        const bool retain_local_original=std::any_of(in.references.begin(),in.references.end(),[&](const auto&r){return r.role!=0&&r.source_index==in.original_source_index;});
        require(in.directory.size()<=prepared->remap.size(),StatusCode::resource_limit);
        auto n=in.directory.size();std::fill_n(prepared->keep.begin(),n,std::uint8_t{});std::fill_n(prepared->remap.begin(),n,UINT64_MAX);
        auto mark=[&](Json& descriptor){auto id=descriptor.at("section").get<std::uint64_t>();require(id<n);prepared->keep[id]=1;};
        run_fields(in.run,mark);for(auto&t:in.traces)trace_fields(t.metadata,mark);
        for(auto&s:in.sources)if(s.source_index!=in.original_source_index||retain_local_original)source_fields(s.metadata,mark);
        auto tile=std::span(prepared->copy);
        for(std::uint64_t id=0;id<n;++id)if(prepared->keep[id]){
            const auto& old=in.directory[id];require(old.kind!=1);auto s=begin(old.kind,old.size);prepared->remap[id]=s.id;
            for(std::uint64_t off=0;off<old.size;){auto count=std::min<std::uint64_t>(tile.size(),old.size-off);fault(IoOperation::source);status(in.read(id,off,tile.first(count)));append(s,tile.first(count));off+=count;}
            finish(s,&old.digest);
        }
        auto remap=[&](Json& descriptor){auto id=descriptor.at("section").get<std::uint64_t>();require(id<n&&prepared->remap[id]!=UINT64_MAX);descriptor["section"]=prepared->remap[id];};
        std::array<std::uint64_t,5> trace_ids{};
        for(std::size_t i=0;i<in.traces.size();++i){auto&t=in.traces[i];trace_fields(t.metadata,remap);t.metadata["run_index"]=global_index;trace_ids[i]=metadata(t.metadata);}
        std::array<std::uint64_t,69> source_map{};
        require(in.sources.size()<=source_map.size(),StatusCode::resource_limit);
        for(auto&s:in.sources){require(s.source_index<source_map.size());
            if(s.source_index==in.original_source_index&&!retain_local_original){source_map[s.source_index]=0;continue;}
            require(prepared->sources.size()<prepared->plan.source_capacity,StatusCode::resource_limit);
            auto new_index=prepared->sources.size();source_map[s.source_index]=new_index;
            source_fields(s.metadata,remap);s.metadata["source_index"]=new_index;prepared->sources.push_back({metadata(s.metadata),s.kind});
        }
        run_fields(in.run,remap);
        if(features.diagnostics){
            Json entries=Json::array();for(std::size_t i=0;i<in.traces.size();++i)entries.push_back({{"kind",in.traces[i].kind},{"metadata_section",trace_ids[i]}});
            if(summary.diagnostics)in.run["diagnostics"]["traces"]=std::move(entries);
            else in.run["diagnostics"]={{"status",3},{"traces",std::move(entries)}};
        }
        const auto K=in.references.size();Json refs;
        refs["role"]=integers(2,"u8",K,[&](auto i){return in.references[i].role;});
        refs["source_index"]=integers(4,"u64",K,[&](auto i){return in.references[i].role==0?0:source_map[in.references[i].source_index];});
        refs["order"]=integers(4,"u64",K,[&](auto i){return in.references[i].order;});
        in.run["provenance"]={{"references",std::move(refs)}};
        for(const auto&ref:in.references){
            SectionState s;
            if(ref.role==0)s=prepared->description.original_input.state;
            else {const auto&src=in.sources[ref.source_index];s=static_cast<SectionState>((src.kind?src.metadata.at("state"):src.metadata.at("document").at("state")).get<unsigned>());}
            ++states[ref.role<4?0:ref.role==4?2:1][static_cast<unsigned>(s)];
        }
        prepared->run_sections[global_index]=metadata(in.run);prepared->seen[global_index]=1;
        status(in.verify_unchanged());
    }
    unsigned aggregate(const std::array<std::uint64_t,4>&s){auto total=s[0]+s[1]+s[2]+s[3];return !total||s[1]==total?1:s[0]==total?0:!s[0]&&!s[3]&&s[2]?2:3;}
    void complete(){
        require(std::all_of(prepared->seen.begin(),prepared->seen.end(),[](auto x){return x==1;}));
        require(any_diagnostics==features.diagnostics&&any_fixed==features.fixed_accuracy);
        const auto&d=prepared->description;
        if(!original_states[0]){
            if(d.original_input.state==SectionState::included)require(d.explicitly_retain_original);
            else require(d.original_input.state==(original_states[2]?SectionState::unavailable:SectionState::disabled));
        }
        Json tokens=Json::array({"serene-container-le-1","json-descriptors-1","utf8-pools-1","frames-77-le-1","zfp-5-le64-serial-1"});
        if(features.diagnostics)tokens.push_back("run-diagnostics-1");tokens.push_back("provenance-sources-1");if(features.fixed_accuracy)tokens.push_back("timeseries-zfp-fixed-accuracy-1");
        Json format{{"name","serene.outputs"},{"version_major",1},{"version_minor",1},{"file_id",d.file_id},{"created_utc",d.created_utc},{"writer_name",d.writer_name},{"writer_version",d.writer_version},{"write_state","complete"},{"required_features",std::move(tokens)},
            {"sections",{{"inputs",aggregate(states[0])},{"logs",aggregate(states[1])},{"context",aggregate(states[2])}}}};
        Json index;auto N=d.runs.size();index["run_index"]=integers(4,"u64",N,[](auto i){return i;});
        index["run_id"]=strings(N,[&](auto i)->std::string_view{return d.runs[i].run_id;});
        index["run_name"]=strings(N,[&](auto i)->std::string_view{return d.runs[i].run_name;},true);
        index["kind"]=integers(2,"u8",N,[&](auto i){return static_cast<unsigned>(d.runs[i].kind);});
        index["run_metadata_section"]=integers(4,"u64",N,[&](auto i){return prepared->run_sections[i];});
        auto S=prepared->sources.size();Json sources;
        sources["source_index"]=integers(4,"u64",S,[](auto i){return i;});sources["kind"]=integers(2,"u8",S,[&](auto i){return prepared->sources[i].kind;});
        sources["metadata_section"]=integers(4,"u64",S,[&](auto i){return prepared->sources[i].metadata_section;});
        Json provenance{{"schema",1},{"original_source_index",0},{"authored_input_sha256",bytes(d.authored_input_sha256)},{"sources",std::move(sources)}};
        Json root{{"format",std::move(format)},{"results",{{"index",std::move(index)}}},{"provenance",std::move(provenance)}};
        auto root_id=metadata(root);auto directory=std::span<const std::byte>(prepared->directory).first(section_count*64);auto file_bytes=add(next,directory.size());write(next,directory);
        std::array<std::byte,128> header{};std::memcpy(header.data(),"SERENE\0\0",8);put(std::span(header).subspan(8,2),1);put(std::span(header).subspan(10,2),1);put(std::span(header).subspan(12,4),0x01020304);put(std::span(header).subspan(16,4),128);put(std::span(header).subspan(20,4),64);
        put(std::span(header).subspan(24,8),file_bytes);put(std::span(header).subspan(32,8),next);put(std::span(header).subspan(40,8),section_count);put(std::span(header).subspan(48,8),root_id);put(std::span(header).subspan(56,8),1);
        auto directory_hash=sha256(directory);std::copy(directory_hash.begin(),directory_hash.end(),header.begin()+64);auto header_hash=sha256(std::span(header).first(96));std::copy(header_hash.begin(),header_hash.end(),header.begin()+96);write(0,header);seek(file_bytes);
    }
};
BundleSession::BundleSession() noexcept=default;BundleSession::~BundleSession()=default;
BundleSession::BundleSession(BundleSession&&) noexcept=default;BundleSession& BundleSession::operator=(BundleSession&&) noexcept=default;
std::expected<BundleSession,Status> BundleSession::open(PreparedBundle& p,const std::filesystem::path& destination,BundleFeatures features,FaultHook hook){
    try{
        require(p.valid());require(std::endian::native==std::endian::little,StatusCode::unsupported_feature);
        require(destination.extension()==".serene");require(!std::filesystem::exists(destination),StatusCode::publication_failure);
        auto parent=destination.parent_path();if(parent.empty())parent=".";require(std::filesystem::is_directory(parent),StatusCode::io_failure);
        BundleSession session;session.impl_=std::make_unique<Impl>();auto&i=*session.impl_;
        bool inactive=false;require(p.impl_->active.compare_exchange_strong(inactive,true),StatusCode::invalid_state);i.prepared=p.impl_.get();
        i.destination=destination;i.features=features;i.hook=hook;i.stats.application_reserved_bytes=p.plan().workspace_bytes;
        auto original_validation=detail::validate_original_document(p.description().original_input,p.description().authored_input_sha256,read_limits(p.impl_->limits.spool));
        if(!original_validation)throw Failure{original_validation.error()};
        std::fill(i.prepared->directory.begin(),i.prepared->directory.end(),std::byte{});std::fill(i.prepared->seen.begin(),i.prepared->seen.end(),std::uint8_t{});i.prepared->sources.clear();
        static std::atomic<std::uint64_t> sequence{};auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned attempt=0;attempt<32;++attempt){
            auto name=destination.filename().native();name.insert(name.begin(),static_cast<std::filesystem::path::value_type>('.'));name+=std::filesystem::path(".bundle-tmp-"+std::to_string(stamp)+"-"+std::to_string(sequence++)).native();i.temp=parent/name;int fd=-1;
#ifdef _WIN32
            auto rc=_wsopen_s(&fd,i.temp.c_str(),_O_CREAT|_O_EXCL|_O_BINARY|_O_RDWR,_SH_DENYRW,_S_IREAD|_S_IWRITE);if(rc==0)i.file=_wfdopen(fd,L"w+b");
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
                std::error_code ec;std::filesystem::remove(i.temp,ec);
            }
            i.temp.clear();
        }
        require(i.file!=nullptr,StatusCode::io_failure);std::array<std::byte,128> zero{};i.write(0,zero);i.original();return session;
    }catch(const Failure&f){return std::unexpected(f.status);}catch(const std::bad_alloc&){return std::unexpected(error(StatusCode::resource_limit,82));}catch(...){return std::unexpected(error(StatusCode::io_failure,82));}
}
Status BundleSession::append_verified_spool(std::uint64_t index,VerifiedSpool& spool) noexcept{
    if(!impl_||(impl_->state!=SessionState::opened&&impl_->state!=SessionState::writing)||!spool.valid())return error(StatusCode::invalid_state,83);
    auto&i=*impl_;spool.impl_->consumed=true;
    try{
        require(index<i.prepared->description.runs.size()&&!i.prepared->seen[index]);
        require(spool.summary().workspace_bytes<=i.prepared->plan.spool_workspace_bytes,StatusCode::resource_limit);
        i.state=SessionState::writing;i.copy(index,spool.impl_->data);return {};
    }catch(const Failure&f){i.state=SessionState::failed;return f.status;}catch(const std::bad_alloc&){i.state=SessionState::failed;return error(StatusCode::resource_limit,83);}catch(...){i.state=SessionState::failed;return error(StatusCode::invalid_contract,83);}
}
Status BundleSession::finalize() noexcept{
    if(!impl_||(impl_->state!=SessionState::opened&&impl_->state!=SessionState::writing))return error(StatusCode::invalid_state,84);auto&i=*impl_;
    try{
        i.state=SessionState::finalizing;i.complete();i.fault(IoOperation::flush);require(std::fflush(i.file)==0,StatusCode::io_failure);
        i.fault(IoOperation::close);auto*file=std::exchange(i.file,nullptr);require(std::fclose(file)==0,StatusCode::io_failure);i.fault(IoOperation::publish);
#ifdef _WIN32
        require(MoveFileExW(i.temp.c_str(),i.destination.c_str(),0)!=0,StatusCode::publication_failure);
#else
        std::error_code ec;std::filesystem::create_hard_link(i.temp,i.destination,ec);require(!ec,StatusCode::publication_failure);std::filesystem::remove(i.temp,ec);
#endif
        i.temp.clear();i.state=SessionState::published;return {};
    }catch(const Failure&f){i.state=SessionState::failed;return f.status;}catch(...){i.state=SessionState::failed;return error(StatusCode::publication_failure,84);}
}
SessionState BundleSession::state()const noexcept{return impl_?impl_->state:SessionState::failed;}
SaveStatistics BundleSession::statistics()const noexcept{return impl_?impl_->stats:SaveStatistics{};}
} // namespace serene::outputs
