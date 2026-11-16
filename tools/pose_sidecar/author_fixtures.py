#!/usr/bin/env python3
"""Independently author tiny .spose byte-contract fixtures, without the C++ writer."""
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib

# Explicit fixture output directory; the author replaces named files when run.
ROOT = Path(__file__).resolve().parents[2] / 'fixtures_pose_sidecar'
# Literal independent four-sample world/catalog fixture; not derived from production code.
CATALOG = {
    'catalog_version': 1, 'recording_id': 'independent-asymmetric-fixture',
    'run_id': 'transient-nonzero-start', 'preparation_id': 'fixture-preparation-17',
    'world': {'axes': 'right_handed_xyz', 'length_unit': 'm', 'orientation': 'hamilton_wxyz_local_to_world'},
    'occurrences': [
        {'id': 10, 'parent_id': None, 'path': '/bench', 'kind': 'assembly'},
        {'id': 21, 'parent_id': 10, 'path': '/bench/blade', 'kind': 'flexible'},
        {'id': 35, 'parent_id': 10, 'path': '/bench/foundation', 'kind': 'anchor'}],
    'samples': [
        {'id': 0, 'kind': 'structural_node', 'occurrence_id': 21, 'material_coordinate_m': 0.0, 'side': 'none'},
        {'id': 1, 'kind': 'structural_node', 'occurrence_id': 21, 'material_coordinate_m': 5.0, 'side': 'none'},
        {'id': 2, 'kind': 'body_port', 'occurrence_id': 21, 'port_id': 'offset_saddle'},
        {'id': 3, 'kind': 'anchor_port', 'occurrence_id': 35, 'port_id': 'winch_eye'}],
    'segments': [{'id': 0, 'occurrence_id': 21, 'sample_ids': [0, 1]}],
    'port_aliases': [{'scope_id': 10, 'alias': 'saddle', 'sample_id': 2}],
    'connections': [{'id': 5, 'kind': 'distance', 'scope_id': 10, 'junction_id': 'bench_junction_A',
                     'first_endpoint_id': '/bench/blade:offset_saddle', 'second_endpoint_id': '/bench/foundation:winch_eye',
                     'first_sample_id': 2, 'second_sample_id': 3}]
}
# Binary64 sine/cosine of a quarter turn used in unit-quaternion fixtures.
Q = math.sqrt(0.5)
# Asymmetric xyz/wxyz world poses; the last frame includes an equivalent negated quaternion.
POSES = [
    [[1, 2, 3, Q, 0, 0, Q], [1, 2, 8, 1, 0, 0, 0],
     [2, 2, 8, Q, Q, 0, 0], [-4, 6, 1, 1, 0, 0, 0]],
    [[1, 2, 3, Q, 0, 0, Q], [1.1, 2.2, 8.3, 1, 0, 0, 0],
     [2.1, 2.2, 8.3, Q, Q, 0, 0], [-4, 6, 1, 1, 0, 0, 0]],
    [[1, 2, 3, Q, 0, 0, Q], [1.25, 2.5, 8.75, 1, 0, 0, 0],
     [2.25, 2.5, 8.75, -Q, -Q, 0, 0], [-4, 6, 1, 1, 0, 0, 0]]
]
# Sparse accepted-state/time stamps and their exact pose rows.
FRAMES = [
    {'stamp': {'ordinal': 0, 'analysis': 1, 'events': 2, 'time': 1.25, 'state_id': 0, 'load_factor': 0.0}, 'poses': POSES[0]},
    {'stamp': {'ordinal': 1, 'analysis': 1, 'events': 4, 'time': 1.29, 'state_id': 4, 'load_factor': 0.0}, 'poses': POSES[1]},
    {'stamp': {'ordinal': 2, 'analysis': 1, 'events': 8, 'time': 1.32, 'state_id': 7, 'load_factor': 0.0}, 'poses': POSES[2]}]

def checksum(data):
    """Return unsigned CRC-32 for borrowed bytes-like data."""
    # Calculate the stdlib CRC and constrain it to the four-byte wire field.
    return zlib.crc32(data) & 0xffffffff

def crc_header(raw, at):
    """Return owned header raw with the CRC field at byte offset at repaired.

    Copies raw into mutable storage, so the caller input remains unchanged."""
    # Copy the header, zero its checksum slot, compute CRC, and return immutable bytes.
    # Owned header/stamp bytes being checksummed or assembled.
    raw = bytearray(raw)
    struct.pack_into('<I', raw, at, 0)
    struct.pack_into('<I', raw, at, checksum(raw))
    return bytes(raw)

