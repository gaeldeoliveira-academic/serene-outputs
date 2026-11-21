"""Author independent Stage00 F-pose candidate bytes and literal reader expectations.

The optional destination must be new. Existing corpora are never rewritten.
Only J07's independently authored catalog/geometry literals are reused; this
module imports no production checker, recorder or numerical solver. Candidate
layout changes require a new explicit corpus, not an edit to accepted evidence.
"""
from pathlib import Path
import copy
import hashlib
import json
import math
import struct
import sys
import zlib

from author_winch_fixtures import CATALOG as WINCH_CATALOG, frames as winch_frames

# New curated corpus destination; this helper refuses replacement.
ROOT = Path(__file__).resolve().parents[2] / 'fixtures_pose_fluid'
U64_MAX = (1 << 64) - 1  # Empty-recording last-ordinal sentinel.
FLUID_COUNT = 6  # Literal two-line population, independent of physical P8.
FLUID_FRAME_BYTES = 80 + 1 + 56 * FLUID_COUNT  # Common stamp, one mask byte, six poses.
CANONICAL_UNAVAILABLE = [0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0]

# Owned independent catalog, preserving physical N/port topology without adding F samples there.
CATALOG = copy.deepcopy(WINCH_CATALOG)
CATALOG.update(recording_id='s00-independent-fluid-candidate-v1', run_id='two-fluid-lines',
               preparation_id='capture-plan-29')
CATALOG['fluid_poses'] = {
    'kind': 5, 'stream_id': 3, 'layout_version': 1, 'encoding': 'raw_f64_le',
    'source_preparation_generation': 2,
    'lines': [
        {'id': 0, 'occurrence_id': 21, 'spanline_id': 'F',
         'source_geometry_fingerprint': 'independent-source-left-17',
         'effective_geometry_fingerprint': 'independent-effective-left-23',
         'approximation_policy': 'preserve_native_boundaries', 'native_element_count': 2},
        {'id': 1, 'occurrence_id': 31, 'spanline_id': 'F',
         'source_geometry_fingerprint': 'independent-source-right-19',
         'effective_geometry_fingerprint': 'independent-effective-right-31',
         'approximation_policy': 'preserve_native_boundaries', 'native_element_count': 1}],
    'samples': [
        {'id': 0, 'line_id': 0, 'material_coordinate_m': 0.0, 'side': 'right', 'native_element': 0},
        {'id': 1, 'line_id': 0, 'material_coordinate_m': 2.0, 'side': 'left', 'native_element': 0},
        {'id': 2, 'line_id': 0, 'material_coordinate_m': 2.0, 'side': 'right', 'native_element': 1},
        {'id': 3, 'line_id': 0, 'material_coordinate_m': 5.0, 'side': 'left', 'native_element': 1},
        {'id': 4, 'line_id': 1, 'material_coordinate_m': 0.0, 'side': 'right', 'native_element': 0},
        {'id': 5, 'line_id': 1, 'material_coordinate_m': 3.0, 'side': 'left', 'native_element': 0}],
    'segments': [
        {'id': 0, 'line_id': 0, 'sample_ids': [0, 1]},
        {'id': 1, 'line_id': 0, 'sample_ids': [2, 3]},
        {'id': 2, 'line_id': 1, 'sample_ids': [4, 5]}]}


def digest(raw):
    """Return SHA-256 of borrowed raw bytes without transformation."""
    # Bind each artifact to its exact independently authored bytes.
    return hashlib.sha256(raw).hexdigest()


def crc(raw):
    """Return unsigned reflected IEEE CRC-32 of borrowed bytes."""
    # Use the stdlib polynomial implementation, independent of production code.
    return zlib.crc32(raw) & 0xffffffff


def repair(raw, offset):
    """Copy raw header, zero its CRC at offset and return the repaired bytes."""
    # Repair only the selected header's own checksum.
    result = bytearray(raw)  # Mutation owned by this helper.
    struct.pack_into('<I', result, offset, 0)
    struct.pack_into('<I', result, offset, crc(result))
    return bytes(result)


