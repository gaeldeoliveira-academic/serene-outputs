"""Run the F-enabled C++ producer and independently inspect its actual recordings.

The executable consumes independently authored literal catalog/row fixtures.
This wrapper imports only the separate reader, never recorder encoding helpers
or the fixture author. It compares all F/physical/winch binary64 representations,
reviews structural backlinks and recovers only complete understood groups.
Workspace counters are public C++ accounting assertions, not RSS measurements.
"""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import uuid


def renumber(rows):
    """Copy borrowed literal rows, assigning only the recorder-owned dense ordinal."""
    # Reordering source states must not change accepted source identities or physical times.
    result = deepcopy(rows)  # Owned oracle for this capture subset.
    for ordinal, row in enumerate(result):
        row['stamp']['ordinal'] = ordinal
    return result


def bits(value):
    """Return the exact binary64 representation of a finite decoded or expected scalar."""
    return struct.pack('<d', value)


def exact_rows(actual, expected):
    """Assert full semantic rows and all payload scalar bits against independent literals.

    JSON numeric equality alone loses the sign of zero. This additional comparison
    covers each valid or unavailable F component and the unchanged other populations.
    """
    # First compare structure, identities and masks, then check representation-preserving values.
    assert actual == expected, 'source rows changed'
    for observed, source in zip(actual, expected):
        for key in ('time', 'load_factor'):
            assert bits(observed['stamp'][key]) == bits(source['stamp'][key]), key
        for left, right in zip(observed['poses'], source['poses']):
            assert b''.join(map(bits, left)) == b''.join(map(bits, right)), 'physical bits changed'
        for left, right in zip(observed.get('features', []), source.get('features', [])):
            for actual_point, expected_point in zip(left['world_xyz_m'], right['world_xyz_m']):
                assert b''.join(map(bits, actual_point)) == b''.join(map(bits, expected_point)), 'winch bits changed'
        for left, right in zip(observed['fluid_poses']['samples'], source['fluid_poses']['samples']):
            assert b''.join(map(bits, left['world_pose'])) == b''.join(map(bits, right['world_pose'])), 'F bits changed'


def inspect_links(path):
    """Inspect bounded producer bytes for group order, copied stamps and structural-only links.

    The separate checker handles CRC, catalog and full semantic validation. This
    deliberately small wire walk independently checks producer framing without
    importing its writer implementation. A truncated final record is not decoded.
    Return only constant-size inspection counters for the evidence report.
    """
    # Traverse complete records; retain at most one three-chunk group plus structural link facts.
    raw = path.read_bytes()  # Test scenarios are explicitly bounded, including the1001-row run.
    catalog_size = struct.unpack_from('<Q', raw, 24)[0]
    catalog = json.loads(raw[64:64 + catalog_size])
    count = len(catalog['fluid_poses']['samples'])
    fluid_width = 80 + (count + 7) // 8 + 56 * count
    widths = {5: fluid_width, 4: 288, 1: 512}
    order = [5, 4, 1] if 'winch_geometry' in catalog else [5, 1]
    offset = 64 + catalog_size  # First possible chunk, following exact catalog bytes.
    sequence = frames = pose_chunks = first_pose = previous = 0
    pending = []  # Complete known chunks in the current bounded group.
    while offset + 80 <= len(raw) and raw[offset:offset + 4] == b'PCHK':
        header = struct.unpack_from('<4sHHIIIIQQQQQIIQ', raw, offset)
        kind, records, first, size, previous_link = header[1], header[8], header[9], header[10], header[14]
        if offset + 80 + size > len(raw):
            break
        assert header[7] == sequence
        sequence += 1
        payload = memoryview(raw)[offset + 80:offset + 80 + size]
        if kind in widths:
            assert kind == order[len(pending)], 'writer group order'
            assert first == frames and size == records * widths[kind], 'writer count/width'
            if kind != 1:
                assert previous_link == 0, 'overlay must not enter structural links'
            pending.append((kind, records, payload))
            if kind == 1:
                assert previous_link == previous, 'structural backlink'
                assert all(item[1] == records for item in pending), 'group record identity'
                for row in range(records):
                    stamp = bytes(payload[row * 512:row * 512 + 64])
                    for overlay_kind, _, overlay in pending[:-1]:
                        start = row * widths[overlay_kind]
                        assert bytes(overlay[start:start + 64]) == stamp, 'copied source stamp bytes'
                first_pose = first_pose or offset
                previous = offset
                pose_chunks += 1
                frames += records
                pending.clear()
        elif kind == 2:
            assert not pending and list(struct.unpack('<4Q', payload)) == [pose_chunks, frames, first_pose, previous]
        elif kind == 3:
            assert not pending
            assert struct.unpack_from('<Q', payload, 24)[0] == frames
            assert struct.unpack_from('<Q', payload, 32)[0] == pose_chunks
        else:
            raise AssertionError('unexpected producer chunk')
        offset += 80 + size
    return {'complete_wire_groups': pose_chunks, 'complete_wire_frames': frames,
            'pending_known_chunks': len(pending), 'fluid_frame_bytes': fluid_width}


