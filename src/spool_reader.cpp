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
struct Failure { Status status; };
void need(bool yes,StatusCode code=StatusCode::invalid_contract,std::uint32_t op=80) {
    if(!yes)throw Failure{error(code,op)};
}
void okay(Status status) {if(!status)throw Failure{status};}
std::uint64_t le(std::span<const std::byte> b) noexcept {
    std::uint64_t n=0;for(std::size_t i=0;i<b.size();++i)n|=std::uint64_t(std::to_integer<unsigned char>(b[i]))<<(8*i);return n;
}
bool positive_zero(double v) noexcept {return v==0&&!std::signbit(v);}
bool valid_uuid(std::string_view s) noexcept {
    if(s.size()!=36)return false;
    for(std::size_t i=0;i<s.size();++i)if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}
    else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;
    return true;
}
bool valid_utc(std::string_view s) noexcept {
    if(s.size()<20||s[4]!='-'||s[7]!='-'||s[10]!='T'||s[13]!=':'||s[16]!=':'||s.back()!='Z')return false;
    auto number=[&](std::size_t start,std::size_t n){int v=0;for(std::size_t i=0;i<n;++i){char c=s[start+i];if(c<'0'||c>'9')return -1;v=v*10+c-'0';}return v;};
    auto y=number(0,4),m=number(5,2),d=number(8,2),h=number(11,2),minutes=number(14,2),seconds=number(17,2);
    if(y<0||m<1||m>12||d<1||h<0||h>23||minutes<0||minutes>59||seconds<0||seconds>59)return false;
    constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(d>days[m-1]+(m==2&&y%4==0&&(y%100!=0||y%400==0)))return false;
    if(s.size()>20){if(s[19]!='.'||s.size()<22)return false;for(std::size_t i=20;i<s.size()-1;++i)if(s[i]<'0'||s[i]>'9')return false;}
    return true;
}
std::uint64_t uint(const Json& j,std::uint64_t limit=UINT64_MAX) {
    need(j.is_number_unsigned() || (j.is_number_integer()&&j.get<std::int64_t>()>=0));
    auto n=j.get<std::uint64_t>();need(n<=limit);return n;
}
std::uint64_t uint(const Json& j,const char* key,std::uint64_t limit=UINT64_MAX) {return uint(j.at(key),limit);}
double real(const Json& j) {need(j.is_number());auto v=j.get<double>();need(std::isfinite(v));return v;}
double real(const Json& j,const char* key) {return real(j.at(key));}
std::string text(const Json& j,bool nonempty=false) {
    need(j.is_string());auto s=j.get<std::string>();need(valid_utf8(s)&&(!nonempty||!s.empty()));return s;
}
std::string text(const Json& j,const char* key,bool nonempty=false) {return text(j.at(key),nonempty);}
bool flag(const Json& j,const char* key) {return uint(j,key,1)!=0;}
void keys(const Json& j,std::initializer_list<const char*> names) {
    need(j.is_object()&&j.size()==names.size());for(auto name:names)need(j.contains(name));
}
constexpr const char* metric_names[]={"force_residual_N","moment_residual_Nm","constraint_translation_m","constraint_rotation_rad","increment_translation_m","increment_rotation_rad"};
struct Budget {
    std::uint64_t metadata_limit{},workspace_limit{},retained{},temporary{},workspace{},peak{};
    void check() {std::uint64_t a{},b{};need(checked_add(retained,temporary,a)&&a<=metadata_limit&&checked_add(a,workspace,b)&&b<=workspace_limit,StatusCode::resource_limit);peak=std::max(peak,b);}
    void retain(std::uint64_t n) {need(checked_add(retained,n,retained),StatusCode::resource_limit);check();}
    void temp(std::uint64_t n) {need(checked_add(temporary,n,temporary),StatusCode::resource_limit);check();}
    void work(std::uint64_t n) {need(checked_add(workspace,n,workspace),StatusCode::resource_limit);check();}
};
struct TempScope { Budget& b;std::uint64_t saved;explicit TempScope(Budget& x):b(x),saved(x.temporary){}~TempScope(){b.temporary=saved;} };
std::uint64_t product(std::uint64_t a,std::uint64_t b) {std::uint64_t out{};need(checked_multiply(a,b,out),StatusCode::resource_limit);return out;}
// Admission for a second retained copy of already-bounded metadata. Walk before
// copying; never serialize the object merely to estimate its allocation cost.
std::uint64_t copy_charge(const Json& j) {
    std::uint64_t charge=128;
    if(j.is_string())charge+=4*j.get_ref<const std::string&>().size();
    else if(j.is_array()){charge+=16*j.size();for(const auto& child:j)charge+=copy_charge(child);}
    else if(j.is_object())for(auto it=j.begin();it!=j.end();++it)charge+=128+4*it.key().size()+copy_charge(it.value());
    return charge;
}
// The document scanner bounds complexity before any general JSON materialization. It is
// deliberately conservative on decoded string size (encoded bytes upper-bound scalar count).
std::uint64_t document_charge(std::string_view s) {
    need(!s.starts_with("\xEF\xBB\xBF"));
    std::uint64_t values=0,members=0,entries=0,depth=0,maximum=0;
    bool in_string=false,escaped=false;std::size_t token=0;
    for(std::size_t i=0;i<s.size();++i) {
        unsigned char c=static_cast<unsigned char>(s[i]);
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
Json strict_parse(std::string_view bytes,bool metadata,bool object_only,std::uint64_t budget,std::uint64_t& charge) {
    if(metadata) {
        okay(json_contract::preflight(bytes,budget));json_contract::Scan scan{bytes};scan.value(0);
        charge=5*bytes.size()+4*scan.strings+128*scan.values+128*scan.members+16*scan.array_entries+256*scan.depth;
    } else {charge=document_charge(bytes);need(charge<=budget,StatusCode::resource_limit);}
    bool duplicate=false;std::vector<std::set<std::string>> stack;
    auto cb=[&](int,nlohmann::json::parse_event_t event,Json& value) {
        if(event==Json::parse_event_t::object_start)stack.emplace_back();
        else if(event==Json::parse_event_t::object_end){need(!stack.empty());stack.pop_back();}
        else if(event==Json::parse_event_t::key){need(!stack.empty());if(!stack.back().insert(value.get<std::string>()).second)duplicate=true;}
        if(event==Json::parse_event_t::value&&value.is_number_float())need(std::isfinite(value.get<double>()));
        return true;
    };
    auto j=Json::parse(bytes,cb,false);need(!j.is_discarded()&&!duplicate&&(!object_only||j.is_object()));return j;
}
// One document grammar/complexity admission operation for public preflight, actual spool
// document reads and common-original validation. Byte loading is caller-specific and budgeted.
std::uint64_t admit_document_json(std::string_view raw,Budget& budget,std::uint64_t parser_limit,bool object_only=false) {
    const auto charge=document_charge(raw);need(charge<=parser_limit,StatusCode::resource_limit);
    budget.temp(charge);std::uint64_t measured{};(void)strict_parse(raw,false,object_only,charge,measured);return charge;
}
}
struct VerifiedSpoolData::Impl {
    std::FILE* file{};SpoolBinding binding;SpoolReadLimits limits;std::uint64_t size{};std::vector<std::byte> tile;
    ~Impl(){if(file)std::fclose(file);}
    void read_at(std::uint64_t offset,std::span<std::byte> out) {
        need(offset<=size&&out.size()<=size-offset);
#ifdef _WIN32
        need(_fseeki64(file,static_cast<__int64>(offset),SEEK_SET)==0,StatusCode::io_failure,81);
#else
        need(fseeko(file,static_cast<off_t>(offset),SEEK_SET)==0,StatusCode::io_failure,81);
#endif
        need(std::fread(out.data(),1,out.size(),file)==out.size(),StatusCode::io_failure,81);
    }
    std::uint64_t extent() {
#ifdef _WIN32
        need(_fseeki64(file,0,SEEK_END)==0,StatusCode::io_failure,81);auto n=_ftelli64(file);
#else
        need(fseeko(file,0,SEEK_END)==0,StatusCode::io_failure,81);auto n=ftello(file);
#endif
        need(n>=0,StatusCode::io_failure,81);return static_cast<std::uint64_t>(n);
    }
    Digest hash(std::uint64_t offset,std::uint64_t count) {
        Sha256 h;while(count){auto n=std::min<std::uint64_t>(count,tile.size());read_at(offset,std::span(tile).first(n));h.update(std::span(tile).first(n));offset+=n;count-=n;}return h.finish();
    }
};
struct SpoolValidator {
    VerifiedSpoolData& out;VerifiedSpoolData::Impl& file;Budget budget;
    std::vector<std::byte> owned,source_used;
    std::uint64_t minor{},catalog_channels{},committed{},accepted{};
    bool feature_d{},feature_p{},feature_f{},used_f{};
    bool partial_original{};
    struct Array {
        SpoolValidator* owner{};std::uint64_t section{},count{},width{};std::uint32_t kind{};std::array<std::uint64_t,2> shape{};unsigned rank{};
        mutable std::array<std::byte,4096> cache{};mutable std::uint64_t cache_begin{UINT64_MAX},cache_size{};
        std::uint64_t bits(std::uint64_t index) const {
            need(index<count&&width<=8);auto off=product(index,width);
            if(cache_begin==UINT64_MAX||off<cache_begin||off+width>cache_begin+cache_size) {
                cache_begin=(off/cache.size())*cache.size();cache_size=std::min<std::uint64_t>(cache.size(),owner->out.directory[section].size-cache_begin);
                okay(owner->out.read(section,cache_begin,std::span(cache).first(cache_size)));
            }
            return le(std::span(cache).subspan(static_cast<std::size_t>(off-cache_begin),static_cast<std::size_t>(width)));
        }
        double real(std::uint64_t index) const {need(kind==5);return std::bit_cast<double>(bits(index));}
    };
    explicit SpoolValidator(VerifiedSpoolData& x):out(x),file(*x.impl_),budget{file.limits.metadata_budget_bytes,file.limits.workspace_budget_bytes} {
        budget.work(file.tile.capacity()+262144); // bounded stack/cursor/parser control reserve
    }
    void mark(std::uint64_t id) {
        need(id<out.directory.size());auto& b=owned[id/8];auto bit=std::byte(1u<<(id%8));need((b&bit)==std::byte{});b|=bit;
    }
    bool is_marked(std::uint64_t id) const {return (owned[id/8]&std::byte(1u<<(id%8)))!=std::byte{};}
    Array array(const Json& j,const char* type,std::initializer_list<std::uint64_t> expected={}) {
        keys(j,{"section","dtype","shape"});auto tag=text(j,"dtype");need(tag==type);
        std::uint32_t kind=0;std::uint64_t width=0;
        if(tag=="u8"){kind=2;width=1;}else if(tag=="u32"){kind=3;width=4;}else if(tag=="u64"){kind=4;width=8;}else if(tag=="f64"){kind=5;width=8;}else if(tag=="frames77"){kind=6;width=77;}else if(tag=="i8"){kind=7;width=1;}else if(tag=="i32"){kind=8;width=4;}else need(false);
        auto id=uint(j,"section");need(id<out.directory.size()&&out.directory[id].kind==kind);
        const auto& shape=j.at("shape");need(shape.is_array()&&!shape.empty()&&shape.size()<=2);
        Array a;a.owner=this;a.section=id;a.kind=kind;a.width=width;a.rank=static_cast<unsigned>(shape.size());a.count=1;
        for(unsigned i=0;i<a.rank;++i){a.shape[i]=uint(shape[i]);need(a.shape[i]<=(1ull<<40),StatusCode::resource_limit);a.count=product(a.count,a.shape[i]);}
        need(product(a.count,width)==out.directory[id].size);if(expected.size()){need(expected.size()==a.rank);unsigned i=0;for(auto n:expected)need(a.shape[i++]==n);}
        mark(id);return a;
    }
    std::vector<std::string> pool(const Json& j,std::uint64_t expected,bool optional=false,std::vector<std::uint8_t>* presence=nullptr) {
        keys(j,optional?std::initializer_list<const char*>{"count","bytes","offsets","present"}:std::initializer_list<const char*>{"count","bytes","offsets"});
        auto n=uint(j,"count",1000000);need(n==expected);auto raw=array(j.at("bytes"),"u8");need(raw.rank==1&&raw.count<=268435456,StatusCode::resource_limit);
        auto offsets=array(j.at("offsets"),"u64",{n+1});need(offsets.bits(0)==0&&offsets.bits(n)==raw.count);
        budget.temp(product(n,sizeof(std::string))+raw.count+product(n,16));
        std::vector<std::string> result;result.reserve(static_cast<std::size_t>(n));
        std::optional<Array> present;if(optional){present=array(j.at("present"),"u8",{n});if(presence){budget.temp(n);presence->reserve(static_cast<std::size_t>(n));}}
        std::uint64_t off=0;
        for(std::uint64_t i=0;i<n;++i) {
            auto end=offsets.bits(i+1);need(end>=off&&end<=raw.count);need(end-off<=1048576,StatusCode::resource_limit);
            std::string s(static_cast<std::size_t>(end-off),'\0');okay(out.read(raw.section,off,std::as_writable_bytes(std::span(s.data(),s.size()))));need(valid_utf8(s));
            if(optional){auto yes=present->bits(i);need(yes<=1&&(yes||s.empty()));if(presence)presence->push_back(static_cast<std::uint8_t>(yes));}
            result.push_back(std::move(s));off=end;
        }
        return result;
    }
    Json metadata(std::uint64_t id) {
        need(id<out.directory.size()&&out.directory[id].kind==1);mark(id);auto n=out.directory[id].size;need(n&&n<=1048576,StatusCode::resource_limit);
        TempScope scope(budget);budget.temp(n);std::string raw(static_cast<std::size_t>(n),'\0');okay(out.read(id,0,std::as_writable_bytes(std::span(raw.data(),raw.size()))));
        std::uint64_t charge{};okay(json_contract::preflight(raw,budget.metadata_limit-budget.retained-budget.temporary));json_contract::Scan scan{raw};scan.value(0);charge=5*raw.size()+4*scan.strings+128*scan.values+128*scan.members+16*scan.array_entries+256*scan.depth;
        budget.retain(charge);std::uint64_t actual{};return strict_parse(raw,true,true,charge,actual);
    }
    void json_bytes(std::uint64_t section,std::uint64_t offset,std::uint64_t count,bool original,bool object_only) {
        TempScope scope(budget);auto limit=original?file.limits.document_bytes:1048576;need(count<=limit,StatusCode::resource_limit);
        budget.temp(count);std::string raw(static_cast<std::size_t>(count),'\0');okay(out.read(section,offset,std::as_writable_bytes(std::span(raw.data(),raw.size()))));
        auto remaining=std::min(budget.metadata_limit-budget.retained-budget.temporary,file.limits.document_parser_budget_bytes);
        if(original){(void)admit_document_json(raw,budget,remaining,object_only);return;}
        std::uint64_t charge{};
        okay(json_contract::preflight(raw,remaining));json_contract::Scan s{raw};s.value(0);charge=5*raw.size()+4*s.strings+128*s.values+128*s.members+16*s.array_entries+256*s.depth;
        need(charge<=remaining,StatusCode::resource_limit);budget.temp(charge);std::uint64_t actual{};(void)strict_parse(raw,!original,object_only,charge,actual);
    }
    SectionState document(const Json& j,bool original_role=false) {
        auto state=uint(j,"state",3);
        if(state==1||state==2){keys(j,{"state"});return static_cast<SectionState>(state);}
        keys(j,{"state","media_type","bytes"});auto media=text(j,"media_type");auto data=array(j.at("bytes"),"u8");need(data.rank==1&&data.count<=4294967296ull,StatusCode::resource_limit);
        if(original_role&&state==3)partial_original=true;
        if(state==0&&media=="application/json")json_bytes(data.section,0,data.count,true,false);
        return static_cast<SectionState>(state);
    }
    void header() {
        need(file.size>=128);std::array<std::byte,128> h{};file.read_at(0,h);
        const std::array<std::byte,8> magic={std::byte{'S'},std::byte{'E'},std::byte{'R'},std::byte{'E'},std::byte{'N'},std::byte{'E'},std::byte{},std::byte{}};
        need(std::equal(magic.begin(),magic.end(),h.begin()));Digest header_digest{};std::copy_n(h.begin()+96,32,header_digest.begin());need(sha256(std::span(h).first(96))==header_digest,StatusCode::verification_failure);
        auto u=[&](std::size_t off,std::size_t n){return le(std::span(h).subspan(off,n));};
        need(u(8,2)==1,StatusCode::unsupported_feature);minor=u(10,2);need(minor<=1,StatusCode::unsupported_feature);
        need(u(12,4)==0x01020304&&std::endian::native==std::endian::little,StatusCode::unsupported_feature);
        need((u(56,8)&~1ull)==0,StatusCode::unsupported_feature);need(u(16,4)==128&&u(20,4)==64&&u(24,8)==file.size&&u(56,8)==1);
        auto start=u(32,8),count=u(40,8);need(count>0);need(count<=1048576,StatusCode::resource_limit);need(start>=128&&start<=file.size&&product(count,64)==file.size-start);
        out.root_section=u(48,8);need(out.root_section<count);
        budget.retain(product(count,std::max<std::size_t>(64,sizeof(DirectoryEntry)))+((count+7)/8));out.directory.resize(static_cast<std::size_t>(count));owned.resize(static_cast<std::size_t>((count+7)/8));
        Digest expected{};std::copy_n(h.begin()+64,32,expected.begin());need(file.hash(start,file.size-start)==expected,StatusCode::verification_failure);
        std::uint64_t next=128;std::array<std::byte,64> row{};
        for(std::uint64_t i=0;i<count;++i) {
            file.read_at(start+i*64,row);need(le(std::span(row).first(8))==i);need(le(std::span(row).subspan(12,4))==0,StatusCode::unsupported_feature);
            auto& d=out.directory[static_cast<std::size_t>(i)];d.kind=static_cast<std::uint32_t>(le(std::span(row).subspan(8,4)));d.offset=le(std::span(row).subspan(16,8));d.size=le(std::span(row).subspan(24,8));
            need(d.kind>=1&&d.kind<=8,StatusCode::unsupported_feature);need(d.offset==next&&next<=start&&d.size<=start-next);next+=d.size;
            std::copy_n(row.begin()+32,32,d.digest.begin());need(file.hash(d.offset,d.size)==d.digest,StatusCode::verification_failure);
        }
        need(next==start);out.summary.file_bytes=file.size;out.summary.section_count=count;
    }
    void root_metadata() {
        out.root=metadata(out.root_section);const auto& fmt=out.root.at("format");need(text(fmt,"name")=="serene.outputs"&&uint(fmt,"version_major")==1&&uint(fmt,"version_minor")==minor&&text(fmt,"write_state")=="complete");
        need(valid_uuid(text(fmt,"file_id",true))&&valid_utc(text(fmt,"created_utc",true)));(void)text(fmt,"writer_name",true);(void)text(fmt,"writer_version",true);
        const auto& features=fmt.at("required_features");need(features.is_array()&&features.size()<=1024);std::set<std::string> names;
        const std::set<std::string> base={"serene-container-le-1","json-descriptors-1","utf8-pools-1","frames-77-le-1","zfp-5-le64-serial-1"};
        for(const auto& f:features){auto s=text(f,true);need(names.insert(s).second);if(!base.contains(s)){need(minor==1,StatusCode::unsupported_feature);if(s=="run-diagnostics-1")feature_d=true;else if(s=="provenance-sources-1")feature_p=true;else if(s=="timeseries-zfp-fixed-accuracy-1")feature_f=true;else need(false,StatusCode::unsupported_feature);}}
        for(const auto& s:base)need(names.contains(s));need(minor==0||feature_d||feature_p||feature_f);
        for(const char* name:{"inputs","logs","context"})uint(fmt.at("sections"),name,3);
        need(feature_p?!out.root.contains("inputs")&&!out.root.contains("logs")&&!out.root.contains("context")&&out.root.contains("provenance"):!out.root.contains("provenance"));
        TempScope scope(budget);const auto& idx=out.root.at("results").at("index");auto indices=array(idx.at("run_index"),"u64");need(indices.rank==1&&indices.count>=1&&indices.count<=65536,StatusCode::resource_limit);
        need(indices.count==1,StatusCode::unsupported_feature,89);need(indices.bits(0)==0);
        auto ids=pool(idx.at("run_id"),1);need(!ids[0].empty());out.summary.run_id=ids[0];
        std::vector<std::uint8_t> present;auto names_=pool(idx.at("run_name"),1,true,&present);out.summary.run_name=names_[0];out.summary.run_name_present=present[0]!=0;
        auto kinds=array(idx.at("kind"),"u8",{1});need(kinds.bits(0)<=1);out.summary.kind=static_cast<RunKind>(kinds.bits(0));
        auto metadata_ids=array(idx.at("run_metadata_section"),"u64",{1});out.run_section=metadata_ids.bits(0);need(out.run_section!=out.root_section);
        out.summary.diagnostics=feature_d;out.summary.indexed_provenance=feature_p;out.summary.fixed_accuracy=feature_f;
        if(!file.binding.expected_run_id.empty())need(out.summary.run_id==file.binding.expected_run_id);
        budget.retain(out.summary.run_id.capacity()+out.summary.run_name.capacity()+2);
        out.run=metadata(out.run_section);
    }
    void run_info();
    void catalogs();
    void values();
    void diagnostics();
    void provenance();
    void logs(const Json& j,unsigned state);
    void finish() {
        for(std::uint64_t i=0;i<out.directory.size();++i)need(is_marked(i));
        need(!feature_f||used_f);need(!feature_d||!out.traces.empty());
        need(!partial_original,StatusCode::unsupported_feature,89);
        out.summary.retained_metadata_bytes=budget.retained;out.summary.workspace_bytes=budget.peak;
    }
};
void SpoolValidator::run_info() {
    const auto& i=out.run.at("info");need(text(i,"run_id")==out.summary.run_id&&uint(i,"kind",1)==static_cast<unsigned>(out.summary.kind));
    auto outcome=uint(i,"run_outcome",feature_d?7:6),numerical=uint(i,"numerical_status",3),observation=uint(i,"observation_status",3),logging=uint(i,"logging_status",4);
    auto state=uint(i,"catalog_state",2);need(state==0||state==2);committed=uint(i,"committed_count",1ull<<40);accepted=uint(i,"accepted_count");
    auto last_accepted=flag(i,"last_accepted_present"),last_recorded=flag(i,"last_recorded_present"),bounds=flag(i,"requested_bounds_present"),elapsed=flag(i,"elapsed_seconds_present"),axis=flag(i,"identity_axis_present");
    auto la=real(i,"last_accepted_value"),lr=real(i,"last_recorded_value"),begin=real(i,"requested_begin"),end=real(i,"requested_end"),seconds=real(i,"elapsed_seconds");
    need((last_accepted||positive_zero(la))&&(last_recorded||positive_zero(lr))&&last_recorded==(committed>0));
    need(bounds?begin<=end:positive_zero(begin)&&positive_zero(end));need(seconds>=0&&(elapsed||positive_zero(seconds)));
    auto scope=text(i,"elapsed_seconds_scope"),convention=text(i,"identity_axis_convention");need(elapsed!=scope.empty()&&axis!=convention.empty());
    for(auto name:{"failure_stage","failure_code","termination_reason"})(void)text(i,name);
    auto assembly=text(i,"assembly_ref");auto axis_name=text(i,"axis_name"),axis_unit=text(i,"axis_unit");
    if(out.summary.kind==RunKind::timeseries)need(axis_name=="time"&&axis_unit=="s");
    else need(!axis_name.empty());
    for(auto name:{"solver_configuration","sampling_configuration","execution"})document(i.at(name));
    if(state==2) {
        need((outcome==4||outcome==5||outcome==6)&&numerical==0&&observation==3&&!committed&&!axis&&!last_accepted&&!accepted);
        for(auto name:{"channels","requests","values","time","step_index","load_levels","level_index"})need(!out.run.contains(name));
    } else {
        need(!assembly.empty());
        if(outcome==0)need(numerical==1&&(observation==0||observation==2)&&(logging==0||logging==3));
        const char* axis_key=out.summary.kind==RunKind::timeseries?"time":"load_levels";
        const char* index_key=out.summary.kind==RunKind::timeseries?"step_index":"level_index";
        need(!out.run.contains(out.summary.kind==RunKind::timeseries?"load_levels":"time")&&!out.run.contains(out.summary.kind==RunKind::timeseries?"level_index":"step_index"));
        auto values=array(out.run.at(axis_key),"f64",{committed});double previous=0;
        for(std::uint64_t n=0;n<committed;++n){auto x=values.real(n);need(std::isfinite(x)&&(!(n&&out.summary.kind==RunKind::timeseries)||x>previous));previous=x;}
        if(committed)need(lr==previous);
        need(out.run.contains(index_key)==axis);
        if(axis){auto indices=array(out.run.at(index_key),"u64",{committed});std::uint64_t previous_id=0;for(std::uint64_t n=0;n<committed;++n){auto id=indices.bits(n);need(!n||id>previous_id);previous_id=id;}}
    }
    need(feature_d||!out.run.contains("diagnostics"));need(feature_p||!out.run.contains("provenance"));
}
void SpoolValidator::catalogs() {
    if(uint(out.run.at("info"),"catalog_state")==2)return;
    TempScope scope(budget);const auto& c=out.run.at("channels");const auto&q=out.run.at("requests");
    auto index=array(c.at("channel_index"),"u64");need(index.rank==1&&index.count<=1000000,StatusCode::resource_limit);catalog_channels=index.count;
    for(std::uint64_t n=0;n<index.count;++n)need(index.bits(n)==n);
    auto scalars=pool(c.at("scalar_id"),index.count);auto groups=pool(c.at("group_id"),index.count);auto requests=pool(c.at("request_id"),index.count);
    auto components=pool(c.at("component_path"),index.count);auto units=pool(c.at("unit"),index.count);(void)components;(void)units;
    budget.temp(product(index.count,96));std::set<std::string> unique;
    for(const auto& s:scalars){budget.temp(s.capacity()+1);need(!s.empty()&&unique.insert(s).second);}
    auto mapping=array(c.at("request_index"),"u64",{index.count});auto nr=uint(q.at("group_id"),"count",1000000);
    auto qgroups=pool(q.at("group_id"),nr);auto qrequests=pool(q.at("request_id"),nr);(void)pool(q.at("schema_version"),nr);(void)pool(q.at("construction_profile"),nr);
    budget.temp(product(nr,128));std::set<std::pair<std::string,std::string>> request_keys;
    for(std::size_t n=0;n<qgroups.size();++n){budget.temp(qgroups[n].capacity()+qrequests[n].capacity()+2);need(!qgroups[n].empty()&&!qrequests[n].empty()&&request_keys.emplace(qgroups[n],qrequests[n]).second);}
    for(std::uint64_t n=0;n<index.count;++n){auto k=mapping.bits(n);need(k<nr&&groups[n]==qgroups[k]&&requests[n]==qrequests[k]);}
    auto state=uint(q,"effective_state",2);
    if(state==0) {
        auto effective=pool(q.at("effective_json"),nr);auto digests=array(q.at("effective_sha256"),"u8",{nr,32});
        for(std::uint64_t n=0;n<nr;++n){Digest expected{};for(unsigned k=0;k<32;++k)expected[k]=std::byte(digests.bits(n*32+k));auto raw=std::as_bytes(std::span(effective[n].data(),effective[n].size()));need(sha256(raw)==expected,StatusCode::verification_failure);std::uint64_t charge{};auto room=budget.metadata_limit-budget.retained-budget.temporary;okay(json_contract::preflight(effective[n],room));TempScope parse_scope(budget);json_contract::Scan scan{effective[n]};scan.value(0);auto cost=5*effective[n].size()+4*scan.strings+128*scan.values+128*scan.members+16*scan.array_entries+256*scan.depth;budget.temp(cost);(void)strict_parse(effective[n],true,true,cost,charge);}
    } else need(!q.contains("effective_json")&&!q.contains("effective_sha256"));
}
void SpoolValidator::values() {
    if(uint(out.run.at("info"),"catalog_state")==2)return;
    TempScope scope(budget);const auto& v=out.run.at("values");const auto& p=v.at("profiles");const auto& shape=v.at("shape");need(shape.is_array()&&shape.size()==2);
    auto outer=uint(shape[0],1ull<<40),inner=uint(shape[1],1ull<<40);bool ts=out.summary.kind==RunKind::timeseries;
    need(outer==(ts?catalog_channels:committed)&&inner==(ts?committed:catalog_channels));if(!catalog_channels)need(!committed&&!outer&&!inner);
    need(text(v,"dtype")=="float64"&&uint(v,"frame_schema")==1);
    need(v.at("axis_names")==Json::array({ts?"channel":"level",ts?"sample":"channel"}));
    auto B=uint(v,"nominal_frame_values",maximum_frame_values);need(B>=4&&B%4==0);out.summary.maximum_frame_values=B;
    auto rows=array(v.at("row_profile_index"),"u32",{outer});auto m=uint(p.at("profile_index").at("shape")[0],1000000);need(m>0);
    auto profile_index=array(p.at("profile_index"),"u32",{m});auto modes=array(p.at("mode"),"u8",{m});auto versions=array(p.at("codec_version"),"u32",{m});auto words=array(p.at("stream_word_bits"),"u32",{m});auto rounding=array(p.at("rounding"),"i8",{m});auto codes=array(p.at("mode_code"),"u64",{m});auto present=array(p.at("tolerance_present"),"u8",{m});auto requested=array(p.at("requested_tolerance"),"f64",{m});auto effective=array(p.at("effective_tolerance"),"f64",{m});
    for(auto [name,value]:std::array<std::pair<const char*,const char*>,5>{{{"codec","zfp"},{"library_version","1.0.1"},{"library_revision","f40868a6a1c190c802e7d8b5987064f044bf7812"},{"byte_order","little"},{"execution","serial"}}}) {
        TempScope one(budget);auto names=pool(p.at(name),m);for(const auto&s:names)need(s==value,StatusCode::unsupported_profile);
    }
    budget.temp(product(m,sizeof(CodecProfile)));std::vector<CodecProfile> profiles;profiles.reserve(static_cast<std::size_t>(m));
    for(std::uint64_t n=0;n<m;++n){need(profile_index.bits(n)==n&&versions.bits(n)==5&&words.bits(n)==64&&rounding.bits(n)==0,StatusCode::unsupported_profile);auto mode=modes.bits(n);need(mode<=1,StatusCode::unsupported_profile);need(mode==0||feature_f);need(ts||mode==0,StatusCode::unsupported_profile);auto profile=CodecProfile::from_wire(static_cast<std::uint8_t>(mode),static_cast<std::uint8_t>(present.bits(n)),requested.real(n),effective.real(n),codes.bits(n));if(!profile)throw Failure{profile.error()};profiles.push_back(*profile);used_f|=mode==1;}
    for(std::uint64_t n=0;n<outer;++n)need(rows.bits(n)<m);
    auto f=product(outer,(inner+B-1)/B);need(f<=(1ull<<32),StatusCode::resource_limit);auto frames=array(v.at("frame_index"),"frames77",{f});auto payload=array(v.at("payload"),"u8");need(payload.rank==1);
    const auto& verification=v.at("verification");auto maxerr=array(verification.at("max_abs_error"),"f64",{f});auto rms=array(verification.at("rms_error"),"f64",{f});auto counts=array(verification.at("verified_count"),"u64",{f});auto equal=array(verification.at("bitwise_equal"),"u8",{f});
    if(!f){need(payload.count==0);return;}
    const auto saved_workspace=budget.workspace;
    auto frame_capacity=std::max(product(B,8),encoded_capacity(B));need(frame_capacity<=maximum_frame_bytes,StatusCode::resource_limit);
    budget.work(frame_capacity+product(B,8)+encoded_capacity(B)+4096);
    std::vector<std::byte> encoded(static_cast<std::size_t>(frame_capacity));std::vector<double> decoded(static_cast<std::size_t>(B));auto codec=Codec::create(B);if(!codec)throw Failure{codec.error()};
    std::array<std::byte,77> record{};std::uint64_t position=0;
    for(std::uint64_t n=0;n<f;++n) {
        okay(out.read(frames.section,n*77,record));auto get=[&](std::size_t off,std::size_t width){return le(std::span(record).subspan(off,width));};
        auto row=get(0,8),begin=get(8,8),count=get(16,8),offset=get(24,8),bytes_=get(32,8),encoding=get(40,1),profile=get(41,4);
        const auto chunks=(inner+B-1)/B;need(row==n/chunks&&begin==(n%chunks)*B&&count==std::min(B,inner-begin)&&count>0&&offset==position&&bytes_>0&&bytes_<=frame_capacity);
        need(profile<m&&profile==rows.bits(row));need(encoding<=1,StatusCode::unsupported_feature);need(bytes_<=payload.count-position);
        if(encoding==0)need(bytes_==product(count,8));else need(bytes_<=encoded_capacity(count),StatusCode::codec_failure);
        okay(out.read(payload.section,offset,std::span(encoded).first(bytes_)));Digest digest{};std::copy_n(record.begin()+45,32,digest.begin());need(sha256(std::span(encoded).first(bytes_))==digest,StatusCode::verification_failure);
        auto maximum=maxerr.real(n),rms_=rms.real(n);auto eq=equal.bits(n);need(counts.bits(n)==count&&eq<=1);
        if(encoding==0||profiles[profile].mode()==CodecMode::reversible)need(positive_zero(maximum)&&positive_zero(rms_)&&eq==1);
        else need(std::isfinite(maximum)&&std::isfinite(rms_)&&rms_>=0&&rms_<=maximum&&maximum<=profiles[profile].requested_tolerance()&&(!eq||(maximum==0&&rms_==0)));
        auto decoded_status=codec->decode(static_cast<Encoding>(encoding),count,std::span(encoded).first(bytes_),std::span(decoded).first(count),profiles[profile]);
        if(!decoded_status&&minor==1&&decoded_status.code==StatusCode::unsupported_profile)decoded_status.code=StatusCode::codec_failure;
        okay(decoded_status);
        for(std::uint64_t j=0;j<count;++j)need(std::isfinite(decoded[j]));position+=bytes_;
    }
    need(position==payload.count);budget.workspace=saved_workspace;
}
void SpoolValidator::logs(const Json& j,unsigned state) {
    if(state==1||state==2){keys(j,{"source_schema","source_index","kind","state"});return;}
    auto data=array(j.at("records_jsonl"),"u8");need(data.rank==1&&data.count<=4294967296ull,StatusCode::resource_limit);
    auto count=uint(j,"complete_record_count"),fragment=uint(j,"trailing_fragment_bytes");need(count<maximum_extent);auto offsets=array(j.at("record_offsets"),"u64",{count+1});need(offsets.bits(0)==0);
    auto end=offsets.bits(count);need(end<=data.count&&fragment==data.count-end);auto dropped=uint(j,"dropped_count");need(flag(j,"dropped_count_present")||!dropped);need(state==3||(!fragment&&!dropped));(void)text(j,"schema",true);document(j.at("info_json"));
    std::uint64_t previous=0;
    for(std::uint64_t i=0;i<count;++i){auto next=offsets.bits(i+1);need(next>previous&&next<=end);need(next-previous<=1048576,StatusCode::resource_limit);std::byte last{};okay(out.read(data.section,next-1,std::span(&last,1)));need(last==std::byte{'\n'});
        for(std::uint64_t at=previous;at<next-1;){auto n=std::min<std::uint64_t>(file.tile.size(),next-1-at);okay(out.read(data.section,at,std::span(file.tile).first(n)));need(std::find(file.tile.begin(),file.tile.begin()+n,std::byte{'\n'})==file.tile.begin()+n);at+=n;}
        json_bytes(data.section,previous,next-previous,false,true);previous=next;}
}
void SpoolValidator::provenance() {
    auto states=out.root.at("format").at("sections");
    std::uint64_t count=6;
    if(feature_p) {
        const auto& p=out.root.at("provenance");need(uint(p,"schema")==1);auto digest=array(p.at("authored_input_sha256"),"u8",{32});for(unsigned i=0;i<32;++i)out.summary.authored_input_sha256[i]=std::byte(digest.bits(i));
        const auto& c=p.at("sources");auto indices=array(c.at("source_index"),"u64");need(indices.rank==1&&indices.count>0&&indices.count<=1000000,StatusCode::resource_limit);count=indices.count;
        auto kinds=array(c.at("kind"),"u8",{count});auto metadata_ids=array(c.at("metadata_section"),"u64",{count});for(std::uint64_t i=0;i<count;++i)need(kinds.bits(i)<=1,StatusCode::unsupported_feature);out.original_source_index=uint(p,"original_source_index");need(out.original_source_index<count&&kinds.bits(out.original_source_index)==0);
        budget.retain((count+7)/8);source_used.resize(static_cast<std::size_t>((count+7)/8));
        // Single-run references have at most69 entries and must use every catalog source.
        need(count<=69);budget.retain(product(count,sizeof(SourceMetadata)));out.sources.reserve(static_cast<std::size_t>(count));
        for(std::uint64_t i=0;i<count;++i){need(indices.bits(i)==i);auto k=kinds.bits(i);need(k<=1,StatusCode::unsupported_feature);SourceMetadata source;source.source_index=i;source.kind=static_cast<std::uint8_t>(k);source.metadata_section=metadata_ids.bits(i);source.metadata=metadata(source.metadata_section);need(uint(source.metadata,"source_schema")==1&&uint(source.metadata,"source_index")==i&&uint(source.metadata,"kind",1)==k);out.sources.push_back(std::move(source));}
        const auto& r=out.run.at("provenance").at("references");auto roles=array(r.at("role"),"u8");need(roles.rank==1&&roles.count>=5&&roles.count<=69);auto ids=array(r.at("source_index"),"u64",{roles.count});auto order=array(r.at("order"),"u64",{roles.count});budget.retain(product(roles.count,sizeof(SourceReference)));out.references.reserve(static_cast<std::size_t>(roles.count));
        for(std::uint64_t i=0;i<roles.count;++i){auto role=roles.bits(i),id=ids.bits(i),ord=order.bits(i);need(role==(i<5?i:5)&&ord==(i<5?0:i-5)&&id<count&&out.sources[id].kind==(role==5?1:0));if(i==0)need(id==out.original_source_index);out.references.push_back({static_cast<std::uint8_t>(role),id,ord});source_used[id/8]|=std::byte(1u<<(id%8));}
        source_used[out.original_source_index/8]|=std::byte(1u<<(out.original_source_index%8));for(std::uint64_t i=0;i<count;++i)need((source_used[i/8]&std::byte(1u<<(i%8)))!=std::byte{});
    } else {
        budget.retain(product(count,sizeof(SourceMetadata))+product(count,sizeof(SourceReference))+1);out.sources.reserve(count);out.references.reserve(count);source_used.resize(1,std::byte{0x3f});
        const char* docs[]={"source_json","effective_json","overrides_json","resources_json","execution_json"};
        for(std::uint64_t i=0;i<5;++i){const char* branch=i<4?"inputs":"context";auto state=uint(states,branch,3);need(out.root.contains(branch)==(state==0||state==3));budget.retain(1024+(state==0||state==3?copy_charge(out.root.at(branch).at(docs[i])):256));Json doc=state==0||state==3?out.root.at(branch).at(docs[i]):Json{{"state",state}};SourceMetadata s;s.source_index=i;s.kind=0;s.metadata={{"source_schema",1},{"source_index",i},{"kind",0},{"document",std::move(doc)}};out.sources.push_back(std::move(s));out.references.push_back({static_cast<std::uint8_t>(i),i,0});}
        auto state=uint(states,"logs",3);need(out.root.contains("logs")==(state==0||state==3));budget.retain(1024+(state==0||state==3?copy_charge(out.root.at("logs")):256));SourceMetadata s;s.source_index=5;s.kind=1;s.metadata=state==0||state==3?out.root.at("logs"):Json::object();s.metadata.update(Json{{"source_schema",1},{"source_index",5},{"kind",1},{"state",state}});out.sources.push_back(std::move(s));out.references.push_back({5,5,0});
    }
    std::vector<unsigned> source_states;source_states.reserve(out.sources.size());
    for(auto& source:out.sources) {
        if(source.kind==0){auto state=document(source.metadata.at("document"),source.source_index==out.original_source_index);source_states.push_back(static_cast<unsigned>(state));}
        else {auto state=uint(source.metadata,"state",3);logs(source.metadata,static_cast<unsigned>(state));source_states.push_back(static_cast<unsigned>(state));}
    }
    const auto& original=out.sources[out.original_source_index].metadata.at("document");auto original_state=uint(original,"state",3);out.summary.original_state=static_cast<SectionState>(original_state);
    if(feature_p)need(original_state!=3);
    if(original_state==0){auto id=uint(original.at("bytes"),"section");auto digest=out.directory[id].digest;if(feature_p)need(digest==out.summary.authored_input_sha256);else out.summary.authored_input_sha256=digest;}
    else if(!feature_p)out.summary.authored_input_sha256=file.binding.authored_input_sha256;
    if(file.binding.trusted_manifest_binding){need(file.binding.expected_file_bytes==file.size&&!file.binding.producer_identity.empty()&&!file.binding.dependency_identity.empty()&&!file.binding.expected_run_id.empty(),StatusCode::unsupported_feature,89);need(out.summary.authored_input_sha256==file.binding.authored_input_sha256);}
    else if(original_state!=0)need(partial_original,StatusCode::unsupported_feature,89);
    if(feature_p) {
        auto aggregate=[&](unsigned category){bool any=false,all_disabled=true,all_included=true,retained=false,unavailable=false;for(const auto& r:out.references){auto c=r.role<4?0u:r.role==4?2u:1u;if(c!=category)continue;any=true;auto s=source_states[r.source_index];all_disabled&=s==1;all_included&=s==0;retained|=s==0||s==3;unavailable|=s==2;}return !any||all_disabled?1u:all_included?0u:!retained&&unavailable?2u:3u;};
        need(uint(states,"inputs")==aggregate(0)&&uint(states,"logs")==aggregate(1)&&uint(states,"context")==aggregate(2));
    } else {
        auto branch=[&](std::uint64_t begin,std::uint64_t end) {bool retained=false,missing=false,all_disabled=true;for(auto n=begin;n<end;++n){auto s=source_states[n];retained|=s==0||s==3;missing|=s==2||s==3;all_disabled&=s==1;}return retained?(missing?3u:0u):(all_disabled?1u:2u);};
        need(uint(states,"inputs")==branch(0,4)&&uint(states,"context")==branch(4,5));
    }
}
void SpoolValidator::diagnostics() {
    if(!feature_d)return;
    TempScope scope(budget);const auto& diag=out.run.at("diagnostics");const auto& entries=diag.at("traces");need(entries.is_array()&&entries.size()<=5);
    budget.retain(product(entries.size(),sizeof(TraceMetadata)));out.traces.reserve(entries.size());
    struct Access {
        SpoolValidator* validator{};unsigned kind{};std::map<std::string,Array> columns;
        static Status read(const void* context,std::uint64_t index,DiagnosticRecord& r) noexcept {
            try {
                const auto& self=*static_cast<const Access*>(context);
                auto u=[&](const char* name){return self.columns.at(name).bits(index);};auto d=[&](const char* name){return self.columns.at(name).real(index);};
                r={};r.record_index=u("record_index");
                if(self.kind==0){r.accepted_step=u("accepted_step");r.time_s=d("time_s");r.nonlinear_iterations=u("nonlinear_iterations");}
                else if(self.kind<=3) {
                    r.attempt_id=u("attempt_id");r.factor=d("factor");auto mask=u("metric_valid");need(mask<=63,StatusCode::unsupported_feature);r.metric_valid=static_cast<std::uint8_t>(mask);
                    if(self.kind==2)r.accepted_level_id=u("accepted_level_id");
                    else {r.certified=static_cast<std::uint8_t>(u("certified"));r.rolled_back=static_cast<std::uint8_t>(u("rolled_back"));r.status_index=static_cast<std::uint32_t>(u("status_index"));
                        if(self.kind==1){r.accepted_level_present=static_cast<std::uint8_t>(u("accepted_level_present"));r.accepted_level_id=u("accepted_level_id");r.newton_record_count=u("newton_record_count");}
                        else {r.iteration_id=u("iteration_id");r.row_kind=static_cast<std::uint8_t>(u("row_kind"));}}
                    for(unsigned i=0;i<6;++i)r.metrics[i]=d(metric_names[i]);
                } else {r.sequence=u("sequence");r.event_name_index=static_cast<std::uint32_t>(u("event_name_index"));r.severity=static_cast<std::uint8_t>(u("severity"));auto mask=u("identity_valid");need(mask<=15,StatusCode::unsupported_feature);r.identity_valid=static_cast<std::uint8_t>(mask);r.time_s=d("time_s");r.accepted_step=u("accepted_step");r.accepted_level_id=u("accepted_level");r.attempt_id=u("attempt_id");}
                return {};
            } catch(const Failure& f){return f.status;}catch(...){return error(StatusCode::invalid_contract,80);}
        }
    };
    std::vector<DiagnosticTraceDescription> descriptions;std::vector<DiagnosticTraceView> views;std::vector<Access> accesses;std::vector<std::vector<DiagnosticSummary>> summaries;
    budget.temp(product(entries.size(),sizeof(DiagnosticTraceDescription)+sizeof(DiagnosticTraceView)+sizeof(Access)+sizeof(std::vector<DiagnosticSummary>)));
    descriptions.reserve(entries.size());views.reserve(entries.size());accesses.reserve(entries.size());summaries.reserve(entries.size());
    std::set<unsigned> kinds;
    for(const auto& entry:entries) {
        auto k=uint(entry,"kind",4);need(kinds.insert(static_cast<unsigned>(k)).second);need((k==0?out.summary.kind==RunKind::timeseries:k==4||out.summary.kind==RunKind::steady_state));
        auto id=uint(entry,"metadata_section");TraceMetadata trace;trace.kind=static_cast<std::uint8_t>(k);trace.metadata_section=id;trace.metadata=metadata(id);out.traces.push_back(std::move(trace));const auto& t=out.traces.back().metadata;
        need(uint(t,"trace_schema")==1&&uint(t,"kind",4)==k&&uint(t,"run_index")==0);
        DiagnosticTraceDescription d;d.kind=static_cast<DiagnosticKind>(k);d.requirement=static_cast<DiagnosticRequirement>(uint(t,"requirement",1));auto capacity=uint(t,"record_capacity");need(capacity>0);d.record_capacity=std::min(capacity,1ull<<40);
        DiagnosticTraceView v;v.kind=d.kind;v.availability=static_cast<DiagnosticAvailability>(uint(t,"availability",3));auto status=uint(t,"capture_status",4);v.capture_status=static_cast<DiagnosticCaptureStatus>(status);auto count=uint(t,"record_count",1ull<<40);need(count<=capacity);if(status==2)need(count==capacity);
        v.records.count=count;v.counts_present=flag(t,"eligible_count_present");need(v.counts_present==flag(t,"omitted_count_present"));v.eligible_count=uint(t,"eligible_count");v.omitted_count=uint(t,"omitted_count");v.capture_error_count=uint(t,"capture_error_count");v.first_missing_ordinal_present=flag(t,"first_missing_ordinal_present");v.first_missing_ordinal=uint(t,"first_missing_ordinal");v.failure_code=t.at("failure_code").get_ref<const std::string&>();need(valid_utf8(v.failure_code));
        accesses.emplace_back();auto& access=accesses.back();access.validator=this;access.kind=static_cast<unsigned>(k);v.records.context=&access;v.records.callback=&Access::read;
        summaries.emplace_back();auto& sums=summaries.back();
        if(status==1||status==4) {
            need(!t.contains("columns")&&!t.contains("criteria")&&!t.contains("summaries"));if(k==1||k==3)d.status_names={"unavailable"};if(k==4)d.event_names={"unavailable"};
        } else {
            need(t.contains("columns"));const auto& cols=t.at("columns");need(cols.is_object()&&cols.size()<=24);budget.temp(product(cols.size(),sizeof(Array)+128));
            std::set<std::string> expected;
            auto column=[&](const char* name,const char* type){expected.insert(name);access.columns.emplace(name,array(cols.at(name),type,{count}));};
            column("record_index","u64");
            if(k==0){column("accepted_step","u64");column("time_s","f64");column("nonlinear_iterations","u64");need(!t.contains("criteria"));}
            else if(k<=3) {
                column("attempt_id","u64");column("factor","f64");column("metric_valid","u8");for(auto name:metric_names)column(name,"f64");
                if(k==2)column("accepted_level_id","u64");else {column("certified","u8");column("rolled_back","u8");column("status_index","u32");expected.insert("status_names");auto names=uint(cols.at("status_names"),"count",64);need(names>0);d.status_names=pool(cols.at("status_names"),names);
                    if(k==1){column("accepted_level_present","u8");column("accepted_level_id","u64");column("newton_record_count","u64");}
                    else {column("iteration_id","u64");column("row_kind","u8");}}
                const auto& c=t.at("criteria");keys(c,{"norm","evaluation_time_s","force_absolute_N","moment_absolute_Nm","relative_tolerance","force_scale_N","moment_scale_Nm","force_limit_N","moment_limit_Nm","constraint_translation_limit_m","constraint_rotation_limit_rad","increment_translation_limit_m","increment_rotation_limit_rad"});need(text(c,"norm")=="block_max_abs_component");
                StaticCriteria x;x.evaluation_time_s=real(c,"evaluation_time_s");x.force_absolute_N=real(c,"force_absolute_N");x.moment_absolute_Nm=real(c,"moment_absolute_Nm");x.relative_tolerance=real(c,"relative_tolerance");x.force_scale_N=real(c,"force_scale_N");x.moment_scale_Nm=real(c,"moment_scale_Nm");x.force_limit_N=real(c,"force_limit_N");x.moment_limit_Nm=real(c,"moment_limit_Nm");x.constraint_translation_limit_m=real(c,"constraint_translation_limit_m");x.constraint_rotation_limit_rad=real(c,"constraint_rotation_limit_rad");x.increment_translation_limit_m=real(c,"increment_translation_limit_m");x.increment_rotation_limit_rad=real(c,"increment_rotation_limit_rad");d.criteria=x;
            } else {
                need(!t.contains("criteria"));column("sequence","u64");column("event_name_index","u32");column("severity","u8");column("identity_valid","u8");column("time_s","f64");column("accepted_step","u64");column("accepted_level","u64");column("attempt_id","u64");expected.insert("event_names");auto names=uint(cols.at("event_names"),"count",64);need(names>0);d.event_names=pool(cols.at("event_names"),names);
            }
            need(cols.size()==expected.size());for(auto it=cols.begin();it!=cols.end();++it)need(expected.contains(it.key()));
            if(t.contains("summaries")) {
                need(k!=4);const auto& s=t.at("summaries");keys(s,{"population","fields"});need(text(s,"population")=="retained_valid_records");const auto& fields=s.at("fields");need(fields.is_object()&&!fields.empty()&&fields.size()<=8);budget.temp(product(fields.size(),sizeof(DiagnosticSummary)));sums.reserve(fields.size());
                for(auto it=fields.begin();it!=fields.end();++it){unsigned measure=8;for(unsigned m=0;m<8;++m)if(it.key()==diagnostic_measure_name(static_cast<DiagnosticMeasure>(m)))measure=m;need(measure<8);DiagnosticSummary s_;s_.measure=static_cast<DiagnosticMeasure>(measure);const auto& a=it.value();keys(a,{"count","min","max","mean","median_present","median"});s_.count=uint(a,"count");if(measure<2){s_.min_integer=uint(a,"min");s_.max_integer=uint(a,"max");}else{s_.min=real(a,"min");s_.max=real(a,"max");}s_.mean=real(a,"mean");s_.median_present=flag(a,"median_present");s_.median=real(a,"median");sums.push_back(s_);}
                v.summaries=sums;
            }
        }
        descriptions.push_back(std::move(d));views.push_back(v);
    }
    auto status=validate_diagnostics(descriptions,views,accepted);if(!status)throw Failure{status.error()};need(uint(diag,"status",4)==static_cast<unsigned>(*status));
    auto outcome=uint(out.run.at("info"),"run_outcome");if(outcome==7)need(*status==DiagnosticStatus::failed);if(outcome==0)need(*status!=DiagnosticStatus::failed);
}
VerifiedSpoolData::VerifiedSpoolData()=default;
VerifiedSpoolData::~VerifiedSpoolData()=default;
VerifiedSpoolData::VerifiedSpoolData(VerifiedSpoolData&&) noexcept=default;
VerifiedSpoolData& VerifiedSpoolData::operator=(VerifiedSpoolData&&) noexcept=default;
std::expected<DocumentPreflight,Status> preflight_document_impl(const DocumentView& document,DocumentPreflightLimits limits,const Digest* expected=nullptr) noexcept {
    try {
        need(static_cast<unsigned>(document.state)<=3);
        if(document.state==SectionState::disabled||document.state==SectionState::unavailable){need(document.bytes.size==0);return DocumentPreflight{};}
        need(!document.media_type.empty()&&valid_utf8(document.media_type));
        need(limits.metadata_budget_bytes&&limits.workspace_budget_bytes&&limits.document_bytes&&limits.document_parser_budget_bytes&&limits.copy_tile_bytes>=4096&&limits.copy_tile_bytes<=1048576);
        need(document.bytes.size<=4294967296ull,StatusCode::resource_limit);
        Budget budget{limits.metadata_budget_bytes,limits.workspace_budget_bytes};budget.work(262144);Sha256 hash;DocumentPreflight result;result.source_bytes=document.bytes.size;
        auto finish_hash=[&](){result.sha256=hash.finish();if(expected)need(result.sha256==*expected,StatusCode::verification_failure);};
        if(document.state==SectionState::included&&document.media_type=="application/json") {
            result.kind=DocumentPreflightKind::json_document;
            need(document.bytes.size<=limits.document_bytes,StatusCode::resource_limit);
            budget.temp(document.bytes.size+32);std::string raw(static_cast<std::size_t>(document.bytes.size),'\0');
            auto storage=std::as_writable_bytes(std::span(raw.data(),raw.size()));
            for(std::uint64_t at=0;at<document.bytes.size;){auto n=std::min(limits.copy_tile_bytes,document.bytes.size-at);auto tile=storage.subspan(at,n);okay(document.bytes.read(at,tile));hash.update(tile);at+=n;}
            finish_hash();
            result.parser_charge_bytes=admit_document_json(raw,budget,limits.document_parser_budget_bytes);
        } else {
            result.kind=DocumentPreflightKind::retained_bytes;
            budget.work(limits.copy_tile_bytes);std::vector<std::byte> tile(static_cast<std::size_t>(limits.copy_tile_bytes));
            for(std::uint64_t at=0;at<document.bytes.size;){auto n=std::min<std::uint64_t>(tile.size(),document.bytes.size-at);auto bytes=std::span(tile).first(n);okay(document.bytes.read(at,bytes));hash.update(bytes);at+=n;}
            finish_hash();
        }
        result.workspace_bytes=budget.peak;return result;
    }catch(const Failure& f){return std::unexpected(f.status);}catch(const std::bad_alloc&){return std::unexpected(error(StatusCode::resource_limit,80));}catch(...){return std::unexpected(error(StatusCode::invalid_contract,80));}
}
std::expected<std::uint64_t,Status> validate_original_document(const DocumentView& document,const Digest& expected,SpoolReadLimits limits) {
    if(document.state==SectionState::partial)return std::unexpected(error(StatusCode::unsupported_feature,89));
    DocumentPreflightLimits public_limits{limits.document_bytes,limits.document_parser_budget_bytes,limits.metadata_budget_bytes,limits.workspace_budget_bytes,limits.copy_tile_bytes};
    auto result=preflight_document_impl(document,public_limits,&expected);if(!result)return std::unexpected(result.error());
    return result->workspace_bytes;
}
std::expected<VerifiedSpoolData,Status> VerifiedSpoolData::open(const std::filesystem::path& path,const SpoolBinding& binding,SpoolReadLimits limits) {
    try {
        need(limits.metadata_budget_bytes&&limits.workspace_budget_bytes&&limits.copy_tile_bytes>=4096&&limits.copy_tile_bytes<=1048576&&limits.document_bytes&&limits.document_parser_budget_bytes,StatusCode::invalid_contract);
        need(binding.expected_run_id.size()<=1048576&&binding.producer_identity.size()<=1048576&&binding.dependency_identity.size()<=1048576,StatusCode::resource_limit);
        need(valid_utf8(binding.expected_run_id)&&valid_utf8(binding.producer_identity)&&valid_utf8(binding.dependency_identity));
        need(2*(binding.expected_run_id.size()+binding.producer_identity.size()+binding.dependency_identity.size())+sizeof(Impl)+sizeof(VerifiedSpoolData)<=std::min(limits.metadata_budget_bytes,limits.workspace_budget_bytes),StatusCode::resource_limit);
        VerifiedSpoolData out;out.impl_=std::make_unique<Impl>();auto& f=*out.impl_;f.binding=binding;f.limits=limits;
        need(limits.copy_tile_bytes<=limits.workspace_budget_bytes,StatusCode::resource_limit);f.tile.resize(static_cast<std::size_t>(limits.copy_tile_bytes));
#ifdef _WIN32
        f.file=_wfsopen(path.c_str(),L"rb",_SH_DENYNO);
#else
        f.file=std::fopen(path.c_str(),"rb");
#endif
        need(f.file!=nullptr,StatusCode::io_failure,81);f.size=f.extent();need(f.size<=maximum_extent,StatusCode::resource_limit);
        if(binding.expected_file_bytes){need(binding.expected_file_bytes==f.size,StatusCode::verification_failure);need(f.hash(0,f.size)==binding.expected_file_sha256,StatusCode::verification_failure);}
        else need(!binding.trusted_manifest_binding,StatusCode::unsupported_feature,89);
        SpoolValidator validate(out);validate.budget.retain(binding.expected_run_id.capacity()+binding.producer_identity.capacity()+binding.dependency_identity.capacity()+sizeof(Impl)+sizeof(VerifiedSpoolData));
        validate.header();validate.root_metadata();validate.run_info();validate.catalogs();validate.values();validate.diagnostics();validate.provenance();validate.finish();
        // Even unbound included originals gain a stable byte identity for the later recheck.
        if(!f.binding.expected_file_bytes){f.binding.expected_file_bytes=f.size;f.binding.expected_file_sha256=f.hash(0,f.size);}
        return out;
    } catch(const Failure& f){return std::unexpected(f.status);}catch(const std::bad_alloc&){return std::unexpected(error(StatusCode::resource_limit,80));}catch(...){return std::unexpected(error(StatusCode::invalid_contract,80));}
}
Status VerifiedSpoolData::read(std::uint64_t section,std::uint64_t relative_offset,std::span<std::byte> bytes_) noexcept {
    try {need(impl_&&section<directory.size());const auto& d=directory[section];need(relative_offset<=d.size&&bytes_.size()<=d.size-relative_offset);impl_->read_at(d.offset+relative_offset,bytes_);return {};}
    catch(const Failure& f){return f.status;}catch(...){return error(StatusCode::io_failure,81);}
}
Status VerifiedSpoolData::verify_unchanged() noexcept {
    try {need(impl_!=nullptr,StatusCode::invalid_state);need(impl_->extent()==impl_->binding.expected_file_bytes,StatusCode::verification_failure);need(impl_->hash(0,impl_->size)==impl_->binding.expected_file_sha256,StatusCode::verification_failure);return {};}
    catch(const Failure& f){return f.status;}catch(...){return error(StatusCode::io_failure,81);}
}
} // namespace serene::outputs::detail
namespace serene::outputs {
std::expected<DocumentPreflight,Status> preflight_document(const DocumentView& document,DocumentPreflightLimits limits) noexcept {
    return detail::preflight_document_impl(document,limits);
}
} // namespace serene::outputs
