"""Author independent inflow-plane fixture bytes and literal row expectations.

Main approved the exact nested manifest predicate on20November2026. Provider
manifest preservation is semantic JSON-object preservation, not whitespace/text
identity. Only earlier independent catalogs/rows are imported; no production
encoder/checker or source asset is used during authoring. Five immutable older
readers may be classified only by a separate explicit command. Synthetic velocity
literals do not qualify provider numerics, Core capture or renderer behavior.
"""
from pathlib import Path
import copy
import hashlib
import importlib.util
import math
import json
import struct
import sys
import zlib

from author_moment_fixtures import CATALOG as MOMENT_CATALOG, frames as moment_frames

# Original emitted documents are embedded as literals: the author and later readers never reopen assets.
# Native path: ../serene-inflow/build/visualization/stage04-typed-inflow-20261120/installed-plane-run-manifest.json
# Mean path: same stage root/release/cli-tests/output/run-manifest.json
NATIVE_MANIFEST_JSON = '{"format":"serene-inflow.initialization.v1","identity":{"inflow_id":"wind","run_id":"plane_selected","diagnostic_provider_id":1,"adapter_source_id":"1e7a3444f4c643b6a4a60743f019f0644993e3193f87bd4bf3a1b43901df38b8","provider_source_id":"1e7a3444f4c643b6a4a60743f019f0644993e3193f87bd4bf3a1b43901df38b8","source_identity_algorithm":"sha256_source_set","schema":{"input_format_version":"3.0-wip.15","construction_profile_version":"1.0","manifest_sha256":"96324e0eeadf55d529497ddb8174432424d15514b060d1628fe449d48bb97bc1"}},"validation":{"scope":"effective_run_candidate","standalone_provider_prepared":true,"core_binding":"not_performed"},"document_path":"C:/Users/12650029/serene-academic/serene-inflow/build/visualization/stage04-typed-inflow-20261120/plane-run.serene","mean":{"mean_speed_mps":10,"reference_height_m":100,"ground_z_m":0,"yaw_deg":0,"upflow_deg":0,"shear":"constant","exponent":0,"global_frame_id":1,"speed_definition":"magnitude_along_flow","yaw_convention":"flow_to_counterclockwise_from_global_positive_x","upflow_convention":"rising_positive_degrees_exclusive_minus90_plus90"},"basis_columns":[[1,0,0],[-0,1,0],[-0,-0,1]],"runtime_profile":{"source_kind":"native_general_kaimal_wnd","velocity_frame":"global","positive_physical_height_required":true,"mean_shear_applies_to":"mean_only","coordinate_velocity_basis":"same_proper_rotation","sampler":"trilinear","streamwise":"finite_support_error_no_wrap_or_time_remapping","transverse":"turbulence_only_clamp","mandatory_clamp_warning":"one_typed_event_per_clamped_point_per_query_including_repeats","clamp_event":"inflow.transverse_clamp","clamp_severity":"warning","diagnostic_backpressure":"blocking","filtered_warning_is_failure":true,"diagnostics_lifecycle":"caller_owns_logger_sink_registration_start_drain_and_health_checks","compatibility_check":"non_null_logger_and_warning_enabled_only_not_readiness_or_delivery"},"placement":{"anchor_global_m":[0,0,100],"lateral_origin_m":0,"vertical_origin_m":0,"phase_m":0,"time_origin_s":0,"advection_speed_mps":2,"sampling_coordinate":"phase_m+advection_speed_mps*(time_s-time_origin_s)-xB"},"amplitude":{"mode":"direct_sigma","sigma_mps":[1,2,3],"resolved_sigma_mps":[1,2,3]},"source":{"path":"C:/Users/12650029/serene-academic/serene-inflow/build/visualization/stage04-typed-inflow-20261120/assets/tiny.wnd","source_bytes":452,"fingerprint":{"algorithm":"fnv1a64","encoding":"hex_lowercase_16","value":"519271a994b0f88d","scope":"entire_source_file","cryptographic":false},"dimensions":[4,3,5],"dimension_axes":"s,y,z","spacings_m":[2,3,4],"support":{"s_m":[0,6],"y_m":[0,6],"z_m":[0,16]},"native_profile":{"byte_order":"little_endian","marker":-99,"model":7,"components":3,"header_bytes":92,"deviation_mapping":"(raw_u,-raw_v,raw_w)/1000","resident_layout":"three_float32_component_blocks_native_i_k_j","sample_index":"component*Ns*Ny*Nz+i*Ny*Nz+k*Ny+j","axis_strides":[15,1,3],"demeaned":false,"variance_restandardized":false},"header_provenance":{"generation_speed_mps":12,"max_frequency_hz":2.5,"lengths_m":[0,0,40,0,0,20,0,0,10],"length_order":"zLu,yLu,xLu,zLv,yLv,xLv,zLw,yLw,xLw","coherence_decay":12,"coherence_scale_m":100,"seed":-123,"generation_speed_used_as_runtime_fallback":false}},"coverage":{"status":"not_supplied_runtime_checks_required","coordinate_system":"box_streamwise_xB_not_global_X","runtime_domain_checks":true,"duration_alone_is_coverage_proof":false},"memory":{"cap_scope":"final_float32_samples_only","resident_cap_bytes":536870912,"resident_sample_bytes":720,"bounded_decode_buffer_bytes":65624,"native_box_object_bytes":1248,"provider_object_bytes":352,"additional_control_storage":"shared_control_block_stream_and_allocator_bookkeeping_not_measured","total_process_ram_capped":false,"provider_copy_storage":"shared_immutable_box_no_sample_copy"}}'
MEAN_MANIFEST_JSON = '{"format":"serene-inflow.initialization.v1","identity":{"inflow_id":"wind","run_id":"selected","diagnostic_provider_id":1,"adapter_source_id":"1e7a3444f4c643b6a4a60743f019f0644993e3193f87bd4bf3a1b43901df38b8","provider_source_id":"1e7a3444f4c643b6a4a60743f019f0644993e3193f87bd4bf3a1b43901df38b8","source_identity_algorithm":"sha256_source_set","schema":{"input_format_version":"3.0-wip.15","construction_profile_version":"1.0","manifest_sha256":"96324e0eeadf55d529497ddb8174432424d15514b060d1628fe449d48bb97bc1"}},"validation":{"scope":"effective_run_candidate","standalone_provider_prepared":true,"core_binding":"not_performed"},"document_path":"C:/Users/12650029/serene-academic/serene-inflow/build/visualization/stage04-typed-inflow-20261120/release/c05-run.serene","mean":{"mean_speed_mps":12,"reference_height_m":1,"ground_z_m":0,"yaw_deg":0,"upflow_deg":0,"shear":"constant","exponent":0,"global_frame_id":1,"speed_definition":"magnitude_along_flow","yaw_convention":"flow_to_counterclockwise_from_global_positive_x","upflow_convention":"rising_positive_degrees_exclusive_minus90_plus90"},"basis_columns":[[1,0,0],[-0,1,0],[-0,-0,1]],"runtime_profile":{"source_kind":"none","velocity_frame":"global","positive_physical_height_required":true,"mean_shear_applies_to":"mean_only","coordinate_velocity_basis":"same_proper_rotation"}}'
NATIVE_MANIFEST_FILE_SHA256 = '2eaad515f011c751c6145b63fee55b86e932d61951a81bbae25f1fd3192f87ef'  # Actual emitted file identity, distinct from source-asset FNV.
MEAN_MANIFEST_FILE_SHA256 = '8431ab7091134cb79d2fc9b740b45f856de13c47b04401213de1e0bbb9d8f549'  # Actual emitted file identity, not a native asset identity.
NATIVE_MANIFEST = json.loads(NATIVE_MANIFEST_JSON)  # Full native prepared-provider metadata, including negative header integers.
MEAN_MANIFEST = json.loads(MEAN_MANIFEST_JSON)  # Mean-only source_kind none: intentionally has no source field.
MAX_U64 = (1 << 64) - 1  # Empty recording's completion sentinel.
ROOT = Path(__file__).resolve().parents[2] / 'fixtures_pose_planes'  # New-only final independent corpus destination.
DELETE = object()  # Author-only field omission marker; never serialized.