def main():
    """Run the requested compiled executable in a fresh directory and verify actual readback.

    --source owns independent fixtures/checker; --output-root receives a new child.
    All errors propagate, preserving this invocation's files for investigation.
    """
    # Resolve explicit inputs; never replace an earlier producer run or fixture corpus.
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--output-root', required=True)
    args = parser.parse_args()
    root = Path(args.source)
    sys.path.insert(0, str(root / 'tools/pose_sidecar'))
    from check_sidecar import check
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')
    out = Path(args.output_root) / ('run-' + stamp + '-' + uuid.uuid4().hex[:8])
    out.parent.mkdir(parents=True, exist_ok=True)
    fixture = root / 'fixtures_pose_fluid'
    subprocess.run([args.exe, str(out), str(fixture / 'catalog.json'),
                    str(fixture / 'expected.json')], check=True)
    expected = json.loads((fixture / 'expected.json').read_text(encoding='utf-8'))
    transient = expected['transient_frames']
    sources = {name: expected[name + '_frames'] for name in ('static', 'prepared', 'masked', 'all_unavailable')}
    sources.update(transient=transient, without_winches=expected['no_winch_frames'],
                   initial_disabled=renumber(transient[1:]),
                   prepared_then_transient=renumber([expected['prepared_frames'][0], transient[0], transient[2]]),
                   workspace_one=transient[:1], raw_one=transient[:1])
    for count in (7, 8, 9):
        sources[f'k{count}'] = expected[f'k{count}_frames']
    signed = deepcopy(transient[:1])
    signed[0]['fluid_poses']['samples'][0]['world_pose'] = [-0.0, 0.0, -0.0, -1.0, -0.0, 0.0, -0.0]
    signed[0]['fluid_poses']['samples'][1]['world_pose'][3] = math.nextafter(.5, 1.0)
    sources['signed_zero'] = signed
    results, wire = {}, {}  # Successful independent results and separate structural byte inspections.
    for name, rows in sources.items():
        path = out / (name + '.spose')
        result = check(path, dump_frames=True)
        assert result['complete'] and result['validated_frames'] == len(rows), name
        assert result['grouped_frames'] == len(rows) and result['fluid_chunks'] == result['pose_chunks'], name
        assert result['fluid_sample_count'] == len(rows[0]['fluid_poses']['samples']), name
        exact_rows(result['frames'], rows)
        results[name], wire[name] = result, inspect_links(path)
    for name in ('empty_failed', 'long_bounded'):
        path = out / (name + '.spose')
        result = check(path)
        assert result['complete']
        assert result['grouped_frames'] == result['validated_frames']
        assert result['fluid_chunks'] == result['feature_chunks'] == result['pose_chunks']
        results[name], wire[name] = result, inspect_links(path)
    assert results['empty_failed']['validated_frames'] == 0
    assert results['empty_failed']['completion']['run'] == 2 and results['empty_failed']['completion']['capture'] == 3
    assert results['long_bounded']['validated_frames'] == 1001 and results['long_bounded']['pose_chunks'] == 501

    # Decode failures only after C++ recorder destruction closes each retained partial file.
    outcomes = {'abandoned': (False, 2), 'close': (True, 3), 'publish': (True, 3), 'collision': (True, 1)}
    outcomes.update({f'bad_packet_{index}': (False, 0) for index in range(29)})
    outcomes.update({name: (False, 0) for name in ('old_base_overload', 'old_winch_overload',
                                                  'unexpected_fluid', 'unexpected_winch')})
    for prefix in ('write_', 'short_write_'):
        for occurrence in range(1, 12):
            outcomes[prefix + str(occurrence)] = (False, 2 if occurrence <= 6 else 3)
    for occurrence in range(1, 7):
        # The third failed flush follows complete pose bytes: readable wire progress may exceed persisted stats.
        outcomes[f'flush_{occurrence}'] = (occurrence == 6, 2 if occurrence <= 2 else 3)
    for name, (complete, count) in outcomes.items():
        path = out / (name + '.spose.partial')
        result = check(path, allow_incomplete=True, dump_frames=True)
        assert result['complete'] == complete and result['validated_frames'] == count, name
        if 'fluid_sample_count' in result:
            assert result['grouped_frames'] == count and result['fluid_chunks'] == result['pose_chunks'], name
        assert result['frames'] == transient[:count], name
        if count:
            exact_rows(result['frames'], transient[:count])
        if name != 'collision':
            assert not (out / (name + '.spose')).exists(), name
        results[name] = result
        if name != 'unexpected_fluid':
            wire[name] = inspect_links(path)
    assert (out / 'collision.spose').read_text() == 'do not replace'
    for name in [*(f'bad_catalog_{index}' for index in range(28)), 'workspace_too_small',
                 'raw_too_small', 'workspace_overflow', 'scratch_too_small', 'count_overflow']:
        assert not (out / (name + '.spose')).exists(), name
        assert not (out / (name + '.spose.partial')).exists(), name

    # Record successful evidence only after full literal, lifecycle and independent wire checks pass.
    evidence = {'reader_results': results, 'independent_wire_inspection': wire}
    (out / 'independent-readback.json').write_text(json.dumps(evidence, indent=2, allow_nan=False), encoding='utf-8')
    print('independent F recorder readback passed:', out)


if __name__ == '__main__':
    main()
