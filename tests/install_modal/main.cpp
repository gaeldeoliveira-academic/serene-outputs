#include <serene/outputs/modal.hpp>
#include <array>
#include <iostream>
#include <numbers>
/// Exercise only installed public APIs; the caller supplies one fresh destination.
int main(int argc,char** argv) {
    // Prepare a bounded physical observation, publish one mass-normalized mode, and report status.
    using namespace serene::outputs;
    if(argc!=2)return 2;
    ModalArchiveDescription d; // Owned one-run identity and reference catalog.
    d.file_id="e21d1221-66e1-4fa9-9d18-0913ab01f002";d.created_utc="2026-11-22T00:00:00Z";d.run_id="installed-modal";d.assembly_ref="fixture";d.physical_id="reference";d.binding_id="coordinates";d.requested={true,true,true,false};
    ModalObservationDescription observation; // Explicit world origin with port-local reference triad.
    observation.request_id="tip";observation.occurrence_id="blade";observation.port_id="tip";observation.reference_triad="port_local";d.observations.push_back(observation);
    ExportLimits limits;limits.frame_values=4;limits.provenance_bytes_bound=1024; // Small admitted serialization policy.
    auto prepared=PreparedModalExport::prepare(std::move(d),{1,1,4},limits);auto codec=Codec::create(4); // Separate owned preparation and serial codec.
    if(!prepared||!codec)return 3;
    const std::array<ModalModeView,1> modes{{{7,0,4.,ModalModeClass::elastic,1./std::numbers::pi}}}; // One positive independent mode with unit modal mass convention.
    const std::array<ModalClusterView,1> clusters{{{8,0,1}}}; // Complete single-mode cutoff cluster.
    const std::array<double,6> shape{1.,-0.,0.,0.,0.,0.}; // Exact world translation/rotation coefficients including signed zero.
    ModalRunResultView result; // All references borrow arrays until finalized below.
    result.result_kind=ModalResultKind::certified_spectrum;result.search_status=ModalSearchStatus::complete;result.certification_status=ModalCertificationStatus::certified;result.solve_code="success";result.modes=modes;result.clusters=clusters;result.frequencies_state=result.shapes_state=SectionState::included;
    result.criteria.minimum_eigenvalue_per_s2=0.;result.criteria.maximum_eigenvalue_per_s2=8.;result.criteria.zero_eigenvalue_tolerance_per_s2=1e-9;result.criteria.negative_eigenvalue_tolerance_per_s2=1e-9;
    result.report.requested_elastic_modes=1;result.report.searched_candidates=1;result.report.retained_elastic_modes=1;result.report.cutoff_cluster_size=1;result.report.certified_lower_eigenvalue_per_s2=0.;result.report.certified_upper_eigenvalue_per_s2=4.;result.report.exhausted_complete_spectrum=true;
    result.report.dense_absolute_uncertainty_per_s2=0.;
    result.shapes=*ValueView::strided(shape,1,6,6,1);
    auto session=ModalArchiveSession::open(*prepared,*codec,argv[1]); // Exclusive unpublished session borrowing owners above.
    if(!session)return 4;
    const auto append=session->append_run(result);if(!append){std::cerr<<status_name(append.code)<<'\n';return 5;} // Complete immutable result write.
    const auto final=session->finalize();if(!final){std::cerr<<status_name(final.code)<<'\n';return 6;} // Checked no-clobber publication.
    std::cout<<argv[1]<<'\n';return 0;
}
