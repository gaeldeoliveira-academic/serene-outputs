"""Author independent applied-load wire fixtures and exact literal row oracles.

Only the prior independent physical/F catalog and literal frame function are
imported. No production recorder/checker, solver or current load writer is used.
The destination must be new; existing F/J07 corpora and their authors are read-only.
Malicious stale/candidate payloads test wire rejection, not numerical rollback.
"""
from pathlib import Path
import copy
import hashlib
import json
import math
import struct
import sys
import zlib

from author_fluid_fixtures import CATALOG as FLUID_CATALOG, frames as fluid_frames

ROOT = Path(__file__).resolve().parents[2] / 'fixtures_pose_applied'
U64_MAX = (1 << 64) - 1
LOAD_FRAME_BYTES = 658  # Common80 plus K5 block337 and K3 block241.
BLOCK_STARTS = (80, 417)  # Second96-byte prefix is intentionally unaligned.
CATALOG = copy.deepcopy(FLUID_CATALOG)
CATALOG['recording_id'] = 's00-independent-applied-layout1'
CATALOG['applied_loads'] = {
    'kind': 6, 'stream_id': 4, 'layout_version': 1, 'encoding': 'raw_f64_le',
    'source_preparation_generation': 2, 'product': 'completed_weak_force_density',
    'force_unit': 'N/m', 'position_unit': 'm', 'vector_frame': 'world',
    'coordinators': [{'id': 0, 'scope': {'kind': 'run', 'run_id': CATALOG['run_id']},
                      'coupling': 'fixed_step_weak_held'}],
    'models': [
        {'id': 0, 'coordinator_id': 0, 'instance': 701, 'model_id': 'physical-model-alpha',
         'run_id': CATALOG['run_id'], 'model_type': 'independent_density_alpha',
         'model_version': 'literal-1', 'layout_revision': 11, 'logical_node_count': 5},
        {'id': 1, 'coordinator_id': 0, 'instance': 19, 'model_id': 'physical-model-beta',
         'run_id': CATALOG['run_id'], 'model_type': 'independent_density_beta',
         'model_version': 'literal-3', 'layout_revision': 7, 'logical_node_count': 3}],
    'attachments': [
        {'id': 0, 'model_catalog_id': 0, 'model_attachment_index': 0, 'attachment_id': 'spanline',
         'target_occurrence_id': 21, 'spanline_id': 'F', 'source_geometry_fingerprint': 'source-alpha-17',
         'effective_geometry_fingerprint': 'effective-alpha-23', 'model_nodes_fingerprint': 'nodes-alpha-41',
         'approximation_policy': 'preserve_native_boundaries', 'native_element_count': 2,
         'model_node_begin': 0, 'station_count': 5, 'station_begin': 0},
        {'id': 1, 'model_catalog_id': 1, 'model_attachment_index': 0, 'attachment_id': 'spanline',
         'target_occurrence_id': 31, 'spanline_id': 'F', 'source_geometry_fingerprint': 'source-beta-19',
         'effective_geometry_fingerprint': 'effective-beta-31', 'model_nodes_fingerprint': 'nodes-beta-43',
         'approximation_policy': 'preserve_native_boundaries', 'native_element_count': 1,
         'model_node_begin': 0, 'station_count': 3, 'station_begin': 5}],
    'stations': [
        {'id': 0, 'attachment_catalog_id': 0, 'logical_node_index': 0, 'material_coordinate_m': 0.0,
         'physical_side': 'continuous', 'physical_side_token': 101,
         'native_consumers': [{'consumer_index': 0, 'native_element': 0, 'limit_side': 'right'}]},
        {'id': 1, 'attachment_catalog_id': 0, 'logical_node_index': 1, 'material_coordinate_m': 1.0,
         'physical_side': 'continuous', 'physical_side_token': 102,
         'native_consumers': [{'consumer_index': 1, 'native_element': 0, 'limit_side': 'left'},
                              {'consumer_index': 2, 'native_element': 1, 'limit_side': 'right'}]},
        {'id': 2, 'attachment_catalog_id': 0, 'logical_node_index': 2, 'material_coordinate_m': 2.0,
         'physical_side': 'left', 'physical_side_token': 201,
         'native_consumers': [{'consumer_index': 3, 'native_element': 1, 'limit_side': 'left'}]},
        {'id': 3, 'attachment_catalog_id': 0, 'logical_node_index': 3, 'material_coordinate_m': 2.0,
         'physical_side': 'right', 'physical_side_token': 202,
         'native_consumers': [{'consumer_index': 4, 'native_element': 1, 'limit_side': 'right'}]},
        {'id': 4, 'attachment_catalog_id': 0, 'logical_node_index': 4, 'material_coordinate_m': 4.0,
         'physical_side': 'continuous', 'physical_side_token': 0,
         'native_consumers': [{'consumer_index': 5, 'native_element': 1, 'limit_side': 'left'}]},
        {'id': 5, 'attachment_catalog_id': 1, 'logical_node_index': 0, 'material_coordinate_m': 0.0,
         'physical_side': 'continuous', 'physical_side_token': 301,
         'native_consumers': [{'consumer_index': 0, 'native_element': 0, 'limit_side': 'right'}]},
        {'id': 6, 'attachment_catalog_id': 1, 'logical_node_index': 1, 'material_coordinate_m': 1.5,
         'physical_side': 'continuous', 'physical_side_token': 302,
         'native_consumers': [{'consumer_index': 1, 'native_element': 0, 'limit_side': 'right'}]},
        {'id': 7, 'attachment_catalog_id': 1, 'logical_node_index': 2, 'material_coordinate_m': 3.0,
         'physical_side': 'continuous', 'physical_side_token': 303,
         'native_consumers': [{'consumer_index': 2, 'native_element': 0, 'limit_side': 'left'}]}]}