def frames(*, winches=True):
    """Return literal P8/K6 accepted states, optionally including J07 winch oracles."""
    # Reuse independently authored physical values; author F geometry separately.
    result = winch_frames()  # Fresh J07 literal dictionaries, never decoded files.
    for ordinal, row in enumerate(result):  # Actual captured row and its fixed phase.
        delta = ordinal * 0.125  # Exact dyadic translation, unrelated to solver mechanics.
        positions = [
            [-2.5 + delta, 0.75, 1.125, 1, 0, 0, 0],
            [0.25 + delta, 2.75, 3.125, .5, .5, .5, .5],
            [0.25 + delta, 2.75, 3.125, .5, -.5, .5, -.5],
            [4.5 + delta, -1.75, 6.125, 0, 0, 1, 0],
            [10.25, -4.5 + delta, 7.75, 0, 1, 0, 0],
            [12.5, -2.25 + delta, 11.875, -.5, -.5, .5, .5]]
        row['fluid_poses'] = {'generation': 2, 'phase': 2 if ordinal == 0 else 3,
                              'samples': [{'valid': True, 'world_pose': p} for p in positions]}
        if not winches:
            for name in ('source_generation', 'source_phase', 'features'):
                del row[name]
    return result


def stamp(frame):
    """Encode borrowed literal frame's64-byte structural stamp with physical P8."""
    # Preserve actual times, source IDs and unchanged static/transient factor meanings.
    s = frame['stamp']  # Exact independently authored state facts.
    return struct.pack('<QIIdQdQQQ', s['ordinal'], s['analysis'], s['events'], s['time'],
                       s['state_id'], s['load_factor'], 8, 0, 0)


def pose_bytes(frame):
    """Return a literal physical frame's raw stamp and P8 xyz/wxyz values."""
    # Retain the original physical doubles independently of F and winch coordinates.
    return stamp(frame) + b''.join(struct.pack('<7d', *p) for p in frame['poses'])


def winch_bytes(frame):
    """Return J07 copied stamp/generation/phase and complete literal A/B/C/D values."""
    # Encode the approved pre-existing winch layout without any production helper.
    raw = stamp(frame) + struct.pack('<QII', frame['source_generation'], frame['source_phase'], 0)
    for entry in frame['features']:  # One independent arrangement in catalog order.
        raw += struct.pack('<II12d', entry['geometry_status'], 0,
                           *(v for point in entry['world_xyz_m'] for v in point))
    return raw


def fluid_bytes(frame):
    """Return candidate F common prefix, packed LSB mask and all literal poses."""
    # Compose the independent mask, then store all rows including canonical unavailable values.
    feature = frame['fluid_poses']  # Borrowed expected F population.
    mask = sum(int(sample['valid']) << i for i, sample in enumerate(feature['samples']))
    raw = stamp(frame) + struct.pack('<QII', feature['generation'], feature['phase'], 0)
    mask_bytes = (len(feature['samples']) + 7) // 8  # Packed width, including a partial final byte.
    raw += mask.to_bytes(mask_bytes, 'little')
    return raw + b''.join(struct.pack('<7d', *sample['world_pose']) for sample in feature['samples'])


def chunk(kind, stream, sequence, records, first, payload, *, previous=0, flags=0, layout=1, encoding=0):
    """Encode one independent80-byte chunk header and payload with explicit wire facts."""
    # Bind header and payload CRCs; negative callers deliberately supply invalid scalar facts.
    header = struct.pack('<4sHHIIIIQQQQQIIQ', b'PCHK', kind, flags, 80, encoding, stream,
                         layout, sequence, records, first, len(payload), len(payload), crc(payload), 0, previous)
    return repair(header, 68) + payload


