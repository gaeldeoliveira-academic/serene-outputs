#include <serene/outputs/model.hpp>
#include <array>
#include <iostream>
/** Exercise the installed 0.2.1 model-only count planner without native dependencies; return zero or a distinct failed-boundary code. */
int main(){
    // Build a capacity-only input, compare exact-budget planning, then reject a one-byte workspace shortage.

    using namespace serene::outputs;
    /* Accepted-step and event capacity declarations borrowed by capacity. */ std::array<DiagnosticCapacity,2> traces={DiagnosticCapacity{DiagnosticKind::transient_accepted_steps,10000},DiagnosticCapacity{DiagnosticKind::run_events,64}};
    /* Owned counts and metadata ceiling requiring no exact catalog. */ ExportCapacity capacity;capacity.scalar_capacity=256;capacity.record_capacity=5000;capacity.profile_capacity=257;capacity.indexed_provenance=true;capacity.maximum_log_sources=2;capacity.metadata_bytes_bound=2*1024*1024;capacity.diagnostics=traces;
    /* Provenance byte ceiling for the installed planning call. */ ExportLimits limits;limits.provenance_bytes_bound=4*1024*1024;/* Count-only workspace/section/file bound result. */ auto plan=plan_export_capacity(capacity,limits);
    if(!plan||plan->source_capacity!=7||!plan->maximum_frames||plan->maximum_archive_bytes<=256*5000*8)return 1;
    /* Limits set to the exact returned workspace requirement. */ auto exact=limits;exact.workspace_budget_bytes=plan->total_bytes;/* Exact-budget plan expected to preserve all file bounds. */ auto same=plan_export_capacity(capacity,exact);if(!same||same->maximum_archive_bytes!=plan->maximum_archive_bytes)return 2;
    --exact.workspace_budget_bytes;/* Expected one-byte-short resource-limit result. */ auto short_budget=plan_export_capacity(capacity,exact);if(short_budget||short_budget.error().code!=StatusCode::resource_limit)return 3;
    std::cout<<"Installed count-only model planner: sections="<<plan->section_capacity<<" sources="<<plan->source_capacity<<" file_bound="<<plan->maximum_archive_bytes<<'\n';return 0;
}
