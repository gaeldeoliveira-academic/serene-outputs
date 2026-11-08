#include "../src/spool_reader.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
// Test-only live C++ allocation accounting. Codec C workspace is separately
// admitted by the production validator using Codec::workspace_bytes().
namespace heap_measure {
struct Header {void* base;std::size_t bytes;};
std::atomic<std::size_t> live{},peak{};
void* allocate(std::size_t n,std::size_t align) {
    align=std::max(align,alignof(Header));
    if(n>SIZE_MAX-sizeof(Header)-align)throw std::bad_alloc();
    auto base=std::malloc(n+sizeof(Header)+align);
    if(!base)throw std::bad_alloc();
    auto address=(reinterpret_cast<std::uintptr_t>(base)+sizeof(Header)+align-1)&~(align-1);
    auto* header=reinterpret_cast<Header*>(address)-1;*header={base,n};
    auto now=live.fetch_add(n)+n,previous=peak.load();while(now>previous&&!peak.compare_exchange_weak(previous,now)){}
    return reinterpret_cast<void*>(address);
}
void release(void* ptr) noexcept {if(ptr){auto* h=static_cast<Header*>(ptr)-1;live.fetch_sub(h->bytes);std::free(h->base);}}
}
void* operator new(std::size_t n){return heap_measure::allocate(n,alignof(std::max_align_t));}
void* operator new[](std::size_t n){return ::operator new(n);}
void* operator new(std::size_t n,std::align_val_t a){return heap_measure::allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p) noexcept{heap_measure::release(p);}
void operator delete[](void*p) noexcept{heap_measure::release(p);}
void operator delete(void*p,std::size_t) noexcept{heap_measure::release(p);}
void operator delete[](void*p,std::size_t) noexcept{heap_measure::release(p);}
void operator delete(void*p,std::align_val_t) noexcept{heap_measure::release(p);}
void operator delete[](void*p,std::align_val_t) noexcept{heap_measure::release(p);}
void operator delete(void*p,std::size_t,std::align_val_t) noexcept{heap_measure::release(p);}
void operator delete[](void*p,std::size_t,std::align_val_t) noexcept{heap_measure::release(p);}
using namespace serene::outputs;
using namespace serene::outputs::detail;
Digest digest(const std::string&s) {
    if(s.size()!=64)throw std::runtime_error("digest length");Digest d;
    for(std::size_t i=0;i<32;++i)d[i]=static_cast<std::byte>(std::stoul(s.substr(i*2,2),nullptr,16));return d;
}
void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
int main(int argc,char**argv) {
    try {
        check(argc==2,"expected manifest path");std::filesystem::path manifest_path=argv[1];
        std::ifstream stream(manifest_path);Json manifest;stream>>manifest;
        unsigned passed=0,failed=0;std::uint64_t small_workspace=0,large_workspace=0,small_peak=0,large_peak=0;
        Json receipt={{"schema",1},{"scope","private bounded spool validation; independent single-run fixtures"},{"cases",Json::array()}};
        receipt["original_helper_cases"]=Json::array();
        auto original_case=[&](const char* name,std::string_view bytes,std::string_view media,StatusCode expected,SpoolReadLimits limits={},bool wrong_digest=false){
            DocumentView document{SectionState::included,ByteSource::text(bytes),media};auto hash=sha256(std::as_bytes(std::span(bytes.data(),bytes.size())));if(wrong_digest)hash={};
            auto before=heap_measure::live.load();heap_measure::peak.store(before);auto result=validate_original_document(document,hash,limits);auto peak=heap_measure::peak.load()-before;
            auto actual=result?StatusCode::ok:result.error().code;bool pass=actual==expected&&(!result||peak<=*result);if(pass)++passed;else{++failed;std::cerr<<"original helper "<<name<<": "<<status_name(actual)<<'\n';}
            receipt["original_helper_cases"].push_back({{"name",name},{"actual",status_name(actual)},{"pass",pass},{"peak_cpp_bytes",peak},{"workspace_bound",result?*result:0}});
        };
        original_case("object","{\"key\":1}","application/json",StatusCode::ok);
        original_case("array_value","[null,true,\"\\u0000\"]","application/json",StatusCode::ok);
        original_case("empty_binary","","application/octet-stream",StatusCode::ok);
        original_case("nonjson_binary","{incomplete","application/octet-stream",StatusCode::ok);
        original_case("empty_json","","application/json",StatusCode::invalid_contract);
        original_case("incomplete_json","{\"key\":","application/json",StatusCode::invalid_contract);
        original_case("duplicate_key","{\"key\":1,\"k\\u0065y\":2}","application/json",StatusCode::invalid_contract);
        original_case("utf8_bom","\xEF\xBB\xBF{}","application/json",StatusCode::invalid_contract);
        original_case("embedded_nul",std::string_view("{}\0hidden",9),"application/json",StatusCode::invalid_contract);
        original_case("wrong_digest","{}","application/json",StatusCode::verification_failure,{},true);
        auto limited=SpoolReadLimits{};limited.document_bytes=1;original_case("document_size","{}","application/json",StatusCode::resource_limit,limited);
        limited={};limited.workspace_budget_bytes=1024;original_case("workspace","{}","application/json",StatusCode::resource_limit,limited);
        limited={};limited.document_parser_budget_bytes=1;original_case("parser_budget","{}","application/json",StatusCode::resource_limit,limited);
        for(const auto& test:manifest.at("cases")) {
            auto path=manifest_path.parent_path()/test.at("file").get<std::string>();
            if(test.value("mutation",false)) {
                auto copy=path;copy+=std::string(".mutation-")+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                std::filesystem::copy_file(path,copy);path=std::move(copy);
            }
            SpoolBinding binding;binding.expected_file_sha256=digest(test.at("sha256"));binding.authored_input_sha256=digest(test.at("authored_sha256"));binding.expected_file_bytes=test.at("bytes");binding.expected_run_id=test.at("run_id");binding.producer_identity="independent-spool-author/v1";binding.dependency_identity="public-zfpy1.0.1.post0/pinned-zfp1.0.1";binding.trusted_manifest_binding=test.at("trusted");
            if(test.value("wrong_binding_digest",false))binding.expected_file_sha256={};
            if(test.value("wrong_binding_extent",false))++binding.expected_file_bytes;
            if(test.value("wrong_authored_digest",false))binding.authored_input_sha256={};
            SpoolReadLimits limits;limits.workspace_budget_bytes=test.value("workspace",limits.workspace_budget_bytes);limits.metadata_budget_bytes=test.value("metadata",limits.metadata_budget_bytes);
            auto before=heap_measure::live.load();heap_measure::peak.store(before);
            auto result=VerifiedSpoolData::open(path,binding,limits);
            auto peak=heap_measure::peak.load()-before;
            auto code=result?StatusCode::ok:result.error().code;auto actual=status_name(code);
            bool success=actual==test.at("expected").get<std::string>();
            if(result) {
                success&=peak<=result->summary.workspace_bytes;
                success&=bool(result->verify_unchanged());
                std::array<std::byte,1> byte{};success&=!result->read(result->directory.size(),0,byte);
                success&=!result->read(0,result->directory[0].size,byte);
                if(test.value("mutation",false)) {
                    std::fstream mutate(path,std::ios::in|std::ios::out|std::ios::binary);mutate.seekp(128);mutate.write("!",1);mutate.flush();mutate.close();
                    success&=result->verify_unchanged().code==StatusCode::verification_failure;
                }
                if(test.value("memory_small",false)){small_workspace=result->summary.workspace_bytes;small_peak=peak;}
                if(test.value("memory_large",false)){large_workspace=result->summary.workspace_bytes;large_peak=peak;}
            }
            Json row={{"file",test.at("file")},{"expected",test.at("expected")},{"actual",actual},{"pass",success},{"peak_cpp_bytes",peak}};
            if(result){row["workspace_bound"]=result->summary.workspace_bytes;row["retained_metadata_bytes"]=result->summary.retained_metadata_bytes;}
            else row["operation"]=result.error().operation;
            receipt["cases"].push_back(std::move(row));
            if(success)++passed;else {++failed;std::cerr<<test.at("file")<<": expected "<<test.at("expected")<<", actual "<<actual<<", peak "<<peak<<(result?", succeeded":"")<<'\n';}
        }
        // Histories grow by2048x; the same bounded section/cursor graph does not.
        auto bounded=small_workspace&&large_workspace&&large_workspace<=small_workspace+65536&&large_peak<=small_peak+65536;
        if(!bounded){++failed;std::cerr<<"history memory bound: "<<small_workspace<<'/'<<large_workspace<<", heap "<<small_peak<<'/'<<large_peak<<'\n';}
        receipt["passed"]=passed;receipt["failed"]=failed;receipt["history_growth_bounded"]=bounded;
        receipt["small_workspace"]=small_workspace;receipt["large_workspace"]=large_workspace;receipt["small_heap_peak"]=small_peak;receipt["large_heap_peak"]=large_peak;
#ifdef _DEBUG
        const char* configuration="Debug";
#else
        const char* configuration="Release";
#endif
        receipt["configuration"]=configuration;
        auto output=manifest_path.parent_path()/(std::string("reader-receipt-")+configuration+".json");
        std::ofstream report(output);report<<receipt.dump(2)<<'\n';report.close();
        std::cout<<"spool-reader "<<passed<<" passed, "<<failed<<" failed; receipt "<<output<<'\n';return failed?1:0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
