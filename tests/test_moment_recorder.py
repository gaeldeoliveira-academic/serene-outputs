"""Run the compiled signed-moment recorder and independently inspect its actual files.

Literal source rows come from the independent moment corpus. The wrapper uses
the separately implemented reader, direct binary64 comparisons and a small
independent wire walk; no producer encoding helpers or fixture author is imported.
Stale/candidate negatives establish file/API rejection, not numerical rollback.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import subprocess
import sys
import uuid


def bits(value):
    """Return little-endian binary64 bytes for numeric value without normalizing signed zero."""
    # Pack the supplied scalar directly so comparisons retain its exact zero sign.
    return struct.pack('<d', value)


def exact_rows(actual, expected):
    """Assert decoded actual rows match independent expected rows, including floating bits.

    Both inputs are borrowed row sequences and remain unchanged. Return None on
    success; raise AssertionError at the first semantic or representation mismatch.
    """
    # Structural equality checks integer provenance/masks; byte equality retains zero signs.
    assert actual == expected, 'independent source rows changed'
    for observed, source in zip(actual, expected):  # Paired decoded row and independent literal oracle.
        for name in ('time', 'load_factor'):  # Floating structural stamp fields whose signs must survive.
            assert bits(observed['stamp'][name]) == bits(source['stamp'][name]), name
        for left, right in zip(observed['poses'], source['poses']):  # Corresponding structural xyz/wxyz rows.
            assert b''.join(map(bits, left)) == b''.join(map(bits, right)), 'physical pose bits'
        for left, right in zip(observed.get('features', []), source.get('features', [])):  # Optional paired winch arrangements.
            for actual_point, expected_point in zip(left['world_xyz_m'], right['world_xyz_m']):  # A/B/C/D world points.
                assert b''.join(map(bits, actual_point)) == b''.join(map(bits, expected_point)), 'winch bits'
        if 'fluid_poses' in source:
            for left, right in zip(observed['fluid_poses']['samples'], source['fluid_poses']['samples']):  # Paired endpoint F rows.
                assert b''.join(map(bits, left['world_pose'])) == b''.join(map(bits, right['world_pose'])), 'F bits'
        # Preserve signed current-N triples without norm-only replacement or sign alignment.
        for left, right in zip(observed.get('elastic_moments', {}).get('moments', []), source.get('elastic_moments', {}).get('moments', [])):
            assert b''.join(map(bits, left['moment_N_Nm'])) == b''.join(map(bits, right['moment_N_Nm'])), 'moment bits'
        # Check actual source coordinates and densities separately from structural/F endpoint poses.
        for left, right in zip(observed.get('applied_loads', {}).get('attachments', []), source.get('applied_loads', {}).get('attachments', [])):  # Paired attachments.
            for name in ('source_time_s', 'end_time_s'):  # Both endpoints require exact binary64 time identity.
                assert bits(left['interval'][name]) == bits(right['interval'][name]), 'interval time bits'
            for actual_sample, expected_sample in zip(left['samples'], right['samples']):  # Paired logical station rows.
                for name in ('source_position_world_m', 'force_density_world_N_per_m'):  # Source XYZ and world force-density triples.
                    assert b''.join(map(bits, actual_sample[name])) == b''.join(map(bits, expected_sample[name])), name


def inspect_links(path):
    """Independently inspect complete wire records, attachment offsets and structural backlinks.

    The separately implemented checker validates CRC and semantics. This walk
    covers producer framing and byte-identical source stamps/prefixes without
    calling a recorder helper. Files are bounded test artifacts, not untrusted
    production inputs or an alternative general reader.

    Read the supplied Path without modifying it. Return complete group/frame counts,
    pending-prefix count, load-row width and attachment offsets; malformed complete
    framing raises an assertion, while a torn final record ends this bounded walk.
    """
    # Read bounded bytes, derive literal catalog widths and traverse complete chunks only.
    raw = path.read_bytes()  # Owned bytes from a bounded producer test artifact.
    catalog_size = struct.unpack_from('<Q', raw, 24)[0]  # Bootstrap's UTF-8 catalog extent.
    catalog = json.loads(raw[64:64 + catalog_size])  # Independent shape metadata for this artifact.
    counts = [item['station_count'] for item in catalog.get('applied_loads', {}).get('attachments', [])]  # item: attachment in wire order.
    starts, load_width = [], 80  # Per-attachment prefix offsets and total row width after the common prefix.
    for count in counts:  # Current attachment's complete logical station count.
        starts.append(load_width)
        load_width += 96 + (count + 7) // 8 + 48 * count
    widths = {1: 512}  # Fixed P8; optional widths below are independently derived from each catalog.
    order = []  # Required known population order for one complete source group.
    if 'fluid_poses' in catalog:
        count = len(catalog['fluid_poses']['samples'])  # F sample count, independent of load station counts.
        widths[5] = 80 + (count + 7) // 8 + 56 * count
        order.append(5)
    if 'applied_loads' in catalog:
        widths[6] = load_width
        order.append(6)
    moment_count = len(catalog.get('elastic_moments', {}).get('cuts', []))  # Dense cut population for this artifact.
    moment_width = 80 + (moment_count + 7) // 8 + 24 * moment_count  # No padding after the packed validity mask.
    if 'elastic_moments' in catalog:
        widths[7] = moment_width
        order.append(7)
    if 'winch_geometry' in catalog:
        widths[4] = 80 + 104 * len(catalog['winch_geometry']['arrangements'])
        order.append(4)
    order.append(1)
    # Walk only complete chunk envelopes; overlays remain pending until their structural counterpart.
    offset = 64 + catalog_size  # Absolute start of the next chunk header.
    sequence = frames = pose_chunks = first_pose = previous = 0  # Next sequence, committed counts, first/last structural offsets.
    pending = []  # At most one bounded complete-prefix group, never a duration index.
    while offset + 80 <= len(raw) and raw[offset:offset + 4] == b'PCHK':
        header = struct.unpack_from('<4sHHIIIIQQQQQIIQ', raw, offset)  # Current fixed 80-byte chunk envelope.
        kind, records, first, size, backlink = header[1], header[8], header[9], header[10], header[14]  # Population, row extent and structural predecessor.
        if offset + 80 + size > len(raw):
            break
        assert header[7] == sequence
        sequence += 1
        payload = memoryview(raw)[offset + 80:offset + 80 + size]  # Borrowed complete payload, retained only within this bounded group.
        if kind in widths:
            assert kind == order[len(pending)], 'writer group order'
            assert first == frames and size == records * widths[kind], 'writer population framing'
            if kind != 1:
                assert backlink == 0, 'overlay must not enter structural links'
            if kind == 6:
                # Every attachment at one time must carry byte-identical actual interval provenance.
                for row in range(records):  # Local row ordinal within this load chunk.
                    start = row * load_width  # Load-row byte base within its payload.
                    prefix = bytes(payload[start + starts[0]:start + starts[0] + 96])  # First attachment's common interval bytes.
                    for block in starts[1:]:  # Subsequent attachment prefix offset within the same row.
                        assert bytes(payload[start + block:start + block + 96]) == prefix, 'same96-byte attachment prefix'
            pending.append((kind, records, payload))
            if kind == 1:
                assert backlink == previous, 'structural predecessor only'
                assert all(item[1] == records for item in pending), 'exact group row counts'  # item: pending kind/count/view tuple.
                for row in range(records):  # Local structural row ordinal shared with every pending overlay.
                    common = bytes(payload[row * 512:row * 512 + 64])  # Structural stamp bytes that establish group identity.
                    for overlay_kind, _, overlay in pending[:-1]:  # Kind and borrowed payload; count was checked above.
                        start = row * widths[overlay_kind]  # Matching overlay row's stamp offset.
                        assert bytes(overlay[start:start + 64]) == common, 'same structural stamp bits'
                # Commit independent wire progress only after every known population matches.
                first_pose = first_pose or offset
                previous = offset
                pose_chunks += 1
                frames += records
                pending.clear()
        elif kind == 2:
            # Terminal records summarize structural groups, never overlay chunk offsets.
            assert not pending
            assert list(struct.unpack('<4Q', payload)) == [pose_chunks, frames, first_pose, previous], 'structural index'
        elif kind == 3:
            assert not pending and struct.unpack_from('<Q', payload, 24)[0] == frames
            assert struct.unpack_from('<Q', payload, 32)[0] == pose_chunks
        else:
            raise AssertionError('unexpected producer chunk')
        offset += 80 + size
    return {'complete_wire_groups': pose_chunks, 'complete_wire_frames': frames,
            'pending_known_chunks': len(pending), 'load_frame_bytes': load_width,
            'attachment_prefix_offsets': starts, 'moment_frame_bytes': moment_width,
            'moment_first_double_offset': 80 + (moment_count + 7) // 8}


def bad_catalog_case(name, info):
    """Return whether a literal fixture name/info requires pre-file producer setup rejection.

    The same independently authored reason categories are selected in the C++
    driver. Missing descriptor alone is valid setup and tested as an extra view.
    """
    # Select catalog/ownership/mapping defects, excluding malformed payload-only cases.
    return name != 'missing_descriptor.spose' and info.get('rejection_reason') in (
        'moment catalog contract', 'missing moment catalog field', 'optional structural-line identity/range predicate',
        'line-coordinate-side uniqueness across provider/native ownership', 'moment line identity uniqueness',
        'explicit base edge occurrence ownership', 'moment base edge coordinate bounds',
        'binary64 line-coordinate-side uniqueness', 'nonfinite JSON metadata')


def main():
    """Run --exe using --source fixtures into a fresh child of --output-root and verify files.

    Return None after writing evidence; process/reader/assertion failures propagate
    while preserving all generated artifacts. No solver or fixture author is run.
    """
    # Resolve explicit input paths and run the requested executable into a unique output owner.
    # Compare literal values/bits, inspect structural links, and distinguish readable from committed progress.
    # Write successful readback evidence only after every source and failure scenario passes.
    parser = argparse.ArgumentParser()  # Explicit compiled driver, staged source and new-output parent.
    parser.add_argument('--exe', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--output-root', required=True)
    args = parser.parse_args()  # Caller-owned path selections; no implicit builds or prior-output reuse.
    root = Path(args.source)  # Read-only source of independent fixtures and separately implemented reader.
    sys.path.insert(0, str(root / 'tools/pose_sidecar'))
    from check_sidecar import check
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')  # Readable UTC evidence-directory prefix.
    out = Path(args.output_root) / ('run-' + stamp + '-' + uuid.uuid4().hex[:8])  # New collision-resistant output owner.
    out.parent.mkdir(parents=True, exist_ok=True)
    fixture = root / 'fixtures_pose_moments'  # Immutable literal catalog and expected source arrays.
    subprocess.run([args.exe, str(out), str(fixture / 'catalog.json'), str(fixture / 'expected.json')], check=True)
    expected = json.loads((fixture / 'expected.json').read_text(encoding='utf-8'))  # Independent semantic and display oracles.
    transient = expected['transient_frames']  # Main three-row full mixed source sequence.
    results, wire = {}, {}  # Semantic reader outputs and independent byte-walk summaries, keyed by scenario.

    # Verify all admitted corpus catalogs, including source-phase subsets and prefixes from valid torn files.
    for name, info in expected['cases'].items():  # Independent fixture name and explicit fully aware classification.
        if not info['valid'] or name == 'opaque_future_planes.spose':
            continue
        rows = expected[info['frames_key']][:info['frames']]  # Only complete rows are submitted to the new recorder.
        path = out / ('valid_' + name)  # Newly finalized artifact, even when the source fixture was partial.
        result = check(path, dump_frames=True)  # Fully aware independent decoding of actual producer output.
        assert result['complete'] and result['validated_frames'] == result['grouped_frames'] == len(rows), name
        assert result['moment_chunks'] == result['pose_chunks'], name
        assert result['moment_cut_count'] == len(expected[info['frames_key']][0]['elastic_moments']['moments']), name
        assert result['completion']['run'] == info['run_outcome'] and result['completion']['capture'] == info['capture_outcome'], name
        exact_rows(result['frames'], rows)
        results['valid_' + name], wire['valid_' + name] = result, inspect_links(path)
    for name, rows in (('mixed_transient', transient), ('workspace_one', transient[:1]), ('raw_one', transient[:1])):
        path = out / (name + '.spose')  # Main borrowed-buffer and exact-budget success artifacts.
        result = check(path, dump_frames=True)  # Complete actual source values and family counters.
        assert result['complete'] and result['validated_frames'] == result['grouped_frames'] == len(rows), name
        assert result['moment_cut_count'] == 7 and result['moment_chunks'] == result['pose_chunks'], name
        exact_rows(result['frames'], rows)
        results[name], wire[name] = result, inspect_links(path)
    path = out / 'long_bounded.spose'  # Bounded recorder duration scenario, checked without retaining decoded history.
    result = check(path)
    assert result['complete'] and result['validated_frames'] == result['grouped_frames'] == 1001
    assert result['moment_chunks'] == result['applied_chunks'] == result['fluid_chunks'] == result['feature_chunks'] == result['pose_chunks'] == 501
    results['long_bounded'], wire['long_bounded'] = result, inspect_links(path)

    # Confirm alignment, signed/nonzero third components and validity distinctions from actual decoded output.
    assert wire['mixed_transient']['moment_frame_bytes'] == 249 and wire['mixed_transient']['moment_first_double_offset'] == 81
    assert wire['valid_cuts_c8.spose']['moment_frame_bytes'] == 273 and wire['valid_cuts_c8.spose']['moment_first_double_offset'] == 81
    assert wire['valid_cuts_c9.spose']['moment_frame_bytes'] == 298 and wire['valid_cuts_c9.spose']['moment_first_double_offset'] == 82
    actual_moments = results['mixed_transient']['frames'][1]['elastic_moments']['moments']  # One positive accepted literal cut population.
    assert actual_moments[4] == {'valid': True, 'moment_N_Nm': [0.0, 0.0, -12.0]}
    assert actual_moments[6] == {'valid': True, 'moment_N_Nm': [0.0, 0.0, 0.0]}
    assert bits(actual_moments[5]['moment_N_Nm'][0]) == bits(-0.0) and bits(actual_moments[5]['moment_N_Nm'][2]) == bits(-0.0)
    assert results['valid_masked.spose']['frames'][1]['elastic_moments']['moments'][2] == {'valid': False, 'moment_N_Nm': [0.0, 0.0, 0.0]}

    # Retained failed files may be wire-complete despite publication failure, or expose only earlier whole groups.
    outcomes = {'abandoned': (False, 2), 'close': (True, 3), 'publish': (True, 3), 'collision': (True, 1),
                'old_base_overload': (False, 0), 'old_winch_overload': (False, 0), 'unexpected_moments': (False, 0)}
    outcomes.update({f'bad_packet_{index}': (False, 0) for index in range(32)})  # Mutation ordinal matches the independent C++ scenario.
    for prefix in ('write_', 'short_write_'):  # Outright versus physically torn writes.
        for occurrence in range(1, 16):  # Ten group writes then index/completion/footer bytes.
            outcomes[prefix + str(occurrence)] = (False, 2 if occurrence <= 10 else 3)
    for occurrence in range(1, 9):  # Five group flushes followed by three terminal-record flushes.
        # Failed structural flush follows complete bytes: readable progress can exceed persisted counters.
        outcomes[f'flush_{occurrence}'] = (occurrence == 8, 2 if occurrence <= 4 else 3)
    for name, (complete, count) in outcomes.items():  # Scenario and literal actual-file recovery expectation.
        path = out / (name + '.spose.partial')  # Failure/destruction evidence retained by the recorder.
        result = check(path, allow_incomplete=True, dump_frames=True)  # Recover only complete understood source groups.
        assert result['complete'] == complete and result['validated_frames'] == count, name
        if 'moment_cut_count' in result:
            assert result['grouped_frames'] == count and result['moment_chunks'] == result['pose_chunks'], name
        exact_rows(result['frames'], transient[:count])
        if name != 'collision':
            assert not (out / (name + '.spose')).exists(), name
        results[name], wire[name] = result, inspect_links(path)
    assert (out / 'collision.spose').read_text() == 'do not replace'

    # Every invalid setup, including raw NaN JSON catalogs, must fail before creating either destination.
    rejected = ['workspace_too_small.spose', 'raw_too_small.spose', 'scratch_too_small.spose',
                'valid_opaque_future_planes.spose']  # Unsupported future writer population is an explicit setup failure.
    for name, info in expected['cases'].items():  # Independent catalog contract rejection selection.
        if bad_catalog_case(name, info):
            rejected.append('bad_catalog_' + name)
    for name in rejected:  # Each setup destination must remain entirely absent.
        assert not (out / name).exists() and not (out / (name + '.partial')).exists(), name
    evidence = {'scope': 'Actual moment wire/API checks; no elastic provider, transport or numerical rollback qualification',
                'reader_results': results, 'independent_wire_inspection': wire}  # Complete successful verification receipt.
    (out / 'independent-readback.json').write_text(json.dumps(evidence, indent=2, allow_nan=False), encoding='utf-8')
    print('independent moment recorder readback passed:', out)


if __name__ == '__main__':
    main()
