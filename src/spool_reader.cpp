#include "spool_reader.hpp"
#include "json_contract.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <utility>
#ifdef _WIN32
#include <io.h>
#include <share.h>
#endif
namespace serene::outputs::detail {
namespace {
/** Private exception carrier used only to unwind admission to a Status-returning boundary. Owns the exact first lower-layer status and no external resources. */
struct Failure { Status status; ///< Structured failure preserved across helper unwinding.
};
/** Require yes or throw private Failure with code/op. Used only inside admission routines whose public boundary translates exceptions to Status. */
void need(bool yes,StatusCode code=StatusCode::invalid_contract,std::uint32_t op=80) {
    // Translate a failed predicate into the supplied structured status without replacing successful control flow.

    if(!yes)throw Failure{error(code,op)};
}
/** Propagate a failed status by throwing the private Failure carrier; successful status has no effect. */
void okay(Status status) {
    // Preserve the exact lower-layer status when unwinding to the public error boundary.
if(!status)throw Failure{status};}
/** Decode borrowed little-endian bytes b into uint64; internal callers supply at most eight bytes. No storage is retained. */
std::uint64_t le(std::span<const std::byte> b) noexcept {
    // Accumulate unsigned byte contributions at increasing eight-bit shifts.

    /* Unsigned little-endian accumulator. */ std::uint64_t n=0;for(/* Byte position and corresponding eight-bit shift. */ std::size_t i=0;i<b.size();++i)n|=std::uint64_t(std::to_integer<unsigned char>(b[i]))<<(8*i);return n;
}
/** Return whether v is the canonical positive binary64 zero, excluding negative zero. */
bool positive_zero(double v) noexcept {
    // Check both numerical equality to zero and its sign bit.
return v==0&&!std::signbit(v);}
/** Check borrowed s for the canonical lowercase 36-byte UUID spelling used by container metadata. Does not impose a UUID version policy. */
bool valid_uuid(std::string_view s) noexcept {
    // Require exact length and separator positions, then admit only lowercase hexadecimal digits.

    if(s.size()!=36)return false;
    for(/* Character position checked against separator or hex-digit grammar. */ std::size_t i=0;i<s.size();++i)if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}
    else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;
    return true;
}
/** Check borrowed UTC timestamp spelling/calendar fields, including optional nonempty fractional seconds and terminal Z. Returns false for malformed dates or leap-second values. */
bool valid_utc(std::string_view s) noexcept {
    // Check fixed separators and parse numeric calendar/time fields.
    // Apply month/leap-year day bounds, then validate optional fractional digits.

    if(s.size()<20||s[4]!='-'||s[7]!='-'||s[10]!='T'||s[13]!=':'||s[16]!=':'||s.back()!='Z')return false;
    /* Borrowed substring decoder: start is byte offset, n is digit count; -1 marks nondigits. */ auto number=[&](std::size_t start,std::size_t n){ /* Decode the requested decimal substring, returning -1 on nondigits. */ /* Decimal field accumulator. */ int v=0;for(/* Digit position in a fixed field or fractional-second suffix. */ std::size_t i=0;i<n;++i){/* ASCII digit being admitted. */ char c=s[start+i];if(c<'0'||c>'9')return -1;v=v*10+c-'0';}return v;};
    /* Parsed year/month/day/hour/minute/second fields before calendar checks. */ auto y=number(0,4),m=number(5,2),d=number(8,2),h=number(11,2),minutes=number(14,2),seconds=number(17,2);
    if(y<0||m<1||m>12||d<1||h<0||h>23||minutes<0||minutes>59||seconds<0||seconds>59)return false;
    /* Nonleap month lengths; February is adjusted by the Gregorian rule. */ constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(d>days[m-1]+(m==2&&y%4==0&&(y%100!=0||y%400==0)))return false;
    if(s.size()>20){if(s[19]!='.'||s.size()<22)return false;for(/* Digit position in a fixed field or fractional-second suffix. */ std::size_t i=20;i<s.size()-1;++i)if(s[i]<'0'||s[i]>'9')return false;}
    return true;
}
/** Return nonnegative integer JSON j as uint64 up to limit. Reject floats, negatives and excess range through Failure. */
std::uint64_t uint(const Json& j,std::uint64_t limit=UINT64_MAX) {
    // Require integer representation, convert to uint64, then enforce the caller limit.

    need(j.is_number_unsigned() || (j.is_number_integer()&&j.get<std::int64_t>()>=0));
    /* Admitted integer value before its caller-specific upper bound. */ auto n=j.get<std::uint64_t>();need(n<=limit);return n;
}
/** Read required property key from object j and apply integer admission with limit. Choose this overload for named metadata fields. */
std::uint64_t uint(const Json& j,const char* key,std::uint64_t limit=UINT64_MAX) {
    // Resolve the required property and delegate scalar admission.
return uint(j.at(key),limit);}
/** Return finite numeric JSON j as binary64; rejects non-numeric or nonfinite conversion through Failure. */
double real(const Json& j) {
    // Check numeric representation, convert, then reject nonfinite results.
need(j.is_number());/* Numeric JSON converted to binary64 before finiteness admission. */ auto v=j.get<double>();need(std::isfinite(v));return v;}
/** Read required numeric property key from j and return its finite double value. Choose this overload for named fields. */
double real(const Json& j,const char* key) {
    // Resolve the property and reuse scalar finite-number admission.
return real(j.at(key));}
/** Return an owned UTF-8 string copied from JSON j; nonempty additionally rejects empty text. No parser borrow survives. */
std::string text(const Json& j,bool nonempty=false) {
    // Require string representation, copy it, then validate UTF-8 and optional nonemptiness.

    need(j.is_string());/* Owned string copy validated before returning it. */ auto s=j.get<std::string>();need(valid_utf8(s)&&(!nonempty||!s.empty()));return s;
}
/** Return owned UTF-8 property key from j, optionally requiring nonempty content. Named-property counterpart of scalar text(). */
std::string text(const Json& j,const char* key,bool nonempty=false) {
    // Resolve the required property and reuse scalar string admission.
return text(j.at(key),nonempty);}
/** Read integer presence flag key from j; only wire integers zero and one are admitted. Returns the Boolean interpretation. */
bool flag(const Json& j,const char* key) {
    // Validate the closed integer flag range before converting to bool.
return uint(j,key,1)!=0;}
/** Require object j to have exactly the borrowed property names, with no omissions or extras. Does not alter JSON. */
void keys(const Json& j,std::initializer_list<const char*> names) {
    // Check object/cardinality, then prove every required property exists.

    need(j.is_object()&&j.size()==names.size());for(/* Required key from the exact allowed set. */ auto name:names)need(j.contains(name));
}
/// Ordered six static residual/constraint/increment columns, matching DiagnosticRecord::metrics.
constexpr const char* metric_names[]={"force_residual_N","moment_residual_Nm","constraint_translation_m","constraint_rotation_rad","increment_translation_m","increment_rotation_rad"};
/** Conservative validation working-set accounting used before allocations. Retained graph, scoped temporary metadata and sequential nonmetadata buffers are distinct; check() maintains their admitted high-water mark. */
struct Budget {
    std::uint64_t metadata_limit{}, ///< Maximum retained plus temporary metadata charge in bytes.
    workspace_limit{}, ///< Maximum total metadata plus working-storage charge in bytes.
    retained{}, ///< Current persistent graph charge in bytes.
    temporary{}, ///< Current scoped metadata/parser charge in bytes.
    workspace{}, ///< Current nonmetadata control/I/O/codec charge in bytes.
    peak{}; ///< Largest successfully admitted total charge in bytes.
/** Check current retained/temporary/workspace charges against both limits and update peak. Throws resource_limit on checked-add overflow or insufficient budget. */
    void check() {
    // Add metadata charges safely, add nonmetadata workspace, enforce both ceilings, and retain the largest admitted total.
/* Checked metadata subtotal and total active workspace charge. */ std::uint64_t a{},b{};need(checked_add(retained,temporary,a)&&a<=metadata_limit&&checked_add(a,workspace,b)&&b<=workspace_limit,StatusCode::resource_limit);peak=std::max(peak,b);}
/** Add n bytes to retained metadata charge and recheck ceilings. This charge persists for the validated graph lifetime. */
    void retain(std::uint64_t n) {
    // Checked-add the retained charge, then validate combined limits.
need(checked_add(retained,n,retained),StatusCode::resource_limit);check();}
/** Add n temporary metadata bytes and recheck ceilings; enclosing TempScope restores this charge when the activity finishes. */
    void temp(std::uint64_t n) {
    // Checked-add temporary charge before the corresponding allocation and validate limits.
need(checked_add(temporary,n,temporary),StatusCode::resource_limit);check();}
/** Add n nonmetadata working bytes and recheck aggregate workspace. Callers explicitly restore workspace for sequential codec activities. */
    void work(std::uint64_t n) {
    // Checked-add working storage and validate the combined high-water mark.
need(checked_add(workspace,n,workspace),StatusCode::resource_limit);check();}
};
/** Nested lifetime guard for temporary metadata admission, used around parser/pool/validation activities. Borrows its Budget and restores only the temporary subtotal on destruction. */
struct TempScope { Budget& b; ///< Borrowed enclosing accounting state.
std::uint64_t saved; ///< Temporary charge at scope entry.
/** Borrow budget x and save its current temporary charge; enclosing operations may add temporary charges until scope exit. */
explicit TempScope(Budget& x):b(x),saved(x.temporary){
    // Retain the baseline temporary charge for this nested lifetime.
}/** Restore the borrowed budget's temporary charge to the entry value; peak and retained charges remain unchanged. */
~TempScope(){
    // Release only this scope's temporary admission charge.
b.temporary=saved;} };
/** Return checked a*b for an untrusted byte/count product, throwing resource_limit on overflow. */
std::uint64_t product(std::uint64_t a,std::uint64_t b) {
    // Compute through the shared checked arithmetic helper before returning the product.
/* Checked multiplication result. */ std::uint64_t out{};need(checked_multiply(a,b,out),StatusCode::resource_limit);return out;}
// Admission for a second retained copy of already-bounded metadata. Walk before
// copying; never serialize the object merely to estimate its allocation cost.
/** Compute a conservative charge for a second owned copy of already-bounded JSON j. Call only after lexical/complexity admission; does not serialize or allocate a duplicate tree. */
std::uint64_t copy_charge(const Json& j) {
    // Charge one value, then recurse through string bytes, array entries or object keys and children.

    /* Accumulated conservative charge for the copied subtree. */ std::uint64_t charge=128;
    if(j.is_string())charge+=4*j.get_ref<const std::string&>().size();
    else if(j.is_array()){charge+=16*j.size();for(/* Array child borrowed from already-admitted metadata. */ const auto& child:j)charge+=copy_charge(child);}
    else if(j.is_object())for(/* Object member iterator exposing owned key and child value. */ auto it=j.begin();it!=j.end();++it)charge+=128+4*it.key().size()+copy_charge(it.value());
    return charge;
}
// The document scanner bounds complexity before any general JSON materialization. It is
// deliberately conservative on decoded string size (encoded bytes upper-bound scalar count).
/** Scan borrowed retained-document bytes s for a conservative JSON parser charge without materializing a tree. Rejects BOM/control/token/complexity excess; strict_parse later proves complete JSON grammar and decoded uniqueness. */
std::uint64_t document_charge(std::string_view s) {
    // Track quoted/escaped regions while counting value, member and container structure.
    // Bound nesting, token lengths and populations before general parsing.
    // Require closed lexical state and return the conservative byte/count/depth charge.

    need(!s.starts_with("\xEF\xBB\xBF"));
    /* Lexical value/member/comma counts, current nesting and maximum nesting. */ std::uint64_t values=0,members=0,entries=0,depth=0,maximum=0;
    /* Quoted-string/escape state; escaped protects the next encoded byte. */ bool in_string=false,escaped=false;/* Length of the current unquoted scalar token. */ std::size_t token=0;
    for(/* Input byte cursor. */ std::size_t i=0;i<s.size();++i) {
        /* Current byte without signed-char ambiguity. */ unsigned char c=static_cast<unsigned char>(s[i]);
        if(in_string) {
            if(escaped){escaped=false;continue;}if(c=='\\'){escaped=true;continue;}if(c=='"'){in_string=false;continue;}need(c>=32);continue;
        }
        if(c=='"'){in_string=true;++values;token=0;}
        else if(c=='{'||c=='['){++values;++depth;maximum=std::max(maximum,depth);need(depth<=128,StatusCode::resource_limit);token=0;}
        else if(c=='}'||c==']'){need(depth>0);--depth;token=0;}
        else if(c==':'){++members;token=0;}
        else if(c==','){++entries;token=0;}
        else if(c==' '||c=='\t'||c=='\r'||c=='\n'){token=0;}
        else {need(c>=32);if(!token)++values;++token;need(token<=128,StatusCode::resource_limit);}
        need(values<=1048576&&members<=1048576,StatusCode::resource_limit);
    }
    need(!in_string&&!escaped&&!depth);
    return 9*s.size()+128*values+128*members+16*entries+256*maximum;
}
/** Return an owned strictly parsed JSON value from bytes. metadata selects the narrower object/UTF-8 metadata scanner; object_only requires an object root; budget limits parser charge and charge receives that estimate. Rejects decoded duplicate keys and nonfinite floats. */
Json strict_parse(std::string_view bytes,bool metadata,bool object_only,std::uint64_t budget,std::uint64_t& charge) {
    // Preflight the selected grammar/complexity profile and calculate its charge.
    // Use a per-object decoded-key stack during parsing to detect duplicates and nonfinite values.
    // Reject discarded parses or a wrong root shape and return the independent owned tree.

    if(metadata) {
        okay(json_contract::preflight(bytes,budget));/* Restricted metadata scanner reproducing the admitted complexity counts. */ json_contract::Scan scan{bytes};scan.value(0);
        charge=5*bytes.size()+4*scan.strings+128*scan.values+128*scan.members+16*scan.array_entries+256*scan.depth;
    } else {charge=document_charge(bytes);need(charge<=budget,StatusCode::resource_limit);}
    /* Whether any decoded key repeats within its own object. */ bool duplicate=false;/* Per-open-object decoded-key sets; nested objects have independent uniqueness. */ std::vector<std::set<std::string>> stack;
    /* Parser callback: unnamed depth is unused; event identifies the parser activity and value is its borrowed decoded value. Returns true to retain every parsed value, while borrowing duplicate/stack only synchronously. */ auto cb=[&](int,nlohmann::json::parse_event_t event,Json& value) {
        // Maintain decoded key sets per object; reject nonfinite numeric values while keeping parser callbacks active.
        if(event==Json::parse_event_t::object_start)stack.emplace_back();
        else if(event==Json::parse_event_t::object_end){need(!stack.empty());stack.pop_back();}
        else if(event==Json::parse_event_t::key){need(!stack.empty());if(!stack.back().insert(value.get<std::string>()).second)duplicate=true;}
        if(event==Json::parse_event_t::value&&value.is_number_float())need(std::isfinite(value.get<double>()));
        return true;
    };
    /* Owned parsed tree; discarded marks syntax failure. */ auto j=Json::parse(bytes,cb,false);need(!j.is_discarded()&&!duplicate&&(!object_only||j.is_object()));return j;
}
// One document grammar/complexity admission operation for public preflight, actual spool
// document reads and common-original validation. Byte loading is caller-specific and budgeted.
/** Admit complete document raw through the shared document scanner/parser. Adds its charge to mutable budget, enforces parser_limit, and returns the charge; object_only narrows the root when requested. Caller owns byte-loading and TempScope lifetime. */
std::uint64_t admit_document_json(std::string_view raw,Budget& budget,std::uint64_t parser_limit,bool object_only=false) {
    // Compute and admit the document charge before constructing JSON.
    // Parse the exact bytes with shared strict semantics and discard the temporary tree.

    /* Conservative complete-document parser charge. */ const auto charge=document_charge(raw);need(charge<=parser_limit,StatusCode::resource_limit);
    budget.temp(charge);/* Charge recomputed by the shared strict parser for the already-admitted bytes. */ std::uint64_t measured{};(void)strict_parse(raw,false,object_only,charge,measured);return charge;
}
}
/** Private read-only file owner for VerifiedSpoolData. Owns binding/limits and one reusable I/O tile; file cursor and tile are single-threaded mutable state. Destruction closes the file after all validation/merge reads finish. */
struct VerifiedSpoolData::Impl {
    std::FILE* file{}; ///< Owned read-only FILE, null until open succeeds.
    SpoolBinding binding; ///< Owned immutable byte/producer identity used by later rechecks.
    SpoolReadLimits limits; ///< Owned limits governing this file working set.
    std::uint64_t size{}; ///< File length observed at open; later recheck also measures actual extent.
    std::vector<std::byte> tile; ///< Reusable bounded file/hash buffer, independent of history length.
/** Close the owned FILE if present; destructor cleanup ignores fclose errors because no publication occurs here. */
    ~Impl(){
    // Release the private read-only file handle on owner destruction.
if(file)std::fclose(file);}
/** Fill output span out from absolute file offset after checking its extent against size. Mutates the shared FILE cursor; throws contract or operation-81 I/O Failure. */
    void read_at(std::uint64_t offset,std::span<std::byte> out) {
    // Check the full range, seek with the platform wide-offset API, then require an exact byte read.

        need(offset<=size&&out.size()<=size-offset);
#ifdef _WIN32
        need(_fseeki64(file,static_cast<__int64>(offset),SEEK_SET)==0,StatusCode::io_failure,81);
#else
        need(fseeko(file,static_cast<off_t>(offset),SEEK_SET)==0,StatusCode::io_failure,81);
#endif
        need(std::fread(out.data(),1,out.size(),file)==out.size(),StatusCode::io_failure,81);
    }
/** Return actual open-file length after seeking to its end. Mutates the FILE cursor and throws operation-81 I/O failure on seek/tell error. */
    std::uint64_t extent() {
    // Seek to file end, query its signed extent and reject negative tell results.

#ifdef _WIN32
        need(_fseeki64(file,0,SEEK_END)==0,StatusCode::io_failure,81);/* Signed file-end offset returned by the platform tell function. */ auto n=_ftelli64(file);
#else
        need(fseeko(file,0,SEEK_END)==0,StatusCode::io_failure,81);/* Signed file-end offset returned by the platform tell function. */ auto n=ftello(file);
#endif
        need(n>=0,StatusCode::io_failure,81);return static_cast<std::uint64_t>(n);
    }
/** Hash exactly count bytes from absolute offset using the owned reusable tile. Returns SHA-256, mutates the FILE cursor/tile, and throws on invalid extent or I/O failure. */
    Digest hash(std::uint64_t offset,std::uint64_t count) {
    // Stream successive bounded tiles into SHA-256 until the requested extent is exhausted.

        /* Incremental digest for the requested file extent. */ Sha256 h;while(count){/* Bytes in the next bounded file/hash tile. */ auto n=std::min<std::uint64_t>(count,tile.size());read_at(offset,std::span(tile).first(n));h.update(std::span(tile).first(n));offset+=n;count-=n;}return h.finish();
    }
};
/** Ephemeral complete-spool admission pipeline used by VerifiedSpoolData::open. Borrows the unpublished owner and its file; owns section/source-used bitmaps and resource accounting. Header, graph, values, traces and provenance are checked before finish exposes the owner to a merger. */
struct SpoolValidator {
    VerifiedSpoolData& out; ///< Unpublished output graph built during admission.
    VerifiedSpoolData::Impl& file; ///< Borrowed file, binding and reusable tile.
    Budget budget; ///< Retained/scoped/working memory charge ledger.
    std::vector<std::byte> owned, ///< One bit per directory section; rejects repeated ownership and orphans.
    source_used; ///< One bit per indexed source; rejects unreferenced source descriptors.
    std::uint64_t minor{}, ///< Admitted format minor version, zero or one.
    catalog_channels{}, ///< Admitted scalar-channel catalog population.
    committed{}, ///< Retained sample/accepted-level population from run.info.
    accepted{}; ///< Known accepted driver-advance population from run.info.
    bool feature_d{}, ///< Declared run-diagnostics extension token.
    feature_p{}, ///< Declared indexed-provenance extension token.
    feature_f{}, ///< Declared fixed-accuracy extension token.
    used_f{}; ///< Whether an admitted profile actually uses fixed accuracy.
    bool partial_original{}; ///< Valid partial legacy original evidence encountered; initial merger import later reports unsupported capability.
    /** Borrowed typed column cursor with an owned 4 KiB tile. Copies retain independent caches but borrow the same validator/file; used only during open validation. Scalar bits()/real() never materialize a full history. */
    struct Array {
        SpoolValidator* owner{}; ///< Borrowed validator/file owner, alive through cursor use.
        std::uint64_t section{}, ///< Directory section ID supplying the column bytes.
        count{}, ///< Scalar or canonical frame-record population.
        width{}; ///< Bytes per column element or frame record.
        std::uint32_t kind{}; ///< Exact admitted section-kind code.
        std::array<std::uint64_t,2> shape{}; ///< Up to two logical dimensions, in wire order.
        unsigned rank{}; ///< Number of active shape dimensions, one or two.
        mutable std::array<std::byte,4096> cache{}; ///< Reusable aligned column-byte tile.
        mutable std::uint64_t cache_begin{UINT64_MAX}, ///< Section-relative tile start; UINT64_MAX marks an empty cache.
        cache_size{}; ///< Number of populated bytes in the current cache tile.
/** Read element index as raw unsigned bits from this bounded array cursor. Requires width<=8; refreshes mutable 4 KiB cache on a miss and returns little-endian bits. Owner/file must outlive the cursor. */
        std::uint64_t bits(std::uint64_t index) const {
    // Check logical index and derive its byte offset.
    // On cache miss, read one aligned bounded tile; decode the selected element from cached bytes.

            need(index<count&&width<=8);/* Section-relative byte offset of the indexed scalar. */ auto off=product(index,width);
            if(cache_begin==UINT64_MAX||off<cache_begin||off+width>cache_begin+cache_size) {
                cache_begin=(off/cache.size())*cache.size();cache_size=std::min<std::uint64_t>(cache.size(),owner->out.directory[section].size-cache_begin);
                okay(owner->out.read(section,cache_begin,std::span(cache).first(cache_size)));
            }
            return le(std::span(cache).subspan(static_cast<std::size_t>(off-cache_begin),static_cast<std::size_t>(width)));
        }
/** Read f64 element index by exact bit reinterpretation; rejects a cursor of any other section kind. No finiteness policy is implied here. */
        double real(std::uint64_t index) const {
    // Require f64 storage and bit-cast the raw element word.
need(kind==5);return std::bit_cast<double>(bits(index));}
    };
/** Borrow already-open owner x and initialize its limits/working reserve. x and its Impl must remain alive through the full validation pipeline; this helper never escapes open(). */
    explicit SpoolValidator(VerifiedSpoolData& x):out(x),file(*x.impl_),budget{file.limits.metadata_budget_bytes,file.limits.workspace_budget_bytes} {
    // Charge the reusable file tile and fixed stack/cursor/parser-control reserve before graph admission.

        budget.work(file.tile.capacity()+262144); // bounded stack/cursor/parser control reserve
    }
/** Claim exclusive ownership of section id in the local bitmap. Rejects out-of-range IDs and any alias already claimed by another descriptor. */
    void mark(std::uint64_t id) {
    // Locate the section bit, reject a prior claim, then set it.

        need(id<out.directory.size());/* Ownership bitmap byte containing this section. */ auto& b=owned[id/8];/* Single section bit that must not already be claimed. */ auto bit=std::byte(1u<<(id%8));need((b&bit)==std::byte{});b|=bit;
    }
/** Return whether already-range-checked directory index id has been claimed. Used by finish to reject orphan sections. */
    bool is_marked(std::uint64_t id) const {
    // Inspect the corresponding ownership bitmap bit.
return (owned[id/8]&std::byte(1u<<(id%8)))!=std::byte{};}
/** Admit typed array descriptor j for exact dtype spelling type. Optional expected dimensions require exact rank/shape; returns a borrowed bounded cursor and claims its section once. */
    Array array(const Json& j,const char* type,std::initializer_list<std::uint64_t> expected={}) {
    // Validate descriptor keys, dtype, directory kind and bounded rank/dimensions.
    // Check byte extent and any expected shape, then mark the section and return a cursor.

        keys(j,{"section","dtype","shape"});/* Owned descriptor dtype spelling. */ auto tag=text(j,"dtype");need(tag==type);
        /* Canonical section-kind code selected from dtype. */ std::uint32_t kind=0;/* Bytes per scalar or canonical frame record. */ std::uint64_t width=0;
        if(tag=="u8"){kind=2;width=1;}else if(tag=="u32"){kind=3;width=4;}else if(tag=="u64"){kind=4;width=8;}else if(tag=="f64"){kind=5;width=8;}else if(tag=="frames77"){kind=6;width=77;}else if(tag=="i8"){kind=7;width=1;}else if(tag=="i32"){kind=8;width=4;}else need(false);
        /* Directory section index referenced by the descriptor. */ auto id=uint(j,"section");need(id<out.directory.size()&&out.directory[id].kind==kind);
        /* Borrowed logical dimension array in the descriptor. */ const auto& shape=j.at("shape");need(shape.is_array()&&!shape.empty()&&shape.size()<=2);
        /* Result cursor borrowing this validator and owning a bounded cache. */ Array a;a.owner=this;a.section=id;a.kind=kind;a.width=width;a.rank=static_cast<unsigned>(shape.size());a.count=1;
        for(/* Dimension index used for shape/product checks. */ unsigned i=0;i<a.rank;++i){a.shape[i]=uint(shape[i]);need(a.shape[i]<=(1ull<<40),StatusCode::resource_limit);a.count=product(a.count,a.shape[i]);}
        need(product(a.count,width)==out.directory[id].size);if(expected.size()){need(expected.size()==a.rank);/* Dimension index used for shape/product checks. */ unsigned i=0;for(/* Expected dimension supplied by the schema-specific caller. */ auto n:expected)need(a.shape[i++]==n);}
        mark(id);return a;
    }
/** Decode string pool j with expected count into owned strings. optional admits a presence column; presence optionally receives copied flags. Claims pool sections, validates offsets/UTF-8/canonical absent entries and charges temporary storage. */
    std::vector<std::string> pool(const Json& j,std::uint64_t expected,bool optional=false,std::vector<std::uint8_t>* presence=nullptr) {
    // Validate pool descriptors and terminal offsets, then admit string/flag storage.
    // Read each bounded string, check UTF-8 and presence semantics, and return the owned vector.

        keys(j,optional?std::initializer_list<const char*>{"count","bytes","offsets","present"}:std::initializer_list<const char*>{"count","bytes","offsets"});
        /* Admitted string count, required to equal the caller's expected population. */ auto n=uint(j,"count",1000000);need(n==expected);/* Cursor over the owned pool byte section. */ auto raw=array(j.at("bytes"),"u8");need(raw.rank==1&&raw.count<=268435456,StatusCode::resource_limit);
        /* Cursor over n+1 monotonic UTF-8 byte offsets. */ auto offsets=array(j.at("offsets"),"u64",{n+1});need(offsets.bits(0)==0&&offsets.bits(n)==raw.count);
        budget.temp(product(n,sizeof(std::string))+raw.count+product(n,16));
        /* Owned decoded strings returned to the caller. */ std::vector<std::string> result;result.reserve(static_cast<std::size_t>(n));
        /* Optional presence-byte column cursor. */ std::optional<Array> present;if(optional){present=array(j.at("present"),"u8",{n});if(presence){budget.temp(n);presence->reserve(static_cast<std::size_t>(n));}}
        /* Start byte offset of the current pool entry. */ std::uint64_t off=0;
        for(/* String pool entry index. */ std::uint64_t i=0;i<n;++i) {
            /* Next string boundary from the offset column. */ auto end=offsets.bits(i+1);need(end>=off&&end<=raw.count);need(end-off<=1048576,StatusCode::resource_limit);
            /* Owned bytes of the current UTF-8 pool entry. */ std::string s(static_cast<std::size_t>(end-off),'\0');okay(out.read(raw.section,off,std::as_writable_bytes(std::span(s.data(),s.size()))));need(valid_utf8(s));
            if(optional){/* Canonical optional presence flag for this string. */ auto yes=present->bits(i);need(yes<=1&&(yes||s.empty()));if(presence)presence->push_back(static_cast<std::uint8_t>(yes));}
            result.push_back(std::move(s));off=end;
        }
        return result;
    }
/** Claim JSON metadata section id, admit its bounded raw/parser charges and return an owned strict object. Retained charge covers the returned graph; raw bytes are temporary. */
    Json metadata(std::uint64_t id) {
    // Validate section kind/size and read its exact bytes under a temporary scope.
    // Preflight the metadata profile, admit its retained parser charge, then parse strictly.

        need(id<out.directory.size()&&out.directory[id].kind==1);mark(id);/* Metadata-section payload byte count. */ auto n=out.directory[id].size;need(n&&n<=1048576,StatusCode::resource_limit);
        /* Scope restoring only temporary raw-byte/parser admission. */ TempScope scope(budget);budget.temp(n);/* Owned exact metadata bytes loaded before parsing. */ std::string raw(static_cast<std::size_t>(n),'\0');okay(out.read(id,0,std::as_writable_bytes(std::span(raw.data(),raw.size()))));
        /* Conservative retained tree charge admitted before materialization. */ std::uint64_t charge{};okay(json_contract::preflight(raw,budget.metadata_limit-budget.retained-budget.temporary));/* Restricted scanner whose counts drive the charge formula. */ json_contract::Scan scan{raw};scan.value(0);charge=5*raw.size()+4*scan.strings+128*scan.values+128*scan.members+16*scan.array_entries+256*scan.depth;
        budget.retain(charge);/* Recomputed parser charge output for the final strict parse. */ std::uint64_t actual{};return strict_parse(raw,true,true,charge,actual);
    }
/** Validate count bytes from section-relative offset as JSON without retaining its tree. original selects the document grammar/byte ceiling; otherwise use restricted metadata JSON. object_only requires an object root. Charges loading and parsing before either allocation. */
    void json_bytes(std::uint64_t section,std::uint64_t offset,std::uint64_t count,bool original,bool object_only) {
    // Admit and load the bounded byte extent under a temporary scope.
    // Use the shared document path for original evidence, otherwise preflight and parse the restricted metadata profile.

        /* Temporary raw-byte/parser lifetime for this JSON extent. */ TempScope scope(budget);/* Applicable original-document or restricted-metadata byte ceiling. */ auto limit=original?file.limits.document_bytes:1048576;need(count<=limit,StatusCode::resource_limit);
        budget.temp(count);/* Owned exact bounded bytes presented to the parser. */ std::string raw(static_cast<std::size_t>(count),'\0');okay(out.read(section,offset,std::as_writable_bytes(std::span(raw.data(),raw.size()))));
        /* Remaining metadata room intersected with the document-parser ceiling. */ auto remaining=std::min(budget.metadata_limit-budget.retained-budget.temporary,file.limits.document_parser_budget_bytes);
        if(original){(void)admit_document_json(raw,budget,remaining,object_only);return;}
        /* Restricted metadata parser charge before materialization. */ std::uint64_t charge{};
        okay(json_contract::preflight(raw,remaining));/* Restricted scanner used to calculate the exact admission formula. */ json_contract::Scan s{raw};s.value(0);charge=5*raw.size()+4*s.strings+128*s.values+128*s.members+16*s.array_entries+256*s.depth;
        need(charge<=remaining,StatusCode::resource_limit);budget.temp(charge);/* Recomputed charge returned by strict_parse. */ std::uint64_t actual{};(void)strict_parse(raw,!original,object_only,charge,actual);
    }
/** Validate document descriptor j and claim its data section when retained. original_role tracks a partial original for the later capability rejection. Returns the exact state; included JSON is strictly parsed, partial/non-JSON bytes remain exact evidence. */
    SectionState document(const Json& j,bool original_role=false) {
    // Admit inactive state-only descriptors without reading nonexistent bytes.
    // For retained content, validate media/type/extent and mark partial original evidence.
    // Strictly parse only complete application/json documents.

        /* Closed document retention-state code. */ auto state=uint(j,"state",3);
        if(state==1||state==2){keys(j,{"state"});return static_cast<SectionState>(state);}
        keys(j,{"state","media_type","bytes"});/* Owned media type deciding complete JSON parsing applicability. */ auto media=text(j,"media_type");/* Retained byte-array cursor whose section is claimed once. */ auto data=array(j.at("bytes"),"u8");need(data.rank==1&&data.count<=4294967296ull,StatusCode::resource_limit);
        if(original_role&&state==3)partial_original=true;
        if(state==0&&media=="application/json")json_bytes(data.section,0,data.count,true,false);
        return static_cast<SectionState>(state);
    }
/** Validate the container header, complete directory and all section digests; retain canonical descriptors and ownership bitmap in out. Throws before exposing any graph with bad extent/hash/contiguity. */
    void header() {
    // Read and hash-check the fixed header, then admit version/endian/flags and directory bounds.
    // Budget directory/bitmap storage and verify the complete directory digest.
    // Visit each contiguous section, verify its payload digest, and record summary extents.

        need(file.size>=128);/* Fixed canonical header buffer. */ std::array<std::byte,128> h{};file.read_at(0,h);
        /* Expected container magic bytes. */ const std::array<std::byte,8> magic={std::byte{'S'},std::byte{'E'},std::byte{'R'},std::byte{'E'},std::byte{'N'},std::byte{'E'},std::byte{},std::byte{}};
        need(std::equal(magic.begin(),magic.end(),h.begin()));/* Recorded digest of the first 96 header bytes. */ Digest header_digest{};std::copy_n(h.begin()+96,32,header_digest.begin());need(sha256(std::span(h).first(96))==header_digest,StatusCode::verification_failure);
        /* Bounded header-field decoder borrowing the fixed header buffer. */ auto u=[&](std::size_t off,std::size_t n){ /* Decode n header bytes at off without retaining a span. */ return le(std::span(h).subspan(off,n));};
        need(u(8,2)==1,StatusCode::unsupported_feature);minor=u(10,2);need(minor<=1,StatusCode::unsupported_feature);
        need(u(12,4)==0x01020304&&std::endian::native==std::endian::little,StatusCode::unsupported_feature);
        need((u(56,8)&~1ull)==0,StatusCode::unsupported_feature);need(u(16,4)==128&&u(20,4)==64&&u(24,8)==file.size&&u(56,8)==1);
        /* Directory byte offset; count is its entry population. */ auto start=u(32,8),count=u(40,8);need(count>0);need(count<=1048576,StatusCode::resource_limit);need(start>=128&&start<=file.size&&product(count,64)==file.size-start);
        out.root_section=u(48,8);need(out.root_section<count);
        budget.retain(product(count,std::max<std::size_t>(64,sizeof(DirectoryEntry)))+((count+7)/8));out.directory.resize(static_cast<std::size_t>(count));owned.resize(static_cast<std::size_t>((count+7)/8));
        /* Recorded complete-directory digest. */ Digest expected{};std::copy_n(h.begin()+64,32,expected.begin());need(file.hash(start,file.size-start)==expected,StatusCode::verification_failure);
        /* Next required contiguous section payload offset. */ std::uint64_t next=128;/* One canonical directory-entry buffer. */ std::array<std::byte,64> row{};
        for(/* Dense section ID expected in the current directory row. */ std::uint64_t i=0;i<count;++i) {
            file.read_at(start+i*64,row);need(le(std::span(row).first(8))==i);need(le(std::span(row).subspan(12,4))==0,StatusCode::unsupported_feature);
            /* Owned retained directory entry being populated. */ auto& d=out.directory[static_cast<std::size_t>(i)];d.kind=static_cast<std::uint32_t>(le(std::span(row).subspan(8,4)));d.offset=le(std::span(row).subspan(16,8));d.size=le(std::span(row).subspan(24,8));
            need(d.kind>=1&&d.kind<=8,StatusCode::unsupported_feature);need(d.offset==next&&next<=start&&d.size<=start-next);next+=d.size;
            std::copy_n(row.begin()+32,32,d.digest.begin());need(file.hash(d.offset,d.size)==d.digest,StatusCode::verification_failure);
        }
        need(next==start);out.summary.file_bytes=file.size;out.summary.section_count=count;
    }
/** Validate root format/features and the single-run catalog; retain owned root/run JSON and summary labels. Multi-run input is an unsupported importer capability, not a general wire-format prohibition. */
    void root_metadata() {
    // Parse root metadata and require supported canonical format/features.
    // Validate legacy/indexed provenance shape, then admit the one-run index/pools.
    // Retain owned labels and parse the distinct run metadata section.

        out.root=metadata(out.root_section);/* Borrowed root format descriptor. */ const auto& fmt=out.root.at("format");need(text(fmt,"name")=="serene.outputs"&&uint(fmt,"version_major")==1&&uint(fmt,"version_minor")==minor&&text(fmt,"write_state")=="complete");
        need(valid_uuid(text(fmt,"file_id",true))&&valid_utc(text(fmt,"created_utc",true)));(void)text(fmt,"writer_name",true);(void)text(fmt,"writer_version",true);
        /* Required-feature JSON array. */ const auto& features=fmt.at("required_features");need(features.is_array()&&features.size()<=1024);/* Owned unique declared feature spellings. */ std::set<std::string> names;
        /* Five mandatory unchanged 1.0 feature tokens. */ const std::set<std::string> base={"serene-container-le-1","json-descriptors-1","utf8-pools-1","frames-77-le-1","zfp-5-le64-serial-1"};
        for(/* Declared feature entry under admission. */ const auto& f:features){/* Owned current feature token. */ auto s=text(f,true);need(names.insert(s).second);if(!base.contains(s)){need(minor==1,StatusCode::unsupported_feature);if(s=="run-diagnostics-1")feature_d=true;else if(s=="provenance-sources-1")feature_p=true;else if(s=="timeseries-zfp-fixed-accuracy-1")feature_f=true;else need(false,StatusCode::unsupported_feature);}}
        for(/* Mandatory base token whose presence is required. */ const auto& s:base)need(names.contains(s));need(minor==0||feature_d||feature_p||feature_f);
        for(/* Optional-section aggregate name whose state code is checked. */ const char* name:{"inputs","logs","context"})uint(fmt.at("sections"),name,3);
        need(feature_p?!out.root.contains("inputs")&&!out.root.contains("logs")&&!out.root.contains("context")&&out.root.contains("provenance"):!out.root.contains("provenance"));
        /* Lifetime of temporary catalog columns and string pools. */ TempScope scope(budget);/* Single-run result-index descriptor. */ const auto& idx=out.root.at("results").at("index");/* Dense run-index column cursor. */ auto indices=array(idx.at("run_index"),"u64");need(indices.rank==1&&indices.count>=1&&indices.count<=65536,StatusCode::resource_limit);
        need(indices.count==1,StatusCode::unsupported_feature,89);need(indices.bits(0)==0);
        /* Owned one-entry run-ID pool. */ auto ids=pool(idx.at("run_id"),1);need(!ids[0].empty());out.summary.run_id=ids[0];
        /* Owned run-name presence flag returned by the optional pool. */ std::vector<std::uint8_t> present;/* Owned optional run-name pool. */ auto names_=pool(idx.at("run_name"),1,true,&present);out.summary.run_name=names_[0];out.summary.run_name_present=present[0]!=0;
        /* One-entry run-kind column. */ auto kinds=array(idx.at("kind"),"u8",{1});need(kinds.bits(0)<=1);out.summary.kind=static_cast<RunKind>(kinds.bits(0));
        /* One-entry run-metadata section-reference column. */ auto metadata_ids=array(idx.at("run_metadata_section"),"u64",{1});out.run_section=metadata_ids.bits(0);need(out.run_section!=out.root_section);
        out.summary.diagnostics=feature_d;out.summary.indexed_provenance=feature_p;out.summary.fixed_accuracy=feature_f;
        if(!file.binding.expected_run_id.empty())need(out.summary.run_id==file.binding.expected_run_id);
        budget.retain(out.summary.run_id.capacity()+out.summary.run_name.capacity()+2);
        out.run=metadata(out.run_section);
    }
    /// Validate the run envelope and bounded axis/identity scans. Mutates the unpublished graph/charges or throws Failure.
    void run_info();
    /// Validate catalog identity, mapping and effective-request metadata. Mutates the unpublished graph/charges or throws Failure.
    void catalogs();
    /// Validate pinned profiles, frame coverage/digests and bounded decode. Mutates the unpublished graph/charges or throws Failure.
    void values();
    /// Validate typed trace schemas and rows through shared diagnostics. Mutates the unpublished graph/charges or throws Failure.
    void diagnostics();
    /// Normalize source catalogs and validate role/digest/state ownership. Mutates the unpublished graph/charges or throws Failure.
    void provenance();
    /// Validate normalized source j for admitted state, claiming data/info sections and scanning complete records.
    void logs(const Json& j,unsigned state);
/** Finish admission by rejecting unowned sections, unused extension declarations and unsupported partial-original import; records retained/peak charges in out.summary. */
    void finish() {
    // Prove every section was claimed exactly once and declared extensions were used.
    // Apply the initial partial-original import capability limit and publish resource charges.

        for(/* Directory section whose exclusive ownership must have been proven. */ std::uint64_t i=0;i<out.directory.size();++i)need(is_marked(i));
        need(!feature_f||used_f);need(!feature_d||!out.traces.empty());
        need(!partial_original,StatusCode::unsupported_feature,89);
        out.summary.retained_metadata_bytes=budget.retained;out.summary.workspace_bytes=budget.peak;
    }
};
/** Validate the single-run envelope, axis/identity columns and disabled/unavailable shapes. Updates committed/accepted counts for later matrix/diagnostic checks; retains no history. */
void SpoolValidator::run_info() {
    // Match run identity/kind and admit closed outcome/presence/value fields.
    // Validate embedded configuration documents and unavailable-catalog omissions.
    // Scan retained axis/identity columns for finite values, monotonicity and last-recorded agreement.

    /* Borrowed single-run info object. */ const auto& i=out.run.at("info");need(text(i,"run_id")==out.summary.run_id&&uint(i,"kind",1)==static_cast<unsigned>(out.summary.kind));
    /* Closed run outcome and numerical/observation/logging status codes. */ auto outcome=uint(i,"run_outcome",feature_d?7:6),numerical=uint(i,"numerical_status",3),observation=uint(i,"observation_status",3),logging=uint(i,"logging_status",4);
    /* Catalog availability code; only included or unavailable are admitted. */ auto state=uint(i,"catalog_state",2);need(state==0||state==2);committed=uint(i,"committed_count",1ull<<40);accepted=uint(i,"accepted_count");
    /* Presence flags for accepted/recorded bounds, elapsed time and identity axis. */ auto last_accepted=flag(i,"last_accepted_present"),last_recorded=flag(i,"last_recorded_present"),bounds=flag(i,"requested_bounds_present"),elapsed=flag(i,"elapsed_seconds_present"),axis=flag(i,"identity_axis_present");
    /* Finite accepted/recorded axis values, requested interval endpoints and elapsed seconds. */ auto la=real(i,"last_accepted_value"),lr=real(i,"last_recorded_value"),begin=real(i,"requested_begin"),end=real(i,"requested_end"),seconds=real(i,"elapsed_seconds");
    need((last_accepted||positive_zero(la))&&(last_recorded||positive_zero(lr))&&last_recorded==(committed>0));
    need(bounds?begin<=end:positive_zero(begin)&&positive_zero(end));need(seconds>=0&&(elapsed||positive_zero(seconds)));
    /* Elapsed-time scope and identity-axis convention spellings. */ auto scope=text(i,"elapsed_seconds_scope"),convention=text(i,"identity_axis_convention");need(elapsed!=scope.empty()&&axis!=convention.empty());
    for(/* Required text/configuration/omitted section key under the current check. */ auto name:{"failure_stage","failure_code","termination_reason"})(void)text(i,name);
    /* Owned assembly reference, required only for an available catalog. */ auto assembly=text(i,"assembly_ref");/* Axis name and physical unit strings. */ auto axis_name=text(i,"axis_name"),axis_unit=text(i,"axis_unit");
    if(out.summary.kind==RunKind::timeseries)need(axis_name=="time"&&axis_unit=="s");
    else need(!axis_name.empty());
    for(/* Required text/configuration/omitted section key under the current check. */ auto name:{"solver_configuration","sampling_configuration","execution"})document(i.at(name));
    if(state==2) {
        need((outcome==4||outcome==5||outcome==6)&&numerical==0&&observation==3&&!committed&&!axis&&!last_accepted&&!accepted);
        for(/* Required text/configuration/omitted section key under the current check. */ auto name:{"channels","requests","values","time","step_index","load_levels","level_index"})need(!out.run.contains(name));
    } else {
        need(!assembly.empty());
        if(outcome==0)need(numerical==1&&(observation==0||observation==2)&&(logging==0||logging==3));
        /* Kind-specific numeric axis descriptor key. */ const char* axis_key=out.summary.kind==RunKind::timeseries?"time":"load_levels";
        /* Kind-specific accepted-identity descriptor key. */ const char* index_key=out.summary.kind==RunKind::timeseries?"step_index":"level_index";
        need(!out.run.contains(out.summary.kind==RunKind::timeseries?"load_levels":"time")&&!out.run.contains(out.summary.kind==RunKind::timeseries?"level_index":"step_index"));
        /* Bounded numeric axis cursor. */ auto values=array(out.run.at(axis_key),"f64",{committed});/* Previous finite axis value for strict transient ordering and last-recorded agreement. */ double previous=0;
        for(/* Retained record or identity index. */ std::uint64_t n=0;n<committed;++n){/* Current decoded axis value. */ auto x=values.real(n);need(std::isfinite(x)&&(!(n&&out.summary.kind==RunKind::timeseries)||x>previous));previous=x;}
        if(committed)need(lr==previous);
        need(out.run.contains(index_key)==axis);
        if(axis){/* Accepted identity column cursor. */ auto indices=array(out.run.at(index_key),"u64",{committed});/* Prior accepted identity for strict increasing-order validation. */ std::uint64_t previous_id=0;for(/* Retained record or identity index. */ std::uint64_t n=0;n<committed;++n){/* Current accepted identity word. */ auto id=indices.bits(n);need(!n||id>previous_id);previous_id=id;}}
    }
    need(feature_d||!out.run.contains("diagnostics"));need(feature_p||!out.run.contains("provenance"));
}
/** Validate channel/request catalogs and optional effective request JSON when the catalog is available. Uses scoped owned string pools and uniqueness sets, claiming every catalog section once. */
void SpoolValidator::catalogs() {
    // Check dense channel indices and unique scalar/request identities.
    // Verify channel-to-request mapping and retain no temporary catalog vectors afterward.
    // For included effective requests, check exact digests and strict restricted JSON; forbid payloads for other states.

    if(uint(out.run.at("info"),"catalog_state")==2)return;
    /* Temporary pool/set accounting for the catalog validation activity. */ TempScope scope(budget);/* Borrowed channel catalog descriptor. */ const auto& c=out.run.at("channels");/* Borrowed request catalog descriptor. */ const auto&q=out.run.at("requests");
    /* Dense channel-index cursor; its count defines the logical channel population. */ auto index=array(c.at("channel_index"),"u64");need(index.rank==1&&index.count<=1000000,StatusCode::resource_limit);catalog_channels=index.count;
    for(/* Channel/request/effective-document index for the local scan. */ std::uint64_t n=0;n<index.count;++n)need(index.bits(n)==n);
    /* Owned scalar identity pool. */ auto scalars=pool(c.at("scalar_id"),index.count);/* Owned per-channel group-ID pool. */ auto groups=pool(c.at("group_id"),index.count);/* Owned per-channel request-ID pool. */ auto requests=pool(c.at("request_id"),index.count);
    /* Owned component-path pool, checked for shape/UTF-8 even when not otherwise interpreted. */ auto components=pool(c.at("component_path"),index.count);/* Owned unit pool; generic reader preserves authored unit spellings. */ auto units=pool(c.at("unit"),index.count);(void)components;(void)units;
    budget.temp(product(index.count,96));/* Unique nonempty scalar IDs. */ std::set<std::string> unique;
    for(/* Scalar ID whose retained string capacity is charged. */ const auto& s:scalars){budget.temp(s.capacity()+1);need(!s.empty()&&unique.insert(s).second);}
    /* Channel-to-request index column. */ auto mapping=array(c.at("request_index"),"u64",{index.count});/* Request catalog population. */ auto nr=uint(q.at("group_id"),"count",1000000);
    /* Owned request group identities. */ auto qgroups=pool(q.at("group_id"),nr);/* Owned request identities within each group. */ auto qrequests=pool(q.at("request_id"),nr);(void)pool(q.at("schema_version"),nr);(void)pool(q.at("construction_profile"),nr);
    budget.temp(product(nr,128));/* Unique scoped request identity pairs. */ std::set<std::pair<std::string,std::string>> request_keys;
    for(/* Request pool index. */ std::size_t n=0;n<qgroups.size();++n){budget.temp(qgroups[n].capacity()+qrequests[n].capacity()+2);need(!qgroups[n].empty()&&!qrequests[n].empty()&&request_keys.emplace(qgroups[n],qrequests[n]).second);}
    for(/* Channel/request/effective-document index for the local scan. */ std::uint64_t n=0;n<index.count;++n){/* Mapped request index for the current channel. */ auto k=mapping.bits(n);need(k<nr&&groups[n]==qgroups[k]&&requests[n]==qrequests[k]);}
    /* Effective-request metadata retention state. */ auto state=uint(q,"effective_state",2);
    if(state==0) {
        /* Owned effective request JSON strings. */ auto effective=pool(q.at("effective_json"),nr);/* Expected SHA-256 byte matrix for effective requests. */ auto digests=array(q.at("effective_sha256"),"u8",{nr,32});
        for(/* Channel/request/effective-document index for the local scan. */ std::uint64_t n=0;n<nr;++n){/* Expected digest assembled for one effective JSON string. */ Digest expected{};for(/* Digest byte position. */ unsigned k=0;k<32;++k)expected[k]=std::byte(digests.bits(n*32+k));/* Borrowed exact bytes of this effective request string. */ auto raw=std::as_bytes(std::span(effective[n].data(),effective[n].size()));need(sha256(raw)==expected,StatusCode::verification_failure);/* Strict parser charge output after admission. */ std::uint64_t charge{};/* Remaining metadata charge before parsing this one request. */ auto room=budget.metadata_limit-budget.retained-budget.temporary;okay(json_contract::preflight(effective[n],room));/* Nested temporary parser-charge lifetime. */ TempScope parse_scope(budget);/* Restricted scanner for this effective request. */ json_contract::Scan scan{effective[n]};scan.value(0);/* Conservative parser charge for the current effective request. */ auto cost=5*effective[n].size()+4*scan.strings+128*scan.values+128*scan.members+16*scan.array_entries+256*scan.depth;budget.temp(cost);(void)strict_parse(effective[n],true,true,cost,charge);}
    } else need(!q.contains("effective_json")&&!q.contains("effective_sha256"));
}
/** Validate logical matrix shape, row profiles, canonical frame coverage, payload hashes and decoded finite values. Decodes one bounded frame at a time; reported error summaries are constrained producer assertions, not recomputed against unavailable originals. */
void SpoolValidator::values() {
    // Admit run-kind shape, fixed pinned profile metadata and row-profile references.
    // Check expected frame/verification columns and budget one encoded/decoded/codec working set.
    // Verify each contiguous canonical frame, expected header profile and declared error bounds, then release sequential workspace charge.

    if(uint(out.run.at("info"),"catalog_state")==2)return;
    /* Lifetime of profile columns and bounded verification metadata. */ TempScope scope(budget);/* Borrowed logical values descriptor. */ const auto& v=out.run.at("values");/* Borrowed codec-profile catalog descriptor. */ const auto& p=v.at("profiles");/* Two logical matrix dimensions in wire order. */ const auto& shape=v.at("shape");need(shape.is_array()&&shape.size()==2);
    /* Logical row count and inner value count per row. */ auto outer=uint(shape[0],1ull<<40),inner=uint(shape[1],1ull<<40);/* Whether rows are channels (transient) rather than accepted levels (static). */ bool ts=out.summary.kind==RunKind::timeseries;
    need(outer==(ts?catalog_channels:committed)&&inner==(ts?committed:catalog_channels));if(!catalog_channels)need(!committed&&!outer&&!inner);
    need(text(v,"dtype")=="float64"&&uint(v,"frame_schema")==1);
    need(v.at("axis_names")==Json::array({ts?"channel":"level",ts?"sample":"channel"}));
    /* Nominal maximum values per independent frame, a positive multiple of four. */ auto B=uint(v,"nominal_frame_values",maximum_frame_values);need(B>=4&&B%4==0);out.summary.maximum_frame_values=B;
    /* Row-to-profile index cursor. */ auto rows=array(v.at("row_profile_index"),"u32",{outer});/* Profile catalog population. */ auto m=uint(p.at("profile_index").at("shape")[0],1000000);need(m>0);
    /* Dense profile-index cursor. */ auto profile_index=array(p.at("profile_index"),"u32",{m});/* Codec-mode cursor. */ auto modes=array(p.at("mode"),"u8",{m});/* Codec stream-version cursor. */ auto versions=array(p.at("codec_version"),"u32",{m});/* Pinned stream-word-size cursor. */ auto words=array(p.at("stream_word_bits"),"u32",{m});/* Pinned signed rounding-mode cursor. */ auto rounding=array(p.at("rounding"),"i8",{m});/* Canonical short-mode code cursor. */ auto codes=array(p.at("mode_code"),"u64",{m});/* Tolerance-presence cursor. */ auto present=array(p.at("tolerance_present"),"u8",{m});/* Requested absolute-tolerance cursor. */ auto requested=array(p.at("requested_tolerance"),"f64",{m});/* Effective power-of-two tolerance cursor. */ auto effective=array(p.at("effective_tolerance"),"f64",{m});
    for(/* Pinned profile text field and its sole supported value. */ auto [name,value]:std::array<std::pair<const char*,const char*>,5>{{{"codec","zfp"},{"library_version","1.0.1"},{"library_revision","f40868a6a1c190c802e7d8b5987064f044bf7812"},{"byte_order","little"},{"execution","serial"}}}) {
        /* Temporary lifetime of one profile string pool. */ TempScope one(budget);/* Owned pool of one pinned profile field across all profiles. */ auto names=pool(p.at(name),m);for(/* One profile spelling compared against the supported pin. */ const auto&s:names)need(s==value,StatusCode::unsupported_profile);
    }
    budget.temp(product(m,sizeof(CodecProfile)));/* Owned immutable admitted profiles selected by frames and rows. */ std::vector<CodecProfile> profiles;profiles.reserve(static_cast<std::size_t>(m));
    for(/* Profile, row or canonical frame ordinal for the current scan. */ std::uint64_t n=0;n<m;++n){need(profile_index.bits(n)==n&&versions.bits(n)==5&&words.bits(n)==64&&rounding.bits(n)==0,StatusCode::unsupported_profile);/* Closed reversible/fixed mode code. */ auto mode=modes.bits(n);need(mode<=1,StatusCode::unsupported_profile);need(mode==0||feature_f);need(ts||mode==0,StatusCode::unsupported_profile);/* Profile factory result validating metadata before frame decoding. */ auto profile=CodecProfile::from_wire(static_cast<std::uint8_t>(mode),static_cast<std::uint8_t>(present.bits(n)),requested.real(n),effective.real(n),codes.bits(n));if(!profile)throw Failure{profile.error()};profiles.push_back(*profile);used_f|=mode==1;}
    for(/* Profile, row or canonical frame ordinal for the current scan. */ std::uint64_t n=0;n<outer;++n)need(rows.bits(n)<m);
    /* Expected frame population from matrix shape and nominal frame size. */ auto f=product(outer,(inner+B-1)/B);need(f<=(1ull<<32),StatusCode::resource_limit);/* Canonical 77-byte frame-index section cursor. */ auto frames=array(v.at("frame_index"),"frames77",{f});/* Concatenated encoded payload section cursor. */ auto payload=array(v.at("payload"),"u8");need(payload.rank==1);
    /* Borrowed producer verification-summary descriptor. */ const auto& verification=v.at("verification");/* Per-frame declared maximum absolute error cursor. */ auto maxerr=array(verification.at("max_abs_error"),"f64",{f});/* Per-frame declared RMS error cursor. */ auto rms=array(verification.at("rms_error"),"f64",{f});/* Per-frame declared verified-count cursor. */ auto counts=array(verification.at("verified_count"),"u64",{f});/* Per-frame declared bitwise-equality cursor. */ auto equal=array(verification.at("bitwise_equal"),"u8",{f});
    if(!f){need(payload.count==0);return;}
    /* Previous active nonmetadata charge restored after frame processing. */ const auto saved_workspace=budget.workspace;
    /* Bound covering both raw and compressed frame bytes. */ auto frame_capacity=std::max(product(B,8),encoded_capacity(B));need(frame_capacity<=maximum_frame_bytes,StatusCode::resource_limit);
    budget.work(frame_capacity+product(B,8)+encoded_capacity(B)+4096);
    /* Owned one-frame encoded input buffer. */ std::vector<std::byte> encoded(static_cast<std::size_t>(frame_capacity));/* Owned one-frame decoded values buffer. */ std::vector<double> decoded(static_cast<std::size_t>(B));/* Prepared codec with the admitted frame capacity. */ auto codec=Codec::create(B);if(!codec)throw Failure{codec.error()};
    /* One canonical frame-index record. */ std::array<std::byte,77> record{};/* Required contiguous payload offset after previous frames. */ std::uint64_t position=0;
    for(/* Profile, row or canonical frame ordinal for the current scan. */ std::uint64_t n=0;n<f;++n) {
        okay(out.read(frames.section,n*77,record));/* Borrowed canonical frame-field decoder. */ auto get=[&](std::size_t off,std::size_t width){ /* Decode width canonical index bytes at off. */ return le(std::span(record).subspan(off,width));};
        /* Decoded row/start/count/offset/byte-size/encoding/profile fields of the current frame. */ auto row=get(0,8),begin=get(8,8),count=get(16,8),offset=get(24,8),bytes_=get(32,8),encoding=get(40,1),profile=get(41,4);
        /* Number of frames in each logical row. */ const auto chunks=(inner+B-1)/B;need(row==n/chunks&&begin==(n%chunks)*B&&count==std::min(B,inner-begin)&&count>0&&offset==position&&bytes_>0&&bytes_<=frame_capacity);
        need(profile<m&&profile==rows.bits(row));need(encoding<=1,StatusCode::unsupported_feature);need(bytes_<=payload.count-position);
        if(encoding==0)need(bytes_==product(count,8));else need(bytes_<=encoded_capacity(count),StatusCode::codec_failure);
        okay(out.read(payload.section,offset,std::span(encoded).first(bytes_)));/* Expected exact encoded-payload digest from the frame record. */ Digest digest{};std::copy_n(record.begin()+45,32,digest.begin());need(sha256(std::span(encoded).first(bytes_))==digest,StatusCode::verification_failure);
        /* Declared maximum and RMS errors for the current stored frame. */ auto maximum=maxerr.real(n),rms_=rms.real(n);/* Declared canonical bitwise-equality flag. */ auto eq=equal.bits(n);need(counts.bits(n)==count&&eq<=1);
        if(encoding==0||profiles[profile].mode()==CodecMode::reversible)need(positive_zero(maximum)&&positive_zero(rms_)&&eq==1);
        else need(std::isfinite(maximum)&&std::isfinite(rms_)&&rms_>=0&&rms_<=maximum&&maximum<=profiles[profile].requested_tolerance()&&(!eq||(maximum==0&&rms_==0)));
        /* Expected-profile decode result; v1.1 remaps a header/profile mismatch to codec_failure. */ auto decoded_status=codec->decode(static_cast<Encoding>(encoding),count,std::span(encoded).first(bytes_),std::span(decoded).first(count),profiles[profile]);
        if(!decoded_status&&minor==1&&decoded_status.code==StatusCode::unsupported_profile)decoded_status.code=StatusCode::codec_failure;
        okay(decoded_status);
        for(/* Decoded value index checked for finiteness. */ std::uint64_t j=0;j<count;++j)need(std::isfinite(decoded[j]));position+=bytes_;
    }
    need(position==payload.count);budget.workspace=saved_workspace;
}
/** Validate normalized JSONL source j for already-admitted state. Inactive sources have only state metadata; retained sources require exact complete-record offsets and preserve a partial trailing fragment without parsing it. */
void SpoolValidator::logs(const Json& j,unsigned state) {
    // Admit record data/offsets, fragment/dropped counts and associated info document.
    // Scan each complete record for one terminal newline and no interior newline.
    // Strictly parse complete JSONL objects under bounded per-record workspace.

    if(state==1||state==2){keys(j,{"source_schema","source_index","kind","state"});return;}
    /* Retained byte section containing complete lines and any trailing fragment. */ auto data=array(j.at("records_jsonl"),"u8");need(data.rank==1&&data.count<=4294967296ull,StatusCode::resource_limit);
    /* Complete-record population and retained trailing byte count. */ auto count=uint(j,"complete_record_count"),fragment=uint(j,"trailing_fragment_bytes");need(count<maximum_extent);/* Record boundary table with an initial zero and one end per complete record. */ auto offsets=array(j.at("record_offsets"),"u64",{count+1});need(offsets.bits(0)==0);
    /* End of the complete prefix, before any unparsed trailing fragment. */ auto end=offsets.bits(count);need(end<=data.count&&fragment==data.count-end);/* Known dropped-record count; absent totals must remain canonical zero. */ auto dropped=uint(j,"dropped_count");need(flag(j,"dropped_count_present")||!dropped);need(state==3||(!fragment&&!dropped));(void)text(j,"schema",true);document(j.at("info_json"));
    /* Start byte offset of the next complete JSONL record. */ std::uint64_t previous=0;
    for(/* Complete-record ordinal being admitted. */ std::uint64_t i=0;i<count;++i){/* Exclusive end byte offset of this complete record. */ auto next=offsets.bits(i+1);need(next>previous&&next<=end);need(next-previous<=1048576,StatusCode::resource_limit);/* Final record byte, which must be its single newline terminator. */ std::byte last{};okay(out.read(data.section,next-1,std::span(&last,1)));need(last==std::byte{'\n'});
        for(/* Streaming offset used to reject interior newline bytes. */ std::uint64_t at=previous;at<next-1;){/* Current bounded scan length, excluding the terminal newline. */ auto n=std::min<std::uint64_t>(file.tile.size(),next-1-at);okay(out.read(data.section,at,std::span(file.tile).first(n)));need(std::find(file.tile.begin(),file.tile.begin()+n,std::byte{'\n'})==file.tile.begin()+n);at+=n;}
        json_bytes(data.section,previous,next-previous,false,true);previous=next;}
}
/** Validate/normalize all provenance sources and run references into out. Enforces source ownership/use, authored digest binding and aggregate state semantics; legacy metadata is copied without changing data section IDs. */
void SpoolValidator::provenance() {
    // Read indexed catalogs/references or normalize the six legacy source roles.
    // Validate each document/log source and bind the original digest to retained bytes or trusted pre-solve evidence.
    // Recompute input/log/context aggregate states while preserving included-empty, disabled and unavailable distinctions.

    /* Owned root aggregate-state description used for final consistency checks. */ auto states=out.root.at("format").at("sections");
    /* Source population; legacy layout normalizes to six role-specific entries. */ std::uint64_t count=6;
    if(feature_p) {
        /* Indexed provenance descriptor borrowed from the retained root metadata. */ const auto& p=out.root.at("provenance");need(uint(p,"schema")==1);/* Exact 32-byte pre-solve authored-input digest column. */ auto digest=array(p.at("authored_input_sha256"),"u8",{32});for(/* Digest byte index copied into the owned summary. */ unsigned i=0;i<32;++i)out.summary.authored_input_sha256[i]=std::byte(digest.bits(i));
        /* Indexed source catalog descriptor. */ const auto& c=p.at("sources");/* Dense source IDs, required to match catalog order. */ auto indices=array(c.at("source_index"),"u64");need(indices.rank==1&&indices.count>0&&indices.count<=1000000,StatusCode::resource_limit);count=indices.count;
        /* Document-versus-JSONL kind for each source. */ auto kinds=array(c.at("kind"),"u8",{count});/* Metadata section ID for each source. */ auto metadata_ids=array(c.at("metadata_section"),"u64",{count});for(/* Source or role ordinal within its bounded canonical catalog. */ std::uint64_t i=0;i<count;++i)need(kinds.bits(i)<=1,StatusCode::unsupported_feature);out.original_source_index=uint(p,"original_source_index");need(out.original_source_index<count&&kinds.bits(out.original_source_index)==0);
        budget.retain((count+7)/8);source_used.resize(static_cast<std::size_t>((count+7)/8));
        // Single-run references have at most69 entries and must use every catalog source.
        need(count<=69);budget.retain(product(count,sizeof(SourceMetadata)));out.sources.reserve(static_cast<std::size_t>(count));
        for(/* Source or role ordinal within its bounded canonical catalog. */ std::uint64_t i=0;i<count;++i){need(indices.bits(i)==i);/* Closed source kind code for the current catalog entry. */ auto k=kinds.bits(i);need(k<=1,StatusCode::unsupported_feature);/* Owned normalized descriptor prepared before insertion into the stable catalog. */ SourceMetadata source;source.source_index=i;source.kind=static_cast<std::uint8_t>(k);source.metadata_section=metadata_ids.bits(i);source.metadata=metadata(source.metadata_section);need(uint(source.metadata,"source_schema")==1&&uint(source.metadata,"source_index")==i&&uint(source.metadata,"kind",1)==k);out.sources.push_back(std::move(source));}
        /* Run-local ordered provenance reference descriptor. */ const auto& r=out.run.at("provenance").at("references");/* Ordered role codes: five singleton document roles then log sources. */ auto roles=array(r.at("role"),"u8");need(roles.rank==1&&roles.count>=5&&roles.count<=69);/* Source IDs addressed by each run reference. */ auto ids=array(r.at("source_index"),"u64",{roles.count});/* Within-role ordinal, zero for singleton document roles. */ auto order=array(r.at("order"),"u64",{roles.count});budget.retain(product(roles.count,sizeof(SourceReference)));out.references.reserve(static_cast<std::size_t>(roles.count));
        for(/* Source or role ordinal within its bounded canonical catalog. */ std::uint64_t i=0;i<roles.count;++i){/* Decoded role, source ID and within-role order of the current reference. */ auto role=roles.bits(i),id=ids.bits(i),ord=order.bits(i);need(role==(i<5?i:5)&&ord==(i<5?0:i-5)&&id<count&&out.sources[id].kind==(role==5?1:0));if(i==0)need(id==out.original_source_index);out.references.push_back({static_cast<std::uint8_t>(role),id,ord});source_used[id/8]|=std::byte(1u<<(id%8));}
        source_used[out.original_source_index/8]|=std::byte(1u<<(out.original_source_index%8));for(/* Source or role ordinal within its bounded canonical catalog. */ std::uint64_t i=0;i<count;++i)need((source_used[i/8]&std::byte(1u<<(i%8)))!=std::byte{});
    } else {
        budget.retain(product(count,sizeof(SourceMetadata))+product(count,sizeof(SourceReference))+1);out.sources.reserve(count);out.references.reserve(count);source_used.resize(1,std::byte{0x3f});
        /* Legacy document keys in the canonical role order. */ const char* docs[]={"source_json","effective_json","overrides_json","resources_json","execution_json"};
        for(/* Source or role ordinal within its bounded canonical catalog. */ std::uint64_t i=0;i<5;++i){/* Legacy aggregate containing this document role. */ const char* branch=i<4?"inputs":"context";/* Admitted section state retained for source/aggregate normalization. */ auto state=uint(states,branch,3);need(out.root.contains(branch)==(state==0||state==3));budget.retain(1024+(state==0||state==3?copy_charge(out.root.at(branch).at(docs[i])):256));/* Owned legacy document descriptor or inactive state-only placeholder. */ Json doc=state==0||state==3?out.root.at(branch).at(docs[i]):Json{{"state",state}};/* Normalized owned source descriptor constructed without changing data-section IDs. */ SourceMetadata s;s.source_index=i;s.kind=0;s.metadata={{"source_schema",1},{"source_index",i},{"kind",0},{"document",std::move(doc)}};out.sources.push_back(std::move(s));out.references.push_back({static_cast<std::uint8_t>(i),i,0});}
        /* Admitted section state retained for source/aggregate normalization. */ auto state=uint(states,"logs",3);need(out.root.contains("logs")==(state==0||state==3));budget.retain(1024+(state==0||state==3?copy_charge(out.root.at("logs")):256));/* Normalized owned source descriptor constructed without changing data-section IDs. */ SourceMetadata s;s.source_index=5;s.kind=1;s.metadata=state==0||state==3?out.root.at("logs"):Json::object();s.metadata.update(Json{{"source_schema",1},{"source_index",5},{"kind",1},{"state",state}});out.sources.push_back(std::move(s));out.references.push_back({5,5,0});
    }
    /* Per-source admitted state used to recompute run aggregate states. */ std::vector<unsigned> source_states;source_states.reserve(out.sources.size());
    for(/* Catalog source whose document or JSONL payload is being validated. */ auto& source:out.sources) {
        if(source.kind==0){/* Admitted section state retained for source/aggregate normalization. */ auto state=document(source.metadata.at("document"),source.source_index==out.original_source_index);source_states.push_back(static_cast<unsigned>(state));}
        else {/* Admitted section state retained for source/aggregate normalization. */ auto state=uint(source.metadata,"state",3);logs(source.metadata,static_cast<unsigned>(state));source_states.push_back(static_cast<unsigned>(state));}
    }
    /* Document descriptor selected by original_source_index. */ const auto& original=out.sources[out.original_source_index].metadata.at("document");/* Original-evidence state, distinct from empty included bytes. */ auto original_state=uint(original,"state",3);out.summary.original_state=static_cast<SectionState>(original_state);
    if(feature_p)need(original_state!=3);
    if(original_state==0){/* Section containing the included original bytes. */ auto id=uint(original.at("bytes"),"section");/* Digest of the already-verified original byte section. */ auto digest=out.directory[id].digest;if(feature_p)need(digest==out.summary.authored_input_sha256);else out.summary.authored_input_sha256=digest;}
    else if(!feature_p)out.summary.authored_input_sha256=file.binding.authored_input_sha256;
    if(file.binding.trusted_manifest_binding){need(file.binding.expected_file_bytes==file.size&&!file.binding.producer_identity.empty()&&!file.binding.dependency_identity.empty()&&!file.binding.expected_run_id.empty(),StatusCode::unsupported_feature,89);need(out.summary.authored_input_sha256==file.binding.authored_input_sha256);}
    else if(original_state!=0)need(partial_original,StatusCode::unsupported_feature,89);
    if(feature_p) {
        /* Recompute an indexed aggregate for category 0 inputs, 1 logs or 2 context. */ auto aggregate=[&](unsigned category){
            // Reduce referenced source states for the requested category without treating empty included bytes as missing.
            /* Flags distinguish empty/all-disabled, all-included, retained mixed and unavailable-only aggregates. */ bool any=false,all_disabled=true,all_included=true,retained=false,unavailable=false;for(/* Reference whose role selects the aggregate population. */ const auto& r:out.references){/* Aggregate category derived from the closed provenance role code. */ auto c=r.role<4?0u:r.role==4?2u:1u;if(c!=category)continue;any=true;/* Admitted state of the current referenced source. */ auto s=source_states[r.source_index];all_disabled&=s==1;all_included&=s==0;retained|=s==0||s==3;unavailable|=s==2;}return !any||all_disabled?1u:all_included?0u:!retained&&unavailable?2u:3u;};
        need(uint(states,"inputs")==aggregate(0)&&uint(states,"logs")==aggregate(1)&&uint(states,"context")==aggregate(2));
    } else {
        /* Recompute a legacy aggregate from the half-open normalized source range. */ auto branch=[&](std::uint64_t begin,std::uint64_t end) {
            // Reduce states over [begin,end), preserving the legacy retained/partial distinction.
            /* Legacy flags distinguish retained partial evidence from disabled/unavailable branches. */ bool retained=false,missing=false,all_disabled=true;for(/* Normalized source index within the selected legacy branch. */ auto n=begin;n<end;++n){/* Admitted state of the current referenced source. */ auto s=source_states[n];retained|=s==0||s==3;missing|=s==2||s==3;all_disabled&=s==1;}return retained?(missing?3u:0u):(all_disabled?1u:2u);};
        need(uint(states,"inputs")==branch(0,4)&&uint(states,"context")==branch(4,5));
    }
}
/** Validate declared typed trace metadata and lossless columns through shared validate_diagnostics. Borrows bounded column cursors during the call; retains only owned trace JSON. Generic dictionaries remain source-owned. */
void SpoolValidator::diagnostics() {
    // Build bounded per-trace metadata, row adapters and optional summary descriptions.
    // Require exact applicable column schemas/criteria/dictionaries and valid count/state shapes.
    // Delegate row/count/metric/summary verification to shared diagnostics, then check aggregate status against run outcome.

    if(!feature_d)return;
    /* Restore temporary adapter/dictionary charges after synchronous validation. */ TempScope scope(budget);/* Run diagnostics descriptor borrowed from owned metadata. */ const auto& diag=out.run.at("diagnostics");/* At most five distinct trace descriptors, one for each closed kind. */ const auto& entries=diag.at("traces");need(entries.is_array()&&entries.size()<=5);
    budget.retain(product(entries.size(),sizeof(TraceMetadata)));out.traces.reserve(entries.size());
    /** Synchronous adapter from bounded lossless column cursors to DiagnosticRecords.
     * diagnostics() reserves its containing vector before publishing callback
     * addresses, so context/column owners remain stable through validate_diagnostics.
     * No row history or callback borrow survives that shared validation call. */
    struct Access {
        SpoolValidator* validator{}; ///< Borrowed enclosing admission pipeline; alive through shared validation.
        unsigned kind{}; ///< Closed diagnostic-kind code selecting the typed row layout.
        std::map<std::string,Array> columns; ///< Owned names/cursors, each with bounded cache and borrowed file ownership.
/** Fill output row r at ordinal index from Access context column cursors. Returns exact Failure status or invalid_contract for unexpected exceptions; context and validator must remain alive through the synchronous shared scan. */
        static Status read(const void* context,std::uint64_t index,DiagnosticRecord& r) noexcept {
    // Bind bounded integer/f64 accessors and clear the output row.
    // Populate the exact transient/static/event fields for this kind, checking closed validity masks.
    // Translate private exceptions into callback Status without exposing borrows.

            try {
                /* Stable adapter borrowed from the pre-reserved vector for this callback. */ const auto& self=*static_cast<const Access*>(context);
                /* Named unsigned-column accessor for the requested row ordinal. */ auto u=[&](const char* name){
                    // Read the requested named unsigned column at the callback row.
                    return self.columns.at(name).bits(index);};/* Named binary64-column accessor for the requested row ordinal. */ auto d=[&](const char* name){
                    // Read the requested named binary64 column at the callback row.
                    return self.columns.at(name).real(index);};
                r={};r.record_index=u("record_index");
                if(self.kind==0){r.accepted_step=u("accepted_step");r.time_s=d("time_s");r.nonlinear_iterations=u("nonlinear_iterations");}
                else if(self.kind<=3) {
                    r.attempt_id=u("attempt_id");r.factor=d("factor");/* Closed validity-bit mask checked before narrowing into the typed record. */ auto mask=u("metric_valid");need(mask<=63,StatusCode::unsupported_feature);r.metric_valid=static_cast<std::uint8_t>(mask);
                    if(self.kind==2)r.accepted_level_id=u("accepted_level_id");
                    else {r.certified=static_cast<std::uint8_t>(u("certified"));r.rolled_back=static_cast<std::uint8_t>(u("rolled_back"));r.status_index=static_cast<std::uint32_t>(u("status_index"));
                        if(self.kind==1){r.accepted_level_present=static_cast<std::uint8_t>(u("accepted_level_present"));r.accepted_level_id=u("accepted_level_id");r.newton_record_count=u("newton_record_count");}
                        else {r.iteration_id=u("iteration_id");r.row_kind=static_cast<std::uint8_t>(u("row_kind"));}}
                    for(/* Canonical static metric ordinal corresponding to metric_names. */ unsigned i=0;i<6;++i)r.metrics[i]=d(metric_names[i]);
                } else {r.sequence=u("sequence");r.event_name_index=static_cast<std::uint32_t>(u("event_name_index"));r.severity=static_cast<std::uint8_t>(u("severity"));/* Closed validity-bit mask checked before narrowing into the typed record. */ auto mask=u("identity_valid");need(mask<=15,StatusCode::unsupported_feature);r.identity_valid=static_cast<std::uint8_t>(mask);r.time_s=d("time_s");r.accepted_step=u("accepted_step");r.accepted_level_id=u("accepted_level");r.attempt_id=u("attempt_id");}
                return {};
            } catch(/* Private validation failure translated to the public callback status. */ const Failure& f){return f.status;}catch(...){return error(StatusCode::invalid_contract,80);}
        }
    };
    /* Owned descriptions/dictionaries kept alive through shared validation. */ std::vector<DiagnosticTraceDescription> descriptions;/* Borrowing trace views pointing only into stable local adapters and summaries. */ std::vector<DiagnosticTraceView> views;/* Pre-reserved adapters whose addresses become callback contexts. */ std::vector<Access> accesses;/* Owned per-trace summary assertions borrowed by views. */ std::vector<std::vector<DiagnosticSummary>> summaries;
    budget.temp(product(entries.size(),sizeof(DiagnosticTraceDescription)+sizeof(DiagnosticTraceView)+sizeof(Access)+sizeof(std::vector<DiagnosticSummary>)));
    descriptions.reserve(entries.size());views.reserve(entries.size());accesses.reserve(entries.size());summaries.reserve(entries.size());
    /* Seen trace kinds, enforcing one descriptor per kind. */ std::set<unsigned> kinds;
    for(/* Requested trace entry in source order. */ const auto& entry:entries) {
        /* Closed trace kind, checked for compatibility with the run kind. */ auto k=uint(entry,"kind",4);need(kinds.insert(static_cast<unsigned>(k)).second);need((k==0?out.summary.kind==RunKind::timeseries:k==4||out.summary.kind==RunKind::steady_state));
        /* Metadata section exclusively owned by this trace. */ auto id=uint(entry,"metadata_section");/* Owned retained trace descriptor for the verified spool graph. */ TraceMetadata trace;trace.kind=static_cast<std::uint8_t>(k);trace.metadata_section=id;trace.metadata=metadata(id);out.traces.push_back(std::move(trace));/* Stable retained trace JSON after insertion. */ const auto& t=out.traces.back().metadata;
        need(uint(t,"trace_schema")==1&&uint(t,"kind",4)==k&&uint(t,"run_index")==0);
        /* Shared validator description reconstructed from trace metadata. */ DiagnosticTraceDescription d;d.kind=static_cast<DiagnosticKind>(k);d.requirement=static_cast<DiagnosticRequirement>(uint(t,"requirement",1));/* Authored capacity; retained records are separately limited to 2^40. */ auto capacity=uint(t,"record_capacity");need(capacity>0);d.record_capacity=std::min(capacity,1ull<<40);
        /* Synchronous typed record view and capture/count state. */ DiagnosticTraceView v;v.kind=d.kind;v.availability=static_cast<DiagnosticAvailability>(uint(t,"availability",3));/* Closed capture status selecting inactive versus recorded shapes. */ auto status=uint(t,"capture_status",4);v.capture_status=static_cast<DiagnosticCaptureStatus>(status);/* Retained row count, bounded independently of larger authored capacity. */ auto count=uint(t,"record_count",1ull<<40);need(count<=capacity);if(status==2)need(count==capacity);
        v.records.count=count;v.counts_present=flag(t,"eligible_count_present");need(v.counts_present==flag(t,"omitted_count_present"));v.eligible_count=uint(t,"eligible_count");v.omitted_count=uint(t,"omitted_count");v.capture_error_count=uint(t,"capture_error_count");v.first_missing_ordinal_present=flag(t,"first_missing_ordinal_present");v.first_missing_ordinal=uint(t,"first_missing_ordinal");v.failure_code=t.at("failure_code").get_ref<const std::string&>();need(valid_utf8(v.failure_code));
        accesses.emplace_back();/* New stable callback adapter for the current trace. */ auto& access=accesses.back();access.validator=this;access.kind=static_cast<unsigned>(k);v.records.context=&access;v.records.callback=&Access::read;
        summaries.emplace_back();/* Summary storage belonging to the current trace view. */ auto& sums=summaries.back();
        if(status==1||status==4) {
            need(!t.contains("columns")&&!t.contains("criteria")&&!t.contains("summaries"));if(k==1||k==3)d.status_names={"unavailable"};if(k==4)d.event_names={"unavailable"};
        } else {
            need(t.contains("columns"));/* Exact typed column descriptor object for a recorded trace. */ const auto& cols=t.at("columns");need(cols.is_object()&&cols.size()<=24);budget.temp(product(cols.size(),sizeof(Array)+128));
            /* Permitted/required column keys collected while claiming sections. */ std::set<std::string> expected;
            /* Add named typed column with exactly the retained row count. */ auto column=[&](const char* name,const char* type){
                // Register the expected name, validate its dtype/shape, and claim its unique data section.
                expected.insert(name);access.columns.emplace(name,array(cols.at(name),type,{count}));};
            column("record_index","u64");
            if(k==0){column("accepted_step","u64");column("time_s","f64");column("nonlinear_iterations","u64");need(!t.contains("criteria"));}
            else if(k<=3) {
                column("attempt_id","u64");column("factor","f64");column("metric_valid","u8");for(/* Canonical static metric name from the shared six-field order. */ auto name:metric_names)column(name,"f64");
                if(k==2)column("accepted_level_id","u64");else {column("certified","u8");column("rolled_back","u8");column("status_index","u32");expected.insert("status_names");/* Nonempty source-owned dictionary population, limited to 64 entries. */ auto names=uint(cols.at("status_names"),"count",64);need(names>0);d.status_names=pool(cols.at("status_names"),names);
                    if(k==1){column("accepted_level_present","u8");column("accepted_level_id","u64");column("newton_record_count","u64");}
                    else {column("iteration_id","u64");column("row_kind","u8");}}
                /* Immutable static criteria required for recorded static traces. */ const auto& c=t.at("criteria");keys(c,{"norm","evaluation_time_s","force_absolute_N","moment_absolute_Nm","relative_tolerance","force_scale_N","moment_scale_Nm","force_limit_N","moment_limit_Nm","constraint_translation_limit_m","constraint_rotation_limit_rad","increment_translation_limit_m","increment_rotation_limit_rad"});need(text(c,"norm")=="block_max_abs_component");
                /* Typed criteria in the wire-specified units for shared validation. */ StaticCriteria x;x.evaluation_time_s=real(c,"evaluation_time_s");x.force_absolute_N=real(c,"force_absolute_N");x.moment_absolute_Nm=real(c,"moment_absolute_Nm");x.relative_tolerance=real(c,"relative_tolerance");x.force_scale_N=real(c,"force_scale_N");x.moment_scale_Nm=real(c,"moment_scale_Nm");x.force_limit_N=real(c,"force_limit_N");x.moment_limit_Nm=real(c,"moment_limit_Nm");x.constraint_translation_limit_m=real(c,"constraint_translation_limit_m");x.constraint_rotation_limit_rad=real(c,"constraint_rotation_limit_rad");x.increment_translation_limit_m=real(c,"increment_translation_limit_m");x.increment_rotation_limit_rad=real(c,"increment_rotation_limit_rad");d.criteria=x;
            } else {
                need(!t.contains("criteria"));column("sequence","u64");column("event_name_index","u32");column("severity","u8");column("identity_valid","u8");column("time_s","f64");column("accepted_step","u64");column("accepted_level","u64");column("attempt_id","u64");expected.insert("event_names");/* Nonempty source-owned dictionary population, limited to 64 entries. */ auto names=uint(cols.at("event_names"),"count",64);need(names>0);d.event_names=pool(cols.at("event_names"),names);
            }
            need(cols.size()==expected.size());for(/* Descriptor iterator checking exact keys or summary fields. */ auto it=cols.begin();it!=cols.end();++it)need(expected.contains(it.key()));
            if(t.contains("summaries")) {
                need(k!=4);/* Single nonempty retained-valid-records summary wrapper. */ const auto& s=t.at("summaries");keys(s,{"population","fields"});need(text(s,"population")=="retained_valid_records");/* Summary fields admitted only for the applicable trace measures. */ const auto& fields=s.at("fields");need(fields.is_object()&&!fields.empty()&&fields.size()<=8);budget.temp(product(fields.size(),sizeof(DiagnosticSummary)));sums.reserve(fields.size());
                for(/* Descriptor iterator checking exact keys or summary fields. */ auto it=fields.begin();it!=fields.end();++it){/* Resolved measure index; eight is the unresolved sentinel. */ unsigned measure=8;for(/* Candidate measure enum matched by its registered wire name. */ unsigned m=0;m<8;++m)if(it.key()==diagnostic_measure_name(static_cast<DiagnosticMeasure>(m)))measure=m;need(measure<8);/* Typed summary with exact integer extrema or binary64 extrema as applicable. */ DiagnosticSummary s_;s_.measure=static_cast<DiagnosticMeasure>(measure);/* Current summary statistics object with canonical count/presence fields. */ const auto& a=it.value();keys(a,{"count","min","max","mean","median_present","median"});s_.count=uint(a,"count");if(measure<2){s_.min_integer=uint(a,"min");s_.max_integer=uint(a,"max");}else{s_.min=real(a,"min");s_.max=real(a,"max");}s_.mean=real(a,"mean");s_.median_present=flag(a,"median_present");s_.median=real(a,"median");sums.push_back(s_);}
                // Shared row scanning verifies count/min/max; rounded mean and optional median remain producer assertions.
                v.summaries=sums;
            }
        }
        descriptions.push_back(std::move(d));views.push_back(v);
    }
    /* Shared row-validation result and recomputed aggregate diagnostic status. */ auto status=validate_diagnostics(descriptions,views,accepted);if(!status)throw Failure{status.error()};need(uint(diag,"status",4)==static_cast<unsigned>(*status));
    /* Run outcome checked against the aggregate diagnostic capture result. */ auto outcome=uint(out.run.at("info"),"run_outcome");if(outcome==7)need(*status==DiagnosticStatus::failed);if(outcome==0)need(*status!=DiagnosticStatus::failed);
}
// Compiler-generated construction leaves metadata empty and the private file owner null.
VerifiedSpoolData::VerifiedSpoolData()=default;
// Compiler-generated destruction releases the graph and Impl-owned FILE/tile.
VerifiedSpoolData::~VerifiedSpoolData()=default;
// Compiler-generated move transfers the file and owned graph without throwing.
VerifiedSpoolData::VerifiedSpoolData(VerifiedSpoolData&&) noexcept=default;
// Compiler-generated move assignment releases old ownership, transfers the source and returns *this.
VerifiedSpoolData& VerifiedSpoolData::operator=(VerifiedSpoolData&&) noexcept=default;
/** Shared synchronous document admission used by public preflight and common-original validation. document borrows immutable bytes; limits govern applicable loading/parser budgets; optional expected verifies exact SHA-256. Returns kind/charges/digest or Status, retaining no bytes. */
std::expected<DocumentPreflight,Status> preflight_document_impl(const DocumentView& document,DocumentPreflightLimits limits,const Digest* expected=nullptr) noexcept {
    // Handle inactive zero-byte states without invoking their source, then validate active metadata/limits.
    // For complete JSON, load bounded bytes and run the shared strict document parser.
    // For partial/non-JSON evidence, stream hash tiles without parsing; return charges and translate failures.

    try {
        need(static_cast<unsigned>(document.state)<=3);
        if(document.state==SectionState::disabled||document.state==SectionState::unavailable){need(document.bytes.size==0);return DocumentPreflight{};}
        need(!document.media_type.empty()&&valid_utf8(document.media_type));
        need(limits.metadata_budget_bytes&&limits.workspace_budget_bytes&&limits.document_bytes&&limits.document_parser_budget_bytes&&limits.copy_tile_bytes>=4096&&limits.copy_tile_bytes<=1048576);
        need(document.bytes.size<=4294967296ull,StatusCode::resource_limit);
        /* Isolated preflight budget; no spool working set is resident during this call. */ Budget budget{limits.metadata_budget_bytes,limits.workspace_budget_bytes};budget.work(262144);/* Incremental digest over the exact retained source bytes. */ Sha256 hash;/* Returned kind, source extent, digest and conservative workspace/parser charges. */ DocumentPreflight result;result.source_bytes=document.bytes.size;
        /* Finalize the source digest and optionally enforce the immutable authored binding. */ auto finish_hash=[&](){
            // Publish the exact-byte digest and reject a mismatch when the caller supplied an authored digest.
            result.sha256=hash.finish();if(expected)need(result.sha256==*expected,StatusCode::verification_failure);};
        if(document.state==SectionState::included&&document.media_type=="application/json") {
            result.kind=DocumentPreflightKind::json_document;
            need(document.bytes.size<=limits.document_bytes,StatusCode::resource_limit);
            budget.temp(document.bytes.size+32);/* Bounded complete JSON bytes, materialized only for included application/json. */ std::string raw(static_cast<std::size_t>(document.bytes.size),'\0');
            /* Writable byte view into the owned JSON string. */ auto storage=std::as_writable_bytes(std::span(raw.data(),raw.size()));
            for(/* Source offset advanced by exact bounded callback reads. */ std::uint64_t at=0;at<document.bytes.size;){/* Current read length, bounded by the configured copy tile. */ auto n=std::min(limits.copy_tile_bytes,document.bytes.size-at);/* Destination subspan within the complete JSON storage. */ auto tile=storage.subspan(at,n);okay(document.bytes.read(at,tile));hash.update(tile);at+=n;}
            finish_hash();
            result.parser_charge_bytes=admit_document_json(raw,budget,limits.document_parser_budget_bytes);
        } else {
            result.kind=DocumentPreflightKind::retained_bytes;
            budget.work(limits.copy_tile_bytes);/* Reusable bounded tile for partial or non-JSON retained evidence. */ std::vector<std::byte> tile(static_cast<std::size_t>(limits.copy_tile_bytes));
            for(/* Source offset advanced by exact bounded callback reads. */ std::uint64_t at=0;at<document.bytes.size;){/* Current read length, bounded by the configured copy tile. */ auto n=std::min<std::uint64_t>(tile.size(),document.bytes.size-at);/* Active prefix of the reusable hashing tile. */ auto bytes=std::span(tile).first(n);okay(document.bytes.read(at,bytes));hash.update(bytes);at+=n;}
            finish_hash();
        }
        result.workspace_bytes=budget.peak;return result;
    }catch(/* Private admission failure translated without losing its category. */ const Failure& f){return std::unexpected(f.status);}catch(const std::bad_alloc&){return std::unexpected(error(StatusCode::resource_limit,80));}catch(...){return std::unexpected(error(StatusCode::invalid_contract,80));}
}
/** Validate common original document against expected authored digest under converted limits before a spool working set is resident. Partial originals are an unsupported merger capability. Returns admitted workspace bytes or Status. */
std::expected<std::uint64_t,Status> validate_original_document(const DocumentView& document,const Digest& expected,SpoolReadLimits limits) {
    // Reject the unsupported partial-original capability, convert limits, and invoke shared preflight with digest verification.
    // Return only the admitted workspace charge needed by bundle planning.

    if(document.state==SectionState::partial)return std::unexpected(error(StatusCode::unsupported_feature,89));
    /* Equivalent public limits, preserving byte/parser/metadata/workspace/tile units. */ DocumentPreflightLimits public_limits{limits.document_bytes,limits.document_parser_budget_bytes,limits.metadata_budget_bytes,limits.workspace_budget_bytes,limits.copy_tile_bytes};
    /* Shared admission result, including any digest or parser failure. */ auto result=preflight_document_impl(document,public_limits,&expected);if(!result)return std::unexpected(result.error());
    return result->workspace_bytes;
}
/** Open read-only path and completely validate a single-run spool against owned binding/limits. Returns move-only file/metadata ownership or translated Status; no partially validated graph escapes. */
std::expected<VerifiedSpoolData,Status> VerifiedSpoolData::open(const std::filesystem::path& path,const SpoolBinding& binding,SpoolReadLimits limits) {
    // Validate limits/binding sizes and allocate the bounded file owner/tile.
    // Open the file, compare expected extent/hash when bound, then run header, graph, value, diagnostic and provenance phases.
    // Finish ownership/capability checks and pin a digest for admitted unbound imports before returning the owner.

    try {
        need(limits.metadata_budget_bytes&&limits.workspace_budget_bytes&&limits.copy_tile_bytes>=4096&&limits.copy_tile_bytes<=1048576&&limits.document_bytes&&limits.document_parser_budget_bytes,StatusCode::invalid_contract);
        need(binding.expected_run_id.size()<=1048576&&binding.producer_identity.size()<=1048576&&binding.dependency_identity.size()<=1048576,StatusCode::resource_limit);
        need(valid_utf8(binding.expected_run_id)&&valid_utf8(binding.producer_identity)&&valid_utf8(binding.dependency_identity));
        need(2*(binding.expected_run_id.size()+binding.producer_identity.size()+binding.dependency_identity.size())+sizeof(Impl)+sizeof(VerifiedSpoolData)<=std::min(limits.metadata_budget_bytes,limits.workspace_budget_bytes),StatusCode::resource_limit);
        /* Local owning result; any failure destroys the partial graph and closes the file. */ VerifiedSpoolData out;out.impl_=std::make_unique<Impl>();/* Private owned file state, including immutable binding and reusable tile. */ auto& f=*out.impl_;f.binding=binding;f.limits=limits;
        need(limits.copy_tile_bytes<=limits.workspace_budget_bytes,StatusCode::resource_limit);f.tile.resize(static_cast<std::size_t>(limits.copy_tile_bytes));
#ifdef _WIN32
        f.file=_wfsopen(path.c_str(),L"rb",_SH_DENYNO);
#else
        f.file=std::fopen(path.c_str(),"rb");
#endif
        need(f.file!=nullptr,StatusCode::io_failure,81);f.size=f.extent();need(f.size<=maximum_extent,StatusCode::resource_limit);
        if(binding.expected_file_bytes){need(binding.expected_file_bytes==f.size,StatusCode::verification_failure);need(f.hash(0,f.size)==binding.expected_file_sha256,StatusCode::verification_failure);}
        else need(!binding.trusted_manifest_binding,StatusCode::unsupported_feature,89);
        /* Synchronous full-graph validator and conservative live-memory accounting. */ SpoolValidator validate(out);validate.budget.retain(binding.expected_run_id.capacity()+binding.producer_identity.capacity()+binding.dependency_identity.capacity()+sizeof(Impl)+sizeof(VerifiedSpoolData));
        validate.header();validate.root_metadata();validate.run_info();validate.catalogs();validate.values();validate.diagnostics();validate.provenance();validate.finish();
        // Even unbound included originals gain a stable byte identity for the later recheck.
        if(!f.binding.expected_file_bytes){f.binding.expected_file_bytes=f.size;f.binding.expected_file_sha256=f.hash(0,f.size);}
        return out;
    } catch(/* Private validation failure whose exact public status is preserved. */ const Failure& f){return std::unexpected(f.status);}catch(const std::bad_alloc&){return std::unexpected(error(StatusCode::resource_limit,80));}catch(...){return std::unexpected(error(StatusCode::invalid_contract,80));}
}
/** Fill bytes_ from section at relative_offset using the owned file cursor. Returns range/contract/I/O Status; byte output is borrowed only during the call. */
Status VerifiedSpoolData::read(std::uint64_t section,std::uint64_t relative_offset,std::span<std::byte> bytes_) noexcept {
    // Check owner, section and relative extent; translate the validated range to an absolute file read.

    try {need(impl_&&section<directory.size());/* Already-admitted directory extent addressed by this section ID. */ const auto& d=directory[section];need(relative_offset<=d.size&&bytes_.size()<=d.size-relative_offset);impl_->read_at(d.offset+relative_offset,bytes_);return {};}
    catch(/* Range or file-read failure converted to the public status. */ const Failure& f){return f.status;}catch(...){return error(StatusCode::io_failure,81);}
}
/** Recheck actual open-file extent and complete SHA-256 against the pinned binding. Returns invalid_state without an owner or verification/I/O status on change; does not lock out external mutation. */
Status VerifiedSpoolData::verify_unchanged() noexcept {
    // Require a live owner, remeasure the file, and hash it in bounded tiles against the admitted identity.

    try {need(impl_!=nullptr,StatusCode::invalid_state);need(impl_->extent()==impl_->binding.expected_file_bytes,StatusCode::verification_failure);need(impl_->hash(0,impl_->size)==impl_->binding.expected_file_sha256,StatusCode::verification_failure);return {};}
    catch(/* Extent/digest or I/O failure converted to the public status. */ const Failure& f){return f.status;}catch(...){return error(StatusCode::io_failure,81);}
}
} // namespace serene::outputs::detail
namespace serene::outputs {
/** Public forwarding entry for synchronous document preflight. document borrows immutable source bytes; limits apply by state/media type. Returns kind, exact digest and conservative charges or Status. */
std::expected<DocumentPreflight,Status> preflight_document(const DocumentView& document,DocumentPreflightLimits limits) noexcept {
    // Delegate to the same document admission implementation used by actual spool and common-original validation.

    return detail::preflight_document_impl(document,limits);
}
} // namespace serene::outputs
