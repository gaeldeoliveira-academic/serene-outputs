#include <serene/outputs/model.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace serene::outputs {
namespace {
Status bad() noexcept { return error(StatusCode::invalid_contract,70); }
bool zero(double x) noexcept { return x==0 && !std::signbit(x); }
bool dictionary(const std::vector<std::string>& names) noexcept {
    if(names.empty() || names.size()>64) return false;
    for(std::size_t i=0;i<names.size();++i) {
        if(names[i].empty() || !valid_utf8(names[i])) return false;
        for(std::size_t j=0;j<i;++j) if(names[i]==names[j]) return false;
    }
    return true;
}
bool integer_measure(DiagnosticMeasure m) noexcept { return static_cast<unsigned>(m)<2; }
bool allowed(DiagnosticKind k,DiagnosticMeasure m) noexcept {
    const auto n=static_cast<unsigned>(m);
    if(n>7) return false;
    if(k==DiagnosticKind::transient_accepted_steps) return n==0;
    if(k==DiagnosticKind::static_stage_attempts) return n>=1;
    return (k==DiagnosticKind::static_accepted_levels || k==DiagnosticKind::static_newton_iterations) && n>=2;
}
Status summaries(const DiagnosticTraceView& v) noexcept {
    if(v.summaries.size()>8) return bad();
    for(std::size_t i=0;i<v.summaries.size();++i) {
        const auto&s=v.summaries[i];
        if(!allowed(v.kind,s.measure)) return bad();
        for(std::size_t j=0;j<i;++j) if(v.summaries[j].measure==s.measure) return bad();
        std::uint64_t count=0,loi=UINT64_MAX,hii=0;
        double lo=std::numeric_limits<double>::infinity(),hi=0;
        for(std::uint64_t j=0;j<v.records.count;++j) {
            DiagnosticRecord r; auto status=v.records.read(j,r);if(!status)return status;
            auto m=static_cast<unsigned>(s.measure);
            if(m>=2 && !(r.metric_valid&(1u<<(m-2))))continue;
            ++count;
            if(m<2) { auto x=m?r.newton_record_count:r.nonlinear_iterations;loi=std::min(loi,x);hii=std::max(hii,x); }
            else { lo=std::min(lo,r.metrics[m-2]);hi=std::max(hi,r.metrics[m-2]); }
        }
        if(count!=s.count || !std::isfinite(s.mean) || !std::isfinite(s.median))return bad();
        if(!count) {
            if((integer_measure(s.measure)?s.min_integer!=0||s.max_integer!=0:!zero(s.min)||!zero(s.max)) || !zero(s.mean) || s.median_present || !zero(s.median))return bad();
        } else {
            if(integer_measure(s.measure)) {
                if(s.min_integer!=loi || s.max_integer!=hii)return bad();
                lo=static_cast<double>(loi);hi=static_cast<double>(hii);
            } else if(s.min!=lo || s.max!=hi)return bad();
            if(s.mean<lo || s.mean>hi || (s.median_present?(s.median<lo||s.median>hi):!zero(s.median)))return bad();
        }
    }
    return {};
}
}
Status DiagnosticRecords::read(std::uint64_t i,DiagnosticRecord& out) const noexcept {
    if(i>=count)return bad();
    if(callback)return callback(context,i,out);
    if(count>contiguous.size())return bad();
    out=contiguous[static_cast<std::size_t>(i)];return {};
}
const char* diagnostic_measure_name(DiagnosticMeasure m) noexcept {
    constexpr const char* names[]={"nonlinear_iterations","newton_record_count","force_residual_N","moment_residual_Nm","constraint_translation_m","constraint_rotation_rad","increment_translation_m","increment_rotation_rad"};
    return static_cast<unsigned>(m)<8?names[static_cast<unsigned>(m)]:"";
}
std::uint64_t diagnostic_section_capacity(DiagnosticKind k) noexcept {
    constexpr std::uint64_t counts[]={5,19,12,18,12};
    return static_cast<unsigned>(k)<5?counts[static_cast<unsigned>(k)]:0;
}
Status validate_diagnostic_description(const DiagnosticTraceDescription& d) noexcept {
    auto k=static_cast<unsigned>(d.kind);
    if(k>4 || static_cast<unsigned>(d.requirement)>1 || !d.record_capacity)return bad();
    if((k==1||k==3)?!dictionary(d.status_names):!d.status_names.empty())return bad();
    if(k==4?!dictionary(d.event_names):!d.event_names.empty())return bad();
    if((k==0||k==4)&&d.criteria)return bad();
    if(d.criteria) {
        const auto&c=*d.criteria;
        if(!std::isfinite(c.evaluation_time_s))return bad();
        for(auto x:{c.force_absolute_N,c.moment_absolute_Nm,c.relative_tolerance,c.force_scale_N,c.moment_scale_Nm})if(!std::isfinite(x)||x<0)return bad();
        for(auto x:{c.force_limit_N,c.moment_limit_Nm,c.constraint_translation_limit_m,c.constraint_rotation_limit_rad,c.increment_translation_limit_m,c.increment_rotation_limit_rad})if(!std::isfinite(x)||x<=0)return bad();
    }
    return {};
}
std::expected<DiagnosticStatus,Status> validate_diagnostics(std::span<const DiagnosticTraceDescription> ds,std::span<const DiagnosticTraceView> vs,std::uint64_t accepted) noexcept {
    if(ds.size()>5 || ds.size()!=vs.size())return std::unexpected(bad());
    if(ds.empty())return DiagnosticStatus::disabled;
    bool required_failed=false,all_complete=true,all_unavailable=true;
    for(std::size_t i=0;i<ds.size();++i) {
        const auto&d=ds[i];const auto&v=vs[i];const auto r=v.records.count;
        if(auto s=validate_diagnostic_description(d);!s)return std::unexpected(s);
        for(std::size_t j=0;j<i;++j)if(ds[j].kind==d.kind)return std::unexpected(bad());
        auto k=static_cast<unsigned>(d.kind),s=static_cast<unsigned>(v.capture_status);
        if(v.kind!=d.kind || s>4 || r>d.record_capacity || r>(1ull<<40) || !valid_utf8(v.failure_code))return std::unexpected(bad());
        const unsigned availability[]={0,2,3,3,2};
        if(static_cast<unsigned>(v.availability)!=availability[s])return std::unexpected(bad());
        std::uint64_t total{};
        if(v.counts_present?(!checked_add(r,v.omitted_count,total)||total!=v.eligible_count):(v.eligible_count||v.omitted_count))return std::unexpected(bad());
        if(!v.first_missing_ordinal_present && v.first_missing_ordinal)return std::unexpected(bad());
        if(s==3?v.capture_error_count==0:v.capture_error_count!=0)return std::unexpected(bad());
        if(s==0 && (!v.counts_present||v.omitted_count||v.first_missing_ordinal_present||!v.failure_code.empty()))return std::unexpected(bad());
        if(s==1 && (r||v.first_missing_ordinal_present||(v.counts_present&&v.eligible_count)))return std::unexpected(bad());
        if((s==2||s==3) && (!v.first_missing_ordinal_present||v.first_missing_ordinal!=r||(v.counts_present&&!v.omitted_count)||v.failure_code.empty()))return std::unexpected(bad());
        if(s==2&&r!=d.record_capacity)return std::unexpected(bad());
        if(s==4 && (r||v.failure_code.empty()||(v.first_missing_ordinal_present&&v.first_missing_ordinal)||(v.counts_present&&(v.first_missing_ordinal_present?!v.omitted_count:v.omitted_count!=0))))return std::unexpected(bad());
        if(k==0&&v.counts_present&&v.eligible_count!=accepted)return std::unexpected(bad());
        required_failed|=d.requirement==DiagnosticRequirement::required&&(s==2||s==3);
        all_complete&=s==0;all_unavailable&=s==1||s==4;
        if(s==1||s==4) {if(!v.summaries.empty())return std::unexpected(bad());continue;}
        if(k>=1&&k<=3&&!d.criteria)return std::unexpected(bad());
        double previous_time=0;std::uint64_t previous_sequence=0;
        for(std::uint64_t j=0;j<r;++j) {
            DiagnosticRecord row;auto status=v.records.read(j,row);if(!status)return std::unexpected(status);
            if(row.record_index!=j)return std::unexpected(bad());
            if(k==0) {
                if(row.accepted_step!=j+1||!std::isfinite(row.time_s)||(j&&row.time_s<=previous_time))return std::unexpected(bad());
                previous_time=row.time_s;
            } else if(k<=3) {
                if(!row.attempt_id||!std::isfinite(row.factor)||row.metric_valid>63)return std::unexpected(bad());
                if(k!=2&&(row.certified>1||row.rolled_back>1||row.status_index>=d.status_names.size()))return std::unexpected(bad());
                if((k==2||row.certified)&&row.metric_valid!=63)return std::unexpected(bad());
                for(unsigned b=0;b<6;++b)if((row.metric_valid&(1u<<b))?(!std::isfinite(row.metrics[b])||row.metrics[b]<0):!zero(row.metrics[b]))return std::unexpected(bad());
                if(k==1&&(row.attempt_id!=j+1||row.accepted_level_present!=row.certified||(!row.accepted_level_present&&row.accepted_level_id)))return std::unexpected(bad());
                if(k==2&&row.accepted_level_id!=j)return std::unexpected(bad());
                if(k==3&&(row.row_kind>1||(!row.iteration_id&&row.row_kind!=1)))return std::unexpected(bad());
            } else {
                if(row.identity_valid>15||row.severity>3||row.event_name_index>=d.event_names.size()||(j&&row.sequence<=previous_sequence))return std::unexpected(bad());
                if((row.identity_valid&1)?!std::isfinite(row.time_s):!zero(row.time_s))return std::unexpected(bad());
                if((!(row.identity_valid&2)&&row.accepted_step)||(!(row.identity_valid&4)&&row.accepted_level_id)||(!(row.identity_valid&8)&&row.attempt_id))return std::unexpected(bad());
                previous_sequence=row.sequence;
            }
        }
        if(auto status=summaries(v);!status)return std::unexpected(status);
    }
    return required_failed?DiagnosticStatus::failed:all_complete?DiagnosticStatus::complete:all_unavailable?DiagnosticStatus::unavailable:DiagnosticStatus::partial;
}
} // namespace serene::outputs
