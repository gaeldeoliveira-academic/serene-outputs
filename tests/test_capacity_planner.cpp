#include <serene/outputs/model.hpp>
#include "allocation_meter.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace serene::outputs;
namespace {
/** Require value; throw the borrowed message on failure. */
void check(bool value,const char* message){
    // Report the first failed capacity assertion through main's exception path.
    if(!value)throw std::runtime_error(message);}
/** Return the ordered numeric plan fields from p for exact formula comparison; no ownership is retained. */
auto fields(const WorkspacePlan&p){
    // Project every independently checked workspace, graph and archive bound into one comparable array.
    return std::array{p.gather_bytes,p.decode_bytes,p.codec_bytes,p.index_bytes,p.copy_bytes,p.directory_bytes,p.serialization_bytes,p.metadata_bytes,p.total_bytes,p.maximum_frames,p.section_capacity,p.maximum_archive_bytes,p.source_capacity};}
/** Build a fresh exact description for the pure-planner matrix. kind sets trace applicability; indexed/effective select provenance/request metadata; traces supplies borrowed kind/capacity pairs copied into the result. */
ArchiveDescription description(RunKind kind,bool indexed,bool effective,std::span<const DiagnosticCapacity> traces){
    // Fill fixed run/catalog dimensions and provenance settings.
    // Create trace declarations with the minimum status/event dictionaries required by their kinds.
    /* Owned exact-description fixture. */ ArchiveDescription d;d.file_id="00000000-0000-4000-8000-000000000013";d.created_utc="2026-11-08T00:00:00Z";d.run.run_id="capacity_test";d.run.assembly_ref="blade";d.run.kind=kind;d.run.record_capacity=17;d.indexed_provenance=indexed;d.maximum_log_sources=3;
    d.run.effective_requests=effective?SectionState::included:SectionState::disabled;
    d.run.requests.push_back({"group","request","schema","construction","{}"});
    for(/* Scalar catalog index. */ unsigned i=0;i<3;++i)d.run.channels.push_back({"scalar"+std::to_string(i),"group","request","component","m",0});
    for(/* Borrowed diagnostic kind/capacity entry. */ const auto& c:traces){/* Owned trace declaration for one capacity entry. */ DiagnosticTraceDescription t;t.kind=c.kind;t.record_capacity=c.record_capacity;if(c.kind==DiagnosticKind::static_stage_attempts||c.kind==DiagnosticKind::static_newton_iterations)t.status_names={"status"};if(c.kind==DiagnosticKind::run_events)t.event_names={"event"};d.run.diagnostics.push_back(std::move(t));}
    return d;
}
}
/** Run pure-versus-exact planner equivalence and boundary checks; return zero on success or one on exception. */
int main(){
    // Compare all plan fields for each run/provenance/effective-metadata combination while measuring allocations.
    // Check monotonicity and exact one-byte budget boundaries.
    // Probe zero/maximum dimensions, frame limits, overflow, trace applicability and source-count bounds.
    try{
    /* Common frame and provenance ceilings for the matrix. */ ExportLimits limits;limits.frame_values=4;limits.provenance_bytes_bound=8192;
    /* Number of successful exact/pure plan comparisons. */ unsigned cases=0;
    for(/* Run-kind variant under test. */ auto kind:{RunKind::timeseries,RunKind::steady_state})for(/* Indexed-provenance branch under test. */ bool indexed:{false,true})for(/* Effective-request metadata branch under test. */ bool effective:{false,true}){
        /* Maximum trace-kind array for the selected run kind. */ std::array<DiagnosticCapacity,4> traces{};/* Used prefix length in the trace-capacity array. */ std::size_t n=0;
        if(kind==RunKind::timeseries)traces[n++]={DiagnosticKind::transient_accepted_steps,123};
        else for(/* Static diagnostic kind code. */ unsigned k=1;k<=3;++k)traces[n++]={static_cast<DiagnosticKind>(k),123};
        traces[n++]={DiagnosticKind::run_events,7};/* Borrowed populated prefix of trace capacities. */ auto used=std::span(traces).first(n);
        /* Prepared exact metadata plan used as the formula oracle. */ auto exact=PreparedExport::prepare(description(kind,indexed,effective,used),limits);check(bool(exact),"exact preparation");
        /* Count-only capacity input matching the exact description. */ ExportCapacity c{kind,3,17,1,effective,indexed,3,exact->plan().metadata_bytes,used};
        /* Requested live-byte baseline before the pure call. */ auto before=allocation_meter::start();/* Allocation-free count-only planning result. */ auto pure=plan_export_capacity(c,limits);/* Additional requested live C++ bytes during planning. */ auto peak=allocation_meter::maximum_since(before);
        check(bool(pure)&&peak==0,"pure planner allocates nothing");check(fields(*pure)==fields(exact->plan()),"pure and exact formulas agree for every field");
        check(pure->source_capacity==(indexed?8:6),"normalized merger source bound");
        /* Monotonically enlarged capacity input. */ auto larger=c;larger.scalar_capacity=5;larger.record_capacity=23;larger.profile_capacity=2;larger.metadata_bytes_bound+=1024;
        /* Plan for the enlarged capacity input. */ auto upper=plan_export_capacity(larger,limits);check(bool(upper)&&upper->maximum_archive_bytes>=pure->maximum_archive_bytes&&upper->total_bytes>=pure->total_bytes,"capacity ceiling monotonicity");
        /* Workspace budget set exactly to the required total. */ auto exact_workspace=limits;exact_workspace.workspace_budget_bytes=pure->total_bytes;check(bool(plan_export_capacity(c,exact_workspace)),"exact workspace ceiling accepted");--exact_workspace.workspace_budget_bytes;
        /* One-byte-short workspace result. */ auto too_small=plan_export_capacity(c,exact_workspace);check(!too_small&&too_small.error().code==StatusCode::resource_limit,"one-byte workspace shortage rejected");
        /* Metadata budget set to the authored metadata ceiling. */ auto exact_metadata=limits;exact_metadata.metadata_budget_bytes=c.metadata_bytes_bound;check(bool(plan_export_capacity(c,exact_metadata)),"exact metadata ceiling accepted");--exact_metadata.metadata_budget_bytes;check(!plan_export_capacity(c,exact_metadata),"metadata ceiling enforced");
        ++cases;
    }
    /* Small reusable valid capacity input. */ ExportCapacity base;base.scalar_capacity=1;base.record_capacity=4;base.metadata_bytes_bound=4096;
    /* Zero-observation capacity case. */ auto empty=base;empty.scalar_capacity=0;empty.record_capacity=0;/* Plan expected to contain no frame records. */ auto empty_plan=plan_export_capacity(empty,limits);check(bool(empty_plan)&&empty_plan->maximum_frames==0,"empty capacity has no frames");
    /* Capacity input mutated across independent boundary cases. */ auto bad=base;bad.profile_capacity=0;check(!plan_export_capacity(bad,limits),"zero profile capacity rejected");
    bad=base;bad.scalar_capacity=1000001;check(!plan_export_capacity(bad,limits),"scalar maximum enforced");
    bad=base;bad.record_capacity=(1ull<<40)+1;check(!plan_export_capacity(bad,limits),"sample capacity maximum enforced");
    bad=base;bad.record_capacity=1ull<<34;/* Plan at the exact maximum supported frame count. */ auto frames=plan_export_capacity(bad,limits);check(bool(frames)&&frames->maximum_frames==(1ull<<32),"exact maximum frames accepted");
    ++bad.record_capacity;check(!plan_export_capacity(bad,limits),"maximum frame overflow rejected");
    bad=base;bad.metadata_bytes_bound=0;check(!plan_export_capacity(bad,limits),"explicit metadata ceiling required");
    /* Limits mutated to overflow the provenance-byte bound. */ auto bytes=limits;bytes.provenance_bytes_bound=maximum_extent;/* Expected overflow/resource-limit result. */ auto overflow=plan_export_capacity(base,bytes);check(!overflow&&overflow.error().code==StatusCode::resource_limit,"source byte bound multiplication overflow rejected");
    /* Single mutable trace-capacity probe. */ std::array<DiagnosticCapacity,1>trace={DiagnosticCapacity{DiagnosticKind::run_events,(1ull<<40)+1}};bad=base;bad.diagnostics=trace;check(bool(plan_export_capacity(bad,limits)),"large authored diagnostic capacity is admitted without wire clamping");
    trace[0].record_capacity=UINT64_MAX;check(!plan_export_capacity(bad,limits),"diagnostic byte bound overflow rejected");trace[0].record_capacity=0;check(!plan_export_capacity(bad,limits),"zero diagnostic capacity rejected");
    trace[0]={DiagnosticKind::static_accepted_levels,1};/* Run-kind/diagnostic-kind incompatibility result. */ auto mismatch=plan_export_capacity(bad,limits);check(!mismatch&&mismatch.error().code==StatusCode::unsupported_feature,"trace applicability checked");
    /* Repeated diagnostic-kind input that must be rejected. */ std::array<DiagnosticCapacity,2>duplicate={DiagnosticCapacity{DiagnosticKind::run_events,1},DiagnosticCapacity{DiagnosticKind::run_events,2}};bad=base;bad.diagnostics=duplicate;check(!plan_export_capacity(bad,limits),"duplicate kind rejected");
    bad=base;bad.maximum_log_sources=64;bad.indexed_provenance=true;/* Plan at the maximum accepted log-source population. */ auto maximum=plan_export_capacity(bad,limits);check(bool(maximum)&&maximum->source_capacity==69,"maximum log source graph");bad.maximum_log_sources=65;check(!plan_export_capacity(bad,limits),"log source ceiling enforced");
    std::cout<<"capacity planner: "<<cases<<" exact-plan matrices, allocation-free planning, ceiling/frame/overflow/kind/source checks passed\n";return 0;
}catch(/* First test failure reported to stderr. */ const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
