# Serene native outputs 1.2 — modal results

Status: M1.04 implementation contract draft, 22 November 2026. Qualification is
recorded separately. This adds single-run modal archives; scalar writers retain
the exact 1.0/1.1 behavior. `FORMAT.md` continues to govern the physical container,
strict JSON, directory ownership, text/document rules, bounds, digests and codec.
The storage selector remains `native_archive`, file suffix `.serene`.

## Admission and independent axes

Bootstrap/root version is exactly 1.2. Required features are exactly the five
1.0 base features plus `modal-results-1`. This first writer uses the legacy
single-run provenance roles from 1.0; indexed provenance and modal bundles are
outside this profile. Root results.index has exactly one row, kind u8=2. Existing
readers reject 1.2 as unsupported. No additional directory kind, byte encoding,
compression profile or codec dependency is introduced. Unknown supported-field
comments cannot authorize binary references. Package version is 0.3.0.

Run metadata has exactly the required known owning branches `info`, `criteria`,
`report`, `modes`, `clusters`, `observations`, `shapes`. Harmless unknown optional
JSON fields follow the baseline rule. There are no channels, requests, time,
load_levels, step_index, level_index, accepted_count or committed_count fields.
Modes and clusters have their own ordinals. Unsupported time/load accessors must
reject this run kind rather than return synthetic axes.

Notation A(dtype,[shape]) and P[N] means baseline array and UTF-8 pool descriptors.
Every descriptor owns a distinct section, including empty sections. Optional
scalar evidence is an object `{present:u8,value:u64|f64}`; absent evidence has
present=0 and canonical integer0 or positive binary64 zero. Present=1 values are
finite and satisfy their stated range. All numeric arrays preserve binary64 bits.

## Exact run metadata

`info` fields:

* `run_id`, `assembly_ref`: nonempty text; `kind`:2; `run_name_present`:u8 and
  `run_name`:text (empty if absent).
* `result_kind`:u8 (0 certified_spectrum, 1 failure_diagnostics).
* `search_status`:u8 (0 complete, 1 incomplete, 2 failed, 3 not_started).
* `certification_status`:u8 (0 certified, 1 failed, 2 unavailable).
* `solve_code`:nonempty text; `diagnostic`:text.
* `reference_state`:u8 (0 included or 2 unavailable), `physical_id`, `binding_id`:
  text, both nonempty when included and both empty when unavailable.
* `requested`: `{eigenvalues:u8,frequencies_hz:u8,mode_shapes:u8,mode_diagnostics:u8}`.
  These preserve authored selection. Signed lambda/class remain required identity
  even when eigenvalues=0. At least one of eigenvalues/frequencies_hz/mode_shapes
  is requested. mode_diagnostics is an independently requested optional quantity.
* `frequencies_state`, `mode_diagnostics_state`:u8 (0 included, 1 unrequested,
  2 unavailable). A state is1 exactly when its corresponding request flag is0.
* `solver_configuration`, `execution`:baseline document descriptors.

Successful spectrum requires solve_code=`success`, diagnostic empty,
search_status=0, certification_status=0, included reference, nonzero mode and
cluster counts, retained_elastic_modes >= requested_elastic_modes > 0, and
certified lower/upper bounds. Certified adverse/indeterminate/near-zero modes are
allowed and are independent physical findings. A failure record requires a code
other than `success`, nonempty diagnostic, certification_status!=0, zero
modes/clusters and retained_elastic_modes=0. A completed search can coexist with
failed numerical certification and still produce only a failure record. Failure
requested_elastic_modes may be0, preserving invalid-options evidence. Requested frequencies,
mode diagnostics and shapes are unavailable on failure; unrequested ones remain1.
A finalized failure record is complete delivery of failure evidence, never a
successful spectrum. Session delivery/publish failure remains a separate API fact.
Successful spectra additionally require searched_candidates=M, exact elastic
class count=retained_elastic_modes, present cutoff_cluster_size=last cluster size,
certified_lower=authored minimum, and all returned lambda within the authored
interval. Certified lower/upper are source-counted coverage evidence, with
lower<=upper and both inside the authored interval; upper need not equal or bound
the last independently certified returned Ritz estimate. Success requires an
available finite nonnegative dense_absolute_uncertainty_per_s2. A nonexhausted
spectrum requires upper_separator strictly above certified_upper; an exhausted
spectrum forbids a separator. If dense_counted_eigenvalues
is present it is >=M. Removing the final cluster must leave fewer elastic modes
than requested, making it the actual quota cutoff cluster. These are archive
consistency checks, not independent operator completeness certification.
Failure coverage is optional independent evidence: lower and upper are either
both present or both absent; present bounds form an ordered pair inside the
authored interval. A present separator requires upper and is strictly greater.
exhausted_complete_spectrum always forbids a separator. These facts never make a
failure record a successful spectrum or authorize returning failed directions.