def make_file(reference, *, catalog=CATALOG, fluid=True, winches=None, gap='', duplicate=False,
              reversed_order=False, early_index=False, outcomes=(1, 1)):
    """Return complete candidate bytes and offsets using fixed two-frame groups.

    reference borrows literal rows; catalog owns topology. Mutator options author
    isolated missing/order/gap cases. early_index ends after the second F batch
    with index/counts reflecting the earlier complete group, testing pending-F
    rejection without corrupting the prior structural-only completion.
    """
    # Serialize bootstrap/catalog; emit complete typed groups in the requested candidate order.
    # Write unchanged structural index/completion/footer, including empty failed recordings.
    enabled_winches = 'winch_geometry' in catalog if winches is None else winches
    cat = json.dumps(catalog, ensure_ascii=True, separators=(',', ':'), allow_nan=False).encode('utf-8')
    header = struct.pack('<8sHHIQQQQIIQ', b'SRNPOSE\0', 1, 0, 64, 0, len(cat), 8, 4096, crc(cat), 0, 0)
    data = bytearray(repair(header, 52) + cat)  # Entire small fixture, owned during authoring only.
    offsets = {'fluid': [], 'winches': [], 'poses': [], 'unknown': []}  # Exact mutation locations.
    sequence = first_pose = previous = batches = committed = 0  # Structural facts/global chunk order.
    for start in range(0, len(reference), 2):  # First dense ordinal of this bounded batch.
        rows = reference[start:start+2]  # Borrowed independent state subset.
        order = ([4] if enabled_winches else []) + ([5] if fluid else []) if reversed_order else \
                ([5] if fluid else []) + ([4] if enabled_winches else [])
        if duplicate and fluid:
            order.insert(1, 5)
        for kind in order:  # Intentional order; no decoder or recorder chooses it.
            key = 'fluid' if kind == 5 else 'winches'
            offsets[key].append(len(data))
            payload = b''.join((fluid_bytes if kind == 5 else winch_bytes)(row) for row in rows)
            data += chunk(kind, 3 if kind == 5 else 2, sequence, len(rows), start, payload)
            sequence += 1
            if early_index and start == 2 and kind == 5:
                break
            if gap == ('after_fluid' if kind == 5 else 'after_winch'):
                offsets['unknown'].append(len(data))
                data += chunk(100, 42, sequence, 1, start, b'independent opaque gap', encoding=17)
                sequence += 1
        if early_index and start == 2:
            break
        offset = len(data)  # Required structural chunk offset, never an overlay offset.
        offsets['poses'].append(offset)
        data += chunk(1, 1, sequence, len(rows), start, b''.join(pose_bytes(row) for row in rows),
                      previous=previous, flags=1)
        sequence += 1
        first_pose = first_pose or offset
        previous = offset
        batches += 1
        committed += len(rows)
    index = len(data)  # Unchanged32-byte structural linked index.
    data += chunk(2, 0, sequence, 1, 0, struct.pack('<4Q', batches, committed, first_pose, previous), flags=1)
    completion = len(data)  # Unchanged64-byte numerical/capture/file outcome record.
    data += chunk(3, 0, sequence+1, 1, 0, struct.pack('<4I6Q', *outcomes, 1, 0,
                  committed, committed, batches, committed-1 if committed else U64_MAX, index, 0), flags=1)
    data += repair(struct.pack('<8sQQII', b'POSEEND\0', index, completion, 0, 0), 24)
    offsets.update(index=index, completion=completion)
    return bytes(data), offsets


def change_payload(raw, chunk_offset, relative, fmt, value):
    """Copy a file, mutate one payload scalar and repair that chunk's CRC pair."""
    # Isolate semantic failure from integrity failure without rewriting other records.
    data = bytearray(raw)  # Owned negative fixture.
    struct.pack_into(fmt, data, chunk_offset+80+relative, value)
    size = struct.unpack_from('<Q', data, chunk_offset+48)[0]  # Existing bounded raw payload size.
    struct.pack_into('<I', data, chunk_offset+64, crc(data[chunk_offset+80:chunk_offset+80+size]))
    data[chunk_offset:chunk_offset+80] = repair(data[chunk_offset:chunk_offset+80], 68)
    return bytes(data)


def change_header(raw, offset, relative, fmt, value):
    """Copy a file, mutate one header scalar and repair only its header CRC."""
    # Payload bytes remain independent, making header count/identity negatives precise.
    data = bytearray(raw)
    struct.pack_into(fmt, data, offset+relative, value)
    data[offset:offset+80] = repair(data[offset:offset+80], 68)
    return bytes(data)


