#!/usr/bin/env python3
"""Independent F-reader qualification using frozen bytes and bounded test rewrites.

No fixture author or production recorder is imported. The frozen manifest owns
the positive geometry oracle; local wire helpers only exercise resource/order
admission and streaming lifetime, never compute expected fluid geometry.
"""
import copy
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import tracemalloc
import unittest
from unittest.mock import patch
import zlib

from check_sidecar import (check, InvalidSidecar, IncompleteSidecar,
                           validate_fluid_catalog, MAX_PENDING_RAW,
                           MAX_DUMP_BYTES, MAX_DUMP_FRAMES)

ROOT = Path(__file__).resolve().parents[2] / 'fixtures_pose_fluid'  # Immutable independent corpus.
HEADER = struct.Struct('<4sHHIIIIQQQQQIIQ')  # Local documented wire header, independent of reader structs.


def checksum(raw):
    """Return the standard unsigned CRC32 for borrowed bytes without modifying them."""
    return zlib.crc32(raw) & 0xffffffff


def repair(raw, offset):
    """Return owned header bytes with their four-byte checksum at offset replaced."""
    data = bytearray(raw)  # Test-owned mutable header copy.
    struct.pack_into('<I', data, offset, 0)
    struct.pack_into('<I', data, offset, checksum(data))
    return bytes(data)


def prefix_and_chunks(raw):
    """Split complete fixture bytes into bootstrap/catalog prefix and chunk triples.

    Returns (prefix, [(offset, header_tuple, payload), ...]); ignores only the
    final footer. Callers use this helper on already hash-bound positive files.
    """
    offset = 64 + struct.unpack_from('<Q', raw, 24)[0]  # First chunk after the bounded catalog.
    prefix = raw[:offset]  # Owned immutable bootstrap and catalog.
    chunks = []  # Small test-owned fixture description, outside reader measurements.
    while raw[offset:offset + 4] == b'PCHK':
        header = HEADER.unpack_from(raw, offset)  # Original scalar wire fields.
        payload = raw[offset + 80:offset + 80 + header[10]]  # Original stored bytes.
        chunks.append((offset, header, payload))
        offset += 80 + header[10]
    return prefix, chunks


def chunk(kind, stream, sequence, ordinal, payload, *, previous=0, records=1, flags=0):
    """Encode a local test chunk from explicit header facts and borrowed payload.

    This builds no geometry: source payloads remain frozen fixture bytes except
    for clearly isolated stamp mutations in the duration test.
    """
    header = HEADER.pack(b'PCHK', kind, flags, 80, 0, stream, 1,
                         sequence, records, ordinal, len(payload), len(payload),
                         checksum(payload), 0, previous)
    return repair(header, 68) + payload


class ReadProbe(io.BytesIO):
    """Test-only byte stream recording read extents, with ordinary context lifetime."""
    def __init__(self, raw):
        """Own the input bytes and an initially empty list of (offset, size) reads."""
        super().__init__(raw)
        self.reads = []  # Read requests, checked after the stream closes.

    def read(self, size=-1):
        """Record the requested extent before forwarding the ordinary bytes read."""
        self.reads.append((self.tell(), size))
        return super().read(size)