`criteria` consists of finite f64 scalar fields:
`minimum_eigenvalue_per_s2`, `maximum_eigenvalue_per_s2`,
`zero_eigenvalue_tolerance_per_s2`, `negative_eigenvalue_tolerance_per_s2`,
`relative_residual_tolerance`, `constraint_tolerance`,
`mass_orthogonality_tolerance`, `cluster_relative_tolerance`,
`cluster_absolute_tolerance_per_s2`. Minimum<=maximum; all tolerances >=0.
An additional required finite nonnegative `cluster_subspace_tolerance` records
the source's accepted mass-weighted cluster subspace sine bound.
The source groups adjacent eigenvalues from the same-run dense count using gap
<= absolute tolerance + relative tolerance*max(abs(count_left),abs(count_right))
+ 2*dense_absolute_uncertainty_per_s2. Returned mode eigenvalues are separately
certified Ritz estimates and may differ from the counted values. Preserve source
membership and grouping parameters: readers and writers do not recompute this
scientific grouping from returned Ritz estimates or recreate the eigensolver.
Reported vectors remain independent and arbitrary sign is allowed.

`report` has these required scalar counts u64:
`requested_elastic_modes`, `searched_candidates`, `retained_elastic_modes`.
Required boolean integer flags: `exhausted_complete_spectrum`, `used_sparse_path`,
`used_dense_fallback`. Required text: `dense_fallback_reason` (may be empty).
Every following count is an optional scalar evidence object:
`physical_coordinate_count`, `fixed_coordinate_count`, `constraint_rows`,
`constraint_rank`, `reduced_dimension`, `reduced_mass_nonzeros`,
`reduced_stiffness_nonzeros`, `dense_counted_eigenvalues`, `cutoff_cluster_size`,
`sparse_requested_candidates`, `sparse_converged_candidates`, `sparse_iterations`,
`krylov_basis_size`, `constraint_workspace_bytes`, `operator_workspace_bytes`,
`dense_workspace_bytes`, `dense_work_bound`, `factor_workspace_bytes`,
`basis_workspace_bytes`, `result_payload_bytes`.
Optional f64 evidence objects:
`lift_constraint_error`, `mass_symmetry_error`, `stiffness_symmetry_error`,
`maximum_mass_orthogonality_error`, `certified_lower_eigenvalue_per_s2`,
`certified_upper_eigenvalue_per_s2`, `upper_separator_eigenvalue_per_s2`,
`dense_absolute_uncertainty_per_s2`, `reduction_seconds`, `dense_count_seconds`,
`sparse_seconds`, `dense_fallback_seconds`. All except eigenvalue bounds/separator
are >=0. Optional report evidence is producer asserted, not reader certification.
When both respective values are available, fixed_coordinate_count must not exceed
physical_coordinate_count, constraint_rank must not exceed constraint_rows, and
reduced_dimension must not exceed physical_coordinate_count.
The additional required optional-f64 object `maximum_cluster_subspace_sine_bound`
preserves the source's estimated nonnegative mass-weighted subspace sine bound.
The required optional-f64 object `maximum_projected_relative_residual` preserves
the nonnegative maximum accepted projected eigenpair residual. On successful
spectra only, available maxima for mass orthogonality, projected residual and
cluster subspace sine must not exceed their respective declared criteria.
Failure evidence may preserve measurements exceeding those thresholds.
Two report array objects are required:
`dependent_constraint_rows:{state:u8,[values:A(u64,[D])]}` and
`effective_shifts_per_s2:{state:u8,[values:A(f64,[S])]}`. State0 included requires
values even if empty; state2 unavailable forbids values. Row identities are unique
and below known constraint_rows; shifts are finite. Counts D,S <=1,000,000.

`modes` contains `count`:M plus these arrays:
`mode_index`:A(u64,[M]) exactly0..M-1; `mode_id`:A(u64,[M]) unique;
`cluster_index`:A(u64,[M]); `eigenvalue_per_s2`:A(f64,[M]), sorted nondecreasing;
`classification`:A(u8,[M]) with 0 near_zero_unresolved,1 indeterminate_negative,
2 adverse_negative,3 elastic. Apply zero tolerance first; then negative values
strictly below minus negative tolerance are adverse; remaining negative values
are indeterminate; positive remaining values are elastic. Frequencies included
adds `frequency_hz`:A(f64,[M]) and `frequency_hz_present`:A(u8,[M]); present exactly
for elastic rows, finite positive value, otherwise canonical positive zero. A
reader does not recompute or rewrite stored Hz. Other frequency states omit both.
Diagnostics included adds each pair `<name>`:A(f64,[M]),
`<name>_present`:A(u8,[M]) for projected_relative_residual,
constraint_relative_error,modal_mass. Present residual/error >=0; mass>0. Other
states omit these six columns. Display scaling always has
`display_scale`:A(f64,[M]),`display_scale_present`:A(u8,[M]); present values>0,
absent values positive0. This multiplier is never applied to scientific values.
On successful spectra, available per-mode projected residual and constraint error
must not exceed their declared criteria; abs(modal_mass-1) must not exceed the
mass orthogonality criterion. These scalar consistency checks do not reconstruct
operators or independently certify eigenvectors.