def digest(raw):
    """Return an exact byte identity for a borrowed independently authored artifact."""
    return hashlib.sha256(raw).hexdigest()


def crc(raw):
    """Return unsigned reflected IEEE CRC32 using only the independent stdlib primitive."""
    return zlib.crc32(raw) & 0xffffffff


def repair_header(raw, offset):
    """Copy a fixed header, zero its own CRC field and replace that CRC only."""
    result = bytearray(raw)  # Owned mutation; caller storage remains untouched.
    struct.pack_into('<I', result, offset, 0)
    struct.pack_into('<I', result, offset, crc(result))
    return bytes(result)


def empty_interval():
    """Return all canonical zero source/endpoint/provenance facts for status0."""
    return {'status': 0, 'source_accepted_id': 0, 'source_time_s': 0.0,
            'end_accepted_id': 0, 'end_time_s': 0.0, 'publication': 0,
            'mechanical_revision': 0, 'state_revision': 0, 'geometry_revision': 0,
            'environment_revision': 0, 'prepared_step_sequence': 0, 'packet': 0}


def zero_sample(*, valid=False):
    """Return six positive-zero scalars with the explicitly requested validity state."""
    return {'valid': valid, 'source_position_world_m': [0.0, 0.0, 0.0],
            'force_density_world_N_per_m': [0.0, 0.0, 0.0]}


def frames():
    """Return independent P8/F6/W2 and asymmetric K5/K3 interval row literals.

    Opaque tokens deliberately decrease across accepted frames. Source points
    are distinct from the endpoint F poses and their source frames are unsaved.
    """
    result = fluid_frames()  # Prior independently authored physical/F values only.
    for index, row in enumerate(result):
        interval = empty_interval()
        if index == 1:
            interval.update(status=1, source_accepted_id=3, source_time_s=1.28,
                            end_accepted_id=4, end_time_s=1.29, publication=91,
                            mechanical_revision=83, state_revision=71, geometry_revision=61,
                            environment_revision=53, prepared_step_sequence=47, packet=39)
        if index == 2:
            interval.update(status=1, source_accepted_id=6, source_time_s=1.31,
                            end_accepted_id=7, end_time_s=1.32, publication=7,
                            mechanical_revision=5, state_revision=3, geometry_revision=2,
                            environment_revision=1, prepared_step_sequence=13, packet=11)
        delta = index * .25  # Exact dyadic change independent of the F endpoint translation.
        physical = [
            [[-20.5 + delta, 3.25, 1.75, 12.5, -2.25, 7.75],
             [-18.25 + delta, 5.5, 2.125, -8.75, 4.5, .625],
             [-16.5 + delta, 7.75, 3.375, 3.125, -6.5, 9.25],
             [-16.5 + delta, 7.75, 3.375, -11.25, 8.625, -4.75],
             [-12.75 + delta, 11.5, 6.25, 5.875, 13.25, -.375]],
            [[31.5, -7.25 + delta, 15.5, -14.75, 3.125, 2.5],
             [34.25, -4.5 + delta, 18.875, 9.5, -7.75, 6.125],
             [38.5, -.75 + delta, 23.25, 2.25, 12.875, -15.5]]]
        attachments = []  # Distinct complete logical populations in immutable catalog order.
        for values in physical:
            samples = [zero_sample() for _ in values] if index == 0 else [
                {'valid': True, 'source_position_world_m': value[:3],
                 'force_density_world_N_per_m': value[3:]} for value in values]
            attachments.append({'interval': copy.deepcopy(interval), 'samples': samples})
        row['applied_loads'] = {'generation': 2, 'phase': 2 if index == 0 else 3,
                                'attachments': attachments}
    return result


def stamp(row):
    """Encode the literal64-byte structural identity with unchanged physical P8."""
    value = row['stamp']
    return struct.pack('<QIIdQdQQQ', value['ordinal'], value['analysis'], value['events'],
                       value['time'], value['state_id'], value['load_factor'], 8, 0, 0)


def interval_bytes(value):
    """Encode one literal96-byte interval prefix with no derived identity relations."""
    return struct.pack('<IIQdQd7Q', value['status'], 0, value['source_accepted_id'],
        value['source_time_s'], value['end_accepted_id'], value['end_time_s'],
        value['publication'], value['mechanical_revision'], value['state_revision'],
        value['geometry_revision'], value['environment_revision'], value['prepared_step_sequence'], value['packet'])


