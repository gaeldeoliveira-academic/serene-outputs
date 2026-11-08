#include <serene/outputs/model.hpp>
#include <array>
#include <iostream>
int main(){
    using namespace serene::outputs;
    std::array<DiagnosticCapacity,2> traces={DiagnosticCapacity{DiagnosticKind::transient_accepted_steps,10000},DiagnosticCapacity{DiagnosticKind::run_events,64}};
    ExportCapacity capacity;capacity.scalar_capacity=256;capacity.record_capacity=5000;capacity.profile_capacity=257;capacity.indexed_provenance=true;capacity.maximum_log_sources=2;capacity.metadata_bytes_bound=2*1024*1024;capacity.diagnostics=traces;
    ExportLimits limits;limits.provenance_bytes_bound=4*1024*1024;auto plan=plan_export_capacity(capacity,limits);
    if(!plan||plan->source_capacity!=7||!plan->maximum_frames||plan->maximum_archive_bytes<=256*5000*8)return 1;
    auto exact=limits;exact.workspace_budget_bytes=plan->total_bytes;auto same=plan_export_capacity(capacity,exact);if(!same||same->maximum_archive_bytes!=plan->maximum_archive_bytes)return 2;
    --exact.workspace_budget_bytes;auto short_budget=plan_export_capacity(capacity,exact);if(short_budget||short_budget.error().code!=StatusCode::resource_limit)return 3;
    std::cout<<"Installed count-only model planner: sections="<<plan->section_capacity<<" sources="<<plan->source_capacity<<" file_bound="<<plan->maximum_archive_bytes<<'\n';return 0;
}
