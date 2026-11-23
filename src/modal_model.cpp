#include <serene/outputs/modal.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace serene::outputs {
namespace {
/// Check borrowed text for the canonical lowercase 8-4-4-4-12 hexadecimal UUID layout.
bool valid_uuid(std::string_view value) noexcept {
    // Require the fixed width, then validate separator positions and lowercase hex digits.
    if (value.size() != 36)
        return false;
    for (std::size_t j /* Character position in the canonical UUID. */ = 0; j < value.size(); j++) {
        if (j == 8 || j == 13 || j == 18 || j == 23) {
            if (value[j] != '-')
                return false;
        } else if (!((value[j] >= '0' && value[j] <= '9') || (value[j] >= 'a' && value[j] <= 'f')))
            return false;
    }
    return true;
}
/// Validate borrowed UTC timestamp spelling, calendar date and optional fractional seconds.
/// Accept only a trailing Z and ordinary 0..59 seconds; do not normalize timezone text.
bool valid_utc(std::string_view s) noexcept {
    // Check separators, parse decimal fields, validate Gregorian bounds/leap years,
    // then admit an optional nonempty decimal fraction before the UTC suffix.
    if (s.size() < 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' ||
        s[16] != ':' || s.back() != 'Z')
        return false;
    // Parse n digits at start, returning -1 on a nondigit; caller has checked width.
    auto number = [&](std::size_t start, std::size_t n) {
        // Accumulate decimal digits without locale or allocation.
        int v = 0; // Parsed decimal field accumulator.
        for (std::size_t j /* Digit offset within the requested timestamp field. */ = 0; j < n; j++) {
            char c /* Current timestamp digit. */ = s[start + j];
            if (c < '0' || c > '9')
                return -1;
            v = 10 * v + c - '0';
        }
        return v;
    };
    int y = number(0, 4), m = number(5, 2), d = number(8, 2), hh = number(11, 2),
        mm = number(14, 2), ss = number(17, 2); // Year/month/day and hour/minute/second fields.
    if (y < 0 || m < 1 || m > 12 || d < 1 || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 ||
        ss > 59)
        return false;
    // Gregorian month lengths, adjusted below for a leap-year February.
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maximum /* Admitted day count for this parsed month/year. */ = days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    if (d > maximum)
        return false;
    if (s.size() > 20) {
        if (s[19] != '.' || s.size() < 22)
            return false;
        for (std::size_t j /* Fractional-second digit cursor. */ = 20; j < s.size() - 1; j++)
            if (s[j] < '0' || s[j] > '9')
                return false;
    }
    return true;
}
/// Small private fixed-status exception used only at preparation/admission boundaries.
struct AdmissionFailure { Status status; ///< Exact bounded failure propagated to the caller.
};
/// Require a model invariant or throw the private fixed-status envelope.
void need(bool condition,StatusCode code=StatusCode::invalid_contract) {
    // Convert a failed invariant to one allocation-free status.
    if(!condition)throw AdmissionFailure{error(code,130)};
}
/// Return whether x is canonical positive zero, including its sign bit.
bool positive_zero(double x) noexcept {
    // Distinguish canonical missing values from preserved scientific signed zero.
    return x==0.&&!std::signbit(x);
}
/// Admit required/optional UTF-8 text using the baseline identity rules.
void text_ok(std::string_view value,bool required=false) {
    // Validate byte syntax and required nonempty identity.
    need(valid_utf8(value)&&(!required||!value.empty()));
}
/// Return overflow-safe checked a+b; resource failure precedes dependent allocation.
std::uint64_t add(std::uint64_t a,std::uint64_t b) {
    // Apply the shared signed-file-extent arithmetic ceiling.
    std::uint64_t result{}; // Admitted sum, assigned only after overflow checks.
    need(checked_add(a,b,result),StatusCode::resource_limit);return result;
}
/// Return overflow-safe checked a*b; resource failure precedes dependent allocation.
std::uint64_t multiply(std::uint64_t a,std::uint64_t b) {
    // Apply shared multiplication admission before returning the product.
    std::uint64_t result{}; // Admitted product, assigned only after overflow checks.
    need(checked_multiply(a,b,result),StatusCode::resource_limit);return result;
}
/// Check optional finite double evidence, optionally nonnegative or strictly positive.
void evidence(const std::optional<double>& v,int sign=0) {
    // Validate only available source facts; missing data is serialized canonically later.
    if(v)need(std::isfinite(*v)&&(sign==0||(sign==1?*v>=0.:*v>0.)));
}
/// Validate one independent included/unavailable report array state and its actual count.
void array_state(SectionState state,std::uint64_t count,std::uint64_t capacity) {
    // Admit explicit evidence state before dependent array traversal.
    need(state==SectionState::included||state==SectionState::unavailable);
    need(state==SectionState::included||count==0);
    need(count<=capacity,StatusCode::resource_limit);
}
/// Validate a requested quantity's retention state independently of scientific success.
void quantity_state(SectionState state,bool requested,bool success) {
    // Distinguish unrequested, unavailable and included-empty populations.
    need(static_cast<unsigned>(state)<=2);
    need((state==SectionState::disabled)==!requested);
    if(!success&&requested)need(state==SectionState::unavailable);
}
/// Validate borrowed document envelope; included JSON parsing remains a caller preflight duty.
void document_ok(const DocumentView& d) {
    // Admit state, media type and finite original byte extent without materialization.
    need(static_cast<unsigned>(d.state)<=3);
    if(d.state==SectionState::included||d.state==SectionState::partial) {
        need(d.media_type=="application/json");need(d.bytes.size<=4294967296ull,StatusCode::resource_limit);
    } else need(d.bytes.size==0);
}
} // namespace

