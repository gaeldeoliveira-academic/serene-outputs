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
/** Hidden ownership/charge record for this executable's allocation overrides. Only allocate/release inspect it; no producer or file data is stored. */ struct Header {/** Original malloc allocation before alignment. */ void* base;/** Requested payload bytes included in live accounting. */ std::size_t bytes;};
/* Current requested C++ live bytes and maximum within the measurement window. */ std::atomic<std::size_t> live{},peak{};
/** Allocate n requested bytes with power-of-two align for test-only global new. Returns aligned owned storage or throws bad_alloc; tracks requested live bytes, not allocator or codec C overhead. */
void* allocate(std::size_t n,std::size_t align) {
    // Reserve header/alignment space with overflow checks and allocate the backing block.
    // Store its ownership and payload charge, then update current/peak live counts.

    align=std::max(align,alignof(Header));
    if(n>SIZE_MAX-sizeof(Header)-align)throw std::bad_alloc();
    /* Owned malloc backing block. */ auto base=std::malloc(n+sizeof(Header)+align);
    if(!base)throw std::bad_alloc();
    /* Aligned payload address following the accounting header. */ auto address=(reinterpret_cast<std::uintptr_t>(base)+sizeof(Header)+align-1)&~(align-1);
    /* Header directly preceding the aligned payload. */ auto* header=reinterpret_cast<Header*>(address)-1;*header={base,n};
    /* Updated live charge; previous is the peak compare/exchange observation. */ auto now=live.fetch_add(n)+n,previous=peak.load();while(now>previous&&!peak.compare_exchange_weak(previous,now)){}
    return reinterpret_cast<void*>(address);
}
/** Free ptr previously returned by allocate, accepting null. Recovers ownership/charged bytes from the preceding hidden header. */
void release(void* ptr) noexcept {
    // Deduct the stored requested-byte charge and free the original backing block.
if(ptr){/* Stored allocation header recovered during delete. */ auto* h=static_cast<Header*>(ptr)-1;live.fetch_sub(h->bytes);std::free(h->base);}}
}
/** Allocate n bytes through the test heap meter; a specifies required alignment in aligned forms. Array forms use the scalar path. Returns owned storage or throws bad_alloc. */
void* operator new(std::size_t n){ /* Forward to the matching tracked allocation/release primitive. */ return heap_measure::allocate(n,alignof(std::max_align_t));}
/** Allocate n bytes through the test heap meter; a specifies required alignment in aligned forms. Array forms use the scalar path. Returns owned storage or throws bad_alloc. */
void* operator new[](std::size_t n){ /* Forward to the matching tracked allocation/release primitive. */ return ::operator new(n);}
/** Allocate n bytes through the test heap meter; a specifies required alignment in aligned forms. Array forms use the scalar path. Returns owned storage or throws bad_alloc. */
void* operator new(std::size_t n,std::align_val_t a){ /* Forward to the matching tracked allocation/release primitive. */ return heap_measure::allocate(n,static_cast<std::size_t>(a));}
/** Allocate n bytes through the test heap meter; a specifies required alignment in aligned forms. Array forms use the scalar path. Returns owned storage or throws bad_alloc. */
void* operator new[](std::size_t n,std::align_val_t a){ /* Forward to the matching tracked allocation/release primitive. */ return ::operator new(n,a);}
/** Release p through the heap meter; null is accepted and sized/aligned metadata is ignored because the stored header owns the charge/base. */
void operator delete(void*p) noexcept{ /* Forward to the matching tracked allocation/release primitive. */ heap_measure::release(p);}
/** Release p through the heap meter; null is accepted and sized/aligned metadata is ignored because the stored header owns the charge/base. */
void operator delete[](void*p) noexcept{ /* Forward to the matching tracked allocation/release primitive. */ heap_measure::release(p);}
/** Release p through the heap meter; null is accepted and sized/aligned metadata is ignored because the stored header owns the charge/base. */
void operator delete(void*p,std::size_t) noexcept{ /* Forward to the matching tracked allocation/release primitive. */ heap_measure::release(p);}
/** Release p through the heap meter; null is accepted and sized/aligned metadata is ignored because the stored header owns the charge/base. */
void operator delete[](void*p,std::size_t) noexcept{ /* Forward to the matching tracked allocation/release primitive. */ heap_measure::release(p);}
/** Release p through the heap meter; null is accepted and sized/aligned metadata is ignored because the stored header owns the charge/base. */
void operator delete(void*p,std::align_val_t) noexcept{ /* Forward to the matching tracked allocation/release primitive. */ heap_measure::release(p);}
/** Release p through the heap meter; null is accepted and sized/aligned metadata is ignored because the stored header owns the charge/base. */
void operator delete[](void*p,std::align_val_t) noexcept{ /* Forward to the matching tracked allocation/release primitive. */ heap_measure::release(p);}
/** Release p through the heap meter; null is accepted and sized/aligned metadata is ignored because the stored header owns the charge/base. */
void operator delete(void*p,std::size_t,std::align_val_t) noexcept{ /* Forward to the matching tracked allocation/release primitive. */ heap_measure::release(p);}
/** Release p through the heap meter; null is accepted and sized/aligned metadata is ignored because the stored header owns the charge/base. */
void operator delete[](void*p,std::size_t,std::align_val_t) noexcept{ /* Forward to the matching tracked allocation/release primitive. */ heap_measure::release(p);}
using namespace serene::outputs;
using namespace serene::outputs::detail;
/** Decode exactly 64 hexadecimal characters s into a digest; malformed fixture data throws. */
Digest digest(const std::string&s) {
    // Check the expected SHA-256 spelling length and decode each byte pair.

    if(s.size()!=64)throw std::runtime_error("digest length");/* Decoded expected SHA-256 bytes. */ Digest d;
    for(/* Digest byte index. */ std::size_t i=0;i<32;++i)d[i]=static_cast<std::byte>(std::stoul(s.substr(i*2,2),nullptr,16));return d;
}
/** Throw borrowed message if good is false. */
void check(bool good,const char* message){
    // Route failed assertions through main's exception handler.
if(!good)throw std::runtime_error(message);}
/** Run private bounded spool validation from manifest argv[1]. Returns one for any case/memory-bound failure, otherwise zero; writes a configuration-specific receipt next to the input manifest. */
int main(int argc,char**argv) {
    // Probe original-document helper semantics and admission charges.
    // Open every independently authored spool with its immutable binding and requested limits, measure live bytes, and test post-verification mutations.
    // Compare small/large-history workspace growth and emit exact per-case outcomes.

    try {
        check(argc==2,"expected manifest path");/* Independent spool manifest supplied by the caller. */ std::filesystem::path manifest_path=argv[1];
        /* Manifest JSON input stream. */ std::ifstream stream(manifest_path);/* Owned independently authored test case list. */ Json manifest;stream>>manifest;
        /* Pass/fail totals for helper and spool cases. */ unsigned passed=0,failed=0;/* Reported workspace and measured live peaks for the paired history-size cases. */ std::uint64_t small_workspace=0,large_workspace=0,small_peak=0,large_peak=0;
        /* Owned per-case evidence accumulated for this invocation. */ Json receipt={{"schema",1},{"scope","private bounded spool validation; independent single-run fixtures"},{"cases",Json::array()}};
        receipt["original_helper_cases"]=Json::array();
        /** Check one original-document case from borrowed bytes/media under limits. expected defines admission status; wrong_digest deliberately corrupts its binding. Updates enclosing counts/receipt, retaining no borrowed data afterward. */
auto original_case=[&](const char* name,std::string_view bytes,std::string_view media,StatusCode expected,SpoolReadLimits limits={},bool wrong_digest=false){
    // Bind document bytes and the selected digest, then measure helper admission.
    // Compare status and workspace bound and append the outcome to the receipt.

            /* Included original document borrowing the current case bytes. */ DocumentView document{SectionState::included,ByteSource::text(bytes),media};/* Expected original-input digest, optionally deliberately corrupted. */ auto hash=sha256(std::as_bytes(std::span(bytes.data(),bytes.size())));if(wrong_digest)hash={};
            /* Live requested-byte baseline before validation. */ auto before=heap_measure::live.load();heap_measure::peak.store(before);/* Original helper or full spool validation result. */ auto result=validate_original_document(document,hash,limits);/* Additional requested live C++ bytes during the bounded call. */ auto peak=heap_measure::peak.load()-before;
            /* Observed status name/code normalized across expected results. */ auto actual=result?StatusCode::ok:result.error().code;/* Whether the helper status and admitted workspace match expectations. */ bool pass=actual==expected&&(!result||peak<=*result);if(pass)++passed;else{++failed;std::cerr<<"original helper "<<name<<": "<<status_name(actual)<<'\n';}
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
        /* Limits reduced for individual admission-failure probes. */ auto limited=SpoolReadLimits{};limited.document_bytes=1;original_case("document_size","{}","application/json",StatusCode::resource_limit,limited);
        limited={};limited.workspace_budget_bytes=1024;original_case("workspace","{}","application/json",StatusCode::resource_limit,limited);
        limited={};limited.document_parser_budget_bytes=1;original_case("parser_budget","{}","application/json",StatusCode::resource_limit,limited);
        for(/* Current owned manifest case borrowed during this iteration. */ const auto& test:manifest.at("cases")) {
            /* Spool path, or an isolated mutable copy for mutation tests. */ auto path=manifest_path.parent_path()/test.at("file").get<std::string>();
            if(test.value("mutation",false)) {
                /* Unique donor copy protecting the original fixture bytes. */ auto copy=path;copy+=std::string(".mutation-")+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                std::filesystem::copy_file(path,copy);path=std::move(copy);
            }
            /* Exact file/authored hashes, extent and trusted producer identity from the manifest. */ SpoolBinding binding;binding.expected_file_sha256=digest(test.at("sha256"));binding.authored_input_sha256=digest(test.at("authored_sha256"));binding.expected_file_bytes=test.at("bytes");binding.expected_run_id=test.at("run_id");binding.producer_identity="independent-spool-author/v1";binding.dependency_identity="public-zfpy1.0.1.post0/pinned-zfp1.0.1";binding.trusted_manifest_binding=test.at("trusted");
            if(test.value("wrong_binding_digest",false))binding.expected_file_sha256={};
            if(test.value("wrong_binding_extent",false))++binding.expected_file_bytes;
            if(test.value("wrong_authored_digest",false))binding.authored_input_sha256={};
            /* Per-case resource budgets overriding the defaults when requested. */ SpoolReadLimits limits;limits.workspace_budget_bytes=test.value("workspace",limits.workspace_budget_bytes);limits.metadata_budget_bytes=test.value("metadata",limits.metadata_budget_bytes);
            /* Live requested-byte baseline before validation. */ auto before=heap_measure::live.load();heap_measure::peak.store(before);
            /* Original helper or full spool validation result. */ auto result=VerifiedSpoolData::open(path,binding,limits);
            /* Additional requested live C++ bytes during the bounded call. */ auto peak=heap_measure::peak.load()-before;
            /* Production status code from full spool validation. */ auto code=result?StatusCode::ok:result.error().code;/* Observed status name/code normalized across expected results. */ auto actual=status_name(code);
            /* Combined per-case status, workspace and post-open invariant verdict. */ bool success=actual==test.at("expected").get<std::string>();
            if(result) {
                success&=peak<=result->summary.workspace_bytes;
                success&=bool(result->verify_unchanged());
                /* One-byte destination used to probe invalid section/range reads. */ std::array<std::byte,1> byte{};success&=!result->read(result->directory.size(),0,byte);
                success&=!result->read(0,result->directory[0].size,byte);
                if(test.value("mutation",false)) {
                    /* Read/write stream for deliberate post-verification mutation. */ std::fstream mutate(path,std::ios::in|std::ios::out|std::ios::binary);mutate.seekp(128);mutate.write("!",1);mutate.flush();mutate.close();
                    success&=result->verify_unchanged().code==StatusCode::verification_failure;
                }
                if(test.value("memory_small",false)){small_workspace=result->summary.workspace_bytes;small_peak=peak;}
                if(test.value("memory_large",false)){large_workspace=result->summary.workspace_bytes;large_peak=peak;}
            }
            /* Per-case result stored in the receipt. */ Json row={{"file",test.at("file")},{"expected",test.at("expected")},{"actual",actual},{"pass",success},{"peak_cpp_bytes",peak}};
            if(result){row["workspace_bound"]=result->summary.workspace_bytes;row["retained_metadata_bytes"]=result->summary.retained_metadata_bytes;}
            else row["operation"]=result.error().operation;
            receipt["cases"].push_back(std::move(row));
            if(success)++passed;else {++failed;std::cerr<<test.at("file")<<": expected "<<test.at("expected")<<", actual "<<actual<<", peak "<<peak<<(result?", succeeded":"")<<'\n';}
        }
        // Histories grow by2048x; the same bounded section/cursor graph does not.
        /* Whether increasing retained history leaves workspace/peak growth within 64 KiB. */ auto bounded=small_workspace&&large_workspace&&large_workspace<=small_workspace+65536&&large_peak<=small_peak+65536;
        if(!bounded){++failed;std::cerr<<"history memory bound: "<<small_workspace<<'/'<<large_workspace<<", heap "<<small_peak<<'/'<<large_peak<<'\n';}
        receipt["passed"]=passed;receipt["failed"]=failed;receipt["history_growth_bounded"]=bounded;
        receipt["small_workspace"]=small_workspace;receipt["large_workspace"]=large_workspace;receipt["small_heap_peak"]=small_peak;receipt["large_heap_peak"]=large_peak;
#ifdef _DEBUG
        /* Compile configuration used only to distinguish receipt paths. */ const char* configuration="Debug";
#else
        /* Compile configuration used only to distinguish receipt paths. */ const char* configuration="Release";
#endif
        receipt["configuration"]=configuration;
        /* Configuration-specific receipt pathname. */ auto output=manifest_path.parent_path()/(std::string("reader-receipt-")+configuration+".json");
        /* Receipt output stream. */ std::ofstream report(output);report<<receipt.dump(2)<<'\n';report.close();
        std::cout<<"spool-reader "<<passed<<" passed, "<<failed<<" failed; receipt "<<output<<'\n';return failed?1:0;
    }catch(/* First fatal fixture or test exception. */ const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