`clusters` contains `grouping_rule`:
"adjacent_gap_absolute_plus_relative_plus_twice_count_uncertainty",
`grouping_basis`:"same_run_dense_count",
`count`:G and A(u64,[G]) columns `cluster_index` (0..G-1),
`cluster_id` (unique), `first_mode_index`, `mode_count` (positive). The ranges
partition [0,M) in table order, and every modes.cluster_index agrees. The archive
checks that partition and the cutoff quota; it preserves the source's counted
grouping without reconstructing it from Ritz values. No stored diagnostic alone
independently proves completeness against an unrecorded operator spectrum.

`observations` contains `count`:O and columns:
`observation_index`:A(u64,[O]) exactly0..O-1; `request_id`:P[O] unique nonempty;
`occurrence_id`:P[O] nonempty; `kind`:A(u8,[O]) (0 port,1 material station);
`port_id`:P[O] nonempty for ports, empty for material stations;
`reference_triad`:P[O], each exactly `port_local`, `beam_material`, `blade_chord`
or `blade_structural_principal`, supplied by the source rather than inferred from
point position. The triad says which physical local basis the quaternion maps;
in particular chord axes and structural principal axes must remain distinct.
`material_s_present`:A(u8,[O]), true for every material station and optional for
named ports backed by material locations; `material_s_m`:A(f64,[O]), finite >=0
when present and canonical positive0 when absent;
`side`:A(u8,[O]) (0 unspecified,1 minus,2 plus; absent material coordinate requires0);
`position_world_m`:A(f64,[O,3]); `orientation_world_wxyz`:A(f64,[O,4]).
Positions/quaternions are finite; quaternion norm differs from1 by at most1e-10.
No normalization or quaternion-sign canonicalization changes source values.
The quaternion maps the reference local section triad into world axes.
When shapes were not requested O=0. Included shapes with O=0 are known-empty,
distinct from unavailable shapes and permitted by the wire independent of input
schema admission. Observation request IDs address physical rows; no frame selector.
The initial four mode classes explicitly do not claim proved rigid or mechanism
subclasses. A later provider that distinguishes them needs a separately qualified
extension; their identities must not be silently collapsed into this profile.

`shapes` always has `state`:u8 (0 included,1 unrequested,2 unavailable).
State1 exactly when mode_shapes request=0; state0 requires a certified spectrum.
Only state0 adds `logical_shape`:[M,O,6], `component_names`:
["translation_x","translation_y","translation_z","rotation_x","rotation_y","rotation_z"],
`component_units`:["m","m","m","rad","rad","rad"], `frame`:"world",
`normalization`:"unit_generalized_mass", `coefficient_convention`:
"per_unit_modal_coordinate", and `values` containing the baseline framed values
descriptor with shape[M,6*O] and axis_names["mode","observation_component"].
Units label physical components; coefficients multiply a modal coordinate whose
physical amplitude is independent. Rotations are infinitesimal angular increments.
No display scaling, sign alignment or normalization alters stored coefficients.
Value profiles are exactly one baseline reversible profile; every row maps to0.
Frames partition each mode row and never cross rows. All coefficients are finite.
Logical shape is metadata only; binary array descriptors remain rank<=2.

## Integrity, limits, memory and immutable delivery

Mode/cluster/observation counts are <=1,000,000; clusters<=modes; observations are
ordered even when repeated physical points have different request IDs. Products
6*O, M*6*O, section sizes and frame counts use checked arithmetic before allocation.
Baseline limits (1MiB metadata, 1MiB per text, 256MiB catalog text, 2^32 frames,
2^40 dimensions, 2^63-1 extents, 1,048,576 sections/frame values and 16MiB encoded
frame bytes) apply independently. Caller budgets can be stricter.

Root, run, catalogs and all report arrays have section hashes. Framed shape values
retain per-frame hashes and reversible round-trip verification plus whole-payload
section digest. Selective access and verify_all retain their baseline distinction.
Every binary descriptor has one owner; full verification rejects aliases/orphans.
No checksum proves authenticity or numerical certification.

Preparation owns copied metadata, capacity policy and reusable scratch. The result
view borrows immutable mode/cluster/report/shape data and document bytes. Core's
lease must retain these through append/finalize/abandonment and forbid replacement
or concurrent mutation. Preparation/export allocation is inactive and bounded by
declared buffers/catalog/metadata policy; no total-process memory guarantee is made.

Writing uses an exclusive unpublished temporary, finalized directory/bootstrap,
checked flush/close and no-clobber publication. Failure never publishes an ordinary
artifact; destruction removes owned unpublished scratch. Finalization of a failure
record remains explicitly identified failure evidence. No power-loss guarantee.

## Required qualification

Independently authored manufactured packets precede writer tests. Include signed
classes, multiplicity/complete cutoff clusters, exact arbitrary signs/signed zero,
rotated offset physical samples, optional states, frequencies-only and known-empty
shapes. Reject malformed dimensions, class/Hz inconsistencies, wrong frames,
bad quaternions, unsupported versions/features, truncation/corruption and an
incomplete run mislabeled successful. Test source/installed writers, isolated new
wheel, new-reader old1.0/1.1 archives and old-reader1.2 rejection. Qualification
does not imply bundle/modal orchestration or platform coverage not actually run.