std::expected<PreparedModalExport,Status> PreparedModalExport::prepare(ModalArchiveDescription d,ModalCapacity capacity,ExportLimits limits) {
    // Validate identities/catalog/capacities, compute conservative reservations, then allocate owned scratch.
    try {
        need(limits.frame_values>0&&limits.frame_values%4==0&&limits.frame_values<=maximum_frame_values);
        need(limits.copy_tile_bytes>=8&&limits.copy_tile_bytes<=maximum_extent&&limits.index_tile_records>0);
        need(capacity.modes<=1000000&&capacity.clusters<=capacity.modes&&capacity.report_entries<=1000000,StatusCode::resource_limit);
        need(d.observations.size()<=1000000,StatusCode::resource_limit);
        text_ok(d.file_id,true);text_ok(d.created_utc,true);need(valid_uuid(d.file_id)&&valid_utc(d.created_utc));text_ok(d.writer_name,true);text_ok(d.writer_version,true);
        text_ok(d.run_id,true);text_ok(d.run_name);text_ok(d.assembly_ref,true);text_ok(d.physical_id);text_ok(d.binding_id);
        need(d.run_name_present||d.run_name.empty());
        need(d.reference_state==SectionState::included||d.reference_state==SectionState::unavailable);
        need(d.reference_state==SectionState::included?(!d.physical_id.empty()&&!d.binding_id.empty()):(d.physical_id.empty()&&d.binding_id.empty()));
        need(d.requested.eigenvalues||d.requested.frequencies_hz||d.requested.mode_shapes);
        need(d.requested.mode_shapes||d.observations.empty());
        std::uint64_t catalog_text{}; // Sum of exact retained UTF-8 bytes, independently protocol-bounded.
        std::uint64_t owned=sizeof(PreparedModalExport); // Conservative owner/vector/string payload reservation.
        for(const auto* s: {&d.file_id,&d.created_utc,&d.writer_name,&d.writer_version,&d.run_id,&d.run_name,&d.assembly_ref,&d.physical_id,&d.binding_id}) {
            owned=add(owned,s->capacity()+1);catalog_text=add(catalog_text,s->size()); // Charge each owned top-level string.
        }
        owned=add(owned,multiply(d.observations.capacity(),sizeof(ModalObservationDescription)));
        need(add(owned,multiply(d.observations.size(),64))<=limits.metadata_budget_bytes,StatusCode::resource_limit);
        std::set<std::string_view> identities; // Initialization-only uniqueness nodes preflighted conservatively above.
        for(const auto& row:d.observations) {
            text_ok(row.request_id,true);text_ok(row.occurrence_id,true);text_ok(row.port_id);text_ok(row.reference_triad,true);
            need(identities.insert(row.request_id).second);
            need(row.reference_triad=="port_local"||row.reference_triad=="beam_material"||row.reference_triad=="blade_chord"||row.reference_triad=="blade_structural_principal");
            need(static_cast<unsigned>(row.kind)<=1&&static_cast<unsigned>(row.side)<=2);
            need(std::isfinite(row.material_s_m)&&row.material_s_m>=0.);
            if(row.kind==ModalObservationKind::port)need(!row.port_id.empty());
            else need(row.port_id.empty()&&row.material_s_present);
            if(!row.material_s_present)need(positive_zero(row.material_s_m)&&row.side==ModalStationSide::unspecified);
            for(auto x:row.position_world_m)need(std::isfinite(x)); // Exact source point components.
            double norm{}; // Quaternion norm evaluated without normalizing source bytes.
            for(auto x:row.orientation_world_wxyz){need(std::isfinite(x));norm=std::hypot(norm,x);}
            need(std::abs(norm-1.)<=1e-10);
            for(const auto* s:{&row.request_id,&row.occurrence_id,&row.port_id,&row.reference_triad}) {
                owned=add(owned,s->capacity()+1);catalog_text=add(catalog_text,s->size()); // Charge copied catalog text.
            }
        }
        need(catalog_text<=256ull*1024*1024,StatusCode::resource_limit);
        need(owned<=limits.metadata_budget_bytes,StatusCode::resource_limit);
        PreparedModalExport p; // Candidate preparation owns nothing externally until complete.
        p.description_=std::move(d);p.capacity_=capacity;p.limits_=limits;
        auto& w=p.plan_; // Candidate's complete conservative reservation accounting.
        const auto inner=multiply(p.description_.observations.size(),6); // Flattened per-mode physical coefficients.
        const auto frames=multiply(capacity.modes,inner/limits.frame_values+(inner%limits.frame_values!=0)); // Maximum independent frames.
        need(frames<=(1ull<<32),StatusCode::resource_limit);
        w.gather_bytes=w.decode_bytes=multiply(limits.frame_values,8);
        w.codec_bytes=add(encoded_capacity(limits.frame_values),4096);
        w.index_bytes=multiply(limits.index_tile_records,77);w.copy_bytes=limits.copy_tile_bytes;
        w.section_capacity=256;w.directory_bytes=w.section_capacity*64;w.serialization_bytes=1048576;
        w.metadata_bytes=add(owned,multiply(std::max({capacity.modes,capacity.clusters,capacity.report_entries}),8));
        w.total_bytes=add(add(add(w.gather_bytes,w.decode_bytes),add(w.codec_bytes,w.index_bytes)),add(add(w.copy_bytes,w.directory_bytes),add(w.serialization_bytes,w.metadata_bytes)));
        need(w.metadata_bytes<=limits.metadata_budget_bytes&&w.total_bytes<=limits.workspace_budget_bytes,StatusCode::resource_limit);
        w.maximum_frames=frames;
        w.maximum_archive_bytes=add(128+w.directory_bytes+2*1048576,add(multiply(limits.provenance_bytes_bound,9),add(multiply(multiply(capacity.modes,inner),8),add(multiply(frames,102),add(catalog_text,multiply(add(add(capacity.modes,capacity.clusters),add(p.description_.observations.size(),capacity.report_entries)),256))))));
        p.gather_.resize(limits.frame_values);p.decode_.resize(limits.frame_values);p.copy_.resize(w.copy_bytes);
        p.index_.resize(w.index_bytes);p.directory_.resize(w.directory_bytes);p.serialization_.resize(w.serialization_bytes);
        p.identity_scratch_.resize(std::max({capacity.modes,capacity.clusters,capacity.report_entries}));
        return p;
    } catch(const AdmissionFailure& f){return std::unexpected(f.status);}
      catch(const std::bad_alloc&){return std::unexpected(error(StatusCode::resource_limit,130));}
      catch(...){return std::unexpected(error(StatusCode::invalid_contract,130));}
}