def frame_bytes(frame):
    """Encode fixture frame into canonical stamp plus dense world-pose bytes.

    frame owns stamp/poses dictionaries; returns new little-endian bytes. This tiny
    author assumes its supplied dictionary layout, leaving semantic checks to the checker."""
    # Read stamp and pose rows, encode the 64-byte stamp, then append each seven-double pose.
    # Borrowed frame stamp dictionary.
    s = frame['stamp']
    # Borrowed dense pose rows for the frame.
    poses = frame['poses']
    # Owned header/stamp bytes being checksummed or assembled.
    raw = struct.pack('<QIIdQdQQQ', s['ordinal'], s['analysis'], s['events'], s['time'],
                      s['state_id'], s['load_factor'], len(poses), 0, 0)
    # One xyz plus wxyz pose encoded as seven doubles.
    return raw + b''.join(struct.pack('<7d', *pose) for pose in poses)

def chunk(kind, sequence, records, first, payload, previous=0, *, flags=1, stream=None, encoding=0):
    """Return a canonical raw fixture chunk header followed by payload.

    kind/sequence/records/first define ordering; previous links pose chunks by byte
    offset. flags, stream and encoding permit deliberate unknown optional cases.
    stream=None selects the normal stream for kind. Payload bytes are copied."""
    # Select the stream default and encode exact header extents/CRC fields.
    # Repair the header checksum and append the independently authored payload.
    # Selected stream identifier, defaulted only for the canonical chunk kind.
    stream = (1 if kind == 1 else 0) if stream is None else stream
    # Owned header/stamp bytes being checksummed or assembled.
    raw = struct.pack('<4sHHIIIIQQQQQIIQ', b'PCHK', kind, flags, 80, encoding, stream, 1,
                      sequence, records, first, len(payload), len(payload), checksum(payload), 0, previous)
    return crc_header(raw, 68) + payload

def make_file(frames, catalog=CATALOG, *, optional=False, outcomes=(1, 1)):
    """Return (owned complete sidecar bytes, pose-chunk offsets) for frames.

    catalog defaults to the literal four-sample fixture; this helper keeps that
    bootstrap sample count. optional inserts an unknown optional stream; outcomes
    supplies run/capture completion codes. Entire tiny fixtures are materialized."""
    # Encode bootstrap/catalog and batches of at most two fixture frames.
    # Optionally insert an unknown stream, tracking pose links and global sequence.
    # Append seek index, completion and footer, then return bytes and mutation offsets.
    # Owned UTF-8 JSON catalog bytes in deterministic compact spelling.
    cat = json.dumps(catalog, separators=(',', ':'), ensure_ascii=False, allow_nan=False).encode('utf-8')
    # Bootstrap bytes before checksum repair.
    header = struct.pack('<8sHHIQQQQIIQ', b'SRNPOSE\0', 1, 0, 64, 0, len(cat), 4, 4096, checksum(cat), 0, 0)
    # Mutable complete-file accumulator for these tiny independent fixtures.
    data = bytearray(crc_header(header, 52) + cat)
    # Next global chunk sequence number.
    # Number of emitted pose chunks.
    seq = batches = 0
    # First pose-chunk byte offset, zero when no frames exist.
    # Last pose-chunk byte offset used by the backward link.
    first_pose = last_pose = 0
    # Owned pose-chunk offsets exposed for isolated negative mutations.
    chunk_offsets = []
    # First frame index of this batch of at most two.
    for begin in range(0, len(frames), 2):
        if optional and begin == 2:
            data += chunk(100, seq, 1, 0, b'optional independently typed payload', flags=0, stream=42, encoding=17)
            seq += 1
        # Current pose-chunk byte offset in the assembled file.
        offset = len(data)
        chunk_offsets.append(offset)
        # Concatenated encoded frames for this tiny batch.
        # Borrowed fixture frame in the current batch.
        payload = b''.join(frame_bytes(f) for f in frames[begin:begin+2])
        data += chunk(1, seq, min(2, len(frames)-begin), begin, payload, last_pose)
        # First pose-chunk byte offset, zero when no frames exist.
        first_pose = first_pose or offset
        # Last pose-chunk byte offset used by the backward link.
        last_pose = offset
        seq += 1
        batches += 1
    # Seek-index chunk byte offset.
    index = len(data)
    data += chunk(2, seq, 1, 0, struct.pack('<4Q', batches, len(frames), first_pose, last_pose))
    # Completion chunk byte offset.
    completion = len(data)
    data += chunk(3, seq+1, 1, 0, struct.pack('<4I6Q', outcomes[0], outcomes[1], 1, 0,
        len(frames), len(frames), batches, len(frames)-1 if frames else (1<<64)-1, index, 0))
    data += crc_header(struct.pack('<8sQQII', b'POSEEND\0', index, completion, 0, 0), 24)
    return bytes(data), chunk_offsets

