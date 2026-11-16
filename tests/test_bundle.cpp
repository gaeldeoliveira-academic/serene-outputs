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
/** Require ok; throw the supplied message on failure. */
void check(bool ok,const std::string& message){
    // Route failed assertions through the common executable exception path.
if(!ok)throw std::runtime_error(message);}
/** Require successful status s, extending message with its status name and operation code on failure. */
void good(Status s,const std::string& message){
    // Format the production status and delegate to the predicate assertion helper.
check(bool(s),message+": "+std::string(status_name(s.code))+" op"+std::to_string(s.operation));}
/** Move a successful expected value v to the caller, or throw a status-rich failure labelled name. Used to transfer move-only prepared/session/spool owners. */
template<class T>T take(std::expected<T,Status>&&v,const std::string& name){
    // Check the error before dereferencing, then transfer ownership of the successful value.
if(!v)good(v.error(),name);return std::move(*v);}
/** Return SHA-256 of exact binary file p using a fixed tile; throws on open or incomplete read. */
Digest hash_file(const std::filesystem::path&p){
    // Open the file, stream all populated tiles into SHA-256, and require clean EOF.
/* Binary input stream owned by this helper. */ std::ifstream f(p,std::ios::binary);check(bool(f),"hash open");/* Incremental SHA-256 state. */ Sha256 h;/* Reusable hash tile. */ std::array<char,65536>b;while(f){f.read(b.data(),b.size());h.update(std::as_bytes(std::span(b.data(),static_cast<std::size_t>(f.gcount()))));}check(f.eof(),"hash read");return h.finish();}
/** Return lowercase hexadecimal text for digest d without retaining its reference. */
std::string hex(const Digest&d){
    // Expand each digest byte into high and low hexadecimal digits.
/* Hexadecimal alphabet. */ constexpr char digits[]="0123456789abcdef";/* Output digest spelling. */ std::string s;for(/* Digest byte being formatted. */ auto b:d){/* Unsigned byte value for nibble formatting. */ auto n=std::to_integer<unsigned>(b);s+=digits[n>>4];s+=digits[n&15];}return s;}
/** Decode exactly 64 hexadecimal characters s into a 32-byte digest; malformed fixture data fails the test. */
Digest digest(const std::string&s){
    // Check the spelling length and parse each two-character byte.
check(s.size()==64,"hex digest size");/* Decoded 32-byte digest. */ Digest d;for(/* Byte, event or additional-run index according to the local loop. */ unsigned i=0;i<32;++i)d[i]=std::byte(std::stoul(s.substr(i*2,2),nullptr,16));return d;}
/** Small independent in-memory inspector used by the shared-original-role regression. Owns complete test bytes and reads only canonical header/directory/uint64/JSON records; intentionally not a bounded production reader. */ struct Inspection {
    /** Owned full test archive used by this independent inspector. */ std::vector<char> bytes;/** Byte offset of the canonical section directory. */ std::uint64_t directory{};
    /** Read test archive p into owned bytes and cache its directory offset. Used only for small independent structural assertions, not as a production reader. */
explicit Inspection(const std::filesystem::path&p){
    // Load the complete test file, then decode the header directory address.
/* Binary input stream owned by this helper. */ std::ifstream f(p,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(f),{});directory=integer(32);}
    /** Decode one little-endian uint64 at byte offset at in owned bytes; asserts the full eight-byte extent. */
std::uint64_t integer(std::uint64_t at){
    // Check the range and assemble the integer from eight unsigned bytes.
check(at+8<=bytes.size(),"inspection extent");/* Decoded integer accumulator. */ std::uint64_t n=0;for(/* Byte, event or additional-run index according to the local loop. */ unsigned i=0;i<8;++i)n|=std::uint64_t(static_cast<unsigned char>(bytes[at+i]))<<(8*i);return n;}
    /** Return the payload offset stored in directory entry section, using canonical 64-byte entry layout. */
std::uint64_t offset(std::uint64_t section){
    // Locate and decode the offset field of the requested directory entry.
return integer(directory+64*section+16);}
    /** Parse the bytes of metadata section into owned JSON; asserts its payload extent first. */
Json json(std::uint64_t section){
    // Read directory offset/size, check the range, then parse that exact span.
/* Section payload offset; size is its recorded byte extent. */ auto at=offset(section),size=integer(directory+64*section+24);check(at+size<=bytes.size(),"metadata extent");return Json::parse(bytes.begin()+at,bytes.begin()+at+size);}
    /** Read uint64 element i from the section described by JSON descriptor d. Used for section-reference columns. */
std::uint64_t value(const Json&d,std::uint64_t i){
    // Resolve the descriptor section and decode its indexed eight-byte value.
return integer(offset(d.at("section"))+i*8);}
};
/** Durable donor descriptor returned by make_spool and consumed by bundle tests. Owns its path, immutable producer binding and conservative run plan; does not retain numerical histories or live writer sessions. */ struct Saved {/** Final donor spool path. */ std::filesystem::path path;/** Expected file/authored digests and immutable producer/dependency identity. */ SpoolBinding binding;/** Owned run catalog identity and conservative section/file/source bounds. */ BundleRunPlan plan;bool diagnostics{}, ///< Whether the donor requires run-diagnostics-1.
fixed{}; ///< Whether the donor requires timeseries-zfp-fixed-accuracy-1.
/** Donor original-input retention state. */ SectionState original{};};
/** Create a fresh single-run donor under dir with owned id/kind and requested feature flags. original selects indexed original-document state; returns its immutable binding and conservative bundle plan after producer histories are destroyed. */
Saved make_spool(const std::filesystem::path&dir,std::string id,RunKind kind,bool indexed,bool diagnostics,bool fixed,SectionState original){
    // Build deterministic numerical values, optional profiles and event rows.
    // Prepare and write the selected legacy/indexed provenance representation, then finalize and emit expected sidecars.
    // Bind actual bytes/digest to the authored input and return the independent donor description.

    /* Owned donor values/provenance surviving through finalization. */ SyntheticCase c(2,kind==RunKind::timeseries?32:3,kind);c.description.writer_version="0.2.0";c.description.run.run_id=id;c.description.indexed_provenance=indexed;c.description.maximum_log_sources=2;c.limits.provenance_bytes_bound=65536;
    c.info.accepted_count=c.axis.size()-1;c.info.last_accepted_value=c.axis.back();c.info.requested_end=c.axis.back();
    if(kind==RunKind::steady_state){c.info.requested_begin=c.axis.front();c.info.identity_axis_convention="accepted_level_zero_is_initial";for(/* Sample or accepted-level index. */ std::size_t i=0;i<c.identities.size();++i)c.identities[i]=i;}
    else for(/* Sample or accepted-level index. */ std::size_t i=0;i<c.axis.size();++i)c.data[2*i+1]=.123456789*i+.3333333333333333;
    if(fixed){c.description.run.profiles={CodecProfile::reversible(),take(CodecProfile::fixed_accuracy(1e-4),"fixed")};c.description.run.row_profile_index={0,1};}
    /* Owned event rows; traces borrows them during append. */ std::array<DiagnosticRecord,2> events{};std::array<DiagnosticTraceView,1> traces{};
    if(diagnostics){/* Run-event declaration with a bounded name dictionary. */ DiagnosticTraceDescription d;d.kind=DiagnosticKind::run_events;d.event_names={"producer.started","producer.completed"};d.record_capacity=2;c.description.run.diagnostics.push_back(d);
        for(/* Byte, event or additional-run index according to the local loop. */ unsigned i=0;i<2;++i){events[i].record_index=i;events[i].sequence=i;events[i].event_name_index=i;events[i].severity=1;}
        traces[0].kind=d.kind;traces[0].records=DiagnosticRecords::memory(events);traces[0].availability=DiagnosticAvailability::included;traces[0].capture_status=DiagnosticCaptureStatus::complete;traces[0].counts_present=true;traces[0].eligible_count=2;
    }
    /* Owned prepared bundle/export plan, depending on local scope. */ auto prepared=take(PreparedExport::prepare(c.description,c.limits),"spool prepare");/* Owned codec used while producing the donor. */ auto codec=take(Codec::create(c.limits.frame_values),"codec");/* Fresh spool/bundle destination for this local case. */ auto path=dir/(id+".serene");/* Owned single-run donor writer. */ auto session=take(ArchiveSession::open(prepared,codec,path),"spool open");
    /* Borrowed donor numerical result view. */ auto v=c.view();if(diagnostics)v.diagnostics=traces;/* Legacy provenance view borrowing the SyntheticCase strings. */ auto legacy=c.provenance();
    if(indexed){/* Indexed five-role provenance view. */ IndexedProvenanceView provenance;provenance.authored_input_sha256=sha256(std::as_bytes(std::span(c.original.data(),c.original.size())));provenance.documents={legacy.inputs.source_json,legacy.inputs.effective_json,legacy.inputs.overrides_json,legacy.inputs.resources_json,legacy.context.execution_json};
        if(original!=SectionState::included)provenance.documents[0]=DocumentView{original,{},{}};
        /* Two log descriptors; one deliberately includes an empty source. */ std::array<LogsView,2> logs={legacy.logs,legacy.logs};/* Owned empty log source, distinct from unavailable. */ std::string second="";logs[1].bytes=ByteSource::text(second);provenance.logs=logs;
        good(session.append_run(v,provenance),"spool append indexed");
    }else{check(original==SectionState::included,"legacy original");good(session.append_run(v,legacy),"spool append legacy");}
    good(session.finalize(),"spool finalize");c.expected(dir/id);
    /* Durable donor metadata returned after writer completion. */ Saved s;s.path=path;s.original=original;s.diagnostics=diagnostics;s.fixed=fixed;
    s.binding={hash_file(path),sha256(std::as_bytes(std::span(c.original.data(),c.original.size()))),std::filesystem::file_size(path),id,"qualified-test-producer-0.2","zfp-1.0.1",true};
    s.plan={id,c.description.run.run_name,true,kind,prepared.plan().section_capacity,prepared.plan().maximum_archive_bytes,69};return s;
}
/** Build an owned bundle catalog from borrowed spools and a common original document. The returned original-input ByteSource borrows original through bundle creation; state controls retained bytes. */
BundleDescription description(std::span<const Saved> spools,const std::string& original,SectionState state=SectionState::included){
    // Fill bundle identity and authored digest, bind the common original according to state, and copy run plans in catalog order.

    /* Owned bundle metadata with a borrowed common-input ByteSource. */ BundleDescription d;d.file_id="00000000-0000-4000-8000-000000000008";d.created_utc="2026-11-08T00:00:00Z";d.authored_input_sha256=sha256(std::as_bytes(std::span(original.data(),original.size())));
    d.original_input={state,state==SectionState::included?ByteSource::text(original):ByteSource{},state==SectionState::included?"application/json":""};for(/* Selected donor whose run plan is copied. */ auto&s:spools)d.runs.push_back(s.plan);return d;
}
/** Return the union of diagnostic/fixed-accuracy feature flags across donor descriptions s; no donor ownership is retained. */
BundleFeatures features(std::span<const Saved>s){
    // Accumulate feature presence over the selected donor catalog.
/* Union of required optional features. */ BundleFeatures f;for(/* Donor descriptor being inspected or recorded. */ auto&v:s){f.diagnostics|=v.diagnostics;f.fixed_accuracy|=v.fixed;}return f;}
/** Verify and append donors s to out in explicit physical order. Each order entry is a catalog index; append consumes each verified spool handle while retaining on-disk sources for retry. */
void append(BundleSession&out,std::span<const Saved>s,std::span<const std::size_t>order){
    // Open each bound donor, append it at its catalog index, and check that the verified handle was consumed.
for(/* Catalog index selected by the requested physical order. */ auto i:order){/* Move-only verified donor consumed by append. */ auto spool=take(VerifiedSpool::open(s[i].path,s[i].binding),"verified open "+s[i].plan.run_id);good(out.append_verified_spool(i,spool),"bundle append "+s[i].plan.run_id);check(!spool.valid(),"append consumes handle");}}
/** Borrowed synchronous fault-hook state for one operation/ordinal. main retains it until session destruction; fired records whether the selected boundary was reached. */ struct Fault {/** Operation to reject. */ IoOperation operation;/** Exact operation ordinal to reject. */ std::uint64_t ordinal;/** Whether the selected fault has executed. */ bool fired{};/** Fault callback borrowing Fault p. Matching operation op and ordinal n mark fired and return synthetic I/O failure; all other calls succeed. */
static Status call(void*p,IoOperation op,std::uint64_t n)noexcept{
    // Compare the selected boundary, mark its invocation, and return the configured failure only there.
/* Fault context borrowed by the callback. */ auto&f=*static_cast<Fault*>(p);if(op==f.operation&&n==f.ordinal){f.fired=true;return error(StatusCode::io_failure,99);}return {};}};
/** Assert that dir contains no bundle-owned temporary filename after sessions leave scope. */
void no_temp(const std::filesystem::path&dir){
    // Scan the case directory and reject any remaining bundle temporary prefix.
for(/* Directory entry checked for leaked temporaries. */ const auto&e:std::filesystem::directory_iterator(dir))check(e.path().filename().string().find(".bundle-tmp-")==std::string::npos,"temporary cleaned");}
}
/** Run bounded bundle assembly, provenance retention and publication-fault checks. argv[1] may name a fresh directory or --auto; optional argv[2] supplies the shared-original-role donor binding. Returns zero or one on exception. */
int main(int argc,char**argv){
    // Create mixed static/exact/fixed donors, assemble in a different physical order and measure workspace.
    // Check retry, leases, missing/duplicate donors, bindings, mutation and original-retention unions.
    // Inspect shared source roles, test resource limits and injected publication failures, then write a receipt.
try{
    /* Fresh directory holding donor spools, bundles and case receipts. */ auto dir=argc>1&&std::string_view(argv[1])!="--auto"?std::filesystem::path(argv[1]):std::filesystem::current_path()/("bundle-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(!std::filesystem::exists(dir),"fresh path");std::filesystem::create_directories(dir);
    /* Common immutable original-input bytes for all selected donors. */ std::string original=SyntheticCase(0,0).original;
    /* Owned static, reversible and fixed donor descriptions. */ std::array<Saved,3>s={make_spool(dir,"static",RunKind::steady_state,false,true,false,SectionState::included),make_spool(dir,"exact",RunKind::timeseries,false,false,false,SectionState::included),make_spool(dir,"fixed",RunKind::timeseries,true,true,true,SectionState::included)};
    /* Physical copy order, deliberately distinct from catalog order. */ const std::array<std::size_t,3>order={2,0,1};/* Live requested-byte baseline before bundle preparation. */ auto measured_before=allocation_meter::start();/* Owned prepared bundle/export plan, depending on local scope. */ auto prepared=take(PreparedBundle::prepare(description(s,original)),"bundle prepare");
    /* Main mixed-feature bundle output pathname. */ auto bundle_path=dir/"mixed_bundle.serene";{/* Owning bundle session for this independent publication attempt. */ auto out=take(BundleSession::open(prepared,bundle_path,features(s)),"bundle open");append(out,s,order);good(out.finalize(),"bundle finalize");check(out.statistics().frames>0,"frame counts");check(std::filesystem::file_size(bundle_path)<=prepared.plan().maximum_archive_bytes,"final archive bound");}
    /* Peak requested C++ live-byte growth across prepare/save. */ auto measured_peak=allocation_meter::maximum_since(measured_before);check(measured_peak<=prepared.plan().workspace_bytes,"live requested C++ heap stays within planned bundle workspace");
    // Retained disk spools are sufficient for retry; all producers and their numerical histories are gone.
    {/* Owning bundle session for this independent publication attempt. */ auto out=take(BundleSession::open(prepared,dir/"retry.serene",features(s)),"retry open");append(out,s,order);good(out.finalize(),"retry finalize");check(hash_file(dir/"retry.serene")==hash_file(bundle_path),"deterministic retry bytes");}
    check(BundleSession::open(prepared,bundle_path,features(s)).error().code==StatusCode::publication_failure,"existing destination preserved");
    {/* Owning bundle session for this independent publication attempt. */ auto out=take(BundleSession::open(prepared,dir/"lease_first.serene",features(s)),"lease first");/* Expected overlapping-session lease rejection. */ auto second=BundleSession::open(prepared,dir/"lease_second.serene",features(s));check(!second&&second.error().code==StatusCode::invalid_state,"prepared bundle lease rejects overlapping sessions");}
    {/* Owning bundle session for this independent publication attempt. */ auto out=take(BundleSession::open(prepared,dir/"missing.serene",features(s)),"missing open");/* Move-only verified donor consumed by append. */ auto spool=take(VerifiedSpool::open(s[0].path,s[0].binding),"missing source");good(out.append_verified_spool(0,spool),"missing append");check(!out.finalize(),"missing catalog run rejected");}
    {/* Owning bundle session for this independent publication attempt. */ auto out=take(BundleSession::open(prepared,dir/"duplicate.serene",features(s)),"duplicate open");/* First verified donor for the duplicate-run case. */ auto a=take(VerifiedSpool::open(s[0].path,s[0].binding),"duplicate source a");good(out.append_verified_spool(0,a),"duplicate append a");/* Second verified donor for the duplicate-run case. */ auto b=take(VerifiedSpool::open(s[0].path,s[0].binding),"duplicate source b");check(!out.append_verified_spool(0,b),"duplicate run rejected");}
    /* Binding copy mutated to test digest/extent/trust admission. */ auto wrong=s[0].binding;wrong.authored_input_sha256={};check(!VerifiedSpool::open(s[0].path,wrong),"wrong authored digest rejected");wrong=s[0].binding;wrong.expected_file_bytes++;check(!VerifiedSpool::open(s[0].path,wrong),"wrong extent rejected");
    wrong=s[0].binding;wrong.trusted_manifest_binding=false;/* Expected unsupported import result for an untrusted binding. */ auto unbound=VerifiedSpool::open(s[0].path,wrong);check(!unbound&&unbound.error().code==StatusCode::unsupported_feature,"unbound external spool is unsupported initial import");
    {/* Bundle description with malformed included original JSON. */ auto bad=description(s,original);/* Owned malformed original bytes retained throughout open. */ std::string malformed="{";bad.original_input.bytes=ByteSource::text(malformed);bad.authored_input_sha256=sha256(std::as_bytes(std::span(malformed.data(),malformed.size())));/* Prepared bundle for this local retention/admission case. */ auto p=take(PreparedBundle::prepare(std::move(bad)),"malformed original plan");check(!BundleSession::open(p,dir/"malformed_original.serene",features(s)),"common included JSON validated before publication");}
    // Both post-verification mutation and truncation must fail before publishing.
    /* Independent donor copy mutated after verification. */ auto changed=dir/"mutated_spool.serene";std::filesystem::copy_file(s[2].path,changed);/* Move-only verified donor consumed by append. */ auto spool=take(VerifiedSpool::open(changed,s[2].binding),"mutation source");
    {/* Read/write stream for the intentional post-verification mutation. */ std::fstream f(changed,std::ios::binary|std::ios::in|std::ios::out);f.seekp(128);/* Single-byte mutation written inside the donor archive. */ char b=77;f.write(&b,1);check(bool(f),"mutation write");}
    {/* Owning bundle session for this independent publication attempt. */ auto out=take(BundleSession::open(prepared,dir/"mutation.serene",features(s)),"mutation open");check(!out.append_verified_spool(2,spool),"changed spool rejected");}
    /* Indexed donors distinguishing disabled from unavailable original input. */ std::array<Saved,2>absent={make_spool(dir,"disabled",RunKind::timeseries,true,false,false,SectionState::disabled),make_spool(dir,"unavailable",RunKind::timeseries,true,false,false,SectionState::unavailable)};
    /* Physical order for two-donor retention/shared-role cases. */ const std::array<std::size_t,2>pair_order={1,0};
    {/* Prepared bundle for this local retention/admission case. */ auto p=take(PreparedBundle::prepare(description(absent,original,SectionState::unavailable)),"union prepare");/* Owning bundle session for this independent publication attempt. */ auto out=take(BundleSession::open(p,dir/"unavailable_union.serene"),"union open");append(out,absent,pair_order);good(out.finalize(),"unavailable union");}
    {/* One-donor view over the disabled original case. */ std::span<const Saved>one(absent.data(),1);/* Only catalog index in a one-run bundle. */ std::array<std::size_t,1>first={0};/* Prepared bundle for this local retention/admission case. */ auto p=take(PreparedBundle::prepare(description(one,original,SectionState::disabled)),"disabled prepare");/* Owning bundle session for this independent publication attempt. */ auto out=take(BundleSession::open(p,dir/"disabled_union.serene"),"disabled open");append(out,one,first);good(out.finalize(),"disabled union");}
    {/* One-donor view over the disabled original case. */ std::span<const Saved>one(absent.data(),1);/* Only catalog index in a one-run bundle. */ std::array<std::size_t,1>first={0};/* Prepared bundle for this local retention/admission case. */ auto p=take(PreparedBundle::prepare(description(one,original)),"extra retention prepare");/* Owning bundle session for this independent publication attempt. */ auto out=take(BundleSession::open(p,dir/"unrequested_retention.serene"),"extra retention open");append(out,one,first);check(!out.finalize(),"all-disabled does not silently retain original");}
    {/* Bundle description explicitly requesting common original retention. */ auto d=description(absent,original);d.explicitly_retain_original=true;/* Prepared bundle for this local retention/admission case. */ auto p=take(PreparedBundle::prepare(std::move(d)),"explicit prepare");/* Owning bundle session for this independent publication attempt. */ auto out=take(BundleSession::open(p,dir/"explicit_retention.serene"),"explicit open");append(out,absent,pair_order);good(out.finalize(),"explicit retention");}
    if(argc>2){
        /* External independent shared-original donor manifest. */ std::filesystem::path binding_path=argv[2];/* JSON binding stream. */ std::ifstream input(binding_path);/* Owned shared-original donor manifest. */ Json b;input>>b;/* Shared-role donor description reconstructed from its binding. */ Saved shared;shared.path=binding_path.parent_path()/b.at("file").get<std::string>();
        shared.binding={digest(b.at("sha256")),digest(b.at("authored_sha256")),b.at("bytes"),b.at("run_id"),b.at("producer_identity"),b.at("dependency_identity"),true};
        shared.plan={b.at("run_id"),b.at("run_name"),true,RunKind::timeseries,b.at("section_count"),b.at("bytes"),b.at("source_count")};
        /* Regular donor plus independently authored shared-role donor. */ std::array<Saved,2> donors={s[0],shared};/* Prepared bundle for this local retention/admission case. */ auto p=take(PreparedBundle::prepare(description(donors,original)),"shared original plan");/* Fresh spool/bundle destination for this local case. */ auto path=dir/"shared_original_roles.serene";
        {/* Owning bundle session for this independent publication attempt. */ auto out=take(BundleSession::open(p,path,features(donors)),"shared original open");append(out,donors,pair_order);good(out.finalize(),"shared original finalize");}
        /* Owned independent structural view of the resulting bundle. */ Inspection inspect(path);/* Root JSON metadata decoded through the section directory. */ auto root=inspect.json(inspect.integer(48));/* Second catalog run metadata. */ auto run=inspect.json(inspect.value(root.at("results").at("index").at("run_metadata_section"),1));/* Indexed provenance reference descriptors for that run. */ auto refs=run.at("provenance").at("references");/* Original/effective source indices after merge. */ auto role0=inspect.value(refs.at("source_index"),0),role1=inspect.value(refs.at("source_index"),1);
        /* Root source-catalog metadata-section descriptor. */ auto catalog=root.at("provenance").at("sources").at("metadata_section");/* Merged original source and distinct effective source metadata. */ auto original_source=inspect.json(inspect.value(catalog,role0)),effective_source=inspect.json(inspect.value(catalog,role1));
        check(role0!=role1&&original_source.at("document").at("state")==0&&effective_source.at("document").at("state")==1,"only original role replaced when source identity is shared");
    }
    /* Bundle limits one byte below the prepared workspace requirement. */ auto tiny=BundleLimits{};tiny.workspace_budget_bytes=prepared.plan().workspace_bytes-1;/* Expected resource-limit admission failure. */ auto limited=PreparedBundle::prepare(description(s,original),tiny);check(!limited&&limited.error().code==StatusCode::resource_limit,"workspace admission");
    /* Bundle description enlarged only in catalog population. */ auto more=description(s,original);for(/* Byte, event or additional-run index according to the local loop. */ unsigned i=0;i<20;++i){/* Owned run-plan copy given a fresh catalog identity. */ auto r=more.runs[0];r.run_id="additional_"+std::to_string(i);more.runs.push_back(std::move(r));}/* Prepared plan for the enlarged catalog. */ auto larger=take(PreparedBundle::prepare(std::move(more)),"larger catalog");check(larger.plan().spool_workspace_bytes==prepared.plan().spool_workspace_bytes&&larger.plan().copy_bytes==prepared.plan().copy_bytes&&larger.plan().workspace_bytes>prepared.plan().workspace_bytes,"one spool workspace plus budgeted catalog scaling");
    /* Number of exercised failure/publication race cases. */ unsigned fault_cases=0;for(/* I/O boundary selected for failure injection. */ auto op:{IoOperation::source,IoOperation::write,IoOperation::flush,IoOperation::close,IoOperation::publish,IoOperation::seek}){
        /* Local callback configuration retained through session destruction. */ Fault f{op,op==IoOperation::write?5ull:0ull};/* Fresh spool/bundle destination for this local case. */ auto path=dir/("fault_"+std::to_string(static_cast<unsigned>(op))+".serene");{
            /* Owning bundle session for this independent publication attempt. */ auto out=BundleSession::open(prepared,path,features(s),{&f,Fault::call});
            if(out){/* Whether append already stopped before finalization. */ bool failed=false;for(/* Catalog index selected by the requested physical order. */ auto i:order){/* Fresh verified donor for the current injected-failure append. */ auto input=take(VerifiedSpool::open(s[i].path,s[i].binding),"fault source");/* Append result deciding whether finalization is attempted. */ auto status=out->append_verified_spool(i,input);if(!status){failed=true;break;}}if(!failed)check(!out->finalize(),"fault finalize");}
        }check(f.fired&&!std::filesystem::exists(path),"fault unpublished");++fault_cases;
    }
    {/* Fresh spool/bundle destination for this local case. */ auto path=dir/"short_write.serene";/* Hook that forces an actual physical short write. */ FaultHook hook;hook.write_limit=[](void*,std::uint64_t n,std::size_t count)noexcept{ /* Reduce the second write by one byte; count is the requested extent. */ return n==1?count-1:count;};/* Owning bundle session for this independent publication attempt. */ auto out=BundleSession::open(prepared,path,features(s),hook);check(!out&&!std::filesystem::exists(path),"short write rejects open without publication");++fault_cases;}
    {/* Fresh spool/bundle destination for this local case. */ auto path=dir/"destination_race.serene";/* Owning bundle session for this independent publication attempt. */ auto out=take(BundleSession::open(prepared,path,features(s)),"race open");append(out,s,order);{/* Competing final-file writer used to test no-clobber publication. */ std::ofstream winner(path);winner<<"existing destination";}/* Digest of the competing destination that must remain unchanged. */ auto winner_hash=hash_file(path);check(out.finalize().code==StatusCode::publication_failure&&hash_file(path)==winner_hash,"destination race never clobbers winner");++fault_cases;}
    no_temp(dir);/* Owned summary of bundle identities, bounds, ordering and fault coverage. */ Json receipt{{"schema",1},{"status","passed"},{"mixed_bundle",bundle_path.filename().string()},{"bundle_sha256",hex(hash_file(bundle_path))},{"catalog_order",{"static","exact","fixed"}},{"physical_order",{"fixed","static","exact"}},{"fault_cases",fault_cases},{"workspace_bytes",prepared.plan().workspace_bytes},{"directory_bytes",prepared.plan().directory_bytes},{"catalog_bytes",prepared.plan().catalog_bytes},{"spool_workspace_bytes",prepared.plan().spool_workspace_bytes},{"maximum_archive_bytes",prepared.plan().maximum_archive_bytes},{"larger_catalog_workspace_bytes",larger.plan().workspace_bytes},{"spools",Json::array()}};
    receipt["measured_peak_cpp_bytes"]=measured_peak;
    for(/* Donor descriptor being inspected or recorded. */ auto&v:s)receipt["spools"].push_back({{"path",v.path.filename().string()},{"run_id",v.plan.run_id},{"sha256",hex(v.binding.expected_file_sha256)},{"bytes",v.binding.expected_file_bytes}});std::ofstream(dir/"receipt.json")<<receipt.dump(2)<<'\n';
    std::cout<<"Bundle assembly, exact encoded-copy, manifest, retention, retry, quota and "<<fault_cases<<" publication fault cases passed: "<<dir.string()<<'\n';return 0;
}catch(/* First failed assertion or production operation. */ const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
