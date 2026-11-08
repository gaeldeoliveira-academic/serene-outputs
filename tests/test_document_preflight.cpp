#include <serene/outputs/bundle.hpp>
#include <serene/outputs/document.hpp>
#include "allocation_meter.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace serene::outputs;
namespace {
void check(bool value,const char*message){if(!value)throw std::runtime_error(message);}
Digest hash_file(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);Sha256 hash;std::array<char,4096> buffer;while(f){f.read(buffer.data(),buffer.size());hash.update(std::as_bytes(std::span(buffer.data(),static_cast<std::size_t>(f.gcount()))));}check(f.eof(),"hash file");return hash.finish();}
struct Source {std::string_view text;mutable unsigned reads{};bool fail{};static Status read(const void*p,std::uint64_t off,std::span<std::byte>out)noexcept{const auto&s=*static_cast<const Source*>(p);++s.reads;if(s.fail)return error(StatusCode::io_failure,97);if(off>s.text.size()||out.size()>s.text.size()-off)return error(StatusCode::invalid_contract,97);std::memcpy(out.data(),s.text.data()+off,out.size());return {};}};
StatusCode verify_document(const std::filesystem::path&p,DocumentView target,SpoolLimits limits={}){
    ArchiveDescription d;d.file_id="00000000-0000-4000-8000-000000000014";d.created_utc="2026-11-08T00:00:00Z";d.run.run_id="document_test";d.run.assembly_ref="assembly";d.indexed_provenance=true;d.maximum_log_sources=0;
    ExportLimits export_limits;export_limits.provenance_bytes_bound=1048576;auto prepared=PreparedExport::prepare(d,export_limits);auto codec=Codec::create(export_limits.frame_values);check(bool(prepared)&&bool(codec),"prepare spool");
    IndexedProvenanceView provenance;provenance.documents[0]={SectionState::included,ByteSource::text("{}"),"application/json"};provenance.authored_input_sha256=sha256(std::as_bytes(std::span("{}",2)));provenance.documents[1]=target;
    RunResultView v;v.info.observation=ObservationStatus::disabled;v.info.logging=LoggingStatus::disabled;v.values=*ValueView::strided({},0,0,0,0);
    {auto session=ArchiveSession::open(*prepared,*codec,p);check(bool(session),"open spool");check(bool(session->append_run(v,provenance))&&bool(session->finalize()),"write intentionally unvalidated document");}
    SpoolBinding binding{hash_file(p),provenance.authored_input_sha256,std::filesystem::file_size(p),d.run.run_id,"document-preflight-test","pinned-zfp1.0.1",true};
    auto verified=VerifiedSpool::open(p,binding,limits);return verified?StatusCode::ok:verified.error().code;
}
}
int main(int argc,char**argv){try{
    auto dir=argc>1?std::filesystem::path(argv[1]):std::filesystem::current_path()/("document-preflight-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(!std::filesystem::exists(dir),"fresh path");std::filesystem::create_directories(dir);
    unsigned cases=0,equivalence=0;
    auto run=[&](const char*name,std::string_view bytes,SectionState state,const char*media,StatusCode expected,bool compare=true,DocumentPreflightLimits limits={}){
        Source source{bytes};ByteSource input{{},&source,Source::read,bytes.size()};DocumentView document{state,input,media};auto before=allocation_meter::start();auto result=preflight_document(document,limits);auto peak=allocation_meter::maximum_since(before);auto actual=result?StatusCode::ok:result.error().code;
        if(actual!=expected)throw std::runtime_error(std::string(name)+": "+std::string(status_name(actual)));
        if(result){check(peak<=result->workspace_bytes,"reported workspace covers live C++ allocation");check(result->source_bytes==bytes.size(),"source extent result");if(state==SectionState::included||state==SectionState::partial)check(result->sha256==sha256(std::as_bytes(std::span(bytes.data(),bytes.size()))),"returned byte digest");else check(result->kind==DocumentPreflightKind::inactive&&source.reads==0,"inactive source is unread");
            if(state==SectionState::included&&std::string_view(media)=="application/json")check(result->kind==DocumentPreflightKind::json_document&&result->parser_charge_bytes,"JSON admission result");
            else if(state==SectionState::included||state==SectionState::partial)check(result->kind==DocumentPreflightKind::retained_bytes&&result->parser_charge_bytes==0,"raw retention is not complete JSON evidence");}
        if(compare){SpoolLimits spool_limits;spool_limits.document_bytes=limits.document_bytes;spool_limits.document_parser_budget_bytes=limits.document_parser_budget_bytes;check(verify_document(dir/(std::string(name)+".serene"),document,spool_limits)==actual,"public/actual spool document admission equivalence");++equivalence;}
        ++cases;return result?*result:DocumentPreflight{};
    };
    auto valid=run("object","{\"key\":1}",SectionState::included,"application/json",StatusCode::ok);
    run("array","[null,true,\"\\u0000\"]",SectionState::included,"application/json",StatusCode::ok);
    run("scalar","123",SectionState::included,"application/json",StatusCode::ok);
    run("empty","",SectionState::included,"application/json",StatusCode::invalid_contract);
    run("incomplete","{\"key\":",SectionState::included,"application/json",StatusCode::invalid_contract);
    run("duplicate","{\"key\":1,\"k\\u0065y\":2}",SectionState::included,"application/json",StatusCode::invalid_contract);
    run("bom","\xEF\xBB\xBF{}",SectionState::included,"application/json",StatusCode::invalid_contract);
    run("nul",std::string_view("{}\0extra",8),SectionState::included,"application/json",StatusCode::invalid_contract);
    run("truncated_utf8",std::string_view("\"\xF0\x9F\"",4),SectionState::included,"application/json",StatusCode::invalid_contract);
    run("nonfinite","1e999",SectionState::included,"application/json",StatusCode::invalid_contract);
    run("partial","{\"key\":",SectionState::partial,"application/json",StatusCode::ok);
    run("partial_utf8",std::string_view("\xF0\x9F",2),SectionState::partial,"application/json",StatusCode::ok);
    run("disabled","",SectionState::disabled,"application/json",StatusCode::ok);
    run("unavailable","",SectionState::unavailable,"application/json",StatusCode::ok);
    run("binary_empty","",SectionState::included,"application/octet-stream",StatusCode::ok,false);
    run("binary_partial","not JSON",SectionState::partial,"application/octet-stream",StatusCode::ok,false);
    auto exact=DocumentPreflightLimits{};exact.document_bytes=9;exact.document_parser_budget_bytes=valid.parser_charge_bytes;
    run("exact_parser","{\"key\":1}",SectionState::included,"application/json",StatusCode::ok,true,exact);
    --exact.document_parser_budget_bytes;run("short_parser","{\"key\":1}",SectionState::included,"application/json",StatusCode::resource_limit,true,exact);
    exact={};exact.document_bytes=8;run("short_bytes","{\"key\":1}",SectionState::included,"application/json",StatusCode::resource_limit,true,exact);
    exact={};exact.workspace_budget_bytes=valid.workspace_bytes;run("exact_workspace","{\"key\":1}",SectionState::included,"application/json",StatusCode::ok,false,exact);
    --exact.workspace_budget_bytes;run("short_workspace","{\"key\":1}",SectionState::included,"application/json",StatusCode::resource_limit,false,exact);
    exact={};exact.metadata_budget_bytes=valid.source_bytes+32+valid.parser_charge_bytes;run("exact_metadata","{\"key\":1}",SectionState::included,"application/json",StatusCode::ok,false,exact);
    --exact.metadata_budget_bytes;run("short_metadata","{\"key\":1}",SectionState::included,"application/json",StatusCode::resource_limit,false,exact);
    Source failing{"{}",0,true};DocumentView io{SectionState::included,ByteSource{{},&failing,Source::read,2},"application/json"};auto failure=preflight_document(io);check(!failure&&failure.error().code==StatusCode::io_failure&&failure.error().operation==97,"source IO error propagates");++cases;
    failing.reads=0;io.bytes.size=4294967297ull;auto extent=preflight_document(io);check(!extent&&extent.error().code==StatusCode::resource_limit&&failing.reads==0,"extent rejected before source read/allocation");++cases;
    io.state=SectionState::disabled;io.bytes.size=1;check(!preflight_document(io)&&failing.reads==0,"inactive nonzero extent rejected");++cases;
    io.bytes.size=0;auto inactive=preflight_document(io);check(bool(inactive)&&failing.reads==0,"inactive callback never invoked");++cases;
    auto invalid_limits=DocumentPreflightLimits{};invalid_limits.copy_tile_bytes=0;io.state=SectionState::included;io.bytes.size=2;check(!preflight_document(io,invalid_limits)&&failing.reads==0,"invalid tile rejected before source");++cases;
    std::ofstream receipt(dir/"receipt.json");receipt<<"{\"status\":\"passed\",\"cases\":"<<cases<<",\"actual_spool_equivalence_cases\":"<<equivalence<<"}\n";
    std::cout<<"document preflight "<<cases<<" checks; "<<equivalence<<" actual-spool equivalence cases passed: "<<dir.string()<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