def main():
    """Write independent fixture bytes and expected.json into ROOT; return None.

    ROOT is the maintained pose fixture directory. Running this author explicitly
    replaces those named artifacts; documentation and ordinary checker runs do not."""
    # Build transient/static/prepared and interrupted/optional/failure variants.
    # Introduce isolated required-feature and checksum failures.
    # Write artifacts, exact hashes and expected decoded-frame evidence.
    ROOT.mkdir(parents=True, exist_ok=True)
    (ROOT/'catalog.json').write_text(json.dumps(CATALOG, indent=2) + '\n', encoding='utf-8')
    # Map of fixture filename to exact complete or intentionally damaged bytes.
    files = {}
    # Complete baseline transient byte fixture.
    # Baseline pose-chunk offsets used to truncate or corrupt one place.
    transient, offsets = make_file(FRAMES)
    files['transient.spose'] = transient
    # One-frame terminal static fixture.
    static = [{'stamp': {'ordinal': 0, 'analysis': 2, 'events': 8, 'time': 0.0,
                         'state_id': 3, 'load_factor': 1.0}, 'poses': POSES[2]}]
    files['static.spose'] = make_file(static)[0]
    # One-frame prepared-only fixture.
    prepared = [{'stamp': {'ordinal': 0, 'analysis': 0, 'events': 1, 'time': 1.25,
                           'state_id': 0, 'load_factor': 0.0}, 'poses': POSES[0]}]
    files['prepared.spose'] = make_file(prepared, outcomes=(0, 1))[0]
    files['incomplete.spose.partial'] = transient[:offsets[1]+80+17]
    files['optional_stream.spose'] = make_file(FRAMES, optional=True)[0]
    files['failed_run_complete.spose'] = make_file(FRAMES[:2], outcomes=(2, 3))[0]
    # Mutable baseline copy receiving an unsupported required feature.
    feature = bytearray(transient)
    struct.pack_into('<Q', feature, 16, 1)
    feature[:64] = crc_header(feature[:64], 52)
    files['unsupported_required_feature.spose'] = bytes(feature)
    # Mutable baseline copy receiving a payload-bit corruption.
    corrupt = bytearray(transient)
    corrupt[offsets[0]+80+70] ^= 1
    files['bad_payload_checksum.spose'] = bytes(corrupt)
    # Owned independent decoded values and expected checker outcomes.
    expected = {
        'description': 'Independent literal little-endian byte fixtures; not generated by production writer.',
        'sample_count': 4,
        'transient_frames': FRAMES, 'static_frames': static, 'prepared_frames': prepared,
        'cases': {
            'transient.spose': {'valid': True, 'complete': True, 'frames': 3, 'pose_chunks': 2},
            'static.spose': {'valid': True, 'complete': True, 'frames': 1, 'pose_chunks': 1},
            'prepared.spose': {'valid': True, 'complete': True, 'frames': 1, 'pose_chunks': 1},
            'incomplete.spose.partial': {'valid': True, 'complete': False, 'frames': 2, 'pose_chunks': 1, 'allow_incomplete': True},
            'optional_stream.spose': {'valid': True, 'complete': True, 'frames': 3, 'pose_chunks': 2, 'optional_chunks': 1},
            'failed_run_complete.spose': {'valid': True, 'complete': True, 'frames': 2, 'run_outcome': 2, 'capture_outcome': 3},
            'unsupported_required_feature.spose': {'valid': False, 'error_contains': 'unsupported version/required feature'},
            'bad_payload_checksum.spose': {'valid': False, 'error_contains': 'payload checksum mismatch'}
        }
    }
    # Fixture filename receiving bytes and manifest metadata.
    # Mutable complete-file accumulator for these tiny independent fixtures.
    for name, data in files.items():
        (ROOT/name).write_bytes(data)
        expected['cases'][name].update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    (ROOT/'expected.json').write_text(json.dumps(expected, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    # Fixture filename receiving bytes and manifest metadata.
    # Mutable complete-file accumulator for these tiny independent fixtures.
    print(json.dumps({name: len(data) for name, data in files.items()}, indent=2))

if __name__ == '__main__':
    main()
