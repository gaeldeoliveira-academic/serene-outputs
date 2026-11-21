"""Run the compiled applied-load recorder and independently inspect its actual files.

Literal source rows come from the independent applied corpus. The wrapper uses
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
        # Check actual source coordinates and densities separately from structural/F endpoint poses.
        for left, right in zip(observed['applied_loads']['attachments'], source['applied_loads']['attachments']):  # Paired attachments.
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
    counts = [item['station_count'] for item in catalog['applied_loads']['attachments']]  # item: attachment in wire order.
    starts, load_width = [], 80  # Per-attachment prefix offsets and total row width after the common prefix.
    for count in counts:  # Current attachment's complete logical station count.
        starts.append(load_width)
        load_width += 96 + (count + 7) // 8 + 48 * count
    widths = {1: 512, 6: load_width}  # Fixed P8 and derived load bytes per row, keyed by chunk kind.
    order = []  # Required known population order for one complete source group.
    if 'fluid_poses' in catalog:
        count = len(catalog['fluid_poses']['samples'])  # F sample count, independent of load station counts.
        widths[5] = 80 + (count + 7) // 8 + 56 * count
        order.append(5)
    order.append(6)
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
            'attachment_prefix_offsets': starts}


def main():
    """Use required --exe/--source/--output-root CLI paths to run and verify every scenario.

    Create a fresh child directory and retain its files for evidence. Return None
    after writing successful readback evidence; argument, process or assertion
    failures propagate without replacing any earlier output directory.
    """
    # Resolve explicit executable/checkout paths and preserve all earlier output directories.
    # Compare full literal values and inspect failed-file recovery independently of writer counters.
    # Write evidence only after all requested assertions have passed.
    parser = argparse.ArgumentParser()  # Explicit driver paths; no inferred build or historical output reuse.
    parser.add_argument('--exe', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--output-root', required=True)
    args = parser.parse_args()  # Executable, source checkout and fresh-output parent chosen by the caller.
    root = Path(args.source)  # Source of the independent checker and literal fixture corpus.
    sys.path.insert(0, str(root / 'tools/pose_sidecar'))
    from check_sidecar import check
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')  # Human-readable UTC portion of the evidence directory.
    out = Path(args.output_root) / ('run-' + stamp + '-' + uuid.uuid4().hex[:8])  # New destination with collision-resistant suffix.
    out.parent.mkdir(parents=True, exist_ok=True)
    fixture = root / 'fixtures_pose_applied'  # Read-only independently authored catalog and complete expected rows.
    subprocess.run([args.exe, str(out), str(fixture / 'catalog.json'), str(fixture / 'expected.json')], check=True)
    expected = json.loads((fixture / 'expected.json').read_text(encoding='utf-8'))  # Literal oracles and fixture classification map.
    transient = expected['transient_frames']  # Common three-row sequence used by success and fault scenarios.
    # Verify published cases against their own oracles, including changed catalogs and station counts.
    positive = ('mixed_transient', 'without_fluid', 'without_winches', 'loads_only', 'first_positive',
                'exact_prior_join', 'zero_time_join', 'zero_end_time', 'prepared', 'static',
                'prepared_then_positive', 'masked', 'all_unavailable', 'signed_zero',
                'multiple_attachments_one_model', 'permuted_station_ranges', 'large_metadata_domains',
                'repeated_consumer_address', 'attachment_k9')
    sources = {name: expected[expected['cases'][name + '.spose']['frames_key']] for name in positive}  # name selects a variant's literal rows.
    sources.update(workspace_one=transient[:1], raw_one=transient[:1])
    results, wire = {}, {}  # Semantic reader results and separately walked wire summaries, keyed by scenario.
    for name, rows in sources.items():  # Scenario stem and its full expected source rows.
        path = out / (name + '.spose')  # Published sidecar emitted by the executable.
        result = check(path, dump_frames=True)  # Fully aware independent reader result with decoded values.
        assert result['complete'] and result['validated_frames'] == result['grouped_frames'] == len(rows), name
        assert result['applied_attachment_count'] == 2 and result['applied_chunks'] == result['pose_chunks'], name
        assert result['applied_station_count'] == sum(len(item['samples']) for item in rows[0]['applied_loads']['attachments']), name  # item: literal attachment.
        exact_rows(result['frames'], rows)
        results[name], wire[name] = result, inspect_links(path)
    for name in ('empty_failed', 'long_bounded'):  # Count-focused cases avoid retaining an unnecessary long frame dump.
        path = out / (name + '.spose')  # Published empty or duration-bound artifact.
        result = check(path)  # Semantic/counter summary without decoded frame retention.
        assert result['complete'] and result['validated_frames'] == result['grouped_frames']
        assert result['applied_chunks'] == result['fluid_chunks'] == result['feature_chunks'] == result['pose_chunks']
        results[name], wire[name] = result, inspect_links(path)
    assert results['empty_failed']['validated_frames'] == 0
    assert results['empty_failed']['completion']['run'] == 2 and results['empty_failed']['completion']['capture'] == 3
    assert results['long_bounded']['validated_frames'] == 1001 and results['long_bounded']['pose_chunks'] == 501

    # Initial packets are unavailable; positive packets retain actual unsaved source points/provenance.
    a, b = transient[1]['applied_loads']['attachments'], transient[2]['applied_loads']['attachments']  # Two positive source intervals.
    assert a[0]['interval'] == a[1]['interval'] and b[0]['interval'] == b[1]['interval']
    assert a[0]['interval']['publication'] > b[0]['interval']['publication']
    assert a[0]['samples'][0]['source_position_world_m'] != transient[1]['fluid_poses']['samples'][0]['world_pose'][:3]
    assert wire['mixed_transient']['attachment_prefix_offsets'] == [80, 417]
    assert wire['mixed_transient']['load_frame_bytes'] == 658 and wire['attachment_k9']['load_frame_bytes'] == 947

    # Map each retained partial file to honest wire completion and recoverable source-row count.
    outcomes = {'abandoned': (False, 2), 'close': (True, 3), 'publish': (True, 3), 'collision': (True, 1),
                'bad_zero_join': (False, 0), 'bad_zero_end': (False, 0),
                'old_base_overload': (False, 0), 'old_winch_overload': (False, 0), 'unexpected_loads': (False, 0)}
    # index selects the C++ mutation scenario; buffered predecessors are not complete wire groups.
    outcomes.update({f'bad_packet_{index}': (False, 0) for index in range(36)})
    outcomes.update({f'bad_unavailable_{index}': (False, 0) for index in range(6)})
    outcomes.update({f'bad_completed_{index}': (False, 2) for index in range(7)})
    for prefix in ('write_', 'short_write_'):  # Outright and physically torn write families.
        for occurrence in range(1, 14):  # One-based finalization write boundary selected in C++.
            outcomes[prefix + str(occurrence)] = (False, 2 if occurrence <= 8 else 3)
    for occurrence in range(1, 8):  # One-based finalization flush boundary, after its bytes were written.
        # The fourth failed flush follows complete structural bytes, so readable progress can exceed persisted stats.
        outcomes[f'flush_{occurrence}'] = (occurrence == 7, 2 if occurrence <= 3 else 3)
    for name, (complete, count) in outcomes.items():  # Scenario and expected actual wire facts, independent of writer counters.
        path = out / (name + '.spose.partial')  # Retained evidence after failure or abandonment.
        result = check(path, allow_incomplete=True, dump_frames=True)  # Recover only jointly validated source groups.
        assert result['complete'] == complete and result['validated_frames'] == count, name
        if 'applied_attachment_count' in result:
            assert result['grouped_frames'] == count and result['applied_chunks'] == result['pose_chunks'], name
        exact_rows(result['frames'], transient[:count])
        if name != 'collision':
            assert not (out / (name + '.spose')).exists(), name
        results[name] = result
        if name != 'unexpected_loads':
            wire[name] = inspect_links(path)
    assert (out / 'collision.spose').read_text() == 'do not replace'
    # Setup rejection must precede creating either a final file or a partial file.
    rejected = ['workspace_too_small.spose', 'raw_too_small.spose', 'scratch_too_small.spose',
                'bad_catalog_binary64_coordinate.spose']
    for name, case in expected['cases'].items():  # Independent fixture filename and its declared rejection category.
        if name != 'missing_descriptor.spose' and case.get('rejection_reason') in (
                'applied catalog contract', 'model attachment/range uniqueness'):
            rejected.append('bad_catalog_' + name)
    for name in rejected:  # Destination for one deliberately inadmissible setup.
        assert not (out / name).exists() and not (out / (name + '.partial')).exists(), name
    # Save the complete readback result only after all semantic, representation and failure checks pass.
    evidence = {'scope': 'Actual output wire/API checks, not numerical candidate rollback qualification',
                'reader_results': results, 'independent_wire_inspection': wire}
    (out / 'independent-readback.json').write_text(json.dumps(evidence, indent=2, allow_nan=False), encoding='utf-8')
    print('independent applied recorder readback passed:', out)


if __name__ == '__main__':
    main()
