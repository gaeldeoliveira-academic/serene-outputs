"""Author independent elastic-moment fixtures, then optionally classify pinned readers.

Only earlier independent catalog/row literals are imported. File framing, new
moment bytes and all expected outcomes are authored here without a production
writer/checker. Classification is a separate explicit command using frozen older
readers; it never determines the fully aware oracle. Existing corpora are immutable.
These are wire/display-arithmetic fixtures, not elastic-provider numerical proof.
"""
from pathlib import Path
import copy
import hashlib
import importlib.util
import json
import math
import struct
import sys
import zlib

from author_applied_fixtures import CATALOG as APPLIED_CATALOG, frames as applied_frames

ROOT = Path(__file__).resolve().parents[2] / 'fixtures_pose_moments'  # New corpus; replacement is forbidden.
MAX_U64 = (1 << 64) - 1  # Empty-completion last ordinal, never a valid accepted identity.
DELETE = object()  # Author-only marker for one omitted catalog field.
MOMENT_FRAME_BYTES = 249  # C7: common80 + one mask byte + seven signed triples.
CATALOG = copy.deepcopy(APPLIED_CATALOG)  # Owned extension of independent P8/F6/loadK5K3/W2 literals.
CATALOG['recording_id'] = 's00-independent-elastic-moments-layout1'
CATALOG['segments'][0]['id'] = 701  # Existing base IDs are stable identities, not dense table offsets.
CATALOG['elastic_moments'] = {
    'kind': 7, 'stream_id': 5, 'layout_version': 1, 'encoding': 'raw_f64_le',
    'source_preparation_generation': 2, 'quantity': 'elastic_section_moment',
    'components': ['Mx', 'My', 'Mz'], 'unit': 'N.m',
    'expression_frame': 'current_structural_N', 'moment_point': 'current_structural_N',
    'action_convention': 'positive_span_on_negative_span',
    'display_interpolation': 'linear_scalar_on_declared_base_edge',
    'lines': [{'id': 0, 'occurrence_id': 21, 'spanline_id': 'N', 'native_element_count': 2,
               'source_geometry_fingerprint': 'moment-source-47',
               'effective_geometry_fingerprint': 'moment-effective-61',
               'approximation_policy': 'literal_structural_realization'}],
    'provider_addresses': [
        {'id': 0, 'line_id': 0, 'native_element': 0,
         'provider_id': 'chrono10_euler_diagonal_elastic_cubic_recovery_v1',
         'material_begin_m': 0.0, 'material_end_m': 3.0},
        {'id': 1, 'line_id': 0, 'native_element': 1,
         'provider_id': 'chrono10_fpm_full_K6_Bx_elastic_native_section_v1',
         'material_begin_m': 3.0, 'material_end_m': 5.0}],
    'cuts': [
        {'id': 0, 'line_id': 0, 'material_coordinate_m': 0.0, 'side': 'right',
         'native_element': 0, 'provider_address_id': 0, 'native_eta': -1.0},
        {'id': 1, 'line_id': 0, 'material_coordinate_m': .37, 'side': 'right',
         'native_element': 0, 'provider_address_id': 0, 'native_eta': -.7533333333333333},
        {'id': 2, 'line_id': 0, 'material_coordinate_m': 2.0, 'side': 'left',
         'native_element': 0, 'provider_address_id': 0, 'native_eta': 1.0 / 3.0},
        {'id': 3, 'line_id': 0, 'material_coordinate_m': 2.0, 'side': 'right',
         'native_element': 0, 'provider_address_id': 0, 'native_eta': 1.0 / 3.0},
        {'id': 4, 'line_id': 0, 'material_coordinate_m': 3.0, 'side': 'left',
         'native_element': 0, 'provider_address_id': 0, 'native_eta': 1.0},
        {'id': 5, 'line_id': 0, 'material_coordinate_m': 3.0, 'side': 'right',
         'native_element': 1, 'provider_address_id': 1, 'native_eta': -1.0},
        {'id': 6, 'line_id': 0, 'material_coordinate_m': 5.0, 'side': 'left',
         'native_element': 1, 'provider_address_id': 1, 'native_eta': 1.0}],
    'sided_breaks': [{'left_cut_id': 2, 'right_cut_id': 3}, {'left_cut_id': 4, 'right_cut_id': 5}],
    'display_intervals': [
        {'id': 0, 'line_id': 0, 'base_segment_id': 701, 'edge_index': 0, 'cut_ids': [0, 1]},
        {'id': 1, 'line_id': 0, 'base_segment_id': 701, 'edge_index': 0, 'cut_ids': [1, 2]},
        {'id': 2, 'line_id': 0, 'base_segment_id': 701, 'edge_index': 0, 'cut_ids': [3, 4]},
        {'id': 3, 'line_id': 0, 'base_segment_id': 701, 'edge_index': 0, 'cut_ids': [5, 6]}]}


def digest(raw):
    """Return SHA-256 for borrowed bytes without normalization or mutation."""
    # Bind independently authored evidence to its exact stored representation.
    return hashlib.sha256(raw).hexdigest()


def crc(raw):
    """Return unsigned IEEE CRC32 of borrowed bytes using the standard library."""
    # Keep checksum computation independent of every Serene implementation.
    return zlib.crc32(raw) & 0xffffffff


def repair_header(raw, offset):
    """Return a copied header with its own CRC field at byte offset repaired."""
    # Clear the selected CRC field, hash the full header, then replace that field.
    result = bytearray(raw)  # Owned mutation buffer; the caller's bytes remain unchanged.
    struct.pack_into('<I', result, offset, 0)
    struct.pack_into('<I', result, offset, crc(result))
    return bytes(result)


def frames():
    """Return three owned source rows with independent signed current-N triples.

    Values preserve nonzero Mz, opposed full-resultant endpoints, true zero and
    signed zero. No stiffness, transport, basis or provider evaluation is claimed.
    """
    # Reuse prior literal source groups, then attach a separate seven-cut population.
    result = applied_frames()  # Fresh old-population literals, not decoded output files.
    for ordinal, row in enumerate(result):  # Actual saved ordinal and its coherent source identity.
        triples = [[3.0, 4.0, 12.0], [-3.0, -4.0, -12.0],
                   [-7.25 - ordinal, 2.5, 9.75], [8.5, -6.25 - ordinal, -4.75],
                   [0.0, 0.0, -12.0], [-0.0, 0.0, -0.0], [0.0, 0.0, 0.0]]  # Dense cut order.
        row['elastic_moments'] = {'generation': 2, 'phase': 2 if ordinal == 0 else 3,
            'moments': [{'valid': True, 'moment_N_Nm': triple} for triple in triples]}
    return result


def stamp(row):
    """Return the borrowed row's literal 64-byte source stamp with unchanged P8."""
    # Encode exact accepted times/identities and recorder ordinals with explicit LE types.
    value = row['stamp']  # Borrowed independent structural stamp dictionary.
    return struct.pack('<QIIdQdQQQ', value['ordinal'], value['analysis'], value['events'],
                       value['time'], value['state_id'], value['load_factor'], 8, 0, 0)