def build_corpus():
    """Return small independent files, outcomes, catalogs and literal expected frames."""
    # Establish asymmetric positive states and explicit legitimate mask states.
    # Author isolated catalog/header/payload negatives and interrupted later groups.
    reference = frames()  # Standard mixed F/winch accepted-state oracle.
    without = frames(winches=False)  # Same physical/F states, absent winch feature.
    no_winch_catalog = copy.deepcopy(CATALOG)
    del no_winch_catalog['winch_geometry']
    files, cases = {}, {}  # Owned bytes and separate full/legacy validity expectations.
    references = {'transient_frames': reference, 'no_winch_frames': without}

    def add(name, raw, *, valid=True, baseline=True, winch=True, complete=True, count=3,
            frames_key='transient_frames', reason='', run=1, capture=1):
        """Register one unique raw case and its independently asserted reader outcomes."""
        # Separate opaque-reader compatibility from candidate F semantics.
        assert name not in files
        files[name] = raw
        cases[name] = {'valid': valid, 'baseline_valid': baseline, 'winch_aware_valid': winch,
                       'complete': complete, 'frames': count, 'frames_key': frames_key,
                       'run_outcome': run, 'capture_outcome': capture}
        if reason:
            cases[name]['rejection_reason'] = reason
        if not complete:
            cases[name]['allow_incomplete'] = True

    good, offsets = make_file(reference)
    add('mixed_transient.spose', good)
    add('without_winches.spose', make_file(without, catalog=no_winch_catalog)[0], frames_key='no_winch_frames')
    add('unknown_gap.spose', make_file(reference, gap='after_fluid')[0])
    # Exercise the one/two-byte mask boundary and deliberately unaligned binary64 rows.
    for population in (7, 8, 9):  # Independent declared F populations; physical P8 remains fixed.
        population_catalog = copy.deepcopy(CATALOG)
        population_frames = copy.deepcopy(reference)
        for sample_id in range(6, population):  # Extra stations on the second line's native element.
            population_catalog['fluid_poses']['samples'].append({
                'id': sample_id, 'line_id': 1, 'material_coordinate_m': float(sample_id - 2),
                'side': 'left', 'native_element': 0})
            population_catalog['fluid_poses']['segments'][2]['sample_ids'].append(sample_id)
        for ordinal, row in enumerate(population_frames):
            delta = ordinal * 0.125  # Exact translation keeps each literal row distinct in time.
            extra_positions = [
                [14.75, -0.125 + delta, 16.5, 0, 0, 0, 1],
                [17.125, 1.75 + delta, 20.25, .5, .5, -.5, .5],
                [19.5, 3.875 + delta, 25.625, -.5, .5, .5, .5]]
            row['fluid_poses']['samples'].extend(
                {'valid': True, 'world_pose': position} for position in extra_positions[:population - 6])
        frames_key = f'k{population}_frames'  # Separate full F oracle; never inferred from decoded bytes.
        references[frames_key] = population_frames
        population_bytes, population_offsets = make_file(population_frames, catalog=population_catalog)
        filename = f'fluid_k{population}.spose'
        add(filename, population_bytes, frames_key=frames_key)
        cases[filename].update(fluid_sample_count=population,
                               fluid_frame_bytes=80 + (population + 7) // 8 + 56 * population,
                               fluid_first_pose_offset=80 + (population + 7) // 8)
        if population != 8:
            # K7 reserves bit7 in byte0; K9 reserves bits1..7 in byte1.
            padding_offset = 80 + (population - 1) // 8
            padding_value = 0xff if population == 7 else 0x81
            add(f'padding_mask_k{population}.spose', change_payload(population_bytes,
                population_offsets['fluid'][0], padding_offset, '<B', padding_value),
                valid=False, frames_key=frames_key, reason='unused high mask bit')
    masked = copy.deepcopy(reference)
    for row in masked:  # Valid zero position and a distinct unavailable identity row coexist.
        row['fluid_poses']['samples'][4] = {'valid': True, 'world_pose': CANONICAL_UNAVAILABLE[:]}
        row['fluid_poses']['samples'][5] = {'valid': False, 'world_pose': CANONICAL_UNAVAILABLE[:]}
    references['masked_frames'] = masked
    masked_bytes, masked_offsets = make_file(masked)
    add('masked.spose', masked_bytes, frames_key='masked_frames')
    unavailable = copy.deepcopy(reference)
    for row in unavailable:
        for sample in row['fluid_poses']['samples']:
            sample.update(valid=False, world_pose=CANONICAL_UNAVAILABLE[:])
    references['all_unavailable_frames'] = unavailable
    add('all_unavailable.spose', make_file(unavailable)[0], frames_key='all_unavailable_frames')
    static = copy.deepcopy(reference[:1])
    static[0]['stamp'].update(analysis=2, events=8, time=2.0, state_id=9, load_factor=.75)
    static[0]['source_phase'] = static[0]['fluid_poses']['phase'] = 1
    references['static_frames'] = static
    add('static.spose', make_file(static)[0], count=1, frames_key='static_frames')
    prepared = copy.deepcopy(reference[:1])
    prepared[0]['stamp'].update(analysis=0, events=1, time=100.0, state_id=0, load_factor=0.0)
    prepared[0]['source_phase'] = prepared[0]['fluid_poses']['phase'] = 0
    references['prepared_frames'] = prepared
    add('prepared.spose', make_file(prepared, outcomes=(0, 1))[0], count=1, frames_key='prepared_frames', run=0)
    no_initial = copy.deepcopy(reference[1:])
    for ordinal, row in enumerate(no_initial):
        row['stamp']['ordinal'] = ordinal
    references['initial_disabled_frames'] = no_initial
    add('initial_disabled.spose', make_file(no_initial)[0], count=2, frames_key='initial_disabled_frames')
    add('empty_failed.spose', make_file([], outcomes=(2, 3))[0], count=0, run=2, capture=3)

    # Every semantic mutation keeps unrelated physical/winch bytes and all CRCs valid.
    for name, kwargs, old_aware in (
            ('missing_fluid.spose', {'fluid': False}, True),
            ('duplicate_fluid.spose', {'duplicate': True}, True),
            ('wrong_order.spose', {'reversed_order': True}, False),
            ('between_winch_pose.spose', {'gap': 'after_winch'}, False),
            ('missing_winches.spose', {'winches': False}, False),
            ('early_index.spose', {'early_index': True}, True)):
        add(name, make_file(reference, **kwargs)[0], valid=False, winch=old_aware,
            count=2 if kwargs.get('early_index') else 3, reason='group order/cardinality')

    for name, field, fmt, value, baseline, old_aware in (
            ('wrong_kind.spose', 4, '<H', 100, True, True),
            ('wrong_stream.spose', 16, '<I', 42, True, True),
            ('wrong_layout.spose', 20, '<I', 2, True, True),
            ('wrong_encoding.spose', 12, '<I', 17, True, True),
            ('required_fluid.spose', 6, '<H', 1, False, False),
            ('reserved_flag.spose', 6, '<H', 2, False, False),
            ('count_mismatch.spose', 32, '<Q', 1, True, True),
            ('count_overflow.spose', 32, '<Q', 1 << 63, False, False),
            ('first_ordinal.spose', 40, '<Q', 1, True, True),
            ('previous_nonzero.spose', 72, '<Q', 1, True, True)):
        add(name, change_header(good, offsets['fluid'][0], field, fmt, value), valid=False,
            baseline=baseline, winch=old_aware, reason='F header contract')

    for name, field, fmt, value in (
            ('wrong_generation.spose', 64, '<Q', 3),
            ('wrong_phase.spose', 72, '<I', 1),
            ('source_reserved.spose', 76, '<I', 1),
            ('copied_time_mismatch.spose', 16, '<d', 1.2501),
            ('copied_sample_count.spose', 40, '<Q', 7),
            ('padding_mask.spose', 80, '<B', 0xbf),
            ('cleared_noncanonical.spose', 80, '<B', 0x3e),
            ('nonfinite_pose.spose', 81, '<d', math.nan),
            ('invalid_quaternion.spose', 81+3*8, '<d', 2.0)):
        add(name, change_payload(good, offsets['fluid'][0], field, fmt, value), valid=False,
            reason='F record contract')
    add('unavailable_negative_zero.spose', change_payload(masked_bytes, masked_offsets['fluid'][0],
        81+5*56, '<d', -0.0), valid=False, frames_key='masked_frames', reason='noncanonical unavailable row')
    # A later row mismatch rejects the whole group even after its first row was individually valid.
    add('second_row_stamp_mismatch.spose', change_payload(good, offsets['fluid'][0],
        FLUID_FRAME_BYTES+16, '<d', 1.2901), valid=False, reason='whole-group stamp join')

    edits = [
        ('missing_descriptor.spose', lambda c: c.pop('fluid_poses')),
        ('wrong_owner.spose', lambda c: c['fluid_poses']['lines'][0].update(occurrence_id=20)),
        ('wrong_side.spose', lambda c: c['fluid_poses']['samples'][1].update(side='both')),
        ('unresolved_side.spose', lambda c: c['fluid_poses']['samples'][0].update(side='none')),
        ('bad_sample_reference.spose', lambda c: c['fluid_poses']['segments'][0].update(sample_ids=[0, 99])),
        ('wrong_line_reference.spose', lambda c: c['fluid_poses']['segments'][0].update(sample_ids=[0, 4])),
        ('reversed_interval.spose', lambda c: c['fluid_poses']['segments'][0].update(sample_ids=[1, 0])),
        ('boolean_reference.spose', lambda c: c['fluid_poses']['samples'][0].update(line_id=False)),
        ('float_reference.spose', lambda c: c['fluid_poses']['samples'][0].update(id=0.0)),
        ('integer_limit.spose', lambda c: c['fluid_poses']['samples'][0].update(native_element=1 << 63)),
        ('native_element_range.spose', lambda c: c['fluid_poses']['samples'][0].update(native_element=2)),
        ('duplicate_address.spose', lambda c: c['fluid_poses']['samples'][2].update(side='left', native_element=0)),
        ('contradictory_native_owner.spose', lambda c: c['fluid_poses']['samples'][2].update(side='left')),
        ('orphan_sample.spose', lambda c: c['fluid_poses']['segments'].pop()),
        ('duplicate_line.spose', lambda c: c['fluid_poses']['lines'][1].update(occurrence_id=21)),
        ('empty_fingerprint.spose', lambda c: c['fluid_poses']['lines'][0].update(source_geometry_fingerprint='')),
        ('catalog_generation_mismatch.spose', lambda c: c['fluid_poses'].update(source_preparation_generation=3)),
        ('zero_generation.spose', lambda c: c['fluid_poses'].update(source_preparation_generation=0)),
        ('empty_population.spose', lambda c: c['fluid_poses'].update(samples=[]))]
    for name, edit in edits:  # Isolated owned metadata mutations, never changing old catalogs.
        catalog = copy.deepcopy(CATALOG)
        edit(catalog)
        add(name, make_file(reference, catalog=catalog)[0], valid=False, reason='F catalog contract')

    damaged = bytearray(good)  # CRC failure deliberately leaves checksums unrepaired.
    damaged[offsets['fluid'][0]+80+81] ^= 1
    add('corrupt_fluid.spose', bytes(damaged), valid=False, baseline=False, winch=False, reason='payload CRC')
    minor = bytearray(good)
    struct.pack_into('<H', minor, 10, 1)
    minor[:64] = repair(minor[:64], 52)
    add('unsupported_minor.spose', bytes(minor), valid=False, baseline=False, winch=False, reason='bootstrap minor')

    for name, at in (
            ('truncated_fluid.spose.partial', offsets['fluid'][1]+80+13),
            ('pending_fluid.spose.partial', offsets['winches'][1]),
            ('truncated_winches.spose.partial', offsets['winches'][1]+80+17),
            ('pending_winches.spose.partial', offsets['poses'][1]),
            ('truncated_pose.spose.partial', offsets['poses'][1]+80+25)):
        add(name, good[:at], complete=False, count=2)
    # A complete pending F batch must reject defects visible without its later pose.
    pending = good[:offsets['winches'][1]]  # Entire later F chunk after one complete group.
    for name, field, fmt, value in (
            ('nonfinite_pending_fluid.spose.partial', 81, '<d', math.inf),
            ('wrong_ordinal_pending_fluid.spose.partial', 0, '<Q', 3),
            ('wrong_phase_pending_fluid.spose.partial', 72, '<I', 1)):
        add(name, change_payload(pending, offsets['fluid'][1], field, fmt, value),
            valid=False, complete=False, count=2, reason='independently malformed complete pending F')
    # Without a counterpart, a valid later F time cannot yet be called a mismatched join.
    add('unjoined_time_pending_fluid.spose.partial', change_payload(pending, offsets['fluid'][1],
        16, '<d', 1.3201), complete=False, count=2)
    return files, cases, references, no_winch_catalog


def write_harness(root, name, files, cases, references, catalog, classification):
    """Write an unchanged generic viewer-test harness for one reader capability subset.

    classification chooses baseline_valid or winch_aware_valid. The historical
    generic test's special transient.spose control is deliberately feature-free
    and excluded from the main F corpus. Other bytes remain exact copies.
    """
    # Keep literal transient/static/prepared pose oracles and reader-specific validity.
    # Exclude only first-capture omission, which the old driver cannot select by filename.
    destination = root / name
    destination.mkdir()
    selected = {}
    for filename, case in cases.items():
        if filename == 'initial_disabled.spose':
            continue
        (destination / filename).write_bytes(files[filename])
        selected[filename] = {k: case[k] for k in ('complete', 'frames', 'run_outcome', 'capture_outcome')}
        selected[filename].update(valid=case[classification], bytes=len(files[filename]),
                                  sha256=digest(files[filename]))
    control_catalog = copy.deepcopy(catalog)
    control_catalog.pop('winch_geometry', None)
    control_catalog.pop('fluid_poses', None)
    (destination / 'transient.spose').write_bytes(make_file(frames(winches=False),
        catalog=control_catalog, fluid=False, winches=False)[0])
    (destination / 'catalog.json').write_bytes((json.dumps(catalog, indent=2)+'\n').encode())
    expected = {k: references[k] for k in ('transient_frames', 'static_frames', 'prepared_frames')}
    expected.update(cases=selected, status='Opaque-subset compatibility only; F semantics not checked')
    (destination / 'expected.json').write_bytes((json.dumps(expected, indent=2)+'\n').encode())


def main():
    """Author one new corpus at argv[1] or the default; print its bounded size/count."""
    # Refuse existing destinations, create independent bytes and bind all expectations.
    # Produce separate legacy/partial-aware harnesses without altering their executables.
    root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else ROOT
    if len(sys.argv) > 2 or root.exists():
        raise SystemExit('usage: author_fluid_fixtures.py [NEW_DIRECTORY]; destination must not exist')
    files, cases, references, no_winch_catalog = build_corpus()
    root.mkdir(parents=True)
    for name, raw in files.items():
        (root / name).write_bytes(raw)
        cases[name].update(bytes=len(raw), sha256=digest(raw))
    expected = {'status': 'Independent candidate F layout; reader execution evidence is separate',
                'geometry_scope': 'Literal byte oracles, not a physical beam/fluid solution',
                'physical_sample_count': 8, 'winch_arrangement_count': 2, 'fluid_sample_count': 6,
                'pose_frame_bytes': 512, 'winch_frame_bytes': 288,
                'fluid_frame_bytes': FLUID_FRAME_BYTES, 'group_frame_bytes': 1217,
                **references, 'no_winch_catalog': no_winch_catalog, 'cases': cases}
    (root / 'catalog.json').write_bytes((json.dumps(CATALOG, indent=2)+'\n').encode())
    (root / 'expected.json').write_bytes((json.dumps(expected, indent=2, allow_nan=False)+'\n').encode())
    write_harness(root, 'legacy-viewer-harness', files, cases, references, CATALOG, 'baseline_valid')
    write_harness(root, 'winch-aware-viewer-harness', files, cases, references, CATALOG, 'winch_aware_valid')
    print(json.dumps({'root': str(root), 'cases': len(files), 'binary_bytes': sum(map(len, files.values())),
                      'fluid_frame_bytes': FLUID_FRAME_BYTES, 'group_frame_bytes': 1217}))


if __name__ == '__main__':
    main()