Status PreparedModalExport::validate(const ModalRunResultView& v) const noexcept {
    // Admit statuses/report/quantities, prove exact table consistency, then check shape view dimensions.
    try {
        need(valid());
        const auto& d=description_;const auto& r=v.report;const auto& c=v.criteria; // Immutable prepared policy and borrowed source facts.
        const auto m=v.modes.size(),g=v.clusters.size(); // Actual independent mode and cluster populations.
        need(m<=capacity_.modes&&g<=capacity_.clusters,StatusCode::resource_limit);
        need(static_cast<unsigned>(v.result_kind)<=1&&static_cast<unsigned>(v.search_status)<=3&&static_cast<unsigned>(v.certification_status)<=2);
        text_ok(v.solve_code,true);text_ok(v.diagnostic);text_ok(r.dense_fallback_reason);
        const bool success=v.result_kind==ModalResultKind::certified_spectrum; // Publication eligibility, independent of signed physics.
        if(success)need(r.requested_elastic_modes>0&&v.solve_code=="success"&&v.diagnostic.empty()&&v.search_status==ModalSearchStatus::complete&&v.certification_status==ModalCertificationStatus::certified&&d.reference_state==SectionState::included&&m>0&&g>0);
        else need(v.solve_code!="success"&&!v.diagnostic.empty()&&v.certification_status!=ModalCertificationStatus::certified&&m==0&&g==0&&r.retained_elastic_modes==0);
        quantity_state(v.frequencies_state,d.requested.frequencies_hz,success);
        quantity_state(v.mode_diagnostics_state,d.requested.mode_diagnostics,success);
        quantity_state(v.shapes_state,d.requested.mode_shapes,success);
        for(auto x:{c.minimum_eigenvalue_per_s2,c.maximum_eigenvalue_per_s2,c.zero_eigenvalue_tolerance_per_s2,c.negative_eigenvalue_tolerance_per_s2,c.relative_residual_tolerance,c.constraint_tolerance,c.mass_orthogonality_tolerance,c.cluster_relative_tolerance,c.cluster_absolute_tolerance_per_s2})need(std::isfinite(x)); // All exact authored criteria.
        need(c.minimum_eigenvalue_per_s2<=c.maximum_eigenvalue_per_s2);
        for(auto x:{c.zero_eigenvalue_tolerance_per_s2,c.negative_eigenvalue_tolerance_per_s2,c.relative_residual_tolerance,c.constraint_tolerance,c.mass_orthogonality_tolerance,c.cluster_relative_tolerance,c.cluster_absolute_tolerance_per_s2})need(x>=0.); // Nonnegative physical/numerical thresholds.
        for(const auto* x:{&r.lift_constraint_error,&r.mass_symmetry_error,&r.stiffness_symmetry_error,&r.maximum_mass_orthogonality_error,&r.dense_absolute_uncertainty_per_s2,&r.reduction_seconds,&r.dense_count_seconds,&r.sparse_seconds,&r.dense_fallback_seconds})evidence(*x,1); // Optional diagnostic/duration facts.
        evidence(r.certified_lower_eigenvalue_per_s2);evidence(r.certified_upper_eigenvalue_per_s2);evidence(r.upper_separator_eigenvalue_per_s2);
        need(r.certified_lower_eigenvalue_per_s2.has_value()==r.certified_upper_eigenvalue_per_s2.has_value());
        if(r.certified_lower_eigenvalue_per_s2)need(*r.certified_lower_eigenvalue_per_s2>=c.minimum_eigenvalue_per_s2&&*r.certified_lower_eigenvalue_per_s2<=*r.certified_upper_eigenvalue_per_s2&&*r.certified_upper_eigenvalue_per_s2<=c.maximum_eigenvalue_per_s2);
        if(r.upper_separator_eigenvalue_per_s2)need(r.certified_upper_eigenvalue_per_s2&&*r.upper_separator_eigenvalue_per_s2>*r.certified_upper_eigenvalue_per_s2);
        if(r.exhausted_complete_spectrum)need(!r.upper_separator_eigenvalue_per_s2);
        need(std::isfinite(c.cluster_subspace_tolerance)&&c.cluster_subspace_tolerance>=0.);
        evidence(r.maximum_cluster_subspace_sine_bound,1);
        evidence(r.maximum_projected_relative_residual,1);
        if(success) {
            if(r.maximum_mass_orthogonality_error)need(*r.maximum_mass_orthogonality_error<=c.mass_orthogonality_tolerance);
            if(r.maximum_projected_relative_residual)need(*r.maximum_projected_relative_residual<=c.relative_residual_tolerance);
            if(r.maximum_cluster_subspace_sine_bound)need(*r.maximum_cluster_subspace_sine_bound<=c.cluster_subspace_tolerance);
        }
        array_state(r.dependent_constraint_rows_state,r.dependent_constraint_rows.size(),capacity_.report_entries);
        array_state(r.effective_shifts_state,r.effective_shifts_per_s2.size(),capacity_.report_entries);
        for(std::size_t k=0;k<r.dependent_constraint_rows.size();++k) {
            const auto row=r.dependent_constraint_rows[k]; // Original source constraint identity.
            if(r.constraint_rows)need(row<*r.constraint_rows);identity_scratch_[k]=row;
        }
        std::sort(identity_scratch_.begin(),identity_scratch_.begin()+r.dependent_constraint_rows.size());
        need(std::adjacent_find(identity_scratch_.begin(),identity_scratch_.begin()+r.dependent_constraint_rows.size())==identity_scratch_.begin()+r.dependent_constraint_rows.size());
        for(auto shift:r.effective_shifts_per_s2)need(std::isfinite(shift)); // Actual signed attempted shifts.
        if(r.physical_coordinate_count&&r.fixed_coordinate_count)need(*r.fixed_coordinate_count<=*r.physical_coordinate_count);
        if(r.constraint_rows&&r.constraint_rank)need(*r.constraint_rank<=*r.constraint_rows);
        if(r.physical_coordinate_count&&r.reduced_dimension)need(*r.reduced_dimension<=*r.physical_coordinate_count);
        std::uint64_t elastic{}; // Exact counted positive directions, checked against source report.
        for(std::size_t k=0;k<m;++k) {
            const auto& row=v.modes[k];const auto lambda=row.eigenvalue_per_s2; // Borrowed certified direction and exact signed estimate.
            need(std::isfinite(lambda)&&lambda>=c.minimum_eigenvalue_per_s2&&lambda<=c.maximum_eigenvalue_per_s2);
            need(k==0||lambda>=v.modes[k-1].eigenvalue_per_s2);
            const auto expected=std::abs(lambda)<=c.zero_eigenvalue_tolerance_per_s2?ModalModeClass::near_zero_unresolved:(lambda<0.?(lambda< -c.negative_eigenvalue_tolerance_per_s2?ModalModeClass::adverse_negative:ModalModeClass::indeterminate_negative):ModalModeClass::elastic); // Source-independent class consistency only.
            need(row.classification==expected);need(row.cluster_index<g);elastic+=expected==ModalModeClass::elastic;identity_scratch_[k]=row.mode_id;
            evidence(row.frequency_hz,2);evidence(row.projected_relative_residual,1);evidence(row.constraint_relative_error,1);evidence(row.modal_mass,2);evidence(row.display_scale,2);
            if(success) {
                if(row.projected_relative_residual)need(*row.projected_relative_residual<=c.relative_residual_tolerance);
                if(row.constraint_relative_error)need(*row.constraint_relative_error<=c.constraint_tolerance);
                if(row.modal_mass)need(std::abs(*row.modal_mass-1.)<=c.mass_orthogonality_tolerance);
            }
            need(row.frequency_hz.has_value()==(v.frequencies_state==SectionState::included&&expected==ModalModeClass::elastic));
            if(v.mode_diagnostics_state!=SectionState::included)need(!row.projected_relative_residual&&!row.constraint_relative_error&&!row.modal_mass);
        }
        std::sort(identity_scratch_.begin(),identity_scratch_.begin()+m);
        need(std::adjacent_find(identity_scratch_.begin(),identity_scratch_.begin()+m)==identity_scratch_.begin()+m);
        std::uint64_t cursor{},last_cluster_elastic{}; // Exact cluster partition endpoint and cutoff quota contribution.
        for(std::size_t k=0;k<g;++k) {
            const auto& cluster=v.clusters[k]; // Borrowed stable cluster and contiguous independent members.
            need(cluster.first_mode_index==cursor&&cluster.mode_count>0&&cluster.mode_count<=m-cursor);
            identity_scratch_[k]=cluster.cluster_id;last_cluster_elastic=0;
            for(std::uint64_t j=0;j<cluster.mode_count;++j) {
                const auto index=cursor+j;const auto& row=v.modes[index]; // Current member in authoritative signed order.
                need(row.cluster_index==k);last_cluster_elastic+=row.classification==ModalModeClass::elastic;
                // Cluster membership is certified on same-run counted eigenvalues, not recomputed from Ritz estimates.
            }
            cursor+=cluster.mode_count;
        }
        need(cursor==m);
        std::sort(identity_scratch_.begin(),identity_scratch_.begin()+g);
        need(std::adjacent_find(identity_scratch_.begin(),identity_scratch_.begin()+g)==identity_scratch_.begin()+g);
        if(success) {
            need(r.searched_candidates==m&&r.retained_elastic_modes==elastic&&elastic>=r.requested_elastic_modes&&elastic-last_cluster_elastic<r.requested_elastic_modes);
            need(r.cutoff_cluster_size&&*r.cutoff_cluster_size==v.clusters.back().mode_count);
            need(r.certified_lower_eigenvalue_per_s2&&*r.certified_lower_eigenvalue_per_s2==c.minimum_eigenvalue_per_s2);
            need(r.certified_upper_eigenvalue_per_s2&&r.dense_absolute_uncertainty_per_s2);
            if(!r.exhausted_complete_spectrum)need(r.upper_separator_eigenvalue_per_s2.has_value());
            if(r.dense_counted_eigenvalues)need(*r.dense_counted_eigenvalues>=m);
        }
        if(v.shapes_state==SectionState::included) {
            need(v.shapes.outer==m&&v.shapes.inner==description_.observations.size()*6);
            if(!v.shapes.callback) {
                const auto check=ValueView::strided(v.shapes.storage,v.shapes.outer,v.shapes.inner,v.shapes.outer_stride,v.shapes.inner_stride); // Independent span extent admission before first shape gather.
                need(check.has_value());
            }
        } else need(v.shapes.outer==0&&v.shapes.inner==0&&v.shapes.storage.empty()&&v.shapes.callback==nullptr);
        document_ok(v.solver_configuration);document_ok(v.execution);
        return {};
    } catch(const AdmissionFailure& f){return f.status;}
      catch(...){return error(StatusCode::invalid_contract,130);}
}

