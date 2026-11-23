#pragma once
#include <serene/outputs/modal.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

/// Owned test packet copied from the independently authored manufactured oracle. Its views
/// borrow immutable vectors through save; this helper neither solves nor authors oracle bytes.
struct ModalCase {
    serene::outputs::ModalArchiveDescription description; ///< Owned identity/ordered physical references.
    std::vector<serene::outputs::ModalModeView> modes; ///< Owned certified per-direction records.
    std::vector<serene::outputs::ModalClusterView> clusters; ///< Owned complete cluster partition.
    std::vector<double> shapes; ///< Owned mode/observation/component coefficients from independent oracle.
    serene::outputs::ModalRunResultView result; ///< Scalar/report facts plus borrows populated by view().
    /// Read the independent oracle and construct a deterministic source result with all selected evidence.
    explicit ModalCase(const std::filesystem::path& oracle) {
        // Parse test-owned JSON, copy physical tables and bind the source's declared certification facts.
        using namespace serene::outputs;
        std::ifstream file(oracle); // Read-only independent oracle stream.
        if(!file)throw std::runtime_error("cannot open independent modal oracle");
        const auto packet=nlohmann::json::parse(file); // Test-only parsed packet, never part of installed library/runtime.
        description.file_id="e21d1221-66e1-4fa9-9d18-0913ab01f001";description.created_utc="2026-11-22T00:00:00Z";
        description.run_id="manufactured-modal";description.run_name_present=true;description.run_name="Manufactured signed spectrum";
        description.assembly_ref="blade/fixture";description.physical_id="manufactured-physical-reference";description.binding_id="manufactured-six-coordinate-binding";
        description.requested={true,true,true,true};
        for(const auto& item:packet.at("observations")) {
            ModalObservationDescription row; // Copied one physical point/triad and source address.
            row.request_id=item.at("request_id");row.occurrence_id=item.at("occurrence_id");row.port_id=item.at("port_id");
            row.kind=item.at("kind")=="port"?ModalObservationKind::port:ModalObservationKind::material_station;
            row.reference_triad=row.kind==ModalObservationKind::port?"port_local":"beam_material";
            row.material_s_m=item.at("material_s_m").is_null()?0.:item.at("material_s_m").get<double>();
            row.material_s_present=!item.at("material_s_m").is_null();
            row.side=item.at("side")=="left"?ModalStationSide::minus:item.at("side")=="right"?ModalStationSide::plus:ModalStationSide::unspecified;
            row.position_world_m=item.at("position_world_m").get<std::array<double,3>>();row.orientation_world_wxyz=item.at("orientation_world_wxyz").get<std::array<double,4>>();
            description.observations.push_back(std::move(row));
        }
        for(std::size_t k=0;k<packet.at("eigenvalues_per_s2").size();++k) {
            ModalModeView row; // One independent direction with exact source eigenvalue/sign.
            row.mode_id=100+k;row.cluster_index=packet.at("cluster_ids").at(k);row.eigenvalue_per_s2=packet.at("eigenvalues_per_s2").at(k);
            const auto label=packet.at("classifications").at(k).get<std::string>(); // Independent oracle's physical class spelling.
            row.classification=label=="elastic"?ModalModeClass::elastic:label=="adverse_negative"?ModalModeClass::adverse_negative:ModalModeClass::near_zero_unresolved;
            if(!packet.at("frequency_hz").at(k).is_null())row.frequency_hz=packet.at("frequency_hz").at(k);
            row.projected_relative_residual=0.;row.constraint_relative_error=0.;row.modal_mass=1.;modes.push_back(row);
        }
        std::uint64_t first{}; // Contiguous mode-table cursor for the independent complete clusters.
        for(const auto& size:packet.at("cluster_sizes")) {
            const auto count=size.get<std::uint64_t>(); // Independent cluster multiplicity.
            clusters.push_back({1000+clusters.size(),first,count});first+=count;
        }
        for(const auto& mode:packet.at("shape_values"))for(const auto& observation:mode)for(const auto& component:observation)shapes.push_back(component.get<double>()); // Preserve exact oracle component order.
        result.result_kind=ModalResultKind::certified_spectrum;result.search_status=ModalSearchStatus::complete;result.certification_status=ModalCertificationStatus::certified;result.solve_code="success";
        result.frequencies_state=result.mode_diagnostics_state=result.shapes_state=SectionState::included;
        result.criteria.minimum_eigenvalue_per_s2=packet.at("search_interval_per_s2").at(0);result.criteria.maximum_eigenvalue_per_s2=packet.at("search_interval_per_s2").at(1);
        result.criteria.zero_eigenvalue_tolerance_per_s2=packet.at("zero_tolerance_per_s2");result.criteria.negative_eigenvalue_tolerance_per_s2=packet.at("negative_tolerance_per_s2");
        result.criteria.cluster_relative_tolerance=packet.at("cluster_relative_tolerance");result.criteria.cluster_absolute_tolerance_per_s2=packet.at("cluster_absolute_tolerance_per_s2");
        result.criteria.relative_residual_tolerance=1e-10;result.criteria.constraint_tolerance=1e-10;result.criteria.mass_orthogonality_tolerance=1e-10;
        auto& report=result.report; // Exact source certification report associated with this packet.
        report.requested_elastic_modes=packet.at("requested_elastic_modes");report.searched_candidates=modes.size();report.retained_elastic_modes=packet.at("found_elastic_modes");
        report.dense_counted_eigenvalues=packet.at("dense_counted_eigenvalues");report.cutoff_cluster_size=clusters.back().mode_count;
        report.dense_absolute_uncertainty_per_s2=0.;
        report.certified_lower_eigenvalue_per_s2=packet.at("certified_lower_per_s2");report.certified_upper_eigenvalue_per_s2=packet.at("certified_upper_per_s2");report.upper_separator_eigenvalue_per_s2=packet.at("upper_separator_per_s2");
        report.physical_coordinate_count=6;report.fixed_coordinate_count=0;report.constraint_rows=0;report.constraint_rank=0;report.reduced_dimension=6;
        report.dependent_constraint_rows_state=SectionState::included;report.effective_shifts_state=SectionState::included;
    }
    /// Return a fresh borrowed view after test storage changes; owner must outlive synchronous export.
    serene::outputs::ModalRunResultView view() const {
        // Bind current table spans and physical shape extents without copying numerical storage.
        auto v=result; // Local scalar report copy; its eventual spans still borrow this object.
        v.modes=modes;v.clusters=clusters;
        if(v.shapes_state==serene::outputs::SectionState::included) {
            const auto width=description.observations.size()*6; // Flattened physical row width.
            const auto values=serene::outputs::ValueView::strided(shapes,modes.size(),width,width,1);
            if(!values)throw std::runtime_error("invalid test shape view");v.shapes=*values;
        } else v.shapes={};
        return v;
    }
};
