#include "modal_case.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
using namespace serene::outputs;
namespace {
/// Fail this bounded test at the first violated named assertion, including Release builds.
void check(bool condition,const char* reason) {
    // Propagate one readable test failure to main's exception boundary.
    if(!condition)throw std::runtime_error(reason);
}
/// Return a small bounded initialization policy that exercises shape tails and tiled indexes.
ExportLimits limits() {
    // Keep frame rows deliberately short and reserve only the manufactured payload.
    ExportLimits result; // Explicit finite frame/copy/provenance policy for tests.
    result.frame_values=4;result.index_tile_records=2;result.copy_tile_bytes=64;result.provenance_bytes_bound=4096;return result;
}
/// Prepare and synchronously publish one final borrowed packet, reporting the first failure.
void save(ModalCase& packet,const std::filesystem::path& path) {
    // Admit copied metadata, create a bounded codec, write immutable borrowed data and publish once.
    auto p=PreparedModalExport::prepare(packet.description,{16,16,32},limits());check(bool(p),"prepare modal");
    auto codec=Codec::create(4);check(bool(codec),"create modal codec");
    auto session=ModalArchiveSession::open(*p,*codec,path);check(bool(session),"open modal");
    check(bool(session->append_run(packet.view())),"append modal");check(bool(session->finalize()),"publish modal");
    check(session->state()==SessionState::published,"published modal state");
    check(!session->finalize(),"double modal publication rejected");
    check(!ModalArchiveSession::open(*p,*codec,path),"modal no-clobber existing destination");
}
/// One operation selected for deterministic synchronous writer fault injection.
struct Fault { IoOperation operation; ///< Operation on which every call fails.
};
/// Return an injected I/O failure only for the selected operation, without mutating source data.
Status fault(void* context,IoOperation operation,std::uint64_t) noexcept {
    // Compare the selected test operation and return its fixed failure status.
    return static_cast<Fault*>(context)->operation==operation?error(StatusCode::io_failure,199):Status{};
}
/// Prove every failed publication path leaves no final artifact and removes owned scratch.
void fault_tests(ModalCase& packet,const std::filesystem::path& directory) {
    // Exercise source, codec and each relevant I/O lifecycle boundary independently.
    for(unsigned k=0;k<7;++k) {
        const auto path=directory/("fault-"+std::to_string(k)+".serene"); // Fresh destination for this deterministic operation failure.
        std::filesystem::path temporary; // Copied scratch path inspected after session destruction.
        {
            auto p=PreparedModalExport::prepare(packet.description,{16,16,32},limits());check(bool(p),"fault preparation");
            auto codec=Codec::create(4);check(bool(codec),"fault codec");Fault selected{static_cast<IoOperation>(k)}; // Caller-owned callback lifetime covers the session.
            auto session=ModalArchiveSession::open(*p,*codec,path,{&selected,fault});
            if(session) {
                temporary=session->temporary_path();const auto append=session->append_run(packet.view()); // First potentially selected failure boundary.
                if(append)check(!session->finalize(),"selected finalize fault propagated");
                check(session->state()==SessionState::failed,"failure poisons modal session");
            }
        }
        check(!std::filesystem::exists(path),"fault never published destination");
        check(temporary.empty()||!std::filesystem::exists(temporary),"unpublished modal temporary removed");
    }
}
} // namespace
/// Run bounded source-writer tests and leave their new archives available for independent readback.
int main(int argc,char** argv) {
    // Admit caller paths, exercise model failures and successful variants, then run publication faults.
    try {
        check(argc==3,"usage: test_modal independent-oracle.json fresh-output-root");
        const std::filesystem::path oracle=argv[1]; // Read-only independent source packet.
        const auto directory=std::filesystem::path(argv[2])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()); // Unique retained invocation artifacts.
        check(!std::filesystem::exists(directory),"fresh output directory required");std::filesystem::create_directories(directory);
        ModalCase packet(oracle);auto prepared=PreparedModalExport::prepare(packet.description,{16,16,32},limits());check(bool(prepared),"model preparation");
        auto view=packet.view();check(bool(prepared->validate(view)),"manufactured packet valid");
        auto bad=view;bad.report.searched_candidates=3;check(!prepared->validate(bad),"inconsistent complete prefix count");
        bad=view;bad.report.retained_elastic_modes=1;check(!prepared->validate(bad),"exact elastic population required");
        bad=view;bad.report.cutoff_cluster_size=1;check(!prepared->validate(bad),"complete cutoff cluster required");
        bad=view;bad.report.certified_upper_eigenvalue_per_s2=5.;check(bool(prepared->validate(bad)),"dense counted upper may differ from Ritz endpoint");
        bad=view;bad.report.upper_separator_eigenvalue_per_s2=4.;check(!prepared->validate(bad),"separate upper boundary required");
        bad=view;bad.report.maximum_projected_relative_residual=1.;check(!prepared->validate(bad),"successful residual must satisfy declared criterion");
        bad=view;bad.report.dense_absolute_uncertainty_per_s2.reset();check(!prepared->validate(bad),"successful grouping requires dense uncertainty evidence");
        bad=view;bad.search_status=ModalSearchStatus::incomplete;check(!prepared->validate(bad),"incomplete cannot masquerade as success");
        bad=view;bad.shapes.inner-=1;check(!prepared->validate(bad),"physical shape extent exact");
        packet.modes[1].mode_id=packet.modes[0].mode_id;check(!prepared->validate(packet.view()),"mode IDs unique");packet.modes[1].mode_id=101;
        packet.modes[0].frequency_hz=1.;check(!prepared->validate(packet.view()),"negative mode has no real frequency");packet.modes[0].frequency_hz.reset();
        auto invalid=packet.description;invalid.observations[0].orientation_world_wxyz={0.,0.,0.,0.};check(!PreparedModalExport::prepare(invalid,{16,16,32},limits()),"invalid reference quaternion rejected");
        auto tiny=limits();tiny.workspace_budget_bytes=1;check(!PreparedModalExport::prepare(packet.description,{16,16,32},tiny),"workspace preflight rejected before dependent buffers");
        save(packet,directory/"manufactured.serene");fault_tests(packet,directory);
        ModalCase shifted(oracle);shifted.modes[3].eigenvalue_per_s2=4.00000001;shifted.modes[3].frequency_hz=std::sqrt(shifted.modes[3].eigenvalue_per_s2)/(2.*std::numbers::pi);shifted.result.report.dense_absolute_uncertainty_per_s2=1e-7;save(shifted,directory/"counted-vs-ritz.serene");
        ModalCase frequencies(oracle);frequencies.description.requested.mode_shapes=false;frequencies.description.observations.clear();frequencies.shapes.clear();frequencies.result.shapes_state=SectionState::disabled;save(frequencies,directory/"frequencies-only.serene");
        ModalCase empty(oracle);empty.description.observations.clear();empty.shapes.clear();save(empty,directory/"known-empty-shapes.serene");
        ModalCase failed(oracle);failed.modes.clear();failed.clusters.clear();failed.shapes.clear();failed.result.result_kind=ModalResultKind::failure_diagnostics;failed.result.search_status=ModalSearchStatus::incomplete;failed.result.certification_status=ModalCertificationStatus::unavailable;failed.result.solve_code="incomplete_prefix";failed.result.diagnostic="manufactured search capacity exhausted";
        failed.result.frequencies_state=failed.result.mode_diagnostics_state=failed.result.shapes_state=SectionState::unavailable;failed.result.report.retained_elastic_modes=0;failed.result.report.cutoff_cluster_size.reset();failed.result.report.certified_lower_eigenvalue_per_s2.reset();failed.result.report.certified_upper_eigenvalue_per_s2.reset();failed.result.report.upper_separator_eigenvalue_per_s2.reset();save(failed,directory/"failure-diagnostics.serene");
        failed.result.report.requested_elastic_modes=0;failed.result.search_status=ModalSearchStatus::complete;failed.result.certification_status=ModalCertificationStatus::failed;failed.result.solve_code="numerical_failure";failed.result.report.certified_lower_eigenvalue_per_s2=-8.;failed.result.report.certified_upper_eigenvalue_per_s2=4.;save(failed,directory/"failed-certification.serene");
        std::cout<<directory.string()<<'\n';return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
