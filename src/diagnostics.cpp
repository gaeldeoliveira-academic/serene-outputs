#include <serene/outputs/model.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace serene::outputs {
namespace {
/// Return the fixed diagnostics-contract failure domain/operation without allocating text.
Status bad() noexcept { /* Build the shared diagnostics error value. */ return error(StatusCode::invalid_contract,70); }
/// Return whether x is canonical positive zero, as required for absent metrics.
bool zero(double x) noexcept { /* Test magnitude and sign bit separately. */ return x==0 && !std::signbit(x); }
/// Admit a borrowed nonempty bounded source dictionary with unique nonempty UTF-8 names.
bool dictionary(const std::vector<std::string>& names) noexcept {
    // Bound the dictionary, validate each spelling, and compare earlier entries for duplicates.
    if(names.empty() || names.size()>64) return false;
    for(std::size_t i /* Current dictionary index. */ =0;i<names.size();++i) {
        if(names[i].empty() || !valid_utf8(names[i])) return false;
        for(std::size_t j /* Earlier dictionary index for uniqueness. */ =0;j<i;++j) if(names[i]==names[j]) return false;
    }
    return true;
}
/// Return whether m addresses one of the exact u64 count columns.
bool integer_measure(DiagnosticMeasure m) noexcept { /* The first two fixed measure ordinals are integer columns. */ return static_cast<unsigned>(m)<2; }
/// Return whether measure m belongs to trace kind k's permitted summary columns.
bool allowed(DiagnosticKind k,DiagnosticMeasure m) noexcept {
    // Bound the measure enum, then apply the fixed kind-to-column summary matrix.
    const auto n /* Closed summary-measure ordinal. */ =static_cast<unsigned>(m);
    if(n>7) return false;
    if(k==DiagnosticKind::transient_accepted_steps) return n==0;
    if(k==DiagnosticKind::static_stage_attempts) return n>=1;
    return (k==DiagnosticKind::static_accepted_levels || k==DiagnosticKind::static_newton_iterations) && n>=2;
}
/// Validate borrowed summary assertions against v's retained valid-record population.
/// Count/extrema are recomputed; mean/median are only finite/range-checked assertions.
Status summaries(const DiagnosticTraceView& v) noexcept {
    // Require unique permitted fields, scan valid records for exact count/extrema,
    // then validate canonical empty values or rounded-statistic endpoint bounds.
    if(v.summaries.size()>8) return bad();
    for(std::size_t i /* Summary field index. */ =0;i<v.summaries.size();++i) {
        const auto&s=v.summaries[i]; // Borrowed producer assertion for this column.
        if(!allowed(v.kind,s.measure)) return bad();
        for(std::size_t j /* Earlier summary field checked for duplicates. */ =0;j<i;++j) if(v.summaries[j].measure==s.measure) return bad();
        std::uint64_t count=0,loi=UINT64_MAX,hii=0; // Valid population and exact integer extrema.
        double lo=std::numeric_limits<double>::infinity(),hi=0; // Nonnegative floating extrema or rounded integer endpoints.
        for(std::uint64_t j /* Retained record ordinal during bounded scan. */ =0;j<v.records.count;++j) {
            DiagnosticRecord r; /* Local copied source record. */ auto status /* Complete-record read status. */ =v.records.read(j,r);if(!status)return status;
            auto m /* Measure ordinal and metric-mask offset selector. */ =static_cast<unsigned>(s.measure);
            if(m>=2 && !(r.metric_valid&(1u<<(m-2))))continue;
            ++count;
            if(m<2) { auto x /* Exact integer observation selected by the measure. */ =m?r.newton_record_count:r.nonlinear_iterations;loi=std::min(loi,x);hii=std::max(hii,x); }
            else { lo=std::min(lo,r.metrics[m-2]);hi=std::max(hi,r.metrics[m-2]); }
        }
        // Empty summaries have canonical zeros; nonempty reported statistics stay within extrema.
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
    // Bound the retained ordinal, delegate callback reads, or copy one contiguous record.
    if(i>=count)return bad();
    if(callback)return callback(context,i,out);
    if(count>contiguous.size())return bad();
    out=contiguous[static_cast<std::size_t>(i)];return {};
}
const char* diagnostic_measure_name(DiagnosticMeasure m) noexcept {
    // Map a closed measure ordinal to its static wire spelling, otherwise return empty text.
    // Borrowed static names in DiagnosticMeasure order.
    constexpr const char* names[]={"nonlinear_iterations","newton_record_count","force_residual_N","moment_residual_Nm","constraint_translation_m","constraint_rotation_rad","increment_translation_m","increment_rotation_rad"};
    return static_cast<unsigned>(m)<8?names[static_cast<unsigned>(m)]:"";
}
std::uint64_t diagnostic_section_capacity(DiagnosticKind k) noexcept {
    // Select fixed per-kind metadata/column section cost; unknown kinds contribute zero.
    // One metadata section plus each mandatory typed column, including empty columns.
    constexpr std::uint64_t counts[]={5,19,12,18,12};
    return static_cast<unsigned>(k)<5?counts[static_cast<unsigned>(k)]:0;
}
Status validate_diagnostic_description(const DiagnosticTraceDescription& d) noexcept {
    // Admit kind/requirement/nonzero capacity, require only relevant dictionaries,
    // and validate any prepared static criteria with their physical sign/finiteness rules.
    auto k /* Closed trace-kind ordinal. */ =static_cast<unsigned>(d.kind);
    if(k>4 || static_cast<unsigned>(d.requirement)>1 || !d.record_capacity)return bad();
    if((k==1||k==3)?!dictionary(d.status_names):!d.status_names.empty())return bad();
    if(k==4?!dictionary(d.event_names):!d.event_names.empty())return bad();
    if((k==0||k==4)&&d.criteria)return bad();
    if(d.criteria) {
        const auto&c=*d.criteria; // Borrowed immutable static convergence criteria.
        if(!std::isfinite(c.evaluation_time_s))return bad();
        for(auto x /* Nonnegative tolerance or scale in its declared physical units. */ :{c.force_absolute_N,c.moment_absolute_Nm,c.relative_tolerance,c.force_scale_N,c.moment_scale_Nm})if(!std::isfinite(x)||x<0)return bad();
        for(auto x /* Strictly positive computed convergence limit. */ :{c.force_limit_N,c.moment_limit_Nm,c.constraint_translation_limit_m,c.constraint_rotation_limit_rad,c.increment_translation_limit_m,c.increment_rotation_limit_rad})if(!std::isfinite(x)||x<=0)return bad();
    }
    return {};
}
std::expected<DiagnosticStatus,Status> validate_diagnostics(std::span<const DiagnosticTraceDescription> ds,std::span<const DiagnosticTraceView> vs,std::uint64_t accepted) noexcept {
    // Match ordered policy/view pairs, validate capture states and known populations,
    // scan retained rows for kind-specific identity/metric invariants, validate summaries,
    // then derive the aggregate diagnostic outcome without inferring numerical success.
    if(ds.size()>5 || ds.size()!=vs.size())return std::unexpected(bad());
    if(ds.empty())return DiagnosticStatus::disabled;
    bool required_failed=false,all_complete=true,all_unavailable=true; // Aggregate capture outcome latches.
    for(std::size_t i /* Ordered trace position. */ =0;i<ds.size();++i) {
        const auto&d=ds[i]; /* Borrowed prepared policy. */ const auto&v=vs[i]; /* Borrowed finalized capture facts. */ const auto r=v.records.count; /* Retained prefix length. */
        if(auto s /* Prepared-policy admission status. */ =validate_diagnostic_description(d);!s)return std::unexpected(s);
        for(std::size_t j /* Earlier trace position for kind uniqueness. */ =0;j<i;++j)if(ds[j].kind==d.kind)return std::unexpected(bad());
        auto k=static_cast<unsigned>(d.kind),s=static_cast<unsigned>(v.capture_status); // Closed kind and capture-status ordinals.
        if(v.kind!=d.kind || s>4 || r>d.record_capacity || r>(1ull<<40) || !valid_utf8(v.failure_code))return std::unexpected(bad());
        // Capture-status to wire-availability correspondence is closed and deterministic.
        const unsigned availability[]={0,2,3,3,2};
        if(static_cast<unsigned>(v.availability)!=availability[s])return std::unexpected(bad());
        std::uint64_t total{}; // Checked retained-plus-omitted known population.
        if(v.counts_present?(!checked_add(r,v.omitted_count,total)||total!=v.eligible_count):(v.eligible_count||v.omitted_count))return std::unexpected(bad());
        if(!v.first_missing_ordinal_present && v.first_missing_ordinal)return std::unexpected(bad());
        if(s==3?v.capture_error_count==0:v.capture_error_count!=0)return std::unexpected(bad());
        if(s==0 && (!v.counts_present||v.omitted_count||v.first_missing_ordinal_present||!v.failure_code.empty()))return std::unexpected(bad());
        if(s==1 && (r||v.first_missing_ordinal_present||(v.counts_present&&v.eligible_count)))return std::unexpected(bad());
        if((s==2||s==3) && (!v.first_missing_ordinal_present||v.first_missing_ordinal!=r||(v.counts_present&&!v.omitted_count)||v.failure_code.empty()))return std::unexpected(bad());
        if(s==2&&r!=d.record_capacity)return std::unexpected(bad());
        if(s==4 && (r||v.failure_code.empty()||(v.first_missing_ordinal_present&&v.first_missing_ordinal)||(v.counts_present&&(v.first_missing_ordinal_present?!v.omitted_count:v.omitted_count!=0))))return std::unexpected(bad());
        // Accepted-step eligibility is the source-qualified driver advance population.
        if(k==0&&v.counts_present&&v.eligible_count!=accepted)return std::unexpected(bad());
        required_failed|=d.requirement==DiagnosticRequirement::required&&(s==2||s==3);
        all_complete&=s==0;all_unavailable&=s==1||s==4;
        if(s==1||s==4) {if(!v.summaries.empty())return std::unexpected(bad());continue;}
        if(k>=1&&k<=3&&!d.criteria)return std::unexpected(bad());
        // Unavailable traces read no records; recorded static traces require prepared criteria.
        double previous_time=0; /* Prior transient physical time. */ std::uint64_t previous_sequence=0; /* Prior source event sequence. */
        for(std::uint64_t j /* Retained prefix ordinal. */ =0;j<r;++j) {
            DiagnosticRecord row; /* Complete copied source row. */ auto status /* Record adapter status. */ =v.records.read(j,row);if(!status)return std::unexpected(status);
            if(row.record_index!=j)return std::unexpected(bad());
            if(k==0) {
                if(row.accepted_step!=j+1||!std::isfinite(row.time_s)||(j&&row.time_s<=previous_time))return std::unexpected(bad());
                previous_time=row.time_s;
            } else if(k<=3) {
                if(!row.attempt_id||!std::isfinite(row.factor)||row.metric_valid>63)return std::unexpected(bad());
                if(k!=2&&(row.certified>1||row.rolled_back>1||row.status_index>=d.status_names.size()))return std::unexpected(bad());
                if((k==2||row.certified)&&row.metric_valid!=63)return std::unexpected(bad());
                for(unsigned b /* Fixed metric column/validity-bit index. */ =0;b<6;++b)if((row.metric_valid&(1u<<b))?(!std::isfinite(row.metrics[b])||row.metrics[b]<0):!zero(row.metrics[b]))return std::unexpected(bad());
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
        if(auto status /* Bounded summary count/extrema and assertion checks. */ =summaries(v);!status)return std::unexpected(status);
    }
    return required_failed?DiagnosticStatus::failed:all_complete?DiagnosticStatus::complete:all_unavailable?DiagnosticStatus::unavailable:DiagnosticStatus::partial;
}
} // namespace serene::outputs
