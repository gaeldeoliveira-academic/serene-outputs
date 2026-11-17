"""Run the feature-enabled C++ recorder and independently read its actual files.

Arguments select the compiled --exe, checkout --source and new --output-root.
Each invocation owns one fresh child directory. The independent checker and
literal fixture expectations establish paired payloads, representation bytes,
recoverable prefixes and completed-but-unpublished temporary files. Requested
workspace counters are tested by C++; they are not process-memory measurements.
"""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import subprocess
import sys
import uuid


def renumber(rows):
    """Return owned expected rows with consecutive recorder ordinals.

    rows borrows independently authored source states. Only the recorder-owned
    ordinal changes when those states are captured in another order/subset.
    """
    # Copy source facts, then assign the consecutive output identity.
    result = deepcopy(rows)  # Owned expected states, leaving original fixtures untouched.
    for ordinal, row in enumerate(result):  # Current output ordinal and copied source state.
        row['stamp']['ordinal'] = ordinal
    return result


def bits(value):
    """Return little-endian binary64 bytes of value, preserving a zero's sign."""
    # Encode only for representation comparison; do not normalize decoded numbers.
    return struct.pack('<d', value)


def main():
    """Create fresh evidence, run the compiled producer and assert independent readback.

    No result is recorded as successful before every assertion passes. Arguments
    and I/O errors propagate; existing run directories and fixture bytes survive.
    """
    # Resolve tools and fresh output ownership, then run the actual C++ producer.
    # Verify completed rows/bytes, interrupted pairs and unpublished complete files.
    # Save this invocation's independent evidence only after all checks succeed.
    parser = argparse.ArgumentParser()  # Public invocation path arguments.
    parser.add_argument('--exe', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--output-root', required=True)
    args = parser.parse_args()  # Explicit executable, checkout and evidence parent.
    root = Path(args.source)  # Checkout containing the independent checker and fixtures.
    sys.path.insert(0, str(root / 'tools/pose_sidecar'))
    from check_sidecar import check
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')  # Human-readable run UTC.
    out = Path(args.output_root) / ('run-' + stamp + '-' + uuid.uuid4().hex[:8])  # Fresh owned child.
    out.parent.mkdir(parents=True, exist_ok=True)
    fixture = root / 'fixtures_pose_winch'  # Read-only independently authored source facts.
    subprocess.run([args.exe, str(out), str(fixture / 'catalog.json'),
                    str(fixture / 'expected.json')], check=True)
    expected = json.loads((fixture / 'expected.json').read_text(encoding='utf-8'))  # Exact row oracles.
    results = {}  # Owned successful checker results keyed by producer scenario.
    detailed = ('transient', 'static', 'prepared_then_transient', 'initial_disabled',
                'consistency_boundary', 'scaled_consistency_boundary', 'signed_zero',
                'shared_boundary')  # Small scenarios whose complete rows are retained.
    for name in (*detailed, 'empty_failed', 'byte_limit', 'long_bounded'):  # Completed producer file.
        result = check(out / (name + '.spose'), dump_frames=name in detailed)  # Independently decoded bytes.
        assert result['complete'] and result['feature_count'] == 2, name
        assert result['paired_frames'] == result['validated_frames'], name
        assert result['feature_chunks'] == result['pose_chunks'], name
        results[name] = result

    # Exact source coordinates include the algebraic C and zero-tension arrangement.
    transient = expected['transient_frames']  # Independent source rows, never writer-decoded expectations.
    assert results['transient']['frames'] == transient, 'borrowed source copy or exact packet changed'
    assert results['static']['frames'] == expected['static_frames'], 'static factor/phase/geometry changed'
    assert results['prepared_then_transient']['frames'] == renumber(
        [expected['prepared_frames'][0], transient[0], transient[2]]), 'prepared translation or reset changed'
    assert results['initial_disabled']['frames'] == renumber(transient[1:]), 'positive first accepted state rejected'
    assert results['transient']['frames'][0]['features'][1]['geometry_status'] == 1
    assert results['empty_failed']['completion']['run'] == 2
    assert results['empty_failed']['completion']['capture'] == 3
    assert results['empty_failed']['validated_frames'] == results['byte_limit']['validated_frames'] == 0
    assert results['long_bounded']['validated_frames'] == 1001
    assert results['long_bounded']['pose_chunks'] == results['long_bounded']['feature_chunks'] == 501

    # Check separate unchanged representations, including inclusive and scaled boundaries.
    tolerance = 2.0 ** -46  # Exact 64*2^-52 for the unit infinity-norm scale.
    boundary = results['consistency_boundary']['frames'][0]  # Both independently serialized populations.
    assert bits(boundary['poses'][2][0]) == bits(0.0)
    assert bits(boundary['features'][0]['world_xyz_m'][0][0]) == bits(tolerance)
    scaled = results['scaled_consistency_boundary']['frames'][0]  # Point norm128 controls every coordinate.
    assert scaled['poses'][2][:3] == [128, 0, 0]
    assert bits(scaled['features'][0]['world_xyz_m'][0][1]) == bits(2.0 ** -39)
    zeros = results['signed_zero']['frames'][0]  # Numerically equal but distinct original zero signs.
    for axis in range(3):  # Each world coordinate preserves its own population's representation.
        assert bits(zeros['poses'][2][axis]) == bits(0.0)
        assert bits(zeros['features'][0]['world_xyz_m'][0][axis]) == bits(-0.0)
    shared = results['shared_boundary']['frames'][0]  # Two forces sharing the canonical physical A/B/D.
    assert bits(shared['poses'][2][0]) == bits(0.0)
    assert bits(shared['features'][0]['world_xyz_m'][0][0]) == bits(tolerance)
    assert bits(shared['features'][1]['world_xyz_m'][0][0]) == bits(-tolerance)

    # A pending feature batch or truncated pose never expands the accepted paired prefix.
    partial_counts = {name: 2 for name in ('abandoned', 'fault_1', 'fault_2',
                                          'between_pair', 'short_feature', 'short_pose')}  # Complete prior pairs.
    partial_counts.update({f'bad_packet_{index}': 0 for index in range(11)})
    partial_counts.update(shared_excess=0, missing_feature_overload=0, unexpected_feature_overload=0)
    for name, count in partial_counts.items():  # Retained failed/unfinalized scenario and accepted wire prefix.
        result = check(out / (name + '.spose.partial'), allow_incomplete=True, dump_frames=True)
        assert not result['complete'] and result['validated_frames'] == count, name
        assert 'completion' not in result, name
        assert not (out / (name + '.spose')).exists(), name
        if 'feature_count' in result:
            assert result['paired_frames'] == count, name
            assert result['feature_chunks'] == result['pose_chunks'], name
        if count:
            assert result['frames'] == transient[:count], name
        results[name] = result

    # Complete wire bytes retained after late I/O/publication failures are still unpublished.
    for name, count in (('fault_3', 3), ('fault_4', 3), ('collision', 1)):  # Late failure and expected full payload.
        result = check(out / (name + '.spose.partial'), dump_frames=True)
        assert result['complete'] and result['paired_frames'] == count, name
        assert result['frames'] == transient[:count], name
        if name != 'collision':
            assert not (out / (name + '.spose')).exists(), name
        results[name] = result
    assert (out / 'collision.spose').read_text() == 'do not replace'
    (out / 'independent-readback.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('independent feature recorder readback passed:', out)


if __name__ == '__main__':
    main()
