#include "synthetic_case.hpp"
#include "allocation_meter.hpp"
#include <iostream>
using namespace serene::outputs;
namespace {
/** Fill output v with deterministic accepted-step row i and 0.01-second spacing. Context is unused; succeeds without retaining history. */
Status row(const void*,std::uint64_t i,DiagnosticRecord&v)noexcept{
    // Clear v, assign ordinal/step/time/iteration fields, then return success.
v={};v.record_index=i;v.accepted_step=i+1;v.time_s=.01*(i+1);v.nonlinear_iterations=5;return {};}
}
/** Measure streaming trace save for count argv[1] and fresh path argv[2]. Emits JSON and returns a nonzero setup/save/bound-failure code. */
int main(int argc,char**argv){
    // Prepare callback-backed trace metadata without storing its row history.
    // Measure archive open/append/finalize live heap growth.
    // Compare published bytes and measured growth against the focused bounds.

    if(argc!=3)return 2;/* Requested generated trace population. */ auto count=std::stoull(argv[1]);/* Fresh archive destination. */ std::filesystem::path path=argv[2];
    /* Small numerical history independent of the trace population. */ SyntheticCase c(2,2);c.description.run.diagnostics.push_back({DiagnosticKind::transient_accepted_steps,DiagnosticRequirement::optional,count});
    c.limits.provenance_bytes_bound=65536;c.info.accepted_count=count;c.info.last_accepted_value=.01*count;c.info.requested_end=c.info.last_accepted_value;
    /* Exporter prepared before tracking. */ auto prepared=PreparedExport::prepare(c.description,c.limits);/* Codec allocated before tracking. */ auto codec=Codec::create(c.limits.frame_values);if(!prepared||!codec)return 3;
    /* Complete callback-backed accepted-step trace. */ DiagnosticTraceView trace;trace.kind=DiagnosticKind::transient_accepted_steps;trace.availability=DiagnosticAvailability::included;trace.capture_status=DiagnosticCaptureStatus::complete;trace.records={{},nullptr,row,count};trace.counts_present=true;trace.eligible_count=count;
    /* Numerical view augmented with the trace descriptor. */ auto v=c.view();v.diagnostics=std::span(&trace,1);/* Borrowed retained provenance. */ auto provenance=c.provenance();
    /* Live-byte baseline before save. */ auto before=allocation_meter::start();/* Owning measured archive session. */ auto session=ArchiveSession::open(*prepared,*codec,path);if(!session)return 4;
    /* Append/finalization outcome. */ auto status=session->append_run(v,provenance);if(!status){std::cerr<<status_name(status.code)<<' '<<status.operation<<'\n';return 5;}status=session->finalize();if(!status)return 6;
    /* Additional requested live C++ bytes during save. */ auto peak=allocation_meter::maximum_since(before);/* Published file extent. */ auto bytes=std::filesystem::file_size(path);
    std::cout<<"{\"trace_rows\":"<<count<<",\"prepared_workspace_bytes\":"<<prepared->plan().total_bytes<<",\"save_peak_cpp_bytes\":"<<peak<<",\"archive_bytes\":"<<bytes<<",\"archive_bound\":"<<prepared->plan().maximum_archive_bytes<<"}\n";
    return peak<65536&&bytes<=prepared->plan().maximum_archive_bytes?0:7;
}