# Exact e1-fast geometry is independently authored, rather than reconstructed from a recording.
PLANE_3X2 = {'id': 0, 'plane_id': 'native-YZ-cut', 'source_id': 0,
    'lower_corner_world_m': [10.0, -4.0, 5.0], 'e1_world': [0.0, 1.0, 0.0],
    'e2_world': [0.0, 0.0, 1.0], 'du_m': 2.0, 'dv_m': 3.0, 'nu': 3, 'nv': 2,
    'sample_order': 'u_fastest'}
POINTS_3X2 = [[10.0, -4.0, 5.0], [10.0, -2.0, 5.0], [10.0, 0.0, 5.0],
             [10.0, -4.0, 8.0], [10.0, -2.0, 8.0], [10.0, 0.0, 8.0]]
# A second proper plane changes both normal and world placement; it has its own independent mask.
SECOND_PLANE_3X2 = {'id': 1, 'plane_id': 'native-minusY-cut', 'source_id': 0,
    'lower_corner_world_m': [-7.5, 11.0, 20.0], 'e1_world': [1.0, 0.0, 0.0],
    'e2_world': [0.0, 0.0, 1.0], 'du_m': 1.25, 'dv_m': 2.5, 'nu': 3, 'nv': 2,
    'sample_order': 'u_fastest'}
POINTS_SECOND_3X2 = [[-7.5, 11.0, 20.0], [-6.25, 11.0, 20.0], [-5.0, 11.0, 20.0],
                    [-7.5, 11.0, 22.5], [-6.25, 11.0, 22.5], [-5.0, 11.0, 22.5]]
MEAN_PLANE_17X17 = {'id': 0, 'plane_id': 'mean-default-counts', 'source_id': 0,
    'lower_corner_world_m': [10.0, -20.0, 12.0], 'e1_world': [0.0, 1.0, 0.0],
    'e2_world': [0.0, 0.0, 1.0], 'du_m': 2.0, 'dv_m': 1.0, 'nu': 17, 'nv': 17,
    'sample_order': 'u_fastest',
    'request_provenance': {'centre_world_m': [10.0, -4.0, 20.0], 'width_m': 32.0, 'height_m': 16.0,
        'counts_omitted': [True, True], 'requested_counts': [17, 17], 'realized_counts': [17, 17],
        'coarsened': False, 'note': 'Synthetic nominal wire/planner example; no provider evaluation claim'}}

def digest(raw):
    """Return SHA-256 of borrowed bytes without altering their representation."""
    # Bind draft source and protected evidence identities without creating output artifacts.
    return hashlib.sha256(raw).hexdigest()


def crc(raw):
    """Return unsigned IEEE CRC32 for borrowed bytes using only the standard library."""
    # Keep the draft framing primitive independent of every production implementation.
    return zlib.crc32(raw) & 0xffffffff


def repair_header(raw, offset):
    """Copy raw header bytes and repair their own four-byte CRC field at offset."""
    # Clear only the selected field, calculate the fixed-header checksum, then replace that field.
    result = bytearray(raw)  # Owned temporary header; borrowed input remains unchanged.
    struct.pack_into('<I', result, offset, 0)
    struct.pack_into('<I', result, offset, crc(result))
    return bytes(result)


def sample(values, valid=True):
    """Return one owned velocity oracle from borrowed world xyz m/s values and exact availability."""
    # Preserve literal component signs and copy the caller's triple into independent storage.
    return {'valid': valid, 'world_velocity_mps': list(values)}


def asymmetric_rows():
    """Return the exact six-point 0x2d mask example, including valid zero and canonical unavailable rows."""
    # Author the e1-fast row order explicitly; no geometry/provider/recorded values are sampled.
    return [sample((1.0, 2.0, 3.0)), sample((0.0, 0.0, 0.0), False),
            sample((-4.0, 5.0, -6.0)), sample((0.0, 0.0, 0.0)),
            sample((0.0, 0.0, 0.0), False), sample((7.0, -8.0, 9.0))]


def second_plane_rows():
    """Return a distinct six-point population with its own 0x32 mask and signed valid values."""
    # Use a different valid-point pattern and asymmetry to expose merged-mask and plane-order defects.
    return [sample((0.0, 0.0, 0.0), False), sample((-11.25, 12.5, -13.75)),
            sample((0.0, 0.0, 0.0), False), sample((0.0, 0.0, 0.0), False),
            sample((-0.0, 0.0, -0.0)), sample((17.125, -18.25, 19.5))]


def catalog_for(manifest, planes, *, enabled=('fluid_poses', 'applied_loads', 'elastic_moments', 'winch_geometry')):
    """Return an owned independent catalog using a full borrowed emitted manifest and plane rows.

    The base and old applied run identities are changed to the manifest's selected
    run so the copied provider object itself stays unchanged. enabled selects old
    population descriptors. This constructs positive literal inputs, not a production validator.
    """
    # Copy the old independent catalog, align only surrounding run identities, and add immutable plane geometry.
    catalog = copy.deepcopy(MOMENT_CATALOG)  # Owned synthetic recording catalog; frozen old literal objects remain untouched.
    identity = manifest['identity']  # Actual emitted selected inflow/run identity, kept verbatim inside the copied manifest.
    catalog['run_id'] = identity['run_id']
    catalog['recording_id'] = 's00-independent-inflow-planes-layout1'
    catalog['fixture_metadata'] = {'scope': 'Synthetic wire fixture, not numerical provider or Core qualification',
        'provider_manifest_role': 'Full emitted metadata retained; velocities below are independently authored literals',
        'empty_note': '', 'unicode_note': '速度 — independent world xyz', 'signed_example': -.375}
    for family in ('fluid_poses', 'applied_loads', 'elastic_moments', 'winch_geometry'):  # Existing independent optional families.
        if family not in enabled:
            del catalog[family]
    if 'applied_loads' in catalog:
        for coordinator in catalog['applied_loads']['coordinators']:  # Scope identity surrounding the copied provider object.
            coordinator['scope']['run_id'] = identity['run_id']
        for model in catalog['applied_loads']['models']:  # Actual catalog's run identity for these synthetic old-population rows.
            model['run_id'] = identity['run_id']
    catalog['inflow_planes'] = {'kind': 8, 'stream_id': 6, 'layout_version': 1, 'encoding': 'raw_f64_le',
        'source_preparation_generation': 2, 'quantity': 'absolute_unperturbed_world_velocity', 'units': 'm/s',
        'sources': [{'id': 0, 'inflow_id': identity['inflow_id'], 'run_id': identity['run_id'],
                     'initialization_manifest': copy.deepcopy(manifest)}], 'planes': copy.deepcopy(planes)}
    return catalog


def frames_for(plane_samples, *, enabled=('fluid_poses', 'applied_loads', 'elastic_moments', 'winch_geometry')):
    """Return owned three-row source groups with borrowed literal plane populations copied into each.

    plane_samples is an ordered list of complete per-plane sample lists. Existing
    physical source identities are independent literals; no provider is constructed.
    """
    # Reuse old independently authored source rows, remove disabled families, and attach complete stamped planes.
    rows = moment_frames()  # Fresh P8/F/load/moment/winch dictionaries; old corpus files are never read or modified.
    for ordinal, row in enumerate(rows):  # Dense recording row and its actual transient source phase.
        for family in ('fluid_poses', 'applied_loads', 'elastic_moments', 'winch_geometry'):  # Optional old source populations.
            if family not in enabled:
                for key in (('source_generation', 'source_phase', 'features') if family == 'winch_geometry' else (family,)):
                    del row[key]
        row['inflow_planes'] = {'generation': 2, 'phase': 2 if ordinal == 0 else 3,
            'planes': [{'plane_id': index, 'samples': copy.deepcopy(values)}
                       for index, values in enumerate(plane_samples)]}
    return rows


def stamp(row):
    """Encode a borrowed literal row's exact 64-byte common structural stamp with P8."""
    # Preserve recorder ordinal, accepted identity/time and physical sample count across all populations.
    value = row['stamp']  # Independent source-state facts, not regenerated solver counters.
    return struct.pack('<QIIdQdQQQ', value['ordinal'], value['analysis'], value['events'], value['time'],
                       value['state_id'], value['load_factor'], 8, 0, 0)