const ModalArchiveDescription& PreparedModalExport::description() const noexcept { // Expose copied immutable metadata.
    return description_;
}
const ModalCapacity& PreparedModalExport::capacity() const noexcept { // Expose admitted count ceilings.
    return capacity_;
}
const ExportLimits& PreparedModalExport::limits() const noexcept { // Expose admitted byte/frame policy.
    return limits_;
}
const WorkspacePlan& PreparedModalExport::plan() const noexcept { // Expose conservative requested-payload accounting.
    return plan_;
}
bool PreparedModalExport::valid() const noexcept { // Detect empty or moved-from required workspaces.
    return !gather_.empty()&&!decode_.empty()&&!copy_.empty()&&!directory_.empty()&&!serialization_.empty();
}
std::span<double> PreparedModalExport::gather_buffer() noexcept { // Borrow serial gather scratch.
    return gather_;
}
std::span<double> PreparedModalExport::decode_buffer() noexcept { // Borrow serial reconstruction scratch.
    return decode_;
}
std::span<std::byte> PreparedModalExport::index_buffer() noexcept { // Borrow canonical index tile.
    return index_;
}
std::span<std::byte> PreparedModalExport::copy_buffer() noexcept { // Borrow canonical/source copy tile.
    return copy_;
}
std::span<std::byte> PreparedModalExport::directory_buffer() noexcept { // Borrow fixed section directory.
    return directory_;
}
std::span<std::byte> PreparedModalExport::serialization_buffer() noexcept { // Borrow bounded JSON serialization workspace.
    return serialization_;
}
} // namespace serene::outputs
