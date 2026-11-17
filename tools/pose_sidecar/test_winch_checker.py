#!/usr/bin/env python3
"""Focused independent J07 checker tests; no C++ writer or solver is imported."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import tracemalloc
import unittest

from check_sidecar import check, InvalidSidecar, IncompleteSidecar, validate_catalog, validate_winch_catalog
from author_winch_fixtures import CATALOG, frames, make_file, change_feature, repair, chunk, crc

# Immutable independent raw corpus authored separately from the production recorder.
ROOT = Path(__file__).resolve().parents[2] / 'fixtures_pose_winch'


class WinchCheckerTests(unittest.TestCase):
    """Exercise full files, isolated contract failures and bounded prefix semantics.

    Owns small literal expectations and temporary files only. Calls the stdlib
    checker as an external consumer would; it never imports recorder code or
    uses decoded output as the expected geometry oracle.
    """

    @classmethod
    def setUpClass(cls):
        """Load the authored reference manifest once for this focused test class."""
        # Retain literal oracle dictionaries, independent of every decoder result.
        cls.expected = json.loads((ROOT / 'expected.json').read_text(encoding='utf-8'))

    def inspect(self, raw, **options):
        """Check borrowed raw bytes through an owned temporary file; return results.

        options forwards explicit recovery/dump limits. The temporary directory
        is removed on success or failure without touching the preserved corpus.
        """
        # Materialize one bounded test input, invoke the public checker and clean up.
        with tempfile.TemporaryDirectory(prefix='serene-j07-checker-') as temporary:
            path = Path(temporary) / 'test.spose'  # Single test-owned input file.
            path.write_bytes(raw)
            return check(path, **options)

    def assert_frames(self, actual, expected):
        """Compare original scalar bits, statuses, phases and exact stamps per frame."""
        # Compare scalar identity facts first, then binary64 payloads without tolerance.
        self.assertEqual(len(actual), len(expected))
        for got, wanted in zip(actual, expected):
            self.assertEqual(got['stamp'], wanted['stamp'])
            self.assertEqual(got['source_generation'], wanted['source_generation'])
            self.assertEqual(got['source_phase'], wanted['source_phase'])
            for p, q in zip(got['poses'], wanted['poses']):
                self.assertEqual(struct.pack('<7d', *p), struct.pack('<7d', *q))
            for feature, reference in zip(got['features'], wanted['features']):
                self.assertEqual(feature['geometry_status'], reference['geometry_status'])
                f = [value for point in feature['world_xyz_m'] for value in point]
                r = [value for point in reference['world_xyz_m'] for value in point]
                self.assertEqual(struct.pack('<12d', *f), struct.pack('<12d', *r))

    def test_independent_corpus(self):
        """Require every immutable file's identity and expected admission outcome."""
        # Bind file bytes before checking exact positives and malformed negatives.
        for name, wanted in self.expected['cases'].items():
            with self.subTest(name=name):
                path = ROOT / name  # Independent byte fixture, never rewritten here.
                raw = path.read_bytes()
                self.assertEqual(len(raw), wanted['bytes'])
                self.assertEqual(hashlib.sha256(raw).hexdigest(), wanted['sha256'])
                if not wanted['valid']:
                    with self.assertRaises(InvalidSidecar):
                        check(path, allow_incomplete=True)
                    continue
                result = check(path, allow_incomplete=wanted.get('allow_incomplete', False), dump_frames=True)
                self.assertEqual(result['complete'], wanted['complete'])
                self.assertEqual(result['validated_frames'], wanted['frames'])
                self.assertEqual(result['paired_frames'], wanted['frames'])
                self.assertEqual(result['feature_chunks'], result['pose_chunks'])
                self.assertEqual(result['feature_count'], 2)
                rows = self.expected[wanted['frames_key']][:wanted['frames']]
                self.assert_frames(result['frames'], rows)
                if not wanted['complete']:
                    self.assertNotIn('completion', result)
                    with self.assertRaises(IncompleteSidecar):
                        check(path)

    def test_representation_bound_preserves_signed_zero(self):
        """Admit the exact 64-epsilon boundary without changing either payload."""
        # Inspect intentionally different physical/feature zero bits and the nonzero bound.
        result = check(ROOT / 'representation_boundary.spose', dump_frames=True)
        row = result['frames'][0]
        physical = row['poses'][2][:3]  # Canonical physical sample, including negative zero.
        feature = row['features'][0]['world_xyz_m'][0]  # Original accepted feature values.
        self.assertEqual(struct.pack('<d', physical[0]), struct.pack('<d', -0.0))
        self.assertEqual(struct.pack('<d', feature[0]), struct.pack('<d', 0.0))
        self.assertEqual(feature[1] - physical[1], 2.0 ** -46)
        with self.assertRaisesRegex(InvalidSidecar, 'coordinate consistency'):
            check(ROOT / 'representation_exceeded.spose')

    def test_shared_sample_uses_canonical_physical_xyz(self):
        """Validate each arrangement independently against the same physical sample."""
        # Shared provenance is valid; a second arrangement cannot redefine a physical point.
        result = check(ROOT / 'shared_ports.spose', dump_frames=True)
        self.assertEqual(result['paired_frames'], 1)
        with self.assertRaisesRegex(InvalidSidecar, 'coordinate consistency'):
            check(ROOT / 'shared_ports_mismatch.spose')

    def test_bad_pending_semantics_are_not_incomplete(self):
        """Reject a complete malformed feature batch even when its pose never arrives."""
        # Repair the bad feature CRC so semantic generation/phase/nonfinite checks are reached.
        raw, offsets = make_file(frames())
        first_feature = offsets['features'][0]
        first_pose = offsets['poses'][0]
        for relative, fmt, value in ((64, '<Q', 17), (72, '<I', 1), (88, '<d', float('inf'))):
            with self.subTest(relative=relative):
                changed = change_feature(raw, first_feature, relative, fmt, value)[:first_pose]
                with self.assertRaises(InvalidSidecar) as caught:
                    self.inspect(changed, allow_incomplete=True)
                self.assertNotIsInstance(caught.exception, IncompleteSidecar)

    def test_known_family_cannot_fall_through_optional_skip(self):
        """Reject every malformed known-family tuple and absent descriptor."""
        # Exercise either reserved discriminator before generic optional-stream handling.
        for name in ('missing_descriptor.spose', 'wrong_kind.spose', 'wrong_stream.spose',
                     'wrong_layout.spose', 'wrong_encoding.spose', 'required_feature.spose'):
            with self.subTest(name=name), self.assertRaises(InvalidSidecar):
                check(ROOT / name, allow_incomplete=True)

    def test_pair_adjacency_and_missing_population(self):
        """Reject a second feature, unrelated optional chunk or index inside a pair."""
        # Add an isolated next header after the first complete feature to reach each ordering rule.
        raw, offsets = make_file(frames())
        prefix = raw[:offsets['poses'][0]]  # Complete staged F with no P yet.
        wrong_chunks = [
            chunk(100, 42, 1, 1, 0, b'optional', flags=0),
            chunk(2, 0, 1, 1, 0, struct.pack('<4Q', 0, 0, 0, 0)),
            chunk(4, 2, 1, 1, 0, b'\0' * 288, flags=0)]
        for wrong in wrong_chunks:
            with self.assertRaises(InvalidSidecar):
                self.inspect(prefix + wrong, allow_incomplete=True)
        with self.assertRaisesRegex(InvalidSidecar, 'missing or mismatching winch pair'):
            check(ROOT / 'missing_features.spose')

    def test_catalog_identity_and_exact_integer_fields(self):
        """Reject duplicate identities, typed-number substitutions and false C provenance."""
        # Mutate parsed catalog copies only; the bounded validator never normalizes identities.
        for edit in (
            lambda c: c['winch_geometry'].update(source_preparation_generation=True),
            lambda c: c['winch_geometry'].update(layout_version=1.0),
            lambda c: c['winch_geometry']['arrangements'][0]['points'][0].update(sample_id=False),
            lambda c: c['winch_geometry']['arrangements'][0]['edges'][0].update(points=[False, 2]),
            lambda c: c['winch_geometry']['arrangements'][1].update(
                owner_occurrence_id=20, key=c['winch_geometry']['arrangements'][0]['key']),
            lambda c: c['winch_geometry']['arrangements'][0]['points'][2].update(orientation=[1, 0, 0, 0]),
            lambda c: c['winch_geometry']['arrangements'][0]['points'][2].update(orientation_wxyz=[1, 0, 0, 0]),
            lambda c: c['winch_geometry']['arrangements'][0].update(force_id='\ud800'),
            lambda c: c['occurrences'][1].update(path='/bench/\udfff')):
            catalog = copy.deepcopy(CATALOG)  # Test-owned metadata mutation.
            edit(catalog)
            with self.assertRaises(InvalidSidecar):
                validate_winch_catalog(validate_catalog(catalog, 8))

    def test_unicode_key_and_logical_body_ports(self):
        """Count UTF-8 bytes exactly and preserve base logical-body port admission."""
        # A multibyte path/force and assembly-owned body ports retain their exact identities.
        catalog = copy.deepcopy(CATALOG)  # Small independent valid metadata variant.
        catalog['occurrences'][1]['path'] = '/bench/\u00e9'
        catalog['occurrences'][2]['kind'] = 'assembly'
        row = catalog['winch_geometry']['arrangements'][0]  # Arrangement in the Unicode scope.
        row['force_id'] = '\u7f06'
        row['key'] = 'winch_with_saddle.v1.o9_/bench/\u00e9.f3_\u7f06'
        layout = validate_winch_catalog(validate_catalog(catalog, 8))
        self.assertEqual(layout.ports[0], (2, 4, 3))
        row['key'] = 'winch_with_saddle.v1.o8_/bench/\u00e9.f1_\u7f06'
        with self.assertRaisesRegex(InvalidSidecar, 'canonical key'):
            validate_winch_catalog(catalog)

    def test_nonfinite_json_numbers_in_descriptive_metadata(self):
        """Reject overflowing JSON numbers even outside registered metadata fields."""
        # Build valid checksummed catalog prefixes so parsing, not later truncation, decides admission.
        catalog = copy.deepcopy(CATALOG)  # Literal catalog with an otherwise unconstrained field.
        catalog['winch_geometry']['arrangements'][0]['extra_numeric_metadata'] = 'numeric-probe'
        template = json.dumps(catalog, separators=(',', ':')).encode('utf-8')
        bootstrap = make_file([])[0][:64]  # Independent fixed header with known sample/chunk bounds.
        for token in (b'1e999', b'-1e999', b'NaN', b'Infinity', b'-Infinity', b'1e308'):
            with self.subTest(token=token):
                raw_catalog = template.replace(b'"numeric-probe"', token)
                header = bytearray(bootstrap)  # Test-owned length/checksum update.
                struct.pack_into('<Q', header, 24, len(raw_catalog))
                struct.pack_into('<I', header, 48, crc(raw_catalog))
                prefix = repair(header, 52) + raw_catalog
                if token == b'1e308':
                    result = self.inspect(prefix, allow_incomplete=True)
                    self.assertEqual(result['catalog']['winch_geometry']['arrangements'][0]
                                     ['extra_numeric_metadata'], 1e308)
                else:
                    with self.assertRaises(InvalidSidecar) as caught:
                        self.inspect(prefix, allow_incomplete=True)
                    self.assertNotIsInstance(caught.exception, IncompleteSidecar)

    def test_default_retention_and_dump_cap(self):
        """Default results retain no geometry history; explicit dumps obey their cap."""
        # Verify the declared two-chunk raw bound and absence of a decoded history by default.
        result = check(ROOT / 'paired_transient.spose')
        self.assertEqual(result['raw_payload_memory_bound_bytes'], 8192)
        self.assertNotIn('frames', result)
        with self.assertRaisesRegex(InvalidSidecar, 'dump cap'):
            check(ROOT / 'paired_transient.spose', dump_frames=True, max_dump_frames=2)
        self.assertEqual(check(ROOT / 'empty_failed.spose', dump_frames=True, max_dump_frames=0)['frames'], [])

    def test_default_memory_does_not_follow_duration(self):
        """Check that a longer stream cannot create an uncapped decoded history."""
        # Author inputs before measuring, so peak reader memory excludes fixture construction.
        peaks = []  # Peak live Python allocation for short and long scans of equal-size chunks.
        with tempfile.TemporaryDirectory(prefix='serene-j07-memory-') as temporary:
            for count in (8, 1024):
                reference = [copy.deepcopy(frames()[0]) for _ in range(count)]
                for ordinal, row in enumerate(reference):
                    row['stamp'].update(ordinal=ordinal, state_id=ordinal, time=1.25 + ordinal * 0.125,
                                        events=2 if ordinal == 0 else (8 if ordinal + 1 == count else 4))
                    row['source_phase'] = 2 if ordinal == 0 else 3
                path = Path(temporary) / f'{count}.spose'  # Bounded test-owned recording.
                path.write_bytes(make_file(reference)[0])
                del reference
                tracemalloc.start()
                try:
                    result = check(path)  # Default scan deliberately retains no frames.
                    peaks.append(tracemalloc.get_traced_memory()[1])
                finally:
                    tracemalloc.stop()
                self.assertEqual(result['paired_frames'], count)
                self.assertNotIn('frames', result)
        self.assertLessEqual(peaks[1], peaks[0] + 64 * 1024,
                             f'default retained memory grew with duration: {peaks}')

    def test_first_capture_can_be_transient_accepted(self):
        """Do not require an initial capture when first source phase is already accepted."""
        # Preserve actual time and positive step identity without deriving an origin from dt.
        result = check(ROOT / 'initial_disabled.spose', dump_frames=True)
        first = result['frames'][0]
        self.assertEqual((first['source_phase'], first['stamp']['state_id'], first['stamp']['time']), (3, 4, 1.29))
        self.assertEqual(result['frames'][-1]['stamp']['events'], 8)


if __name__ == '__main__':
    unittest.main()