def record_bytes(row, kind):
    """Encode one borrowed literal population for kind 1/4/5/6/7 as owned raw bytes.

    Old-population encodings are written explicitly here; no production encoder
    or earlier fixture writer is invoked. Unsupported kinds raise AssertionError.
    """
    # Encode the common stamp, then the selected fixed-width literal population.
    # Keep masks LSB-first and place dense scalars immediately after their masks.
    common = stamp(row)  # Same source identity bytes for every population in this row.
    if kind == 1:
        return common + b''.join(struct.pack('<7d', *sample) for sample in row['poses'])
    if kind == 4:
        prefix = common + struct.pack('<QII', row['source_generation'], row['source_phase'], 0)  # Winch source prefix.
        return prefix + b''.join(struct.pack('<II12d', item['geometry_status'], 0,
            *(scalar for point in item['world_xyz_m'] for scalar in point)) for item in row['features'])
    family = {5: 'fluid_poses', 6: 'applied_loads', 7: 'elastic_moments'}[kind]  # Known literal family selector.
    population = row[family]  # Borrowed family values and source provenance.
    raw = common + struct.pack('<QII', population['generation'], population['phase'], 0)  # Common 80-byte prefix.
    if kind in (5, 7):
        values = population['samples' if kind == 5 else 'moments']  # Dense F-pose or moment rows.
        mask = sum(int(item['valid']) << index for index, item in enumerate(values))  # Explicit bit index equals row ID.
        raw += mask.to_bytes((len(values) + 7) // 8, 'little')
        return raw + b''.join(struct.pack('<7d' if kind == 5 else '<3d',
            *item['world_pose' if kind == 5 else 'moment_N_Nm']) for item in values)
    assert kind == 6
    # Preserve each attachment's real source point/density and common 96-byte interval facts.
    for attachment in population['attachments']:  # Complete unequal K5/K3 logical populations.
        value = attachment['interval']  # Borrowed opaque provenance and accepted interval endpoints.
        raw += struct.pack('<IIQdQd7Q', value['status'], 0, value['source_accepted_id'], value['source_time_s'],
            value['end_accepted_id'], value['end_time_s'], value['publication'], value['mechanical_revision'],
            value['state_revision'], value['geometry_revision'], value['environment_revision'],
            value['prepared_step_sequence'], value['packet'])
        values = attachment['samples']  # Dense attachment-local station rows.
        mask = sum(int(item['valid']) << index for index, item in enumerate(values))  # LSB-first local validity.
        raw += mask.to_bytes((len(values) + 7) // 8, 'little')
        raw += b''.join(struct.pack('<6d', *item['source_position_world_m'],
                                  *item['force_density_world_N_per_m']) for item in values)
    return raw


def chunk(kind, stream, sequence, records, first, payload, *, previous=0, flags=0, encoding=0):
    """Return one literal chunk from explicit header identities and borrowed payload.

    previous is a structural backlink, flags defaults optional, and encoding
    defaults raw. Callers may supply deliberately invalid facts for negative cases.
    """
    # Encode header facts, bind the payload CRC, then repair the header's independent CRC.
    header = struct.pack('<4sHHIIIIQQQQQIIQ', b'PCHK', kind, flags, 80, encoding, stream, 1,
        sequence, records, first, len(payload), len(payload), crc(payload), 0, previous)  # Fixed 80-byte envelope.
    return repair_header(header, 68) + payload


def make_file(reference, *, catalog=CATALOG, order=None, gap_after=None, future=False,
              early_index=False, outcomes=(1, 1), allow_nan=False):
    """Return owned fixture bytes and chunk offsets from borrowed rows/catalog.

    order overrides known batch order; gap_after inserts optional100; future adds
    opaque8 after moments; early_index terminates the second group after moments.
    outcomes supplies run/capture completion. allow_nan authors invalid JSON only.
    """
    # Serialize an independent bootstrap/catalog, write two-row groups, then structural-only trailers.
    cat = json.dumps(catalog, separators=(',', ':'), ensure_ascii=True, allow_nan=allow_nan).encode()  # Catalog bytes.
    header = struct.pack('<8sHHIQQQQIIQ', b'SRNPOSE\0', 1, 0, 64, 0, len(cat), 8, 4096, crc(cat), 0, 0)  # Bootstrap.
    raw = bytearray(repair_header(header, 52) + cat)  # Owned file under construction.
    selected = order if order is not None else [kind for kind, key in
        ((5, 'fluid_poses'), (6, 'applied_loads'), (7, 'elastic_moments'), (4, 'winch_geometry')) if key in catalog] + [1]
    keys = {5: 'fluid', 6: 'loads', 7: 'moments', 4: 'winches', 1: 'poses'}  # Offset-family names, not wire authority.
    streams = {5: 3, 6: 4, 7: 5, 4: 2, 1: 1}  # Frozen exact known kind/stream registry.
    offsets = {key: [] for key in (*keys.values(), 'unknown', 'future')}  # Per-family exact file offsets.
    sequence = first_pose = previous = batches = committed = 0  # Chunk ordinal, structural links and committed counts.
    for start in range(0, len(reference), 2):  # First recording ordinal of the bounded two-row batch.
        rows = reference[start:start + 2]  # Borrowed complete group rows shared by all populations.
        for kind in selected:  # Explicit known group order; negative cases deliberately violate it.
            offset = len(raw)  # Current chunk header position used for mutation and structural backlinks.
            offsets[keys[kind]].append(offset)
            raw += chunk(kind, streams[kind], sequence, len(rows), start,
                b''.join(record_bytes(row, kind) for row in rows), previous=previous if kind == 1 else 0,
                flags=1 if kind == 1 else 0)
            sequence += 1
            if kind == 1:
                first_pose = first_pose or offset
                previous = offset
                batches += 1
                committed += len(rows)
            if early_index and start == 2 and kind == 7:
                break
            if future and kind == 7:
                offsets['future'].append(len(raw))
                raw += chunk(8, 6, sequence, len(rows), start, b'future-plane-opaque-no-semantic-claim')
                sequence += 1
            if gap_after == kind:
                offsets['unknown'].append(len(raw))
                raw += chunk(100, 42, sequence, 1, start, b'independent-opaque-moment-gap', encoding=17)
                sequence += 1
        if early_index and start == 2:
            break
    # Index/completion/footer refer only to complete structural batches, never optional overlays.
    index = len(raw)  # Structural seek-index header position.
    raw += chunk(2, 0, sequence, 1, 0, struct.pack('<4Q', batches, committed, first_pose, previous), flags=1)
    completion = len(raw)  # Completion header position recorded in the footer.
    raw += chunk(3, 0, sequence + 1, 1, 0, struct.pack('<4I6Q', *outcomes, 1, 0, committed,
        committed, batches, committed - 1 if committed else MAX_U64, index, 0), flags=1)
    raw += repair_header(struct.pack('<8sQQII', b'POSEEND\0', index, completion, 0, 0), 24)
    offsets.update(index=index, completion=completion)
    return bytes(raw), offsets


def payload_change(raw, offset, relative, fmt, value):
    """Return copied file with one payload scalar changed and both affected CRCs repaired."""
    # Mutate only the selected scalar, then bind its payload and chunk header checksums.
    data = bytearray(raw)  # Owned copy retaining all unrelated bytes and offsets.
    struct.pack_into(fmt, data, offset + 80 + relative, value)
    size = struct.unpack_from('<Q', data, offset + 48)[0]  # Existing complete payload byte extent.
    struct.pack_into('<I', data, offset + 64, crc(data[offset + 80:offset + 80 + size]))
    data[offset:offset + 80] = repair_header(data[offset:offset + 80], 68)
    return bytes(data)


def header_change(raw, offset, relative, fmt, value):
    """Return copied file with one header scalar changed and its header CRC repaired."""
    # Preserve payload and all structural offsets while isolating one header contract defect.
    data = bytearray(raw)  # Owned mutation buffer.
    struct.pack_into(fmt, data, offset + relative, value)
    data[offset:offset + 80] = repair_header(data[offset:offset + 80], 68)
    return bytes(data)


def edited(catalog, path, value):
    """Return a catalog copy with one tuple-addressed value replaced or DELETE omitted."""
    # Follow borrowed dict/list keys in the owned copy, then perform one explicit mutation.
    result = copy.deepcopy(catalog)  # Independent case catalog.
    owner = result  # Current container along the tuple path.
    for key in path[:-1]:  # Dict key or list index leading to the targeted field.
        owner = owner[key]
    if value is DELETE:
        del owner[path[-1]]
    else:
        owner[path[-1]] = copy.deepcopy(value)
    return result


def renumber(rows):
    """Copy borrowed rows and replace only their dense recording ordinals."""
    # Preserve physical accepted identities and times while starting a new recording at ordinal zero.
    result = copy.deepcopy(rows)  # Independent sequence for initial-omitted/phase-subset cases.
    for ordinal, row in enumerate(result):  # Dense recording ordinal and corresponding owned source row.
        row['stamp']['ordinal'] = ordinal
    return result


def build_corpus():
    """Return owned case bytes, independently declared reader outcomes and literal oracles.

    No reader is imported or called here. Negative catalog/frame cases isolate
    known moment semantics; older subsets retain their own explicit expectations.
    """
    # Establish positive literals and mask/alignment variants, then mutate isolated contracts.
    # Preserve every complete earlier group in partial-file cases and classify only known facts.
    reference = frames()  # Main P8/F6/loadK5K3/C7/W2 independent three-row source sequence.
    files, cases = {}, {}  # New case bytes and independently authored classification records.
    references = {'transient_frames': reference}  # Literal frame arrays indexed by manifest frames_key.

    def add(name, raw, *, valid=True, baseline=True, winch=True, fluid=True, applied=True,
            complete=True, count=3, frames_key='transient_frames', reason='', run=1, capture=1):
        """Register named bytes and explicit full/subset outcomes without consulting a reader.

        count/complete describe committed structural groups, frames_key selects
        borrowed literal rows, and reason records the intended isolated defect.
        """
        # Reject accidental replacement, then retain exact bytes and separate capability expectations.
        assert name not in files, name
        files[name] = raw
        cases[name] = {'valid': valid, 'fully_aware_valid': valid, 'baseline_valid': baseline,
            'winch_aware_valid': winch, 'f_aware_valid': fluid, 'applied_aware_valid': applied,
            'complete': complete, 'frames': count, 'frames_key': frames_key,
            'run_outcome': run, 'capture_outcome': capture}
        if reason:
            cases[name]['rejection_reason'] = reason
        if not complete:
            cases[name]['allow_incomplete'] = True

    good, offsets = make_file(reference)  # Baseline bytes and exact mutation addresses.
    add('mixed_transient.spose', good)
    for name, remove in (('without_fluid', ('fluid_poses',)), ('without_loads', ('applied_loads',)),
                         ('without_winches', ('winch_geometry',)),
                         ('moments_only', ('fluid_poses', 'applied_loads', 'winch_geometry'))):
        catalog, rows = copy.deepcopy(CATALOG), copy.deepcopy(reference)  # Independent family-subset case.
        for family in remove:  # One omitted descriptor and its corresponding literal population.
            del catalog[family]
            for row in rows:  # Remove only the omitted family, preserving all actual source facts.
                for field in (('source_generation', 'source_phase', 'features') if family == 'winch_geometry' else (family,)):
                    del row[field]
        references[name + '_frames'] = rows
        add(name + '.spose', make_file(rows, catalog=catalog)[0], frames_key=name + '_frames')
    for kind in (5, 6, 7):  # Unknown bytes may precede/between overlays and precede the final winch/pose suffix.
        add(f'unknown_after_{kind}.spose', make_file(reference, gap_after=kind)[0])
    future_catalog = copy.deepcopy(CATALOG)  # Unsupported future descriptor is deliberately opaque to this slice.
    future_catalog['inflow_planes'] = {'kind': 8, 'stream_id': 6, 'layout_version': 1,
                                     'literal_future_metadata': 'not-claimed-as-plane-validation'}
    add('opaque_future_planes.spose', make_file(reference, catalog=future_catalog, future=True)[0])
    first_positive = renumber(reference[1:])  # Initial capture is omitted without relabeling accepted identities.
    references['first_positive_frames'] = first_positive
    add('first_positive.spose', make_file(first_positive)[0], count=2, frames_key='first_positive_frames')
    for name, analysis, phase, time, identity, factor, events in (
            ('prepared', 0, 0, 100.0, 0, 0.0, 1), ('static', 2, 1, 2.0, 9, .75, 8)):
        rows = copy.deepcopy(reference[:1])  # A valid requested phase subset; phase0 is never compulsory.
        rows[0]['stamp'].update(analysis=analysis, events=events, time=time, state_id=identity, load_factor=factor)
        rows[0]['source_phase'] = phase
        for family in ('fluid_poses', 'applied_loads', 'elastic_moments'):  # All present known source facts agree exactly.
            rows[0][family]['phase'] = phase
        references[name + '_frames'] = rows
        add(name + '.spose', make_file(rows)[0], count=1, frames_key=name + '_frames')
    prepared_positive = renumber([references['prepared_frames'][0], *reference[1:]])  # Prepared time is not an accepted clock.
    references['prepared_then_positive_frames'] = prepared_positive
    add('prepared_then_positive.spose', make_file(prepared_positive)[0], frames_key='prepared_then_positive_frames')
    for name in ('masked', 'all_unavailable'):  # Valid +0 remains distinct from a clear availability mask.
        rows = copy.deepcopy(reference)  # Owned mask variant with exact canonical unavailable triples.
        for row in rows:  # The same catalog permits legitimate availability variation across actual records.
            selected = range(7) if name == 'all_unavailable' else (2, 5)  # Explicit cut IDs to make unavailable.
            for index in selected:  # Dense cut index, never inferred from moment magnitude.
                row['elastic_moments']['moments'][index] = {'valid': False, 'moment_N_Nm': [0.0, 0.0, 0.0]}
        references[name + '_frames'] = rows
        add(name + '.spose', make_file(rows)[0], frames_key=name + '_frames')
    add('empty_failed.spose', make_file([], outcomes=(2, 3))[0], count=0, run=2, capture=3)

    # Add continuous stations with new dense cut IDs, retaining explicit nonoverlapping base-edge intervals.
    count_cases = {}  # C8/C9 bytes and offsets for second-mask-byte mutation.
    for count in (8, 9):  # Mask-boundary populations; C7 is the unchanged baseline.
        catalog, rows = copy.deepcopy(CATALOG), copy.deepcopy(reference)  # Independent topology and payload extension.
        moment = catalog['elastic_moments']  # Owned descriptor being extended without changing old evidence.
        moment['cuts'].append({'id': 7, 'line_id': 0, 'material_coordinate_m': 1.125, 'side': 'right',
            'native_element': 0, 'provider_address_id': 0, 'native_eta': -.25})
        moment['display_intervals'][1]['cut_ids'] = [1, 7]
        moment['display_intervals'].append({'id': 4, 'line_id': 0, 'base_segment_id': 701, 'edge_index': 0, 'cut_ids': [7, 2]})
        if count == 9:
            moment['cuts'].append({'id': 8, 'line_id': 0, 'material_coordinate_m': 4.125, 'side': 'right',
                'native_element': 1, 'provider_address_id': 1, 'native_eta': .125})
            moment['display_intervals'][3]['cut_ids'] = [5, 8]
            moment['display_intervals'].append({'id': 5, 'line_id': 0, 'base_segment_id': 701, 'edge_index': 0, 'cut_ids': [8, 6]})
        for ordinal, row in enumerate(rows):  # Distinct literal triples detect cut-order and unaligned-double errors.
            row['elastic_moments']['moments'].append({'valid': True, 'moment_N_Nm': [17.125 + ordinal, -2.75, 5.625]})
            if count == 9:
                row['elastic_moments']['moments'].append({'valid': True, 'moment_N_Nm': [-19.75, 6.125 + ordinal, -8.375]})
        references[f'c{count}_frames'] = rows
        data, addresses = make_file(rows, catalog=catalog)  # Complete valid alignment case and its header offsets.
        count_cases[count] = (data, addresses, catalog)
        add(f'cuts_c{count}.spose', data, frames_key=f'c{count}_frames')
    add('padding_mask_c7.spose', payload_change(good, offsets['moments'][0], 80, '<B', 0xff),
        valid=False, reason='unused mask high bit')
    data, addresses, _ = count_cases[9]  # C9 has one used bit in the second mask byte.
    add('padding_mask_c9.spose', payload_change(data, addresses['moments'][0], 81, '<B', 0x81),
        valid=False, frames_key='c9_frames', reason='unused second-mask-byte high bit')

    # Optional structural metadata constrains only the approved occurrence/local-ID/range mapping.
    structural = copy.deepcopy(CATALOG)  # Matching row ID intentionally differs from moment line ID zero.
    structural['structural_lines'] = [{'id': 911, 'occurrence_id': 21, 'local_id': 'N', 'sample_begin': 0, 'sample_count': 2}]
    add('structural_table_without_line_ids.spose', make_file(reference, catalog=structural)[0])
    structural['segments'][0]['line_id'] = 911
    structural['samples'][0]['line_id'] = structural['samples'][1]['line_id'] = 911
    add('structural_table_row_identity.spose', make_file(reference, catalog=structural)[0])
    generic = copy.deepcopy(structural)  # Literal N spelling and F fingerprint/provider equality are not wire requirements.
    generic['structural_lines'][0].update(local_id='beam.axis.alpha', source_geometry_fingerprint='unrelated-structural-source')
    generic['elastic_moments']['lines'][0]['spanline_id'] = 'beam.axis.alpha'
    generic['elastic_moments']['provider_addresses'][0]['provider_id'] = 'qualified-owner-extension-example'
    generic['elastic_moments']['descriptive_capability'] = {'requested_phases': [1, 3], 'note': 'descriptive-only'}
    add('generic_metadata_not_authority.spose', make_file(reference, catalog=generic)[0])
    no_rebind = edited(CATALOG, ('elastic_moments', 'cuts', 1, 'native_eta'), -.5)  # Interior eta bounds, not a reconstructed native binding.
    add('native_eta_not_recomputed.spose', make_file(reference, catalog=no_rebind)[0])
    selected_elements = copy.deepcopy(CATALOG)  # Selected-provider table need not enumerate every native element.
    selected_elements['elastic_moments']['lines'][0]['native_element_count'] = 3
    selected_elements['elastic_moments']['provider_addresses'][1]['native_element'] = 2
    for index in (5, 6):  # Cuts on the selected last element retain the corresponding exact ownership.
        selected_elements['elastic_moments']['cuts'][index]['native_element'] = 2
    add('selected_native_elements_only.spose', make_file(reference, catalog=selected_elements)[0])
    reordered = copy.deepcopy(CATALOG)  # Table order carries no interpolation-chain authority; interval IDs remain dense.
    reordered['elastic_moments']['display_intervals'].reverse()
    for index, interval in enumerate(reordered['elastic_moments']['display_intervals']):  # Dense row IDs after deliberate permutation.
        interval['id'] = index
    add('permuted_display_intervals.spose', make_file(reference, catalog=reordered)[0])

    # Group framing failures are independently classified for all older known-family subsets.
    for name, order, winch, fluid, applied in (
            ('missing_moments', [5, 6, 4, 1], True, True, True),
            ('duplicate_moments', [5, 6, 7, 7, 4, 1], True, True, True),
            ('moments_before_fluid', [7, 5, 6, 4, 1], True, True, True),
            ('moments_before_loads', [5, 7, 6, 4, 1], True, True, True),
            ('moments_between_winch_pose', [5, 6, 4, 7, 1], False, False, False),
            ('missing_fluid', [6, 7, 4, 1], True, False, False),
            ('missing_loads', [5, 7, 4, 1], True, True, False),
            ('missing_winches', [5, 6, 7, 1], False, False, False)):
        add(name + '.spose', make_file(reference, order=order)[0], valid=False,
            winch=winch, fluid=fluid, applied=applied, reason='known group order/cardinality')
    add('unknown_between_winch_pose.spose', make_file(reference, gap_after=4)[0],
        valid=False, winch=False, fluid=False, applied=False, reason='forbidden winch/pose gap')
    add('early_index.spose', make_file(reference, early_index=True)[0], valid=False,
        fluid=False, applied=False, count=2, reason='index while understood group is pending')
    for name, field, fmt, value, legacy in (
            ('wrong_kind', 4, '<H', 100, True), ('wrong_stream', 16, '<I', 42, True),
            ('wrong_layout', 20, '<I', 2, True), ('wrong_encoding', 12, '<I', 17, True),
            ('required_moments', 6, '<H', 1, False), ('reserved_flag', 6, '<H', 2, False),
            ('count_mismatch', 32, '<Q', 1, True), ('zero_count', 32, '<Q', 0, True),
            ('count_overflow', 32, '<Q', 1 << 63, False), ('first_ordinal', 40, '<Q', 1, True),
            ('previous_nonzero', 72, '<Q', 1, True), ('decoded_size', 56, '<Q', 497, True)):
        add(name + '.spose', header_change(good, offsets['moments'][0], field, fmt, value), valid=False,
            baseline=legacy, winch=legacy, fluid=legacy, applied=legacy, reason='moment chunk header contract')
    for name, field, fmt, value, winch, fluid, applied in (
            ('moment_kind_fluid_stream', 16, '<I', 3, True, False, False),
            ('moment_kind_load_stream', 16, '<I', 4, True, True, False),
            ('moment_kind_winch_stream', 16, '<I', 2, False, False, False),
            ('fluid_kind_moment_stream', 4, '<H', 5, True, False, False),
            ('load_kind_moment_stream', 4, '<H', 6, True, True, False)):
        add(name + '.spose', header_change(good, offsets['moments'][0], field, fmt, value), valid=False,
            winch=winch, fluid=fluid, applied=applied, reason='known kind OR stream collision cannot be skipped')

    # Completed moment records must validate even when a later structural counterpart is missing.
    for name, field, fmt, value in (
            ('wrong_generation', 64, '<Q', 3), ('zero_generation', 64, '<Q', 0),
            ('generation_overflow', 64, '<Q', 1 << 63), ('wrong_phase', 72, '<I', 1),
            ('unknown_phase', 72, '<I', 4), ('source_reserved', 76, '<I', 1),
            ('copied_time', 16, '<d', 1.2501), ('copied_identity', 24, '<Q', 1),
            ('copied_events', 12, '<I', 4), ('copied_factor', 32, '<d', -0.0),
            ('physical_count', 40, '<Q', 7), ('stamp_reserved', 48, '<Q', 1),
            ('record_ordinal', 0, '<Q', 1), ('nonfinite_mx', 81, '<d', math.nan),
            ('nonfinite_my', 89, '<d', math.inf), ('nonfinite_mz', 97, '<d', -math.inf),
            ('cleared_nonzero', 80, '<B', 0x7e)):
        add(name + '.spose', payload_change(good, offsets['moments'][0], field, fmt, value),
            valid=False, reason='moment completed record contract')
    unavailable_rows = references['all_unavailable_frames']  # Canonical zero baseline for clear-mask scalar negatives.
    unavailable, unavailable_offsets = make_file(unavailable_rows)
    for name, field, value in (('unavailable_nonzero', 81, 1.0), ('unavailable_negative_zero', 89, -0.0)):
        add(name + '.spose', payload_change(unavailable, unavailable_offsets['moments'][0], field, '<d', value),
            valid=False, frames_key='all_unavailable_frames', reason='unavailable triple requires exact positive zeros')

    # Mutate exact catalog identities and mappings without changing authored old-population facts.
    prefix = ('elastic_moments',)  # Tuple-address base for the new descriptor only.
    edits = [
        ('missing_descriptor', prefix, DELETE), ('catalog_kind', prefix + ('kind',), 6),
        ('catalog_stream', prefix + ('stream_id',), 4), ('catalog_layout', prefix + ('layout_version',), 2),
        ('catalog_encoding', prefix + ('encoding',), 'zfp'), ('catalog_generation', prefix + ('source_preparation_generation',), 3),
        ('catalog_generation_float', prefix + ('source_preparation_generation',), 2.0),
        ('catalog_generation_bool', prefix + ('source_preparation_generation',), True),
        ('catalog_generation_zero', prefix + ('source_preparation_generation',), 0),
        ('catalog_quantity', prefix + ('quantity',), 'total_section_moment'),
        ('catalog_components_order', prefix + ('components',), ['My', 'Mx', 'Mz']),
        ('catalog_components_partial', prefix + ('components',), ['Mx', 'My']),
        ('catalog_unit', prefix + ('unit',), 'N'), ('catalog_frame', prefix + ('expression_frame',), 'reference_N'),
        ('catalog_point', prefix + ('moment_point',), 'native_P'),
        ('catalog_sign', prefix + ('action_convention',), 'negative_span_on_positive_span'),
        ('catalog_interpolation', prefix + ('display_interpolation',), 'interpolate_vector_then_norm'),
        ('empty_lines', prefix + ('lines',), []), ('empty_providers', prefix + ('provider_addresses',), []),
        ('empty_cuts', prefix + ('cuts',), []), ('empty_intervals', prefix + ('display_intervals',), []),
        ('missing_sided_breaks', prefix + ('sided_breaks',), DELETE),
        ('line_non_dense', prefix + ('lines', 0, 'id'), 1), ('line_bool_id', prefix + ('lines', 0, 'id'), False),
        ('line_wrong_owner', prefix + ('lines', 0, 'occurrence_id'), 20),
        ('line_unknown_owner', prefix + ('lines', 0, 'occurrence_id'), 999),
        ('line_empty_local_id', prefix + ('lines', 0, 'spanline_id'), ''),
        ('line_nul_local_id', prefix + ('lines', 0, 'spanline_id'), 'N\0other'),
        ('line_empty_source_fp', prefix + ('lines', 0, 'source_geometry_fingerprint'), ''),
        ('line_empty_effective_fp', prefix + ('lines', 0, 'effective_geometry_fingerprint'), ''),
        ('line_empty_policy', prefix + ('lines', 0, 'approximation_policy'), ''),
        ('line_zero_elements', prefix + ('lines', 0, 'native_element_count'), 0),
        ('line_float_elements', prefix + ('lines', 0, 'native_element_count'), 2.0),
        ('line_element_cap', prefix + ('lines', 0, 'native_element_count'), 1000001),
        ('provider_non_dense', prefix + ('provider_addresses', 1, 'id'), 7),
        ('provider_wrong_line', prefix + ('provider_addresses', 0, 'line_id'), 1),
        ('provider_float_element', prefix + ('provider_addresses', 0, 'native_element'), 0.0),
        ('provider_element_range', prefix + ('provider_addresses', 0, 'native_element'), 2),
        ('provider_duplicate_element', prefix + ('provider_addresses', 1, 'native_element'), 0),
        ('provider_empty_id', prefix + ('provider_addresses', 0, 'provider_id'), ''),
        ('provider_nul_id', prefix + ('provider_addresses', 0, 'provider_id'), 'provider\0address'),
        ('provider_negative_begin', prefix + ('provider_addresses', 0, 'material_begin_m'), -1.0),
        ('provider_equal_bounds', prefix + ('provider_addresses', 0, 'material_end_m'), 0.0),
        ('provider_reversed_bounds', prefix + ('provider_addresses', 1, 'material_end_m'), 2.0),
        ('provider_boolean_bound', prefix + ('provider_addresses', 0, 'material_end_m'), True),
        ('cut_non_dense', prefix + ('cuts', 1, 'id'), 7),
        ('cut_unknown_line', prefix + ('cuts', 1, 'line_id'), 1),
        ('cut_unknown_provider', prefix + ('cuts', 1, 'provider_address_id'), 2),
        ('cut_float_provider', prefix + ('cuts', 1, 'provider_address_id'), 0.0),
        ('cut_native_mismatch', prefix + ('cuts', 1, 'native_element'), 1),
        ('cut_side_none', prefix + ('cuts', 1, 'side'), 'none'),
        ('cut_coordinate_bool', prefix + ('cuts', 1, 'material_coordinate_m'), True),
        ('cut_below_provider', prefix + ('cuts', 1, 'material_coordinate_m'), -.1),
        ('cut_above_provider', prefix + ('cuts', 1, 'material_coordinate_m'), 3.1),
        ('cut_lower_outward_side', prefix + ('cuts', 0, 'side'), 'left'),
        ('cut_upper_outward_side', prefix + ('cuts', 6, 'side'), 'right'),
        ('cut_lower_eta', prefix + ('cuts', 0, 'native_eta'), -.999),
        ('cut_upper_eta', prefix + ('cuts', 6, 'native_eta'), .999),
        ('cut_interior_eta_lower', prefix + ('cuts', 1, 'native_eta'), -1.0),
        ('cut_interior_eta_upper', prefix + ('cuts', 1, 'native_eta'), 1.0),
        ('cut_eta_boolean', prefix + ('cuts', 1, 'native_eta'), True),
        ('cut_duplicate_same_side', prefix + ('cuts', 3, 'side'), 'left'),
        ('pair_missing', prefix + ('sided_breaks',), [{'left_cut_id': 4, 'right_cut_id': 5}]),
        ('pair_reversed', prefix + ('sided_breaks', 0), {'left_cut_id': 3, 'right_cut_id': 2}),
        ('pair_self', prefix + ('sided_breaks', 0), {'left_cut_id': 2, 'right_cut_id': 2}),
        ('pair_coordinate_mismatch', prefix + ('sided_breaks', 0), {'left_cut_id': 2, 'right_cut_id': 5}),
        ('pair_unknown_cut', prefix + ('sided_breaks', 0, 'right_cut_id'), 99),
        ('pair_boolean_cut', prefix + ('sided_breaks', 0, 'left_cut_id'), True),
        ('pair_duplicate', prefix + ('sided_breaks',), [{'left_cut_id': 2, 'right_cut_id': 3}] * 2 + [{'left_cut_id': 4, 'right_cut_id': 5}]),
        ('interval_non_dense', prefix + ('display_intervals', 0, 'id'), 1),
        ('interval_wrong_line', prefix + ('display_intervals', 0, 'line_id'), 1),
        ('interval_unknown_base_segment', prefix + ('display_intervals', 0, 'base_segment_id'), 0),
        ('interval_float_segment', prefix + ('display_intervals', 0, 'base_segment_id'), 701.0),
        ('interval_edge_range', prefix + ('display_intervals', 0, 'edge_index'), 1),
        ('interval_negative_edge', prefix + ('display_intervals', 0, 'edge_index'), -1),
        ('interval_boolean_edge', prefix + ('display_intervals', 0, 'edge_index'), False),
        ('interval_wrong_cut_count', prefix + ('display_intervals', 0, 'cut_ids'), [0, 1, 2]),
        ('interval_self', prefix + ('display_intervals', 0, 'cut_ids'), [0, 0]),
        ('interval_reversed', prefix + ('display_intervals', 0, 'cut_ids'), [1, 0]),
        ('interval_unknown_cut', prefix + ('display_intervals', 0, 'cut_ids'), [0, 99]),
        ('interval_upper_right', prefix + ('display_intervals', 1, 'cut_ids'), [1, 3]),
        ('interval_lower_left', prefix + ('display_intervals', 2, 'cut_ids'), [2, 4]),
        ('interval_cross_break', prefix + ('display_intervals', 1, 'cut_ids'), [1, 4]),
        ('interval_cross_provider', prefix + ('display_intervals', 2, 'cut_ids'), [3, 6]),
        ('interval_overlap', prefix + ('display_intervals', 1, 'cut_ids'), [0, 2]),
        ('unmapped_cut', prefix + ('display_intervals',), CATALOG['elastic_moments']['display_intervals'][1:])]
    for name, path, value in edits:  # One independently specified field/ownership/mapping defect per catalog.
        catalog = edited(CATALOG, path, value)  # Owned negative catalog; old populations stay byte-equivalent after encoding.
        add(name + '.spose', make_file(reference, catalog=catalog, order=[5, 6, 7, 4, 1])[0],
            valid=False, reason='moment catalog contract')
    for key in ('quantity', 'components', 'unit', 'expression_frame', 'moment_point', 'action_convention',
                'display_interpolation', 'provider_addresses', 'cuts', 'lines', 'display_intervals'):
        catalog = edited(CATALOG, prefix + (key,), DELETE)  # Required descriptor field intentionally absent.
        add('missing_' + key + '.spose', make_file(reference, catalog=catalog)[0], valid=False, reason='missing moment catalog field')

    # Matched structural rows, not moment-line ordinals, constrain the optional metadata relation.
    structural_edits = [
        ('structural_no_matching_row', ('structural_lines', 0, 'local_id'), 'other'),
        ('structural_wrong_occurrence', ('structural_lines', 0, 'occurrence_id'), 31),
        ('structural_duplicate_match', ('structural_lines',), structural['structural_lines'] * 2),
        ('structural_id_float', ('structural_lines', 0, 'id'), 911.0),
        ('structural_begin_boolean', ('structural_lines', 0, 'sample_begin'), False),
        ('structural_count_float', ('structural_lines', 0, 'sample_count'), 2.0),
        ('structural_range_overflow', ('structural_lines', 0, 'sample_begin'), (1 << 63) - 1),
        ('structural_range_outside_samples', ('structural_lines', 0, 'sample_count'), 9),
        ('structural_edge_endpoint_outside_range', ('structural_lines', 0, 'sample_count'), 1),
        ('structural_wrong_segment_line_id', ('segments', 0, 'line_id'), 0),
        ('structural_wrong_sample_line_id', ('samples', 1, 'line_id'), 0),
        ('structural_sample_line_bool', ('samples', 0, 'line_id'), True)]
    for name, path, value in structural_edits:  # Only the newly approved optional mapping predicate is violated.
        add(name + '.spose', make_file(reference, catalog=edited(structural, path, value))[0],
            valid=False, reason='optional structural-line identity/range predicate')

    # Additional mapping predicates require coordinated edits to isolate the intended geometry relation.
    duplicate_owner = copy.deepcopy(CATALOG)  # Same line/s/side must remain unique across different native owners.
    duplicate_owner['elastic_moments']['provider_addresses'][1]['material_begin_m'] = 0.0
    duplicate_owner['elastic_moments']['cuts'][5].update(material_coordinate_m=.37, native_eta=-.8)
    add('duplicate_coordinate_different_native_owner.spose', make_file(reference, catalog=duplicate_owner)[0],
        valid=False, reason='line-coordinate-side uniqueness across provider/native ownership')
    duplicate_line = copy.deepcopy(CATALOG)  # A second descriptor line cannot duplicate occurrence/local identity.
    duplicate_line['elastic_moments']['lines'].append(dict(duplicate_line['elastic_moments']['lines'][0], id=1))
    add('duplicate_moment_line_identity.spose', make_file(reference, catalog=duplicate_line)[0], valid=False, reason='moment line identity uniqueness')
    other_owner = copy.deepcopy(CATALOG)  # The base edge still belongs to21, while the moment provider claims31.
    other_owner['elastic_moments']['lines'][0]['occurrence_id'] = 31
    add('mapped_edge_wrong_occurrence.spose', make_file(reference, catalog=other_owner)[0], valid=False, reason='explicit base edge occurrence ownership')
    for name, lower, upper in (('base_edge_decreasing', 5.0, 0.0), ('base_edge_zero_length', 0.0, 0.0),
                               ('cut_outside_base_edge', .5, 5.0), ('base_difference_nonfinite', -1e308, 1e308)):
        catalog = copy.deepcopy(CATALOG)  # Base finite coordinates remain legal to older generic readers.
        catalog['samples'][0]['material_coordinate_m'], catalog['samples'][1]['material_coordinate_m'] = lower, upper
        add(name + '.spose', make_file(reference, catalog=catalog)[0], valid=False, reason='moment base edge coordinate bounds')
    rounded = copy.deepcopy(count_cases[8][2])  # Distinct JSON integer spellings collapse to one binary64 cut coordinate.
    rounded['samples'][1]['material_coordinate_m'] = 18014398509481984
    rounded['elastic_moments']['provider_addresses'][0]['material_end_m'] = 13510798882111488
    rounded['elastic_moments']['provider_addresses'][1].update(material_begin_m=13510798882111488, material_end_m=18014398509481984)
    for index, coordinate in enumerate((0, 9007199254740992, 11000000000000000, 11000000000000000,
                                        13510798882111488, 13510798882111488, 18014398509481984, 9007199254740993)):
        rounded['elastic_moments']['cuts'][index]['material_coordinate_m'] = coordinate  # Same-side cuts1/7 collide after binary64 conversion.
    add('binary64_duplicate_cut_coordinate.spose', make_file(references['c8_frames'], catalog=rounded)[0],
        valid=False, frames_key='c8_frames', reason='binary64 line-coordinate-side uniqueness')
    for name, path in (('nonfinite_cut_coordinate', prefix + ('cuts', 1, 'material_coordinate_m')),
                       ('nonfinite_native_eta', prefix + ('cuts', 1, 'native_eta')),
                       ('nonfinite_provider_bound', prefix + ('provider_addresses', 0, 'material_end_m'))):
        catalog = edited(CATALOG, path, math.nan)  # Deliberately invalid JSON numeric extension, rejected by every parser.
        add(name + '.spose', make_file(reference, catalog=catalog, allow_nan=True)[0],
            valid=False, baseline=False, winch=False, fluid=False, applied=False, reason='nonfinite JSON metadata')

    # Preserve prior complete groups across torn suffixes; complete independently malformed prefixes still fail.
    damaged = bytearray(good)  # Payload corruption without checksum repair tests framing before semantic interpretation.
    damaged[offsets['moments'][0] + 80 + 81] ^= 1
    add('corrupt_moment_crc.spose', bytes(damaged), valid=False, baseline=False, winch=False, fluid=False, applied=False, reason='payload CRC')
    for name, at in (('truncated_moment_header', offsets['moments'][1] + 39),
                     ('truncated_moment_payload', offsets['moments'][1] + 80 + 91),
                     ('pending_moments', offsets['winches'][1]),
                     ('truncated_winches', offsets['winches'][1] + 80 + 19),
                     ('pending_winches', offsets['poses'][1]),
                     ('truncated_pose', offsets['poses'][1] + 80 + 31)):
        add(name + '.spose.partial', good[:at], complete=False, count=2)
    pending = good[:offsets['winches'][1]]  # Complete F/load/moment prefix with no final winch/pose counterpart.
    for name, field, fmt, value in (
            ('pending_nonfinite_moment', 81, '<d', math.inf), ('pending_wrong_phase', 72, '<I', 1),
            ('pending_wrong_ordinal', 0, '<Q', 3), ('pending_wrong_generation', 64, '<Q', 3),
            ('pending_copied_time_disagrees_with_present_F', 16, '<d', 1.3201)):
        add(name + '.spose.partial', payload_change(pending, offsets['moments'][1], field, fmt, value),
            valid=False, complete=False, count=2, reason='complete independently malformed pending moment record')
    unjoined = pending  # Every present known population agrees; missing pose alone provides no contradictory counterpart.
    for key in ('fluid', 'loads', 'moments'):  # Align actual copied time across the complete pending prefix.
        unjoined = payload_change(unjoined, offsets[key][1], 16, '<d', 1.3201)
    for block in (80, 417):  # Applied endpoint time must still match the altered common source stamp.
        unjoined = payload_change(unjoined, offsets['loads'][1], block + 32, '<d', 1.3201)
    add('unjoined_pending_time.spose.partial', unjoined, complete=False, count=2)
    return files, cases, references


def protected_identity():
    """Return aggregate and per-file hashes of earlier F/load corpora and their authors.

    Read every old artifact without importing a writer or changing any prior bytes.
    The aggregate uses stable relative path ordering and exact file contents.
    """
    # Inventory the two frozen populations and bind both their generators and all existing evidence.
    source = Path(__file__).resolve().parents[2]  # Staged source root owning old immutable evidence.
    paths = [source / 'tools/pose_sidecar/author_fluid_fixtures.py',
             source / 'tools/pose_sidecar/author_applied_fixtures.py']  # Prior independent authors.
    for folder in ('fixtures_pose_fluid', 'fixtures_pose_applied'):  # Previously sealed fixture families.
        paths.extend(sorted(path for path in (source / folder).rglob('*') if path.is_file()))
    entries = {path.relative_to(source).as_posix(): digest(path.read_bytes()) for path in paths}  # Exact old file identities.
    return digest(json.dumps(entries, sort_keys=True, separators=(',', ':')).encode()), entries


def write_corpus(root):
    """Create one new corpus at root, returning its exact manifest/count/byte summary.

    Existing destinations reject before writing. Earlier evidence is hashed before
    and after authoring; all failures retain any files already created for review.
    """
    # Build all literal cases without readers, create the fresh destination, then bind exact evidence identities.
    if root.exists():
        raise SystemExit('fixture destination must not exist: ' + str(root))
    before, protected = protected_identity()  # Immutable old-evidence baseline, including author source hashes.
    files, cases, references = build_corpus()  # Independent authored bytes, classifications and exact expected source rows.
    root.mkdir(parents=True)
    for name, raw in files.items():  # One new case and its owned binary bytes.
        (root / name).write_bytes(raw)
        cases[name].update(bytes=len(raw), sha256=digest(raw))
    display = {  # Literal arithmetic oracles; no provider interpolation or off-node pose proof is implied.
        'opposed_endpoints': {'cut_ids': [0, 1], 'endpoint_triples_N_Nm': [[3, 4, 12], [-3, -4, -12]],
            'endpoint_resultants_Nm': [13, 13], 'midpoint_resultant_Nm': 13,
            'midpoint_signed_components_Nm': [0, 0, 0], 'forbidden_norm_after_component_interpolation_Nm': 0},
        'pure_third_component': {'cut_id': 4, 'moment_N_Nm': [0, 0, -12], 'resultant_Nm': 12},
        'in_element_break': {'coordinate_m': 2, 'left_cut_id': 2, 'right_cut_id': 3,
                            'native_element': 0, 'provider_address_id': 0},
        'masked_suppressed_interval_ids': [1, 3], 'all_unavailable_suppressed_interval_ids': [0, 1, 2, 3]}
    expected = {'status': 'Independent moment-layout1 literal oracle; execution evidence is separate',
        'scope': 'Wire and display arithmetic only, not elastic provider, moment transport or numerical rollback qualification',
        'physical_sample_count': 8, 'fluid_sample_count': 6, 'winch_arrangement_count': 2,
        'applied_attachment_count': 2, 'attachment_station_counts': [5, 3],
        'moment_cut_count': 7, 'moment_provider_count': 2, 'moment_line_count': 1,
        'moment_frame_bytes': MOMENT_FRAME_BYTES, 'group_frame_bytes': 2124,
        'known_group_order': [5, 6, 7, 4, 1], 'moment_mask_offset': 80,
        'moment_first_double_offsets': {'C7': 81, 'C8': 81, 'C9': 82},
        'moment_frame_widths': {'C7': 249, 'C8': 273, 'C9': 298}, 'base_segment_ids': [701],
        'display_oracles': display, 'protected_F_load_tree_sha256': before,
        'protected_F_load_files': protected, **references, 'cases': cases}
    (root / 'catalog.json').write_bytes((json.dumps(CATALOG, indent=2) + '\n').encode())
    (root / 'expected.json').write_bytes((json.dumps(expected, indent=2, allow_nan=False) + '\n').encode())
    assert protected_identity()[0] == before, 'old F/load evidence changed during moment authoring'
    summary = {'root': str(root), 'cases': len(files), 'binary_bytes': sum(map(len, files.values())),
               'expected_sha256': digest((root / 'expected.json').read_bytes()),
               'protected_F_load_tree_sha256': before}  # Exact new-corpus identity returned to the coordinator.
    return summary


def classify_pinned(root, pinned):
    """Check existing corpus against four explicitly supplied immutable older readers.

    root supplies authored expectations; pinned supplies base/winch/F/applied Python
    files. Return results only when every declared classification matches; save
    mismatch evidence before raising. This never invents a fully aware moment result.
    """
    # Load fixed older reader identities, compare each observed subset result with the authored manifest,
    # and preserve all outcomes even on failure without changing a fixture or expectation.
    expected = json.loads((root / 'expected.json').read_text(encoding='utf-8'))  # Independent already-authored outcomes.
    before = protected_identity()[0]  # Old corpora remain immutable even while older checkers run.
    evidence = {'scope': 'Pinned older capability subsets; moment semantics remain opaque', 'readers': {}}  # Execution evidence only.
    mismatches = []  # Explicit disagreements retained for review, never used to derive new expectations automatically.
    for filename, field in (('base_checker.py', 'baseline_valid'), ('winch_checker.py', 'winch_aware_valid'),
                            ('F_checker.py', 'f_aware_valid'), ('applied_checker.py', 'applied_aware_valid')):
        path = pinned / filename  # Required immutable checker path, independent of live production changes.
        module_name = '_pinned_moment_subset_' + filename.replace('.', '_')  # Unique import identity for dataclass lookups.
        spec = importlib.util.spec_from_file_location(module_name, path)  # Explicit file-based import; no ambient module search.
        module = importlib.util.module_from_spec(spec)  # Older implementation loaded only in this separate classification path.
        sys.modules[module_name] = module
        spec.loader.exec_module(module)
        result = {'sha256': digest(path.read_bytes()), 'cases': {}}  # Reader identity and observed per-case results.
        for name, case in expected['cases'].items():  # Fixture filename and independently specified capability expectation.
            try:
                observed = module.check(root / name, allow_incomplete=case.get('allow_incomplete', False))  # Bounded no-dump check.
                entry = {'valid': True, 'complete': observed['complete'], 'frames': observed['validated_frames']}
                for key in ('implemented_families', 'understood_families', 'ignored_families'):  # Retain honest capability reporting.
                    if key in observed:
                        entry[key] = observed[key]
            except Exception as error:  # Reader rejection is evidence; unexpected errors remain visible in its type/message.
                entry = {'valid': False, 'error_type': type(error).__name__, 'error': str(error)}
            result['cases'][name] = entry
            if entry['valid'] != case[field] or (entry['valid'] and
                    (entry['complete'] != case['complete'] or entry['frames'] != case['frames'])):
                mismatches.append({'reader': filename, 'case': name, 'expected_valid': case[field],
                    'expected_complete': case['complete'], 'expected_frames': case['frames'], 'observed': entry})
        evidence['readers'][filename] = result
    evidence['mismatches'] = mismatches
    evidence['protected_F_load_tree_unchanged'] = protected_identity()[0] == before
    destination = root / 'legacy-classifications.json'  # New execution receipt; never overwrite a failed attempt.
    if destination.exists():
        raise SystemExit('classification receipt already exists: ' + str(destination))
    destination.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8', newline='\n')
    assert evidence['protected_F_load_tree_unchanged'], 'old F/load evidence changed during classification'
    if mismatches:
        raise AssertionError(json.dumps(mismatches, indent=2))
    return {'cases_per_reader': len(expected['cases']), 'readers': 4,
            'classifications': 4 * len(expected['cases']), 'receipt_sha256': digest(destination.read_bytes())}


def main():
    """Author a NEW_DIRECTORY, or classify an existing corpus using --classify CORPUS PINNED.

    No argument selects the new default fixture directory. Wrong arguments or an
    existing destination stop before replacement. Print only a concise JSON receipt.
    """
    # Select explicit authoring versus old-reader classification; never run native executables or live checkers.
    arguments = sys.argv[1:]  # CLI action and caller-selected paths.
    if len(arguments) == 3 and arguments[0] == '--classify':
        result = classify_pinned(Path(arguments[1]).resolve(), Path(arguments[2]).resolve())  # Separate execution evidence.
    elif len(arguments) <= 1:
        result = write_corpus(Path(arguments[0]).resolve() if arguments else ROOT)  # Fresh independently authored corpus.
    else:
        raise SystemExit('usage: author_moment_fixtures.py [NEW_DIRECTORY] | --classify CORPUS PINNED')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