class FluidCheckerTests(unittest.TestCase):
    """Own immutable oracles and temporary inputs for F-specific reader checks.

    All methods use the public checker as a separate consumer. No fixture or
    production source is modified, and no solver/build qualification is implied.
    """
    @classmethod
    def setUpClass(cls):
        """Read the frozen manifest once, retaining only independent expected values."""
        cls.expected = json.loads((ROOT / 'expected.json').read_text(encoding='utf-8'))

    def inspect(self, raw, **options):
        """Check owned test bytes through a temporary file and return the result."""
        with tempfile.TemporaryDirectory(prefix='serene-f-checker-') as temporary:
            path = Path(temporary) / 'test.spose'  # Only this test owns the temporary input.
            path.write_bytes(raw)
            return check(path, **options)

    def assert_frames(self, actual, expected):
        """Compare source identities and every physical/F binary64 bit without tolerance."""
        self.assertEqual(actual, expected)
        for got, wanted in zip(actual, expected):
            for pose, reference in zip(got['poses'], wanted['poses']):
                self.assertEqual(struct.pack('<7d', *pose), struct.pack('<7d', *reference))
            for sample, reference in zip(got['fluid_poses']['samples'], wanted['fluid_poses']['samples']):
                self.assertEqual(sample['valid'], reference['valid'])
                self.assertEqual(struct.pack('<7d', *sample['world_pose']),
                                 struct.pack('<7d', *reference['world_pose']))
            for feature, reference in zip(got.get('features', []), wanted.get('features', [])):
                values = [value for point in feature['world_xyz_m'] for value in point]
                expected_values = [value for point in reference['world_xyz_m'] for value in point]
                self.assertEqual(struct.pack('<12d', *values), struct.pack('<12d', *expected_values))

    def test_frozen_independent_corpus(self):
        """Bind all 71 fixtures to hashes and require exact outcomes, including torn groups."""
        self.assertEqual(len(self.expected['cases']), 71)
        for name, wanted in self.expected['cases'].items():
            with self.subTest(name=name):
                path = ROOT / name  # Read-only independently authored fixture.
                raw = path.read_bytes()
                self.assertEqual(len(raw), wanted['bytes'])
                self.assertEqual(hashlib.sha256(raw).hexdigest(), wanted['sha256'])
                if not wanted['valid']:
                    with self.assertRaises(InvalidSidecar) as caught:
                        check(path, allow_incomplete=True)
                    self.assertNotIsInstance(caught.exception, IncompleteSidecar)
                    continue
                result = check(path, allow_incomplete=True, dump_frames=True)
                self.assertEqual(result['complete'], wanted['complete'])
                self.assertEqual(result['validated_frames'], wanted['frames'])
                self.assertEqual(result['grouped_frames'], wanted['frames'])
                self.assertEqual(result['fluid_chunks'], result['pose_chunks'])
                if 'feature_chunks' in result:
                    self.assertEqual(result['feature_chunks'], result['pose_chunks'])
                self.assert_frames(result['frames'], self.expected[wanted['frames_key']][:wanted['frames']])
                if not wanted['complete']:
                    self.assertNotIn('completion', result)
                    with self.assertRaises(IncompleteSidecar):
                        check(path)

    def test_unaligned_k7_k8_k9_and_mask_boundaries(self):
        """Decode byte81/82 rows directly, preserving all validity bits and quaternion signs."""
        for count in (7, 8, 9):
            with self.subTest(count=count):
                result = check(ROOT / f'fluid_k{count}.spose', dump_frames=True)
                self.assertEqual(result['fluid_sample_count'], count)
                self.assert_frames(result['frames'], self.expected[f'k{count}_frames'])
                expected_charge = (64 + 56 * result['sample_count']) + 80 + 57 * count
                if 'feature_count' in result:
                    expected_charge += 80 + 104 * result['feature_count']
                self.assertEqual(result['logical_retained_frame_bytes'], expected_charge)
                self.assertEqual(result['logical_retained_bytes'], expected_charge * result['validated_frames'])

    def test_catalog_additional_metadata_and_resolved_side(self):
        """Reject bad UTF8/NUL/finite metadata and every unresolved or mistyped identity."""
        catalog = check(ROOT / 'without_winches.spose')['catalog']
        for value in ('', 'bad\0text', '\ud800', float('inf'), float('nan'), 10 ** 400):
            with self.subTest(value=repr(value)):
                changed = copy.deepcopy(catalog)
                changed['fluid_poses']['probe'] = value
                with self.assertRaises(InvalidSidecar):
                    validate_fluid_catalog(changed)
        for side in ('none', 'automatic', '', 1, True):
            with self.subTest(side=side):
                changed = copy.deepcopy(catalog)
                changed['fluid_poses']['samples'][0]['side'] = side
                with self.assertRaises(InvalidSidecar):
                    validate_fluid_catalog(changed)

    def test_native_element_count_boundary(self):
        """Apply the frozen one-million count bound to the native address domain too."""
        # Admit the inclusive bound without allocating native elements, then reject its successor.
        catalog = check(ROOT / 'without_winches.spose')['catalog']  # Independent small F population.
        catalog['fluid_poses']['lines'][0]['native_element_count'] = 1_000_000
        self.assertIsNotNone(validate_fluid_catalog(catalog))
        for count in (0, 1_000_001, (1 << 63) - 1):
            with self.subTest(count=count):
                catalog['fluid_poses']['lines'][0]['native_element_count'] = count
                with self.assertRaisesRegex(InvalidSidecar, 'native element count'):
                    validate_fluid_catalog(catalog)

    def test_header_preadmission_precedes_payload_read(self):
        """Reject a whole group over64MiB even when each declared chunk fits64MiB."""
        raw = (ROOT / 'mixed_transient.spose').read_bytes()
        prefix, chunks = prefix_and_chunks(raw)
        prefix = bytearray(prefix)
        struct.pack_into('<Q', prefix, 40, 64 * 1024 * 1024)  # Raise only the per-chunk bootstrap ceiling.
        prefix[:64] = repair(prefix[:64], 52)
        fields = list(chunks[0][1])  # First independently checked family is F.
        self.assertEqual(fields[1], 5)
        records = MAX_PENDING_RAW // self.expected['group_frame_bytes'] + 1
        fields[8] = records
        fields[10] = fields[11] = records * self.expected['fluid_frame_bytes']
        self.assertLessEqual(fields[10], 64 * 1024 * 1024)
        probe = ReadProbe(bytes(prefix) + repair(HEADER.pack(*fields), 68))
        with patch.object(Path, 'open', return_value=probe):
            with self.assertRaisesRegex(InvalidSidecar, 'pending raw group cap'):
                check('probe.spose', allow_incomplete=True)
        self.assertEqual(probe.reads[-1], (len(prefix), 80))

    def test_lowered_group_and_retained_caps_precede_reads(self):
        """Exercise raw, logical and frame preadmission with no retained batch allocation."""
        raw = (ROOT / 'mixed_transient.spose').read_bytes()
        prefix, chunks = prefix_and_chunks(raw)
        records = chunks[0][1][8]
        logical = self.expected['pose_frame_bytes'] + self.expected['winch_frame_bytes'] + 80 + 57 * self.expected['fluid_sample_count']
        cases = (
            ({'max_pending_raw_bytes': records * self.expected['group_frame_bytes'] - 1}, 'pending raw group cap'),
            ({'dump_frames': True, 'max_dump_bytes': records * logical - 1}, 'logical frame dump byte cap'),
            ({'dump_frames': True, 'max_dump_frames': records - 1}, 'frame dump cap'),
        )
        for options, error in cases:
            with self.subTest(options=options):
                probe = ReadProbe(raw)
                with patch.object(Path, 'open', return_value=probe):
                    with self.assertRaisesRegex(InvalidSidecar, error):
                        check('probe.spose', **options)
                self.assertEqual(probe.reads[-1], (len(prefix), 80))
        self.assertEqual(MAX_PENDING_RAW, 64 * 1024 * 1024)
        self.assertEqual(MAX_DUMP_BYTES, 256 * 1024 * 1024)
        self.assertEqual(MAX_DUMP_FRAMES, 100000)
        for options in ({'max_pending_raw_bytes': MAX_PENDING_RAW + 1},
                        {'max_dump_bytes': MAX_DUMP_BYTES + 1},
                        {'max_dump_frames': MAX_DUMP_FRAMES + 1}):
            with self.subTest(options=options), self.assertRaises(InvalidSidecar):
                check(ROOT / 'mixed_transient.spose', **options)

    def test_opaque_future_families_remain_optional(self):
        """Skip later families by CRC/bounds and explicitly report that their semantics were ignored."""
        prefix, chunks = prefix_and_chunks((ROOT / 'without_winches.spose').read_bytes())
        f_header, f_payload = chunks[0][1:]
        for kind, stream, name in ((100, 42, 'unknown_optional'),):
            with self.subTest(kind=kind):
                raw = prefix + chunk(5, 3, 0, 0, f_payload, records=f_header[8])
                raw += chunk(kind, stream, 1, 0, b'opaque family; deliberately not a future record')
                result = self.inspect(raw, allow_incomplete=True, dump_frames=True)
                self.assertIn('fluid_poses', result['understood_families'])
                self.assertIn(name, result['ignored_families'])
                self.assertEqual(result['frames'], [])
                self.assertEqual(result['fluid_chunks'], 0)
                damaged = raw[:-1] + bytes([raw[-1] ^ 1])
                with self.assertRaisesRegex(InvalidSidecar, 'checksum'):
                    self.inspect(damaged, allow_incomplete=True)

    def test_cross_stream_prefix_requires_both_complete_records(self):
        """A complete F/winch disagreement fails even when the later structural pose is absent."""
        prefix, chunks = prefix_and_chunks((ROOT / 'mixed_transient.spose').read_bytes())
        fh, fp = chunks[0][1:]
        wh, wp = chunks[1][1:]
        changed = bytearray(fp)
        struct.pack_into('<d', changed, 16, 1.25001)  # Plausible F time; differs only from its later partner.
        raw = prefix + chunk(5, 3, 0, 0, changed, records=fh[8])
        result = self.inspect(raw, allow_incomplete=True)
        self.assertEqual(result['validated_frames'], 0)
        raw += chunk(4, 2, 1, 0, wp, records=wh[8])
        with self.assertRaisesRegex(InvalidSidecar, 'common prefix mismatch'):
            self.inspect(raw, allow_incomplete=True)

    def test_unknown_family_reporting_is_constant_size(self):
        """Do not retain a duration-sized registry of arbitrary unknown kind/stream pairs."""
        prefix, _ = prefix_and_chunks((ROOT / 'without_winches.spose').read_bytes())
        data = bytearray(prefix)
        for sequence in range(1024):
            data.extend(chunk(100 + sequence, 42 + sequence, sequence, 0, b'opaque'))
        result = self.inspect(data, allow_incomplete=True)
        self.assertEqual(result['ignored_families'], ['unknown_optional'])
        self.assertEqual(result['optional_chunks'], 1024)
        self.assertEqual(result['validated_frames'], 0)

    def test_default_memory_does_not_follow_duration(self):
        """Scan8/1024 equal-size F groups with no decoded history or growing retained storage."""
        prefix, chunks = prefix_and_chunks((ROOT / 'without_winches.spose').read_bytes())
        fwidth = self.expected['fluid_frame_bytes']
        pwidth = self.expected['pose_frame_bytes']
        fsample = chunks[0][2][:fwidth]  # Frozen first fluid record; geometry never recomputed.
        psample = chunks[1][2][:pwidth]  # Frozen first physical record.
        peaks = []  # Peak allocations during reader calls only.
        with tempfile.TemporaryDirectory(prefix='serene-f-memory-') as temporary:
            for count in (8, 1024):
                data = bytearray(prefix)
                previous = 0
                for ordinal in range(count):
                    fp, pp = bytearray(fsample), bytearray(psample)
                    for row in (fp, pp):
                        struct.pack_into('<QIIdQ', row, 0, ordinal, 1, 2 if ordinal == 0 else 4,
                                         1.25 + ordinal * 0.125, ordinal)
                    struct.pack_into('<I', fp, 72, 2 if ordinal == 0 else 3)
                    data.extend(chunk(5, 3, 2 * ordinal, ordinal, fp))
                    offset = len(data)
                    data.extend(chunk(1, 1, 2 * ordinal + 1, ordinal, pp, previous=previous, flags=1))
                    previous = offset
                path = Path(temporary) / f'{count}.spose.partial'
                path.write_bytes(data)
                del data, fp, pp
                tracemalloc.start()
                try:
                    result = check(path, allow_incomplete=True)
                    peaks.append(tracemalloc.get_traced_memory()[1])
                finally:
                    tracemalloc.stop()
                self.assertEqual(result['grouped_frames'], count)
                self.assertEqual(result['fluid_chunks'], count)
                self.assertNotIn('frames', result)
        self.assertLessEqual(peaks[1], peaks[0] + 64 * 1024,
                             f'default retained memory grew with duration: {peaks}')


if __name__ == '__main__':
    unittest.main()
