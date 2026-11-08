#include "synthetic_case.hpp"
#include "allocation_meter.hpp"
#include <serene/outputs/bundle.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace serene::outputs;
using Json=nlohmann::json;
namespace {
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
void good(Status s,const std::string& message){check(bool(s),message+": "+std::string(status_name(s.code))+" op"+std::to_string(s.operation));}
template<class T>T take(std::expected<T,Status>&&v,const std::string& name){if(!v)good(v.error(),name);return std::move(*v);}
Digest hash_file(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);check(bool(f),"hash open");Sha256 h;std::array<char,65536>b;while(f){f.read(b.data(),b.size());h.update(std::as_bytes(std::span(b.data(),static_cast<std::size_t>(f.gcount()))));}check(f.eof(),"hash read");return h.finish();}
std::string hex(const Digest&d){constexpr char digits[]="0123456789abcdef";std::string s;for(auto b:d){auto n=std::to_integer<unsigned>(b);s+=digits[n>>4];s+=digits[n&15];}return s;}
Digest digest(const std::string&s){check(s.size()==64,"hex digest size");Digest d;for(unsigned i=0;i<32;++i)d[i]=std::byte(std::stoul(s.substr(i*2,2),nullptr,16));return d;}
struct Inspection {
    std::vector<char> bytes;std::uint64_t directory{};
    explicit Inspection(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(f),{});directory=integer(32);}
    std::uint64_t integer(std::uint64_t at){check(at+8<=bytes.size(),"inspection extent");std::uint64_t n=0;for(unsigned i=0;i<8;++i)n|=std::uint64_t(static_cast<unsigned char>(bytes[at+i]))<<(8*i);return n;}
    std::uint64_t offset(std::uint64_t section){return integer(directory+64*section+16);}
    Json json(std::uint64_t section){auto at=offset(section),size=integer(directory+64*section+24);check(at+size<=bytes.size(),"metadata extent");return Json::parse(bytes.begin()+at,bytes.begin()+at+size);}
    std::uint64_t value(const Json&d,std::uint64_t i){return integer(offset(d.at("section"))+i*8);}
};
struct Saved {std::filesystem::path path;SpoolBinding binding;BundleRunPlan plan;bool diagnostics{},fixed{};SectionState original{};};
Saved make_spool(const std::filesystem::path&dir,std::string id,RunKind kind,bool indexed,bool diagnostics,bool fixed,SectionState original){
    SyntheticCase c(2,kind==RunKind::timeseries?32:3,kind);c.description.writer_version="0.2.0";c.description.run.run_id=id;c.description.indexed_provenance=indexed;c.description.maximum_log_sources=2;c.limits.provenance_bytes_bound=65536;
    c.info.accepted_count=c.axis.size()-1;c.info.last_accepted_value=c.axis.back();c.info.requested_end=c.axis.back();
    if(kind==RunKind::steady_state){c.info.requested_begin=c.axis.front();c.info.identity_axis_convention="accepted_level_zero_is_initial";for(std::size_t i=0;i<c.identities.size();++i)c.identities[i]=i;}
    else for(std::size_t i=0;i<c.axis.size();++i)c.data[2*i+1]=.123456789*i+.3333333333333333;
    if(fixed){c.description.run.profiles={CodecProfile::reversible(),take(CodecProfile::fixed_accuracy(1e-4),"fixed")};c.description.run.row_profile_index={0,1};}
    std::array<DiagnosticRecord,2> events{};std::array<DiagnosticTraceView,1> traces{};
    if(diagnostics){DiagnosticTraceDescription d;d.kind=DiagnosticKind::run_events;d.event_names={"producer.started","producer.completed"};d.record_capacity=2;c.description.run.diagnostics.push_back(d);
        for(unsigned i=0;i<2;++i){events[i].record_index=i;events[i].sequence=i;events[i].event_name_index=i;events[i].severity=1;}
        traces[0].kind=d.kind;traces[0].records=DiagnosticRecords::memory(events);traces[0].availability=DiagnosticAvailability::included;traces[0].capture_status=DiagnosticCaptureStatus::complete;traces[0].counts_present=true;traces[0].eligible_count=2;
    }
    auto prepared=take(PreparedExport::prepare(c.description,c.limits),"spool prepare");auto codec=take(Codec::create(c.limits.frame_values),"codec");auto path=dir/(id+".serene");auto session=take(ArchiveSession::open(prepared,codec,path),"spool open");
    auto v=c.view();if(diagnostics)v.diagnostics=traces;auto legacy=c.provenance();
    if(indexed){IndexedProvenanceView provenance;provenance.authored_input_sha256=sha256(std::as_bytes(std::span(c.original.data(),c.original.size())));provenance.documents={legacy.inputs.source_json,legacy.inputs.effective_json,legacy.inputs.overrides_json,legacy.inputs.resources_json,legacy.context.execution_json};
        if(original!=SectionState::included)provenance.documents[0]=DocumentView{original,{},{}};
        std::array<LogsView,2> logs={legacy.logs,legacy.logs};std::string second="";logs[1].bytes=ByteSource::text(second);provenance.logs=logs;
        good(session.append_run(v,provenance),"spool append indexed");
    }else{check(original==SectionState::included,"legacy original");good(session.append_run(v,legacy),"spool append legacy");}
    good(session.finalize(),"spool finalize");c.expected(dir/id);
    Saved s;s.path=path;s.original=original;s.diagnostics=diagnostics;s.fixed=fixed;
    s.binding={hash_file(path),sha256(std::as_bytes(std::span(c.original.data(),c.original.size()))),std::filesystem::file_size(path),id,"qualified-test-producer-0.2","zfp-1.0.1",true};
    s.plan={id,c.description.run.run_name,true,kind,prepared.plan().section_capacity,prepared.plan().maximum_archive_bytes,69};return s;
}
BundleDescription description(std::span<const Saved> spools,const std::string& original,SectionState state=SectionState::included){
    BundleDescription d;d.file_id="00000000-0000-4000-8000-000000000008";d.created_utc="2026-11-08T00:00:00Z";d.authored_input_sha256=sha256(std::as_bytes(std::span(original.data(),original.size())));
    d.original_input={state,state==SectionState::included?ByteSource::text(original):ByteSource{},state==SectionState::included?"application/json":""};for(auto&s:spools)d.runs.push_back(s.plan);return d;
}
BundleFeatures features(std::span<const Saved>s){BundleFeatures f;for(auto&v:s){f.diagnostics|=v.diagnostics;f.fixed_accuracy|=v.fixed;}return f;}
void append(BundleSession&out,std::span<const Saved>s,std::span<const std::size_t>order){for(auto i:order){auto spool=take(VerifiedSpool::open(s[i].path,s[i].binding),"verified open "+s[i].plan.run_id);good(out.append_verified_spool(i,spool),"bundle append "+s[i].plan.run_id);check(!spool.valid(),"append consumes handle");}}
struct Fault {IoOperation operation;std::uint64_t ordinal;bool fired{};static Status call(void*p,IoOperation op,std::uint64_t n)noexcept{auto&f=*static_cast<Fault*>(p);if(op==f.operation&&n==f.ordinal){f.fired=true;return error(StatusCode::io_failure,99);}return {};}};
void no_temp(const std::filesystem::path&dir){for(const auto&e:std::filesystem::directory_iterator(dir))check(e.path().filename().string().find(".bundle-tmp-")==std::string::npos,"temporary cleaned");}
}
int main(int argc,char**argv){try{
    auto dir=argc>1&&std::string_view(argv[1])!="--auto"?std::filesystem::path(argv[1]):std::filesystem::current_path()/("bundle-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(!std::filesystem::exists(dir),"fresh path");std::filesystem::create_directories(dir);
    std::string original=SyntheticCase(0,0).original;
    std::array<Saved,3>s={make_spool(dir,"static",RunKind::steady_state,false,true,false,SectionState::included),make_spool(dir,"exact",RunKind::timeseries,false,false,false,SectionState::included),make_spool(dir,"fixed",RunKind::timeseries,true,true,true,SectionState::included)};
    const std::array<std::size_t,3>order={2,0,1};auto measured_before=allocation_meter::start();auto prepared=take(PreparedBundle::prepare(description(s,original)),"bundle prepare");
    auto bundle_path=dir/"mixed_bundle.serene";{auto out=take(BundleSession::open(prepared,bundle_path,features(s)),"bundle open");append(out,s,order);good(out.finalize(),"bundle finalize");check(out.statistics().frames>0,"frame counts");check(std::filesystem::file_size(bundle_path)<=prepared.plan().maximum_archive_bytes,"final archive bound");}
    auto measured_peak=allocation_meter::maximum_since(measured_before);check(measured_peak<=prepared.plan().workspace_bytes,"live requested C++ heap stays within planned bundle workspace");
    // Retained disk spools are sufficient for retry; all producers and their numerical histories are gone.
    {auto out=take(BundleSession::open(prepared,dir/"retry.serene",features(s)),"retry open");append(out,s,order);good(out.finalize(),"retry finalize");check(hash_file(dir/"retry.serene")==hash_file(bundle_path),"deterministic retry bytes");}
    check(BundleSession::open(prepared,bundle_path,features(s)).error().code==StatusCode::publication_failure,"existing destination preserved");
    {auto out=take(BundleSession::open(prepared,dir/"lease_first.serene",features(s)),"lease first");auto second=BundleSession::open(prepared,dir/"lease_second.serene",features(s));check(!second&&second.error().code==StatusCode::invalid_state,"prepared bundle lease rejects overlapping sessions");}
    {auto out=take(BundleSession::open(prepared,dir/"missing.serene",features(s)),"missing open");auto spool=take(VerifiedSpool::open(s[0].path,s[0].binding),"missing source");good(out.append_verified_spool(0,spool),"missing append");check(!out.finalize(),"missing catalog run rejected");}
    {auto out=take(BundleSession::open(prepared,dir/"duplicate.serene",features(s)),"duplicate open");auto a=take(VerifiedSpool::open(s[0].path,s[0].binding),"duplicate source a");good(out.append_verified_spool(0,a),"duplicate append a");auto b=take(VerifiedSpool::open(s[0].path,s[0].binding),"duplicate source b");check(!out.append_verified_spool(0,b),"duplicate run rejected");}
    auto wrong=s[0].binding;wrong.authored_input_sha256={};check(!VerifiedSpool::open(s[0].path,wrong),"wrong authored digest rejected");wrong=s[0].binding;wrong.expected_file_bytes++;check(!VerifiedSpool::open(s[0].path,wrong),"wrong extent rejected");
    wrong=s[0].binding;wrong.trusted_manifest_binding=false;auto unbound=VerifiedSpool::open(s[0].path,wrong);check(!unbound&&unbound.error().code==StatusCode::unsupported_feature,"unbound external spool is unsupported initial import");
    {auto bad=description(s,original);std::string malformed="{";bad.original_input.bytes=ByteSource::text(malformed);bad.authored_input_sha256=sha256(std::as_bytes(std::span(malformed.data(),malformed.size())));auto p=take(PreparedBundle::prepare(std::move(bad)),"malformed original plan");check(!BundleSession::open(p,dir/"malformed_original.serene",features(s)),"common included JSON validated before publication");}
    // Both post-verification mutation and truncation must fail before publishing.
    auto changed=dir/"mutated_spool.serene";std::filesystem::copy_file(s[2].path,changed);auto spool=take(VerifiedSpool::open(changed,s[2].binding),"mutation source");
    {std::fstream f(changed,std::ios::binary|std::ios::in|std::ios::out);f.seekp(128);char b=77;f.write(&b,1);check(bool(f),"mutation write");}
    {auto out=take(BundleSession::open(prepared,dir/"mutation.serene",features(s)),"mutation open");check(!out.append_verified_spool(2,spool),"changed spool rejected");}
    std::array<Saved,2>absent={make_spool(dir,"disabled",RunKind::timeseries,true,false,false,SectionState::disabled),make_spool(dir,"unavailable",RunKind::timeseries,true,false,false,SectionState::unavailable)};
    const std::array<std::size_t,2>pair_order={1,0};
    {auto p=take(PreparedBundle::prepare(description(absent,original,SectionState::unavailable)),"union prepare");auto out=take(BundleSession::open(p,dir/"unavailable_union.serene"),"union open");append(out,absent,pair_order);good(out.finalize(),"unavailable union");}
    {std::span<const Saved>one(absent.data(),1);std::array<std::size_t,1>first={0};auto p=take(PreparedBundle::prepare(description(one,original,SectionState::disabled)),"disabled prepare");auto out=take(BundleSession::open(p,dir/"disabled_union.serene"),"disabled open");append(out,one,first);good(out.finalize(),"disabled union");}
    {std::span<const Saved>one(absent.data(),1);std::array<std::size_t,1>first={0};auto p=take(PreparedBundle::prepare(description(one,original)),"extra retention prepare");auto out=take(BundleSession::open(p,dir/"unrequested_retention.serene"),"extra retention open");append(out,one,first);check(!out.finalize(),"all-disabled does not silently retain original");}
    {auto d=description(absent,original);d.explicitly_retain_original=true;auto p=take(PreparedBundle::prepare(std::move(d)),"explicit prepare");auto out=take(BundleSession::open(p,dir/"explicit_retention.serene"),"explicit open");append(out,absent,pair_order);good(out.finalize(),"explicit retention");}
    if(argc>2){
        std::filesystem::path binding_path=argv[2];std::ifstream input(binding_path);Json b;input>>b;Saved shared;shared.path=binding_path.parent_path()/b.at("file").get<std::string>();
        shared.binding={digest(b.at("sha256")),digest(b.at("authored_sha256")),b.at("bytes"),b.at("run_id"),b.at("producer_identity"),b.at("dependency_identity"),true};
        shared.plan={b.at("run_id"),b.at("run_name"),true,RunKind::timeseries,b.at("section_count"),b.at("bytes"),b.at("source_count")};
        std::array<Saved,2> donors={s[0],shared};auto p=take(PreparedBundle::prepare(description(donors,original)),"shared original plan");auto path=dir/"shared_original_roles.serene";
        {auto out=take(BundleSession::open(p,path,features(donors)),"shared original open");append(out,donors,pair_order);good(out.finalize(),"shared original finalize");}
        Inspection inspect(path);auto root=inspect.json(inspect.integer(48));auto run=inspect.json(inspect.value(root.at("results").at("index").at("run_metadata_section"),1));auto refs=run.at("provenance").at("references");auto role0=inspect.value(refs.at("source_index"),0),role1=inspect.value(refs.at("source_index"),1);
        auto catalog=root.at("provenance").at("sources").at("metadata_section");auto original_source=inspect.json(inspect.value(catalog,role0)),effective_source=inspect.json(inspect.value(catalog,role1));
        check(role0!=role1&&original_source.at("document").at("state")==0&&effective_source.at("document").at("state")==1,"only original role replaced when source identity is shared");
    }
    auto tiny=BundleLimits{};tiny.workspace_budget_bytes=prepared.plan().workspace_bytes-1;auto limited=PreparedBundle::prepare(description(s,original),tiny);check(!limited&&limited.error().code==StatusCode::resource_limit,"workspace admission");
    auto more=description(s,original);for(unsigned i=0;i<20;++i){auto r=more.runs[0];r.run_id="additional_"+std::to_string(i);more.runs.push_back(std::move(r));}auto larger=take(PreparedBundle::prepare(std::move(more)),"larger catalog");check(larger.plan().spool_workspace_bytes==prepared.plan().spool_workspace_bytes&&larger.plan().copy_bytes==prepared.plan().copy_bytes&&larger.plan().workspace_bytes>prepared.plan().workspace_bytes,"one spool workspace plus budgeted catalog scaling");
    unsigned fault_cases=0;for(auto op:{IoOperation::source,IoOperation::write,IoOperation::flush,IoOperation::close,IoOperation::publish,IoOperation::seek}){
        Fault f{op,op==IoOperation::write?5ull:0ull};auto path=dir/("fault_"+std::to_string(static_cast<unsigned>(op))+".serene");{
            auto out=BundleSession::open(prepared,path,features(s),{&f,Fault::call});
            if(out){bool failed=false;for(auto i:order){auto input=take(VerifiedSpool::open(s[i].path,s[i].binding),"fault source");auto status=out->append_verified_spool(i,input);if(!status){failed=true;break;}}if(!failed)check(!out->finalize(),"fault finalize");}
        }check(f.fired&&!std::filesystem::exists(path),"fault unpublished");++fault_cases;
    }
    {auto path=dir/"short_write.serene";FaultHook hook;hook.write_limit=[](void*,std::uint64_t n,std::size_t count)noexcept{return n==1?count-1:count;};auto out=BundleSession::open(prepared,path,features(s),hook);check(!out&&!std::filesystem::exists(path),"short write rejects open without publication");++fault_cases;}
    {auto path=dir/"destination_race.serene";auto out=take(BundleSession::open(prepared,path,features(s)),"race open");append(out,s,order);{std::ofstream winner(path);winner<<"existing destination";}auto winner_hash=hash_file(path);check(out.finalize().code==StatusCode::publication_failure&&hash_file(path)==winner_hash,"destination race never clobbers winner");++fault_cases;}
    no_temp(dir);Json receipt{{"schema",1},{"status","passed"},{"mixed_bundle",bundle_path.filename().string()},{"bundle_sha256",hex(hash_file(bundle_path))},{"catalog_order",{"static","exact","fixed"}},{"physical_order",{"fixed","static","exact"}},{"fault_cases",fault_cases},{"workspace_bytes",prepared.plan().workspace_bytes},{"directory_bytes",prepared.plan().directory_bytes},{"catalog_bytes",prepared.plan().catalog_bytes},{"spool_workspace_bytes",prepared.plan().spool_workspace_bytes},{"maximum_archive_bytes",prepared.plan().maximum_archive_bytes},{"larger_catalog_workspace_bytes",larger.plan().workspace_bytes},{"spools",Json::array()}};
    receipt["measured_peak_cpp_bytes"]=measured_peak;
    for(auto&v:s)receipt["spools"].push_back({{"path",v.path.filename().string()},{"run_id",v.plan.run_id},{"sha256",hex(v.binding.expected_file_sha256)},{"bytes",v.binding.expected_file_bytes}});std::ofstream(dir/"receipt.json")<<receipt.dump(2)<<'\n';
    std::cout<<"Bundle assembly, exact encoded-copy, manifest, retention, retry, quota and "<<fault_cases<<" publication fault cases passed: "<<dir.string()<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
