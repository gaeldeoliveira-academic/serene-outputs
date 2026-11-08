#include "synthetic_case.hpp"
#include "allocation_meter.hpp"
#include <iostream>
using namespace serene::outputs;
namespace {
Status row(const void*,std::uint64_t i,DiagnosticRecord&v)noexcept{v={};v.record_index=i;v.accepted_step=i+1;v.time_s=.01*(i+1);v.nonlinear_iterations=5;return {};}
}
int main(int argc,char**argv){
    if(argc!=3)return 2;auto count=std::stoull(argv[1]);std::filesystem::path path=argv[2];
    SyntheticCase c(2,2);c.description.run.diagnostics.push_back({DiagnosticKind::transient_accepted_steps,DiagnosticRequirement::optional,count});
    c.limits.provenance_bytes_bound=65536;c.info.accepted_count=count;c.info.last_accepted_value=.01*count;c.info.requested_end=c.info.last_accepted_value;
    auto prepared=PreparedExport::prepare(c.description,c.limits);auto codec=Codec::create(c.limits.frame_values);if(!prepared||!codec)return 3;
    DiagnosticTraceView trace;trace.kind=DiagnosticKind::transient_accepted_steps;trace.availability=DiagnosticAvailability::included;trace.capture_status=DiagnosticCaptureStatus::complete;trace.records={{},nullptr,row,count};trace.counts_present=true;trace.eligible_count=count;
    auto v=c.view();v.diagnostics=std::span(&trace,1);auto provenance=c.provenance();
    auto before=allocation_meter::start();auto session=ArchiveSession::open(*prepared,*codec,path);if(!session)return 4;
    auto status=session->append_run(v,provenance);if(!status){std::cerr<<status_name(status.code)<<' '<<status.operation<<'\n';return 5;}status=session->finalize();if(!status)return 6;
    auto peak=allocation_meter::maximum_since(before);auto bytes=std::filesystem::file_size(path);
    std::cout<<"{\"trace_rows\":"<<count<<",\"prepared_workspace_bytes\":"<<prepared->plan().total_bytes<<",\"save_peak_cpp_bytes\":"<<peak<<",\"archive_bytes\":"<<bytes<<",\"archive_bound\":"<<prepared->plan().maximum_archive_bytes<<"}\n";
    return peak<65536&&bytes<=prepared->plan().maximum_archive_bytes?0:7;
}