def record_bytes(row, kind):
    """Encode one independent literal population using explicit reviewed wire offsets."""
    common = stamp(row)
    if kind == 1:
        return common + b''.join(struct.pack('<7d', *sample) for sample in row['poses'])
    if kind == 4:
        raw = common + struct.pack('<QII', row['source_generation'], row['source_phase'], 0)
        return raw + b''.join(struct.pack('<II12d', item['geometry_status'], 0,
            *(value for point in item['world_xyz_m'] for value in point)) for item in row['features'])
    feature = row['fluid_poses' if kind == 5 else 'applied_loads']
    raw = common + struct.pack('<QII', feature['generation'], feature['phase'], 0)
    if kind == 5:
        samples = feature['samples']
        mask = sum(int(sample['valid']) << index for index, sample in enumerate(samples))
        return raw + mask.to_bytes((len(samples) + 7) // 8, 'little') + b''.join(
            struct.pack('<7d', *sample['world_pose']) for sample in samples)
    assert kind == 6
    for attachment in feature['attachments']:
        samples = attachment['samples']
        mask = sum(int(sample['valid']) << index for index, sample in enumerate(samples))
        raw += interval_bytes(attachment['interval']) + mask.to_bytes((len(samples) + 7) // 8, 'little')
        raw += b''.join(struct.pack('<6d', *sample['source_position_world_m'],
                                   *sample['force_density_world_N_per_m']) for sample in samples)
    return raw


def chunk(kind, stream, sequence, records, first, payload, *, previous=0, flags=0, encoding=0):
    """Encode an independent80-byte header and bounded payload with separate CRCs."""
    header = struct.pack('<4sHHIIIIQQQQQIIQ', b'PCHK', kind, flags, 80, encoding, stream, 1,
                         sequence, records, first, len(payload), len(payload), crc(payload), 0, previous)
    return repair_header(header, 68) + payload


def make_file(reference, *, catalog=CATALOG, order=None, gap_after=None, early_index=False, outcomes=(1, 1)):
    """Return independently authored bytes and exact chunk offsets for isolated mutations.

    Fixed two-row groups preserve old generic P8 probes. Optional order/gap/index
    controls create framing negatives; their filenames and independent manifest
    outcomes distinguish wire validation from numerical publication evidence.
    """
    cat = json.dumps(catalog, separators=(',', ':'), ensure_ascii=True, allow_nan=False).encode('utf-8')
    header = struct.pack('<8sHHIQQQQIIQ', b'SRNPOSE\0', 1, 0, 64, 0, len(cat), 8, 4096, crc(cat), 0, 0)
    raw = bytearray(repair_header(header, 52) + cat)
    selected = order if order is not None else ([5] if 'fluid_poses' in catalog else []) + \
        ([6] if 'applied_loads' in catalog else []) + ([4] if 'winch_geometry' in catalog else []) + [1]
    offsets = {'fluid': [], 'loads': [], 'winches': [], 'poses': [], 'unknown': []}
    keys, streams = {5: 'fluid', 6: 'loads', 4: 'winches', 1: 'poses'}, {5: 3, 6: 4, 4: 2, 1: 1}
    sequence = first_pose = previous = batches = committed = 0
    for start in range(0, len(reference), 2):
        rows = reference[start:start + 2]
        for kind in selected:
            offset = len(raw)
            offsets[keys[kind]].append(offset)
            raw += chunk(kind, streams[kind], sequence, len(rows), start,
                         b''.join(record_bytes(row, kind) for row in rows),
                         previous=previous if kind == 1 else 0, flags=1 if kind == 1 else 0)
            sequence += 1
            if kind == 1:
                first_pose = first_pose or offset
                previous = offset
                batches += 1
                committed += len(rows)
            if early_index and start == 2 and kind == 6:
                break
            if gap_after == kind:
                offsets['unknown'].append(len(raw))
                raw += chunk(100, 42, sequence, 1, start, b'opaque-independent-gap', encoding=17)
                sequence += 1
        if early_index and start == 2:
            break
    index = len(raw)
    raw += chunk(2, 0, sequence, 1, 0, struct.pack('<4Q', batches, committed, first_pose, previous), flags=1)
    completion = len(raw)
    raw += chunk(3, 0, sequence + 1, 1, 0, struct.pack('<4I6Q', *outcomes, 1, 0, committed,
        committed, batches, committed - 1 if committed else U64_MAX, index, 0), flags=1)
    raw += repair_header(struct.pack('<8sQQII', b'POSEEND\0', index, completion, 0, 0), 24)
    offsets.update(index=index, completion=completion)
    return bytes(raw), offsets


def payload_change(raw, offset, relative, fmt, value):
    """Copy a file, mutate one payload scalar and repair only its two affected CRCs."""
    data = bytearray(raw)
    struct.pack_into(fmt, data, offset + 80 + relative, value)
    size = struct.unpack_from('<Q', data, offset + 48)[0]
    struct.pack_into('<I', data, offset + 64, crc(data[offset + 80:offset + 80 + size]))
    data[offset:offset + 80] = repair_header(data[offset:offset + 80], 68)
    return bytes(data)


def header_change(raw, offset, relative, fmt, value):
    """Copy a file, mutate one header scalar and repair its own checksum."""
    data = bytearray(raw)
    struct.pack_into(fmt, data, offset + relative, value)
    data[offset:offset + 80] = repair_header(data[offset:offset + 80], 68)
    return bytes(data)


def renumber(rows):
    """Copy independent source rows and change only the dense recorder-owned ordinal."""
    result = copy.deepcopy(rows)
    for ordinal, row in enumerate(result):
        row['stamp']['ordinal'] = ordinal
    return result


def build_corpus():
    """Return bounded case bytes, separate full/subset expectations and literal row arrays."""
    reference = frames()
    files, cases = {}, {}
    references = {'transient_frames': reference}

    def add(name, raw, *, valid=True, baseline=True, winch=True, fluid=True, complete=True,
            count=3, frames_key='transient_frames', reason='', run=1, capture=1):
        """Register an independent outcome without invoking a production or pinned reader."""
        assert name not in files
        files[name] = raw
        cases[name] = {'valid': valid, 'baseline_valid': baseline, 'winch_aware_valid': winch,
                       'f_aware_valid': fluid, 'complete': complete, 'frames': count,
                       'frames_key': frames_key, 'run_outcome': run, 'capture_outcome': capture}
        if reason:
            cases[name]['rejection_reason'] = reason
        if not complete:
            cases[name]['allow_incomplete'] = True

    good, offsets = make_file(reference)
    add('mixed_transient.spose', good)
    for name, remove in (('without_fluid', ('fluid_poses',)), ('without_winches', ('winch_geometry',)),
                         ('loads_only', ('fluid_poses', 'winch_geometry'))):
        catalog, rows = copy.deepcopy(CATALOG), copy.deepcopy(reference)
        for key in remove:
            del catalog[key]
            for row in rows:
                if key == 'winch_geometry':
                    for field in ('source_generation', 'source_phase', 'features'):
                        del row[field]
                else:
                    del row[key]
        references[name + '_frames'] = rows
        add(name + '.spose', make_file(rows, catalog=catalog)[0], frames_key=name + '_frames')
    for kind in (5, 6):
        add(f'unknown_after_{kind}.spose', make_file(reference, gap_after=kind)[0])
    first_positive = renumber(reference[1:])
    references['first_positive_frames'] = first_positive
    add('first_positive.spose', make_file(first_positive)[0], count=2, frames_key='first_positive_frames')
    joined = copy.deepcopy(reference)
    for index in (1, 2):
        previous = reference[index - 1]['stamp']
        for attachment in joined[index]['applied_loads']['attachments']:
            attachment['interval'].update(source_accepted_id=previous['state_id'], source_time_s=previous['time'])
    references['exact_prior_join_frames'] = joined
    add('exact_prior_join.spose', make_file(joined)[0], frames_key='exact_prior_join_frames')
    zero_join = copy.deepcopy(joined)
    zero_join[0]['stamp']['time'] = -0.0
    for attachment in zero_join[1]['applied_loads']['attachments']:
        attachment['interval']['source_time_s'] = -0.0
    references['zero_time_join_frames'] = zero_join
    add('zero_time_join.spose', make_file(zero_join)[0], frames_key='zero_time_join_frames')
    wrong_zero_join = copy.deepcopy(zero_join)
    for attachment in wrong_zero_join[1]['applied_loads']['attachments']:
        attachment['interval']['source_time_s'] = 0.0
    add('equal_source_id_zero_sign.spose', make_file(wrong_zero_join)[0], valid=False,
        frames_key='zero_time_join_frames', reason='equal source ID requires bitwise signed-zero time equality')
    zero_end = renumber(reference[1:2])
    zero_end[0]['stamp']['time'] = 0.0
    for attachment in zero_end[0]['applied_loads']['attachments']:
        attachment['interval'].update(source_time_s=-1.0, end_time_s=0.0)
    references['zero_end_frames'] = zero_end
    add('zero_end_time.spose', make_file(zero_end)[0], count=1, frames_key='zero_end_frames')
    wrong_zero_end = copy.deepcopy(zero_end)
    for attachment in wrong_zero_end[0]['applied_loads']['attachments']:
        attachment['interval']['end_time_s'] = -0.0
    add('end_time_zero_sign.spose', make_file(wrong_zero_end)[0], valid=False, count=1,
        frames_key='zero_end_frames', reason='end time must equal common structural time bitwise')
    for name, analysis, phase, time, state_id, factor, events in (
            ('prepared', 0, 0, 100.0, 0, 0.0, 1), ('static', 2, 1, 2.0, 9, .75, 8)):
        rows = copy.deepcopy(reference[:1])
        rows[0]['stamp'].update(analysis=analysis, events=events, time=time, state_id=state_id, load_factor=factor)
        rows[0]['source_phase'] = rows[0]['fluid_poses']['phase'] = rows[0]['applied_loads']['phase'] = phase
        references[name + '_frames'] = rows
        add(name + '.spose', make_file(rows)[0], count=1, frames_key=name + '_frames')
    prepared_positive = renumber([references['prepared_frames'][0], *reference[1:]])
    references['prepared_then_positive_frames'] = prepared_positive
    add('prepared_then_positive.spose', make_file(prepared_positive)[0], frames_key='prepared_then_positive_frames')
    for name in ('masked', 'all_unavailable', 'signed_zero'):
        rows = copy.deepcopy(reference)
        for row in rows[1:]:
            attachments = row['applied_loads']['attachments']
            if name == 'masked':
                attachments[0]['samples'][0]['force_density_world_N_per_m'] = [0.0, 0.0, 0.0]
                attachments[0]['samples'][1] = zero_sample()
                attachments[1]['samples'][2] = zero_sample()
            elif name == 'all_unavailable':
                for attachment in attachments:
                    attachment['samples'] = [zero_sample() for _ in attachment['samples']]
            else:
                attachments[0]['samples'][0] = {'valid': True, 'source_position_world_m': [-0.0, 0.0, -0.0],
                    'force_density_world_N_per_m': [-0.0, 0.0, -0.0]}
        references[name + '_frames'] = rows
        add(name + '.spose', make_file(rows)[0], frames_key=name + '_frames')
    add('empty_failed.spose', make_file([], outcomes=(2, 3))[0], count=0, run=2, capture=3)

    # A valid alternate manifest proves model-wide offsets are not replaced by global station IDs.
    multiple = copy.deepcopy(CATALOG)
    load = multiple['applied_loads']
    load['models'] = load['models'][:1]
    load['models'][0]['logical_node_count'] = 8
    load['attachments'][1].update(model_catalog_id=0, model_attachment_index=1, attachment_id='rear', model_node_begin=5)
    for item in load['stations'][5:]:
        item['logical_node_index'] += 5
    add('multiple_attachments_one_model.spose', make_file(reference, catalog=multiple)[0])
    permuted = copy.deepcopy(CATALOG)
    load = permuted['applied_loads']
    load['attachments'][0]['station_begin'] = 3
    load['attachments'][1]['station_begin'] = 0
    load['stations'] = load['stations'][5:] + load['stations'][:5]
    for index, station in enumerate(load['stations']):
        station['id'] = index
    add('permuted_station_ranges.spose', make_file(reference, catalog=permuted)[0])
    large_domains = copy.deepcopy(CATALOG)
    large_domains['applied_loads']['attachments'][0]['native_element_count'] = 1000001
    large_domains['applied_loads']['models'][0]['logical_node_count'] = 1000007
    add('large_metadata_domains.spose', make_file(reference, catalog=large_domains)[0])
    repeated = copy.deepcopy(CATALOG)
    repeated['applied_loads']['stations'][1]['native_consumers'].append(
        {'consumer_index': 6, 'native_element': 1, 'limit_side': 'right'})
    add('repeated_consumer_address.spose', make_file(reference, catalog=repeated)[0])

    # The second attachment's K9 mask has a second byte, while both prefix and rows stay unaligned.
    k9_catalog, k9_rows = copy.deepcopy(CATALOG), copy.deepcopy(reference)
    load = k9_catalog['applied_loads']
    load['models'][1]['logical_node_count'] = load['attachments'][1]['station_count'] = 9
    for index in range(3, 9):
        load['stations'].append({'id': 5 + index, 'attachment_catalog_id': 1, 'logical_node_index': index,
            'material_coordinate_m': (index - 2) * .125, 'physical_side': 'continuous',
            'physical_side_token': 400 + index,
            'native_consumers': [{'consumer_index': index, 'native_element': 0, 'limit_side': 'right'}]})
    for ordinal, row in enumerate(k9_rows):
        for index in range(3, 9):
            value = zero_sample() if ordinal == 0 else {'valid': True,
                'source_position_world_m': [40.0 + index, -3.5 + ordinal * .25, 17.0 + index * .5],
                'force_density_world_N_per_m': [index * -1.25, index * .875, index * 2.125]}
            row['applied_loads']['attachments'][1]['samples'].append(value)
    references['k9_frames'] = k9_rows
    k9, k9_offsets = make_file(k9_rows, catalog=k9_catalog)
    add('attachment_k9.spose', k9, frames_key='k9_frames')
    add('padding_mask_k9.spose', payload_change(k9, k9_offsets['loads'][0], 947 + 417 + 97, '<B', 0x81),
        valid=False, frames_key='k9_frames', reason='unused second mask byte high bit')

    # Isolated group violations retain independent outcomes for each older capability subset.
    for name, kwargs, old_winch, old_fluid in (
            ('missing_loads', {'order': [5, 4, 1]}, True, True),
            ('duplicate_loads', {'order': [5, 6, 6, 4, 1]}, True, True),
            ('loads_before_fluid', {'order': [6, 5, 4, 1]}, True, True),
            ('loads_between_winch_pose', {'order': [5, 4, 6, 1]}, False, False),
            ('unknown_between_winch_pose', {'gap_after': 4}, False, False),
            ('missing_winches', {'order': [5, 6, 1]}, False, False),
            ('missing_fluid', {'order': [6, 4, 1]}, True, False),
            ('early_index', {'early_index': True}, True, False)):
        add(name + '.spose', make_file(reference, **kwargs)[0], valid=False,
            winch=old_winch, fluid=old_fluid, count=2 if name == 'early_index' else 3, reason='group order/cardinality')
    for name, field, fmt, value, legacy in (
            ('wrong_kind', 4, '<H', 100, True), ('wrong_stream', 16, '<I', 42, True),
            ('wrong_layout', 20, '<I', 2, True), ('wrong_encoding', 12, '<I', 17, True),
            ('required_loads', 6, '<H', 1, False), ('reserved_flag', 6, '<H', 2, False),
            ('count_mismatch', 32, '<Q', 1, True), ('count_overflow', 32, '<Q', 1 << 63, False),
            ('first_ordinal', 40, '<Q', 1, True), ('previous_nonzero', 72, '<Q', 1, True)):
        add(name + '.spose', header_change(good, offsets['loads'][0], field, fmt, value), valid=False,
            baseline=legacy, winch=legacy, fluid=legacy, reason='applied chunk header contract')
    for name, field, fmt, value in (
            ('wrong_generation', 64, '<Q', 3), ('wrong_phase', 72, '<I', 1),
            ('source_reserved', 76, '<I', 1), ('copied_time', 16, '<d', 1.2501),
            ('physical_count', 40, '<Q', 7), ('initial_status_completed', 80, '<I', 1),
            ('unavailable_prefix_garbage', 80 + 40, '<Q', 7),
            ('unavailable_mask_garbage', 80 + 96, '<B', 1),
            ('unavailable_row_garbage', 80 + 97, '<d', 1.0),
            ('unavailable_negative_zero', 417 + 97, '<d', -0.0)):
        add(name + '.spose', payload_change(good, offsets['loads'][0], field, fmt, value), valid=False,
            reason='applied common/status0 contract')
    # Mutate the first positive record after one initial record in the same complete batch.
    for name, relative, fmt, value in (
            ('unknown_status', 0, '<I', 2), ('positive_status_unavailable', 0, '<I', 0),
            ('interval_reserved', 4, '<I', 1), ('source_after_end', 8, '<Q', 5),
            ('source_equals_end', 8, '<Q', 4), ('source_time_after_end', 16, '<d', 1.3),
            ('source_time_equals_end', 16, '<d', 1.29), ('end_id_mismatch', 24, '<Q', 5),
            ('end_time_mismatch', 32, '<d', 1.2901), ('end_time_nonfinite', 32, '<d', math.inf),
            ('zero_publication', 40, '<Q', 0), ('zero_mechanical', 48, '<Q', 0),
            ('zero_state', 56, '<Q', 0), ('zero_geometry', 64, '<Q', 0),
            ('zero_environment', 72, '<Q', 0), ('zero_sequence', 80, '<Q', 0),
            ('zero_packet', 88, '<Q', 0), ('identity_overflow', 88, '<Q', 1 << 63),
            ('padding_mask', 96, '<B', 0x9f), ('cleared_noncanonical', 96, '<B', 0x1e),
            ('nonfinite_source_position', 97, '<d', math.nan), ('nonfinite_density', 97 + 24, '<d', math.inf)):
        add(name + '.spose', payload_change(good, offsets['loads'][0], LOAD_FRAME_BYTES + 80 + relative, fmt, value),
            valid=False, reason='positive interval/mask/value contract')
    for name, relative, fmt, value in (
            ('attachment_source_id_mismatch', 8, '<Q', 2), ('attachment_source_time_mismatch', 16, '<d', 1.27),
            ('attachment_publication_mismatch', 40, '<Q', 92), ('attachment_revision_mismatch', 64, '<Q', 62),
            ('attachment_sequence_mismatch', 80, '<Q', 48), ('attachment_packet_mismatch', 88, '<Q', 40)):
        add(name + '.spose', payload_change(good, offsets['loads'][0], LOAD_FRAME_BYTES + 417 + relative, fmt, value),
            valid=False, reason='same-frame96-byte prefix identity')

    def interval_case(name, updates, *, reason):
        """Author the same isolated bad accepted-interval facts on both complete attachments."""
        rows = copy.deepcopy(reference)
        for attachment in rows[2]['applied_loads']['attachments']:
            attachment['interval'].update(updates)
        add(name + '.spose', make_file(rows)[0], valid=False, reason=reason)

    interval_case('source_older_than_previous', {'source_accepted_id': 3, 'source_time_s': 1.28}, reason='saved accepted source bound')
    interval_case('equal_source_id_wrong_time', {'source_accepted_id': 4, 'source_time_s': 1.2901}, reason='equal source ID requires exact time')
    interval_case('larger_source_id_equal_time', {'source_accepted_id': 5, 'source_time_s': 1.29}, reason='larger source ID requires later time')
    interval_case('larger_source_id_earlier_time', {'source_accepted_id': 5, 'source_time_s': 1.28}, reason='source time prior accepted bound')
    interval_case('stale_completed_payload', reference[1]['applied_loads']['attachments'][0]['interval'], reason='old completed endpoint cannot be relabeled')
    interval_case('failed_candidate_endpoint', {'end_accepted_id': 8, 'end_time_s': 1.33}, reason='unaccepted candidate endpoint differs from captured stamp')
    interval_case('rollback_attempt_status_zero', empty_interval(), reason='positive accepted capture cannot conceal missing completed packet')

    # Each catalog edit isolates ownership, identity, range or exact token interpretation.
    edits = [
        ('missing_descriptor', lambda c: c.pop('applied_loads')),
        ('catalog_kind', lambda c: c['applied_loads'].update(kind=5)),
        ('catalog_stream', lambda c: c['applied_loads'].update(stream_id=3)),
        ('catalog_layout', lambda c: c['applied_loads'].update(layout_version=2)),
        ('catalog_encoding', lambda c: c['applied_loads'].update(encoding='other')),
        ('catalog_generation', lambda c: c['applied_loads'].update(source_preparation_generation=3)),
        ('catalog_generation_float', lambda c: c['applied_loads'].update(source_preparation_generation=2.0)),
        ('catalog_product', lambda c: c['applied_loads'].update(product='endpoint_diagnostic')),
        ('catalog_force_unit', lambda c: c['applied_loads'].update(force_unit='N')),
        ('catalog_vector_frame', lambda c: c['applied_loads'].update(vector_frame='local')),
        ('coordinator_empty', lambda c: c['applied_loads'].update(coordinators=[])),
        ('coordinator_scope', lambda c: c['applied_loads']['coordinators'][0].update(scope={'kind': 'assembly', 'occurrence_id': 11})),
        ('coordinator_run', lambda c: c['applied_loads']['coordinators'][0]['scope'].update(run_id='other-run')),
        ('coordinator_coupling', lambda c: c['applied_loads']['coordinators'][0].update(coupling='strong')),
        ('model_bad_coordinator', lambda c: c['applied_loads']['models'][0].update(coordinator_id=99)),
        ('model_zero_instance', lambda c: c['applied_loads']['models'][0].update(instance=0)),
        ('model_duplicate_instance', lambda c: c['applied_loads']['models'][1].update(instance=701)),
        ('model_duplicate_id', lambda c: c['applied_loads']['models'][1].update(model_id='physical-model-alpha')),
        ('model_wrong_run', lambda c: c['applied_loads']['models'][0].update(run_id='other-run')),
        ('model_empty_type', lambda c: c['applied_loads']['models'][0].update(model_type='')),
        ('model_zero_layout', lambda c: c['applied_loads']['models'][0].update(layout_revision=0)),
        ('model_incomplete_population', lambda c: c['applied_loads']['models'][0].update(logical_node_count=4)),
        ('attachment_bad_model', lambda c: c['applied_loads']['attachments'][0].update(model_catalog_id=99)),
        ('attachment_boolean_reference', lambda c: c['applied_loads']['attachments'][0].update(model_catalog_id=False)),
        ('attachment_wrong_target', lambda c: c['applied_loads']['attachments'][0].update(target_occurrence_id=20)),
        ('attachment_unknown_target', lambda c: c['applied_loads']['attachments'][0].update(target_occurrence_id=999)),
        ('attachment_empty_spanline', lambda c: c['applied_loads']['attachments'][0].update(spanline_id='')),
        ('attachment_empty_fingerprint', lambda c: c['applied_loads']['attachments'][0].update(model_nodes_fingerprint='')),
        ('attachment_overlap', lambda c: c['applied_loads']['attachments'][1].update(station_begin=4)),
        ('attachment_gap', lambda c: c['applied_loads']['attachments'][1].update(station_begin=6)),
        ('attachment_zero_count', lambda c: c['applied_loads']['attachments'][0].update(station_count=0)),
        ('attachment_wrong_count', lambda c: c['applied_loads']['attachments'][0].update(station_count=4)),
        ('attachment_model_node_range', lambda c: c['applied_loads']['attachments'][0].update(model_node_begin=1)),
        ('attachment_zero_elements', lambda c: c['applied_loads']['attachments'][0].update(native_element_count=0)),
        ('station_wrong_owner', lambda c: c['applied_loads']['stations'][0].update(attachment_catalog_id=1)),
        ('station_float_id', lambda c: c['applied_loads']['stations'][0].update(id=0.0)),
        ('station_wrong_logical_index', lambda c: c['applied_loads']['stations'][1].update(logical_node_index=2)),
        ('station_negative_coordinate', lambda c: c['applied_loads']['stations'][1].update(material_coordinate_m=-1)),
        ('station_wrong_side', lambda c: c['applied_loads']['stations'][1].update(physical_side='none')),
        ('station_boolean_token', lambda c: c['applied_loads']['stations'][1].update(physical_side_token=True)),
        ('station_token_overflow', lambda c: c['applied_loads']['stations'][1].update(physical_side_token=1 << 63)),
        ('station_contradictory_side', lambda c: c['applied_loads']['stations'][3].update(physical_side_token=201)),
        ('station_duplicate_address', lambda c: c['applied_loads']['stations'][3].update(physical_side_token=201, physical_side='left')),
        ('consumer_empty', lambda c: c['applied_loads']['stations'][1].update(native_consumers=[])),
        ('consumer_duplicate_index', lambda c: c['applied_loads']['stations'][1]['native_consumers'][1].update(consumer_index=1)),
        ('consumer_index_gap', lambda c: c['applied_loads']['stations'][1]['native_consumers'][1].update(consumer_index=8)),
        ('consumer_bad_element', lambda c: c['applied_loads']['stations'][1]['native_consumers'][1].update(native_element=2)),
        ('consumer_unresolved_side', lambda c: c['applied_loads']['stations'][1]['native_consumers'][1].update(limit_side='none')),
        ('consumer_float_index', lambda c: c['applied_loads']['stations'][1]['native_consumers'][1].update(consumer_index=2.0))]
    for name, edit in edits:
        catalog = copy.deepcopy(CATALOG)
        edit(catalog)
        add(name + '.spose', make_file(reference, catalog=catalog, order=[5, 6, 4, 1])[0], valid=False,
            reason='applied catalog contract')
    for name, edit in (
            ('duplicate_attachment_index', lambda c: c['applied_loads']['attachments'][1].update(model_attachment_index=0)),
            ('duplicate_attachment_id', lambda c: c['applied_loads']['attachments'][1].update(attachment_id='spanline')),
            ('model_ranges_overlap', lambda c: c['applied_loads']['attachments'][1].update(model_node_begin=4))):
        catalog = copy.deepcopy(multiple)
        edit(catalog)
        add(name + '.spose', make_file(reference, catalog=catalog)[0], valid=False, reason='model attachment/range uniqueness')

    damaged = bytearray(good)
    damaged[offsets['loads'][0] + 80 + LOAD_FRAME_BYTES + 177] ^= 1
    add('corrupt_loads.spose', bytes(damaged), valid=False, baseline=False, winch=False, fluid=False, reason='payload CRC')
    for name, at in (
            ('truncated_loads', offsets['loads'][1] + 80 + 23),
            ('pending_loads', offsets['winches'][1]),
            ('truncated_winches', offsets['winches'][1] + 80 + 19),
            ('pending_winches', offsets['poses'][1]),
            ('truncated_pose', offsets['poses'][1] + 80 + 31)):
        add(name + '.spose.partial', good[:at], complete=False, count=2)
    pending = good[:offsets['winches'][1]]
    for name, field, fmt, value in (
            ('pending_nonfinite_density', 80 + 97 + 24, '<d', math.inf),
            ('pending_wrong_phase', 72, '<I', 1),
            ('pending_wrong_ordinal', 0, '<Q', 3),
            ('pending_stale_end', 80 + 24, '<Q', 4)):
        add(name + '.spose.partial', payload_change(pending, offsets['loads'][1], field, fmt, value),
            valid=False, complete=False, count=2, reason='complete independently malformed pending load record')
    # All present known partners agree; a missing pose alone supplies no contradictory timestamp.
    unjoined = payload_change(pending, offsets['fluid'][1], 16, '<d', 1.3201)
    unjoined = payload_change(unjoined, offsets['loads'][1], 16, '<d', 1.3201)
    for start in BLOCK_STARTS:
        unjoined = payload_change(unjoined, offsets['loads'][1], start + 32, '<d', 1.3201)
    add('unjoined_pending_time.spose.partial', unjoined, complete=False, count=2)
    return files, cases, references


def protected_identity():
    """Fingerprint all old F artifacts and its author without writing or regenerating them."""
    source = Path(__file__).resolve().parents[2]
    selected = [source / 'tools/pose_sidecar/author_fluid_fixtures.py']
    selected += sorted(path for path in (source / 'fixtures_pose_fluid').rglob('*') if path.is_file())
    entries = {str(path.relative_to(source)).replace('\\', '/'): digest(path.read_bytes()) for path in selected}
    return digest(json.dumps(entries, sort_keys=True, separators=(',', ':')).encode()), entries


def main():
    """Author one new corpus, assert old evidence unchanged, and bind exact sizes/hashes."""
    root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else ROOT
    if len(sys.argv) > 2 or root.exists():
        raise SystemExit('usage: author_applied_fixtures.py [NEW_DIRECTORY]; destination must not exist')
    before, protected = protected_identity()
    files, cases, references = build_corpus()
    root.mkdir(parents=True)
    for name, raw in files.items():
        (root / name).write_bytes(raw)
        cases[name].update(bytes=len(raw), sha256=digest(raw))
    expected = {'status': 'Independent applied-layout1 wire oracle; execution evidence is separate',
        'scope': 'Literal physical density/source-position oracles, not numerical model or rollback proof',
        'physical_sample_count': 8, 'fluid_sample_count': 6, 'winch_arrangement_count': 2,
        'applied_attachment_count': 2, 'attachment_station_counts': [5, 3], 'native_consumer_count': 9,
        'load_frame_bytes': LOAD_FRAME_BYTES, 'group_frame_bytes': 1875, 'attachment_block_offsets': list(BLOCK_STARTS),
        'attachment_first_sample_offsets': [177, 514], 'protected_F_tree_sha256': before,
        'protected_F_files': protected, **references, 'cases': cases}
    (root / 'catalog.json').write_bytes((json.dumps(CATALOG, indent=2) + '\n').encode('utf-8'))
    (root / 'expected.json').write_bytes((json.dumps(expected, indent=2, allow_nan=False) + '\n').encode('utf-8'))
    assert protected_identity()[0] == before, 'prior F evidence changed during authoring'
    print(json.dumps({'root': str(root), 'cases': len(files), 'binary_bytes': sum(map(len, files.values())),
                      'protected_F_tree_sha256': before, 'expected_sha256': digest((root / 'expected.json').read_bytes())}))


if __name__ == '__main__':
    main()