def record_bytes(row, kind):
    """Return owned literal raw bytes for a known source population kind1/4/5/6/7/8.

    All offsets are authored from frozen wire layouts. No production writer/checker
    or older fixture encoder is used. Plane masks are independent, with no padding.
    """
    # Encode the common identity, then the selected dense literal payload.
    # Retain per-plane masks and canonical unavailable slots without display transforms or normalization.
    common = stamp(row)  # Exact common64 bytes used by all enabled source populations.
    if kind == 1:
        return common + b''.join(struct.pack('<7d', *value) for value in row['poses'])
    if kind == 4:
        raw = common + struct.pack('<QII', row['source_generation'], row['source_phase'], 0)  # Winch source prefix.
        return raw + b''.join(struct.pack('<II12d', item['geometry_status'], 0,
            *(scalar for point in item['world_xyz_m'] for scalar in point)) for item in row['features'])
    family = {5: 'fluid_poses', 6: 'applied_loads', 7: 'elastic_moments', 8: 'inflow_planes'}[kind]  # Frozen known-family selector.
    population = row[family]  # Borrowed generation/phase and complete literal family rows.
    raw = common + struct.pack('<QII', population['generation'], population['phase'], 0)  # Exactly one80-byte prefix per population frame.
    if kind in (5, 7):
        values = population['samples' if kind == 5 else 'moments']  # F-pose or signed current-N cut order.
        mask = sum(int(value['valid']) << index for index, value in enumerate(values))  # LSB-first validity for dense row IDs.
        raw += mask.to_bytes((len(values) + 7) // 8, 'little')
        return raw + b''.join(struct.pack('<7d' if kind == 5 else '<3d',
            *value['world_pose' if kind == 5 else 'moment_N_Nm']) for value in values)
    if kind == 8:
        for plane in population['planes']:  # Plane blocks follow dense catalog order, each with its own mask.
            values = plane['samples']  # Original-point world velocities, independent of any drawing offset/warp.
            mask = sum(int(value['valid']) << index for index, value in enumerate(values))  # e1-fast point index within this plane.
            raw += mask.to_bytes((len(values) + 7) // 8, 'little')
            raw += b''.join(struct.pack('<3d', *value['world_velocity_mps']) for value in values)
        return raw
    assert kind == 6
    for attachment in population['attachments']:  # Prior complete unequal applied populations in their own catalog order.
        value = attachment['interval']  # Literal actual source/end identity and unchanged opaque tokens.
        raw += struct.pack('<IIQdQd7Q', value['status'], 0, value['source_accepted_id'], value['source_time_s'],
            value['end_accepted_id'], value['end_time_s'], value['publication'], value['mechanical_revision'],
            value['state_revision'], value['geometry_revision'], value['environment_revision'],
            value['prepared_step_sequence'], value['packet'])
        values = attachment['samples']  # Complete attachment-local physical source rows.
        mask = sum(int(value['valid']) << index for index, value in enumerate(values))  # Applied validity remains independent of plane masks.
        raw += mask.to_bytes((len(values) + 7) // 8, 'little')
        raw += b''.join(struct.pack('<6d', *value['source_position_world_m'],
                                  *value['force_density_world_N_per_m']) for value in values)
    return raw


def chunk(kind, stream, sequence, records, first, payload, *, flags=0, previous=0, encoding=0):
    """Return independently framed header/payload bytes from explicit borrowed wire facts.

    Optional flags, structural previous and raw encoding default to their canonical
    values; negative callers can deliberately alter one fact in memory.
    """
    # Bind exact stored/decoded payload length and separate payload/header CRC fields.
    header = struct.pack('<4sHHIIIIQQQQQIIQ', b'PCHK', kind, flags, 80, encoding, stream, 1,
        sequence, records, first, len(payload), len(payload), crc(payload), 0, previous)  # Fixed80-byte chunk envelope.
    return repair_header(header, 68) + payload


def make_file(catalog, rows, *, order=None, gap_after=None, outcomes=(1, 1), early_index=False, allow_nan=False):
    """Return independently grouped bytes and exact chunk offsets from literal inputs.

    catalog/rows are borrowed literal inputs. order can override known group order;
    gap_after inserts opaque100 after one selected population. outcomes supplies
    run/capture completion facts. early_index ends the second pending group; allow_nan authors deliberately invalid JSON.
    """
    # Frame a catalog and bounded two-row groups, optionally insert an opaque gap, then emit structural-only trailers.
    document = json.dumps(catalog, separators=(',', ':'), ensure_ascii=False, allow_nan=allow_nan).encode()  # Immutable catalog bytes.
    bootstrap = struct.pack('<8sHHIQQQQIIQ', b'SRNPOSE\0', 1, 0, 64, 0, len(document), 8, 65536, crc(document), 0, 0)  # Mean289 rows require >4096 chunk bytes.
    raw = bytearray(repair_header(bootstrap, 52) + document)  # In-memory owned draft, with no filesystem destination.
    families = ((5, 'fluid_poses'), (6, 'applied_loads'), (7, 'elastic_moments'), (8, 'inflow_planes'), (4, 'winch_geometry'))
    selected = order if order is not None else [kind for kind, name in families if name in catalog] + [1]  # Frozen order before adjacent winch/pose.
    streams = {1: 1, 4: 2, 5: 3, 6: 4, 7: 5, 8: 6}  # Exact known kind/stream identities.
    offsets = {kind: [] for kind in streams}  # Per-kind header positions for future isolated mutations.
    offsets[100] = []
    sequence = batches = committed = first_pose = previous = 0  # Chunk order, structural counts and linked offsets.
    for first in range(0, len(rows), 2):  # First recording ordinal of each complete group batch.
        batch = rows[first:first + 2]  # Borrowed complete source rows shared by all enabled chunks.
        for kind in selected:  # Known population, with explicit override only for later negative authoring.
            offset = len(raw)  # Header byte position, retained for independent framing checks.
            offsets[kind].append(offset)
            raw += chunk(kind, streams[kind], sequence, len(batch), first,
                b''.join(record_bytes(row, kind) for row in batch), flags=1 if kind == 1 else 0,
                previous=previous if kind == 1 else 0)
            sequence += 1
            if kind == 1:
                first_pose = first_pose or offset
                previous = offset
                batches += 1
                committed += len(batch)
            if early_index and first == 2 and kind == 8:
                break
            if kind == gap_after:
                offsets[100].append(len(raw))
                raw += chunk(100, 42, sequence, 1, first, b'opaque-plane-draft-gap', encoding=17)
                sequence += 1
        if early_index and first == 2:
            break
    index = len(raw)  # Structural seek-index location; optional chunks never enter these counts or links.
    raw += chunk(2, 0, sequence, 1, 0, struct.pack('<4Q', batches, committed, first_pose, previous), flags=1)
    completion = len(raw)  # Completion location recorded in the final fixed footer.
    raw += chunk(3, 0, sequence + 1, 1, 0, struct.pack('<4I6Q', *outcomes, 1, 0, committed,
        committed, batches, committed - 1 if committed else MAX_U64, index, 0), flags=1)
    raw += repair_header(struct.pack('<8sQQII', b'POSEEND\0', index, completion, 0, 0), 24)
    offsets.update(index=index, completion=completion)
    return bytes(raw), offsets


def positive_cases():
    """Return owned positive catalogs/rows plus independent geometry/offset descriptions.

    These known-source literal inputs include all common wire phases without
    claiming Core supports any route beyond its separately qualified capture scope.
    """
    # Assemble literal one/two-plane and 8/9-point layouts, mean289 geometry, family subsets and wire phase subsets.
    drafts = {}  # Owned positive cases keyed by descriptive name, prior to binary framing and manifest registration.
    one_rows = [asymmetric_rows()]  # Exact six-point 0x2d population.
    catalog = catalog_for(NATIVE_MANIFEST, [PLANE_3X2])  # Full actual native manifest, unchanged inside the draft source row.
    rows = frames_for(one_rows)  # Three independent coherent accepted source groups.
    drafts['mixed_native_3x2'] = {'catalog': catalog, 'frames': rows, 'plane_frame_bytes': 225,
        'mask_offsets': [80], 'first_double_offsets': [81], 'masks': [[0x2d]], 'world_points': [POINTS_3X2]}
    drafts['two_planes'] = {'catalog': catalog_for(NATIVE_MANIFEST, [PLANE_3X2, SECOND_PLANE_3X2]),
        'frames': frames_for([asymmetric_rows(), second_plane_rows()]), 'plane_frame_bytes': 370,
        'mask_offsets': [80, 225], 'first_double_offsets': [81, 226], 'masks': [[0x2d], [0x32]],
        'world_points': [POINTS_3X2, POINTS_SECOND_3X2]}
    for count, nu, nv in ((8, 2, 4), (9, 3, 3)):  # Legal rectangular grids straddling the packed-mask byte boundary.
        plane = copy.deepcopy(PLANE_3X2)  # Independent realized geometry, preserving e1-fast ordering.
        plane.update(plane_id=f'boundary-{count}', nu=nu, nv=nv)
        values = [sample((0.0, 0.0, 0.0), False) for _ in range(count)]  # Only the final original point is available.
        values[-1] = sample((-21.25, 22.5, -23.75))
        drafts[f'points_{count}'] = {'catalog': catalog_for(NATIVE_MANIFEST, [plane]),
            'frames': frames_for([values]), 'plane_frame_bytes': 80 + (count + 7) // 8 + 24 * count,
            'mask_offsets': [80], 'first_double_offsets': [80 + (count + 7) // 8],
            'masks': [[0x80] if count == 8 else [0x00, 0x01]]}
    mean_speed = MEAN_MANIFEST['mean']['mean_speed_mps']  # Actual copied emitted mean is12m/s; no fake native provenance is introduced.
    mean_values = [sample((mean_speed, 0.0, 0.0)) for _ in range(289)]  # Literal constant world+X rows, not provider samples.
    drafts['mean_17x17'] = {'catalog': catalog_for(MEAN_MANIFEST, [MEAN_PLANE_17X17]),
        'frames': frames_for([mean_values]), 'plane_frame_bytes': 7053,
        'mask_offsets': [80], 'first_double_offsets': [117], 'masks': [[0xff] * 36 + [0x01]],
        'centre_index': 144, 'centre_world_m': [10.0, -4.0, 20.0], 'point_count': 289}
    for name, enabled in (
            ('without_fluid', ('applied_loads', 'elastic_moments', 'winch_geometry')),
            ('without_loads', ('fluid_poses', 'elastic_moments', 'winch_geometry')),
            ('without_moments', ('fluid_poses', 'applied_loads', 'winch_geometry')),
            ('without_winches', ('fluid_poses', 'applied_loads', 'elastic_moments')),
            ('planes_only', ())):  # Independent feature omission preserves the required plane population.
        drafts[name] = {'catalog': catalog_for(NATIVE_MANIFEST, [PLANE_3X2], enabled=enabled),
                        'frames': frames_for(one_rows, enabled=enabled), 'plane_frame_bytes': 225}
    for name, analysis, phase, time, identity, factor, events in (
            ('prepared', 0, 0, 100.0, 0, 0.0, 1), ('static', 2, 1, 2.0, 9, .75, 8)):
        phase_rows = copy.deepcopy(rows[:1])  # Wire-valid phase subset, without claiming Core's initial HHT route supports it.
        phase_rows[0]['stamp'].update(analysis=analysis, events=events, time=time, state_id=identity, load_factor=factor)
        phase_rows[0]['source_phase'] = phase
        for family in ('fluid_poses', 'applied_loads', 'elastic_moments', 'inflow_planes'):  # All present copied source phases agree.
            phase_rows[0][family]['phase'] = phase
        drafts[name] = {'catalog': copy.deepcopy(catalog), 'frames': phase_rows, 'plane_frame_bytes': 225}
    first_positive = copy.deepcopy(rows[1:])  # Initial capture omitted without altering accepted source identity/time.
    for ordinal, row in enumerate(first_positive):  # Recording ordinals alone restart at zero.
        row['stamp']['ordinal'] = ordinal
    drafts['first_positive'] = {'catalog': copy.deepcopy(catalog), 'frames': first_positive, 'plane_frame_bytes': 225}
    unavailable = [sample((0.0, 0.0, 0.0), False) for _ in range(6)]  # Whole-plane legitimate unavailability, not unsupported-provider fallback.
    drafts['all_unavailable'] = {'catalog': copy.deepcopy(catalog), 'frames': frames_for([unavailable]), 'plane_frame_bytes': 225}
    signed_zero = asymmetric_rows()  # Authoritative validity distinguishes a true signed-zero vector from unavailable +0.
    signed_zero[3] = sample((-0.0, 0.0, -0.0))
    drafts['valid_signed_zero'] = {'catalog': copy.deepcopy(catalog), 'frames': frames_for([signed_zero]), 'plane_frame_bytes': 225}
    extras = copy.deepcopy(NATIVE_MANIFEST)  # Descriptive extra values cannot replace original normative provider fields.
    extras['fixture_descriptive_metadata'] = {'empty': '', 'unicode': '風速 — blå', 'negative_header_example': -123,
                                             'finite_signed_scalar': -.125, 'scope': 'Synthetic fixture metadata only'}
    drafts['extra_metadata'] = {'catalog': catalog_for(extras, [PLANE_3X2]), 'frames': copy.deepcopy(rows), 'plane_frame_bytes': 225}
    return drafts


def protected_identity():
    """Return stable hashes for all earlier independent authors/corpora without modifying them."""
    # Bind every old F/load/moment artifact before optional in-memory draft checks.
    root = Path(__file__).resolve().parents[2]  # Staged source tree; only the new plane author belongs to this task.
    paths = [root / 'tools/pose_sidecar' / name for name in
             ('author_fluid_fixtures.py', 'author_applied_fixtures.py', 'author_moment_fixtures.py')]  # Frozen author definitions.
    for directory in ('fixtures_pose_fluid', 'fixtures_pose_applied', 'fixtures_pose_moments'):  # All existing case bytes and receipts.
        paths.extend(path for path in sorted((root / directory).rglob('*')) if path.is_file())
    entries = {path.relative_to(root).as_posix(): digest(path.read_bytes()) for path in paths}  # Exact protected per-file identities.
    return digest(json.dumps(entries, sort_keys=True, separators=(',', ':')).encode()), entries


CATALOG = catalog_for(NATIVE_MANIFEST, [PLANE_3X2, SECOND_PLANE_3X2])  # Main two-plane12-point catalog; all five optional families enabled.
PLANE_FRAME_BYTES = 370  # Common80 + two independently masked six-vector blocks145 each.


def frames():
    """Return main owned P8/F/load/moment/two-plane/winch literal source groups."""
    # Keep distinct independent per-plane masks/values while retaining all earlier source facts.
    return frames_for([asymmetric_rows(), second_plane_rows()])


def edited(catalog, path, value):
    """Return a catalog copy with one tuple-addressed field replaced, or omitted for DELETE."""
    # Traverse only the owned copy; leave every earlier literal catalog and caller container untouched.
    result = copy.deepcopy(catalog)  # Independent isolated catalog mutation.
    owner = result  # Current mapping/list container along the addressed field path.
    for key in path[:-1]:  # Dict key or dense list index leading to the edited field.
        owner = owner[key]
    if value is DELETE:
        del owner[path[-1]]
    else:
        owner[path[-1]] = copy.deepcopy(value)
    return result


def payload_change(raw, offset, relative, fmt, value):
    """Return copied bytes with one payload scalar changed and both affected CRCs repaired."""
    # Isolate one payload contract defect while preserving all header identities and unrelated bytes.
    data = bytearray(raw)  # Owned file copy.
    struct.pack_into(fmt, data, offset + 80 + relative, value)
    size = struct.unpack_from('<Q', data, offset + 48)[0]  # Existing full payload byte count.
    struct.pack_into('<I', data, offset + 64, crc(data[offset + 80:offset + 80 + size]))
    data[offset:offset + 80] = repair_header(data[offset:offset + 80], 68)
    return bytes(data)


def header_change(raw, offset, relative, fmt, value):
    """Return copied bytes with one selected chunk-header fact changed and its CRC repaired."""
    # Keep payload bytes unchanged so known-family header admission is tested independently.
    data = bytearray(raw)  # Owned mutation buffer.
    struct.pack_into(fmt, data, offset + relative, value)
    data[offset:offset + 80] = repair_header(data[offset:offset + 80], 68)
    return bytes(data)


def build_corpus():
    """Return new case bytes, independent full/subset expectations and literal value arrays.

    No checker or producer is called. Main's approved nested predicate is applied
    to authored case intent; geometry/value oracles never derive from a recording.
    """
    # Register positive geometry/provider examples, then isolated typed catalog and frame defects.
    # Keep complete-pending semantic failures separate from recoverable torn counterparts.
    reference = frames()  # Main two-plane12-point three-row literal source sequence.
    files, cases = {}, {}  # New binary case population and independently declared reader classifications.
    references = {'transient_frames': reference}  # Full literal source dictionaries keyed by manifest frames_key.

    def add(name, raw, *, valid=True, baseline=True, winch=True, fluid=True, applied=True, moment=True,
            complete=True, count=3, frames_key='transient_frames', reason='', run=1, capture=1):
        """Register raw case bytes and explicit independent full/five-subset expectations.

        name must be new; count/complete describe complete understood structural
        groups. frames_key selects literal values; reason names the isolated defect.
        """
        # Preserve separate capability outcomes; no observed reader result determines the oracle.
        assert name not in files, name
        files[name] = raw
        cases[name] = {'valid': valid, 'fully_aware_valid': valid, 'baseline_valid': baseline,
            'winch_aware_valid': winch, 'f_aware_valid': fluid, 'applied_aware_valid': applied, 'moment_aware_valid': moment,
            'complete': complete, 'frames': count, 'frames_key': frames_key, 'run_outcome': run, 'capture_outcome': capture}
        if reason:
            cases[name]['rejection_reason'] = reason
        if not complete:
            cases[name]['allow_incomplete'] = True

    good, offsets = make_file(CATALOG, reference)  # Main independent complete file and exact header offsets.
    add('mixed_transient.spose', good)
    positives = positive_cases()  # Previously checked literal single/two/8/9/mean289 layouts and phase/source subsets.
    positive_bytes = {}  # Retain exact geometry-variant bytes/offsets for isolated mask cases.
    for name, case in positives.items():  # Independent positive catalog, source rows and literal geometry metadata.
        references[name + '_frames'] = case['frames']
        data, locations = make_file(case['catalog'], case['frames'])  # Whole valid file without production framing helpers.
        positive_bytes[name] = (data, locations)
        add(name + '.spose', data, count=len(case['frames']), frames_key=name + '_frames')
    for kind in (5, 6, 7, 8):  # Opaque chunks may appear between overlays and immediately before the final winch/pose suffix.
        add(f'unknown_after_{kind}.spose', make_file(CATALOG, reference, gap_after=kind)[0])
    add('empty_failed.spose', make_file(CATALOG, [], outcomes=(2, 3))[0], count=0, run=2, capture=3)

    # Main permits descriptive future source kinds without implying a runtime provider capability.
    future = copy.deepcopy(MEAN_MANIFEST)  # Full known-shape metadata but no fabricated native asset fields.
    future['runtime_profile']['source_kind'] = 'future_descriptive_provider'
    future_catalog = catalog_for(future, [PLANE_3X2, SECOND_PLANE_3X2])  # Source and selected-run joins remain exact.
    add('future_source_kind_descriptive.spose', make_file(future_catalog, reference)[0])
    future['source'] = {'future_note': '', 'signed_header_example': -17, 'description': '未知 source metadata'}
    add('future_source_metadata_preserved.spose', make_file(catalog_for(future, [PLANE_3X2, SECOND_PLANE_3X2]), reference)[0])
    for name, count in (('native_zero_source_bytes', 0), ('native_max_source_bytes', (1 << 63) - 1)):
        catalog = edited(CATALOG, ('inflow_planes', 'sources', 0, 'initialization_manifest', 'source', 'source_bytes'), count)
        add(name + '.spose', make_file(catalog, reference)[0])  # Wire provenance admission, not a claim that actual asset size changed.
    nonexistent = edited(CATALOG, ('inflow_planes', 'sources', 0, 'initialization_manifest', 'source', 'path'),
                         'Z:/independent-fixture-must-never-open/missing-source.wnd')  # Readers must not resolve even nonexistent assets.
    add('source_asset_never_reopened.spose', make_file(nonexistent, reference)[0])
    supplemental = copy.deepcopy(CATALOG)  # A stronger identity labels its exact separate byte subject without replacing FNV.
    supplemental['inflow_planes']['sources'][0]['initialization_manifest']['supplemental_digest'] = {
        'algorithm': 'sha256', 'value': NATIVE_MANIFEST_FILE_SHA256,
        'subject': 'original_emitted_initialization_manifest_file_utf8_bytes_including_original_line_ending',
        'not_source_asset_digest': True}
    add('supplemental_exact_subject_digest.spose', make_file(supplemental, reference)[0])
    semantic_json = copy.deepcopy(CATALOG)  # Lexical whitespace/key order is not the immutable provider object's identity.
    original_manifest = semantic_json['inflow_planes']['sources'][0]['initialization_manifest']
    semantic_json['inflow_planes']['sources'][0]['initialization_manifest'] = dict(reversed(list(original_manifest.items())))
    add('semantic_manifest_preservation.spose', make_file(semantic_json, reference)[0])
    tiny_spacing = copy.deepcopy(CATALOG)  # Small finite realized grid extents remain representable and need no snap/normalization.
    tiny_spacing['inflow_planes']['planes'][0]['du_m'] = .125
    add('realized_spacing_literal.spose', make_file(tiny_spacing, reference)[0])
    basis_inside = copy.deepcopy(CATALOG)  # Representation bound is checked without rewriting the recorded basis.
    basis_inside['inflow_planes']['planes'][0]['e1_world'] = [0.0, 1.0 + 1e-13, 0.0]
    add('basis_inside_representation_tolerance.spose', make_file(basis_inside, reference)[0])

    # Missing/duplicate/out-of-order known batches are errors only for readers implementing those families.
    for name, order, winch, fluid, applied, moment in (
            ('missing_planes', [5, 6, 7, 4, 1], True, True, True, True),
            ('duplicate_planes', [5, 6, 7, 8, 8, 4, 1], True, True, True, True),
            ('planes_before_fluid', [8, 5, 6, 7, 4, 1], True, True, True, True),
            ('planes_before_loads', [5, 8, 6, 7, 4, 1], True, True, True, True),
            ('planes_before_moments', [5, 6, 8, 7, 4, 1], True, True, True, True),
            ('planes_between_winch_pose', [5, 6, 7, 4, 8, 1], False, False, False, False),
            ('missing_fluid', [6, 7, 8, 4, 1], True, False, False, False),
            ('missing_loads', [5, 7, 8, 4, 1], True, True, False, False),
            ('missing_moments', [5, 6, 8, 4, 1], True, True, True, False),
            ('missing_winches', [5, 6, 7, 8, 1], False, False, False, False)):
        add(name + '.spose', make_file(CATALOG, reference, order=order)[0], valid=False,
            winch=winch, fluid=fluid, applied=applied, moment=moment, reason='known group order/cardinality')
    add('unknown_between_winch_pose.spose', make_file(CATALOG, reference, gap_after=4)[0], valid=False,
        winch=False, fluid=False, applied=False, moment=False, reason='forbidden winch/pose gap')
    add('early_index.spose', make_file(CATALOG, reference, early_index=True)[0], valid=False,
        fluid=False, applied=False, moment=False, count=2, reason='index while understood group is pending')
    for name, field, fmt, value, legacy in (
            ('wrong_kind', 4, '<H', 100, True), ('wrong_stream', 16, '<I', 42, True),
            ('wrong_layout', 20, '<I', 2, True), ('wrong_encoding', 12, '<I', 17, True),
            ('required_planes', 6, '<H', 1, False), ('reserved_flag', 6, '<H', 2, False),
            ('count_mismatch', 32, '<Q', 1, True), ('zero_count', 32, '<Q', 0, True),
            ('count_overflow', 32, '<Q', 1 << 63, False), ('first_ordinal', 40, '<Q', 1, True),
            ('previous_nonzero', 72, '<Q', 1, True), ('decoded_size', 56, '<Q', 739, True),
            ('chunk_decoded_ceiling', 56, '<Q', 64 * 1024 * 1024 + 1, False)):
        add(name + '.spose', header_change(good, offsets[8][0], field, fmt, value), valid=False,
            baseline=legacy, winch=legacy, fluid=legacy, applied=legacy, moment=legacy, reason='plane chunk header contract')
    for name, field, value, winch, fluid, applied, moment in (
            ('plane_kind_winch_stream', 16, 2, False, False, False, False),
            ('plane_kind_fluid_stream', 16, 3, True, False, False, False),
            ('plane_kind_load_stream', 16, 4, True, True, False, False),
            ('plane_kind_moment_stream', 16, 5, True, True, True, False),
            ('fluid_kind_plane_stream', 4, 5, True, False, False, False),
            ('load_kind_plane_stream', 4, 6, True, True, False, False),
            ('moment_kind_plane_stream', 4, 7, True, True, True, False)):
        add(name + '.spose', header_change(good, offsets[8][0], field, '<I' if field == 16 else '<H', value),
            valid=False, winch=winch, fluid=fluid, applied=applied, moment=moment, reason='known kind OR stream collision')

    # Literal payload offsets discriminate both independent masks, signed valid zero and canonical invalid +0.
    for name, field, fmt, value in (
            ('wrong_generation', 64, '<Q', 3), ('zero_generation', 64, '<Q', 0),
            ('generation_overflow', 64, '<Q', 1 << 63), ('wrong_phase', 72, '<I', 1),
            ('unknown_phase', 72, '<I', 4), ('source_reserved', 76, '<I', 1),
            ('copied_time', 16, '<d', 1.2501), ('copied_identity', 24, '<Q', 1),
            ('copied_events', 12, '<I', 4), ('copied_factor', 32, '<d', -0.0),
            ('physical_count_is_not_point_count', 40, '<Q', 12), ('stamp_reserved', 48, '<Q', 1),
            ('record_ordinal', 0, '<Q', 1), ('nonfinite_ux', 81, '<d', math.nan),
            ('nonfinite_uy', 89, '<d', math.inf), ('nonfinite_uz', 97, '<d', -math.inf),
            ('first_mask_high_bits', 80, '<B', 0xad), ('second_mask_high_bits', 225, '<B', 0xb2),
            ('cleared_nonzero_velocity', 80, '<B', 0x2c), ('unavailable_negative_zero', 105, '<d', -0.0),
            ('unavailable_nonzero', 113, '<d', 1.0), ('second_unavailable_negative_zero', 226, '<d', -0.0),
            ('second_valid_nonfinite', 250, '<d', math.inf)):
        add(name + '.spose', payload_change(good, offsets[8][0], field, fmt, value), valid=False,
            reason='plane completed record/mask/value contract')
    data, locations = positive_bytes['points_9']  # Nine samples require two mask bytes and unaligned first scalar offset82.
    add('padding_mask_points9.spose', payload_change(data, locations[8][0], 81, '<B', 0x81), valid=False,
        frames_key='points_9_frames', reason='unused high bits in second mask byte')
    data, locations = positive_bytes['mean_17x17']  #289 samples require37 mask bytes, with only one bit used in the last byte.
    add('padding_mask_mean289.spose', payload_change(data, locations[8][0], 116, '<B', 0x03), valid=False,
        frames_key='mean_17x17_frames', reason='unused high bits in final mean mask byte')

    # Main's exact nested provider predicate is metadata admission, not runtime capability or asset verification.
    plane = ('inflow_planes',)  # Tuple path to the optional descriptor.
    source = plane + ('sources', 0)  # Dense source identity surrounding the copied provider manifest.
    manifest = source + ('initialization_manifest',)  # Semantically preserved provider object.
    geometry = plane + ('planes', 0)  # First realized world grid.
    edits = [
        ('missing_descriptor', plane, DELETE), ('catalog_kind', plane + ('kind',), 7),
        ('catalog_stream', plane + ('stream_id',), 5), ('catalog_layout', plane + ('layout_version',), 2),
        ('catalog_encoding', plane + ('encoding',), 'zfp'), ('catalog_generation', plane + ('source_preparation_generation',), 3),
        ('catalog_generation_float', plane + ('source_preparation_generation',), 2.0),
        ('catalog_generation_bool', plane + ('source_preparation_generation',), True),
        ('catalog_generation_zero', plane + ('source_preparation_generation',), 0),
        ('catalog_quantity', plane + ('quantity',), 'relative_velocity'), ('catalog_units', plane + ('units',), 'km/h'),
        ('sources_empty', plane + ('sources',), []), ('planes_empty', plane + ('planes',), []),
        ('source_id_not_dense', source + ('id',), 1), ('source_id_bool', source + ('id',), False),
        ('source_inflow_empty', source + ('inflow_id',), ''), ('source_inflow_integer', source + ('inflow_id',), 7),
        ('source_run_mismatch', source + ('run_id',), 'another-run'), ('source_run_bool', source + ('run_id',), True),
        ('manifest_not_object', source + ('initialization_manifest',), []),
        ('manifest_format', manifest + ('format',), 'serene-inflow.initialization.v2'),
        ('manifest_missing_format', manifest + ('format',), DELETE),
        ('manifest_identity_absent', manifest + ('identity',), DELETE), ('manifest_identity_not_object', manifest + ('identity',), []),
        ('manifest_inflow_mismatch', manifest + ('identity', 'inflow_id'), 'other-wind'),
        ('manifest_inflow_bool', manifest + ('identity', 'inflow_id'), True),
        ('manifest_run_mismatch', manifest + ('identity', 'run_id'), 'other-run'),
        ('manifest_run_null', manifest + ('identity', 'run_id'), None),
        ('manifest_run_empty', manifest + ('identity', 'run_id'), ''),
        ('manifest_validation_absent', manifest + ('validation',), DELETE),
        ('manifest_validation_array', manifest + ('validation',), []),
        ('manifest_prepared_false', manifest + ('validation', 'standalone_provider_prepared'), False),
        ('manifest_prepared_integer', manifest + ('validation', 'standalone_provider_prepared'), 1),
        ('manifest_prepared_string', manifest + ('validation', 'standalone_provider_prepared'), 'true'),
        ('manifest_prepared_absent', manifest + ('validation', 'standalone_provider_prepared'), DELETE),
        ('manifest_core_binding_rewritten', manifest + ('validation', 'core_binding'), 'performed'),
        ('manifest_core_binding_absent', manifest + ('validation', 'core_binding'), DELETE),
        ('manifest_runtime_absent', manifest + ('runtime_profile',), DELETE),
        ('manifest_runtime_not_object', manifest + ('runtime_profile',), []),
        ('manifest_kind_empty', manifest + ('runtime_profile', 'source_kind'), ''),
        ('manifest_kind_bool', manifest + ('runtime_profile', 'source_kind'), True),
        ('manifest_kind_absent', manifest + ('runtime_profile', 'source_kind'), DELETE),
        ('manifest_source_absent', manifest + ('source',), DELETE),
        ('manifest_source_not_object', manifest + ('source',), []),
        ('manifest_native_path_absent', manifest + ('source', 'path'), DELETE),
        ('manifest_native_path_empty', manifest + ('source', 'path'), ''),
        ('manifest_native_path_boolean', manifest + ('source', 'path'), True),
        ('manifest_native_bytes_absent', manifest + ('source', 'source_bytes'), DELETE),
        ('manifest_native_bytes_negative', manifest + ('source', 'source_bytes'), -1),
        ('manifest_native_bytes_float', manifest + ('source', 'source_bytes'), 452.0),
        ('manifest_native_bytes_boolean', manifest + ('source', 'source_bytes'), False),
        ('manifest_native_bytes_overflow', manifest + ('source', 'source_bytes'), 1 << 63),
        ('manifest_fingerprint_absent', manifest + ('source', 'fingerprint'), DELETE),
        ('manifest_fingerprint_not_object', manifest + ('source', 'fingerprint'), []),
        ('manifest_fingerprint_algorithm', manifest + ('source', 'fingerprint', 'algorithm'), 'sha256'),
        ('manifest_fingerprint_encoding', manifest + ('source', 'fingerprint', 'encoding'), 'hex'),
        ('manifest_fingerprint_value_upper', manifest + ('source', 'fingerprint', 'value'), '519271A994B0F88D'),
        ('manifest_fingerprint_value_short', manifest + ('source', 'fingerprint', 'value'), '519271a994b0f88'),
        ('manifest_fingerprint_value_long', manifest + ('source', 'fingerprint', 'value'), '0519271a994b0f88d0'),
        ('manifest_fingerprint_value_nonhex', manifest + ('source', 'fingerprint', 'value'), '519271a994b0f88z'),
        ('manifest_fingerprint_value_integer', manifest + ('source', 'fingerprint', 'value'), 519271),
        ('manifest_fingerprint_scope', manifest + ('source', 'fingerprint', 'scope'), 'manifest_bytes'),
        ('manifest_fingerprint_crypto_true', manifest + ('source', 'fingerprint', 'cryptographic'), True),
        ('manifest_fingerprint_crypto_integer', manifest + ('source', 'fingerprint', 'cryptographic'), 0),
        ('manifest_fingerprint_crypto_string', manifest + ('source', 'fingerprint', 'cryptographic'), 'false'),
        ('manifest_transverse_changed', manifest + ('runtime_profile', 'transverse'), 'plane_no_clamp'),
        ('manifest_transverse_absent', manifest + ('runtime_profile', 'transverse'), DELETE),
        ('manifest_descriptive_nul', manifest + ('document_path',), 'path\0suffix'),
        ('plane_id_not_dense', geometry + ('id',), 1), ('plane_id_float', geometry + ('id',), 0.0),
        ('plane_request_empty', geometry + ('plane_id',), ''), ('plane_request_nul', geometry + ('plane_id',), 'plane\0other'),
        ('plane_source_unknown', geometry + ('source_id',), 1), ('plane_source_float', geometry + ('source_id',), 0.0),
        ('plane_lower_extent', geometry + ('lower_corner_world_m',), [10, -4]),
        ('plane_lower_boolean', geometry + ('lower_corner_world_m',), [True, -4, 5]),
        ('plane_basis_extent', geometry + ('e1_world',), [0, 1]), ('plane_basis_zero', geometry + ('e1_world',), [0, 0, 0]),
        ('plane_basis_scaled', geometry + ('e1_world',), [0, 2, 0]),
        ('plane_basis_outside_tolerance', geometry + ('e1_world',), [0, 1 + 1e-9, 0]),
        ('plane_basis_parallel', geometry + ('e2_world',), [0, 1, 0]),
        ('plane_basis_dot_outside', geometry + ('e2_world',), [0, 1e-9, 1]),
        ('plane_du_zero', geometry + ('du_m',), 0), ('plane_dv_negative', geometry + ('dv_m',), -3),
        ('plane_du_bool', geometry + ('du_m',), True), ('plane_nu_one', geometry + ('nu',), 1),
        ('plane_nv_one', geometry + ('nv',), 1), ('plane_nu_float', geometry + ('nu',), 3.0),
        ('plane_nv_bool', geometry + ('nv',), True), ('plane_nu_negative', geometry + ('nu',), -3),
        ('plane_nu_int_overflow', geometry + ('nu',), 1 << 63),
        ('plane_product_overflow', geometry + ('nu',), (1 << 63) - 1),
        ('plane_aggregate_point_cap', geometry + ('nu',), 500000),
        ('plane_width_nonfinite', geometry + ('du_m',), 1e308),
        ('plane_point_nonfinite', geometry + ('lower_corner_world_m',), [10, 1.7976931348623157e308, 5]),
        ('plane_order_wrong', geometry + ('sample_order',), 'v_fastest'),
        ('plane_duplicate_request_id', plane + ('planes', 1, 'plane_id'), 'native-YZ-cut')]
    for name, path, value in edits:  # One isolated exact-type, provenance or geometry defect per catalog.
        catalog = edited(CATALOG, path, value)  # Owned negative metadata, never written back into old sources.
        if name == 'plane_point_nonfinite':
            catalog['inflow_planes']['planes'][0]['du_m'] = 1e308  # Extent is finite at nu2 below; corner addition alone overflows.
            catalog['inflow_planes']['planes'][0]['nu'] = 2
        add(name + '.spose', make_file(catalog, reference, order=[5, 6, 7, 8, 4, 1])[0],
            valid=False, reason='plane catalog contract')
    for field in ('quantity', 'units', 'sources', 'planes'):  # Required top-level descriptor fields, independently omitted.
        add('missing_catalog_' + field + '.spose', make_file(edited(CATALOG, plane + (field,), DELETE), reference, order=[5, 6, 7, 8, 4, 1])[0],
            valid=False, reason='plane catalog contract')
    for field in ('algorithm', 'encoding', 'value', 'scope', 'cryptographic'):  # Native fingerprint is a complete typed labelled record.
        add('missing_fingerprint_' + field + '.spose', make_file(edited(CATALOG,
            manifest + ('source', 'fingerprint', field), DELETE), reference)[0], valid=False, reason='plane catalog contract')
    for field in ('lower_corner_world_m', 'e1_world', 'e2_world', 'du_m', 'dv_m', 'nu', 'nv', 'sample_order'):
        add('missing_plane_' + field + '.spose', make_file(edited(CATALOG, geometry + (field,), DELETE), reference)[0],
            valid=False, reason='plane catalog contract')
    orphan = copy.deepcopy(CATALOG)  # An unreferenced source remains invalid even if its full metadata is internally coherent.
    orphan['inflow_planes']['sources'].append(dict(copy.deepcopy(orphan['inflow_planes']['sources'][0]), id=1))
    add('orphan_source.spose', make_file(orphan, reference)[0], valid=False, reason='plane catalog contract')
    mean_catalog = catalog_for(MEAN_MANIFEST, [PLANE_3X2, SECOND_PLANE_3X2])  # Mean manifest must omit source altogether.
    for name, value in (('mean_source_null', None), ('mean_source_empty', {}), ('mean_source_native', NATIVE_MANIFEST['source'])):
        add(name + '.spose', make_file(edited(mean_catalog, manifest + ('source',), value), reference)[0],
            valid=False, reason='plane catalog contract')
    seven = copy.deepcopy(CATALOG)  # A seven-point rectangle cannot satisfy both required axis minima.
    seven['inflow_planes']['planes'][0].update(nu=7, nv=1)
    add('seven_point_rectangle.spose', make_file(seven, reference)[0], valid=False, reason='plane catalog contract')
    for name, path in (('nonfinite_plane_spacing', geometry + ('du_m',)),
                       ('nonfinite_manifest_extra', manifest + ('fixture_extra_number',))):
        add(name + '.spose', make_file(edited(CATALOG, path, math.nan), reference, allow_nan=True)[0], valid=False,
            baseline=False, winch=False, fluid=False, applied=False, moment=False, reason='nonfinite JSON metadata')

    # Complete defects remain errors before a later torn counterpart; otherwise only earlier whole groups survive.
    damaged = bytearray(good)  # Corrupt payload without repairing CRC so every subset observes framing failure.
    damaged[offsets[8][0] + 80 + 81] ^= 1
    add('corrupt_plane_crc.spose', bytes(damaged), valid=False, baseline=False, winch=False, fluid=False, applied=False, moment=False, reason='payload CRC')
    for name, at in (('truncated_plane_header', offsets[8][1] + 39),
                     ('truncated_plane_payload', offsets[8][1] + 80 + 91),
                     ('pending_planes', offsets[4][1]), ('truncated_winches', offsets[4][1] + 80 + 19),
                     ('pending_winches', offsets[1][1]), ('truncated_pose', offsets[1][1] + 80 + 31)):
        add(name + '.spose.partial', good[:at], complete=False, count=2)
    pending = good[:offsets[4][1]]  # Complete F/load/moment/plane prefix without a winch/pose counterpart.
    for name, field, fmt, value in (
            ('pending_nonfinite_velocity', 81, '<d', math.inf), ('pending_wrong_phase', 72, '<I', 1),
            ('pending_wrong_ordinal', 0, '<Q', 3), ('pending_wrong_generation', 64, '<Q', 3),
            ('pending_bad_mask', 225, '<B', 0xb2), ('pending_copied_time', 16, '<d', 1.3201)):
        add(name + '.spose.partial', payload_change(pending, offsets[8][1], field, fmt, value), valid=False,
            complete=False, count=2, reason='complete independently malformed pending plane record')
    unjoined = pending  # Missing pose alone cannot contradict a prefix whose present populations all agree.
    for kind in (5, 6, 7, 8):  # Alter all present source timestamps together, including their completed applied endpoint facts.
        unjoined = payload_change(unjoined, offsets[kind][1], 16, '<d', 1.3201)
    for block in (80, 417):  # Both attachment prefixes retain exact common-endpoint agreement.
        unjoined = payload_change(unjoined, offsets[6][1], block + 32, '<d', 1.3201)
    add('unjoined_pending_time.spose.partial', unjoined, complete=False, count=2)
    return files, cases, references, positives


def write_corpus(root):
    """Create one new corpus at root and return its exact manifest/count/byte summary.

    Refuse existing destinations. Hash all old evidence before/after authoring,
    and retain any partial attempt rather than deleting or repairing old artifacts.
    """
    # Build independent bytes without readers, create the new destination, then bind exact case and oracle identities.
    if root.exists():
        raise SystemExit('fixture destination must not exist: ' + str(root))
    before, protected = protected_identity()  # Frozen old author/corpus identities, never regenerated.
    files, cases, references, positives = build_corpus()  # Independent binary cases, classifications and exact source values.
    root.mkdir(parents=True)
    for name, raw in files.items():  # One new fixture and its already-authored bytes.
        (root / name).write_bytes(raw)
        cases[name].update(bytes=len(raw), sha256=digest(raw))
    geometry = {name: {key: value for key, value in case.items() if key not in ('catalog', 'frames')}
                for name, case in positives.items()}  # Independent geometry/mask/offset facts without repeated complete catalogs.
    expected = {'status': 'Independent inflow-plane layout1 literal oracle; execution evidence is separate',
        'scope': 'Synthetic world velocity/metadata wire oracles, not provider numerics, Core capture, numerical rollback or renderer qualification',
        'physical_sample_count': 8, 'fluid_sample_count': 6, 'winch_arrangement_count': 2,
        'applied_attachment_count': 2, 'attachment_station_counts': [5, 3], 'moment_cut_count': 7,
        'inflow_source_count': 1, 'plane_count': 2, 'plane_point_counts': [6, 6], 'plane_point_count': 12,
        'plane_frame_bytes': 370, 'group_frame_bytes': 2494, 'known_group_order': [5, 6, 7, 8, 4, 1],
        'plane_mask_offsets': [80, 225], 'plane_first_double_offsets': [81, 226],
        'plane_masks': [[0x2d], [0x32]], 'world_points': [POINTS_3X2, POINTS_SECOND_3X2],
        'normals_world': [[1, 0, 0], [0, -1, 0]], 'positive_geometry_oracles': geometry,
        'provider_manifest_admission': {'native_source_bytes': 'exact integer in [0,INT64_MAX]',
            'none_source_field': 'absent', 'future_source_kind': 'nonempty descriptive metadata, not runtime capability',
            'preservation': 'semantic JSON object, not whitespace/text identity',
            'asset_access': 'never reopen or rehash source assets'},
        'embedded_original_manifest_files': {'native_sha256': NATIVE_MANIFEST_FILE_SHA256, 'mean_sha256': MEAN_MANIFEST_FILE_SHA256},
        'protected_old_tree_sha256': before, 'protected_old_files': protected, **references, 'cases': cases}
    (root / 'catalog.json').write_bytes((json.dumps(CATALOG, indent=2, ensure_ascii=False) + '\n').encode())
    (root / 'expected.json').write_bytes((json.dumps(expected, indent=2, ensure_ascii=False, allow_nan=False) + '\n').encode())
    assert protected_identity()[0] == before, 'old fixture or author evidence changed during plane authoring'
    return {'root': str(root), 'cases': len(files), 'binary_bytes': sum(map(len, files.values())),
            'expected_sha256': digest((root / 'expected.json').read_bytes()), 'protected_old_tree_sha256': before}


def classify_pinned(root, pinned):
    """Classify existing authored files using five explicit immutable older Python readers.

    root supplies literal expected outcomes; pinned supplies named historical
    reader files. Save all observations/mismatches before raising. This never
    derives or claims fully aware plane semantics from opaque older readers.
    """
    # Load exact historical modules, compare observed subset results to prior independent expectations,
    # and preserve the complete evidence receipt without changing fixture bytes or manifest outcomes.
    expected = json.loads((root / 'expected.json').read_text(encoding='utf-8'))  # Already-authored fully independent expectations.
    before = protected_identity()[0]  # Immutable previous family evidence baseline.
    evidence = {'scope': 'Pinned older capability subsets; inflow-plane semantics remain opaque', 'readers': {}}  # Execution evidence only.
    mismatches = []  # Differences retained for human review; never silently used to change oracle facts.
    for filename, field in (('base_checker.py', 'baseline_valid'), ('winch_checker.py', 'winch_aware_valid'),
                            ('F_checker.py', 'f_aware_valid'), ('applied_checker.py', 'applied_aware_valid'),
                            ('moment_checker.py', 'moment_aware_valid')):
        path = pinned / filename  # Explicit immutable checker path, never a live production import.
        module_name = '_pinned_plane_subset_' + filename.replace('.', '_')  # Unique dataclass-safe import identity.
        spec = importlib.util.spec_from_file_location(module_name, path)  # File-only historical reader load.
        module = importlib.util.module_from_spec(spec)  # Older implementation used only by this separate classification action.
        sys.modules[module_name] = module
        spec.loader.exec_module(module)
        result = {'sha256': digest(path.read_bytes()), 'cases': {}}  # Historical reader byte identity and per-case observed outcomes.
        for name, case in expected['cases'].items():  # Named binary case and its fixed independent classification.
            try:
                observed = module.check(root / name, allow_incomplete=case.get('allow_incomplete', False))  # No retained frame history needed.
                entry = {'valid': True, 'complete': observed['complete'], 'frames': observed['validated_frames']}
                for key in ('implemented_families', 'understood_families', 'ignored_families'):  # Keep honest subset capability reports.
                    if key in observed:
                        entry[key] = observed[key]
            except Exception as error:  # Preserve exact rejection type/message, including unexpected implementation errors.
                entry = {'valid': False, 'error_type': type(error).__name__, 'error': str(error)}
            result['cases'][name] = entry
            if entry['valid'] != case[field] or (entry['valid'] and
                    (entry['complete'] != case['complete'] or entry['frames'] != case['frames'])):
                mismatches.append({'reader': filename, 'case': name, 'expected_valid': case[field],
                    'expected_complete': case['complete'], 'expected_frames': case['frames'], 'observed': entry})
        evidence['readers'][filename] = result
    evidence['mismatches'] = mismatches
    evidence['protected_old_tree_unchanged'] = protected_identity()[0] == before
    destination = root / 'legacy-classifications.json'  # New attempt receipt, never overwrite an earlier success or failure.
    if destination.exists():
        raise SystemExit('classification receipt already exists: ' + str(destination))
    destination.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8', newline='\n')
    assert evidence['protected_old_tree_unchanged'], 'old evidence changed during pinned plane classification'
    if mismatches:
        raise AssertionError(json.dumps(mismatches, indent=2))
    return {'cases_per_reader': len(expected['cases']), 'readers': 5,
            'classifications': 5 * len(expected['cases']), 'receipt_sha256': digest(destination.read_bytes())}


def main():
    """Author NEW_DIRECTORY or classify an existing corpus with --classify CORPUS PINNED.

    No argument selects the new default plane corpus. Wrong arguments or existing
    output destinations stop before replacement. Print only the exact summary.
    """
    # Keep independent authoring separate from optional old-reader execution evidence.
    arguments = sys.argv[1:]  # Explicit CLI action and caller-selected filesystem paths.
    if len(arguments) == 3 and arguments[0] == '--classify':
        result = classify_pinned(Path(arguments[1]).resolve(), Path(arguments[2]).resolve())  # Older capabilities only.
    elif len(arguments) <= 1:
        result = write_corpus(Path(arguments[0]).resolve() if arguments else ROOT)  # New independently authored final corpus.
    else:
        raise SystemExit('usage: author_plane_fixtures.py [NEW_DIRECTORY] | --classify CORPUS PINNED')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
