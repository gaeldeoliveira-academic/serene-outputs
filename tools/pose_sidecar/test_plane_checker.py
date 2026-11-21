"""Independent finite-plane catalog, wire and bounded-storage checker regressions.

Physical expected values come only from the immutable independent corpus. Small
framing helpers are shared with the moment tests, never with a fixture author or
native writer. Tests qualify admission, preservation and transaction boundaries;
they do not claim provider numerics, Core coverage or renderer qualification.
"""
import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import tracemalloc
import unittest
from unittest.mock import patch

from check_sidecar import (check, InvalidSidecar, IncompleteSidecar, validate_plane_catalog,
                           MAX_PENDING_RAW, MAX_DUMP_BYTES, MAX_DUMP_FRAMES, MAX_INT, MAX_SAMPLES)
from test_moment_checker import HEADER, ReadProbe, chunk, crc, repaired, split

ROOT = Path(__file__).resolve().parents[2] / 'fixtures_pose_planes'  # Immutable independently authored plane corpus.


class PlaneCheckerTests(unittest.TestCase):
    """Own original catalog/oracles and isolate every mutable regression input.

    Class objects are read-only after setup. Temporary byte streams/files exist
    only within a test, and no fixture author, external asset or provider is used.
    Public results are checked against literal binary64 bits, including signed zero.
    """
    @classmethod
    def setUpClass(cls):
        """Read immutable expected outcomes and the baseline catalog once."""
        # Load original oracles without modifying their source files or values.
        cls.expected = json.loads((ROOT / 'expected.json').read_text(encoding='utf-8'))  # Owned independent value/outcome oracles.
        cls.catalog = json.loads((ROOT / 'catalog.json').read_text(encoding='utf-8'))  # Original source/geometry metadata for copied probes.

    def inspect(self, raw, **options):
        """Check borrowed raw bytes in an isolated file, forwarding options unchanged.

        Return the public result; validation exceptions propagate. The sole temporary
        input is removed on exit even when the intentionally corrupt bytes fail.
        """
        # Limit all byte mutations and temporary storage to this test's lifetime.
        with tempfile.TemporaryDirectory(prefix='serene-plane-checker-') as temporary:
            path = Path(temporary) / 'input.spose'  # Sole test-owned input file.
            path.write_bytes(raw)
            return check(path, **options)

    def assert_values(self, actual, expected):
        """Recursively compare decoded actual values with independent expected literals.

        Shapes, Boolean types and every stored binary64 bit must agree. No geometry,
        magnitude, provider calculation or tolerance creates an expected velocity.
        """
        # Check ordinary shape/value equality, then strengthen float and Boolean comparisons.
        self.assertEqual(actual, expected)
        if isinstance(expected, float):
            self.assertEqual(struct.pack('<d', actual), struct.pack('<d', expected))
        elif isinstance(expected, bool):
            self.assertIs(type(actual), bool)
        elif isinstance(expected, dict):
            for key, value in expected.items():  # Original expected member and value.
                self.assert_values(actual[key], value)
        elif isinstance(expected, list):
            for value, wanted in zip(actual, expected):  # Original corresponding rows/components.
                self.assert_values(value, wanted)

    def test_frozen_independent_corpus(self):
        """Bind all223 files to literal hashes and compare every valid entire mixed frame."""
        # Verify immutable artifacts, exact classifications and fully committed frame oracles.
        self.assertEqual(len(self.expected['cases']), 223)
        self.assertEqual(sum(case['valid'] for case in self.expected['cases'].values()), 38)
        for name, wanted in self.expected['cases'].items():  # Frozen filename and independent expected result.
            with self.subTest(name=name):
                path = ROOT / name  # Read-only original case.
                raw = path.read_bytes()  # Bytes used only for the independent identity check.
                self.assertEqual(len(raw), wanted['bytes'])
                self.assertEqual(hashlib.sha256(raw).hexdigest(), wanted['sha256'])
                if not wanted['valid']:
                    with self.assertRaises(InvalidSidecar) as caught:
                        check(path, allow_incomplete=True)
                    self.assertNotIsInstance(caught.exception, IncompleteSidecar)
                    continue
                result = check(path, allow_incomplete=True, dump_frames=True)  # Complete committed groups only.
                self.assertEqual(result['complete'], wanted['complete'])
                self.assertEqual(result['validated_frames'], wanted['frames'])
                self.assertEqual(result['grouped_frames'], wanted['frames'])
                self.assertEqual(result['plane_chunks'], result['pose_chunks'])
                for key in ('moment_chunks', 'applied_chunks', 'fluid_chunks', 'feature_chunks'):  # Every enabled partner shares the same atomic batch count.
                    if key in result:
                        self.assertEqual(result[key], result['pose_chunks'])
                self.assert_values(result['frames'], self.expected[wanted['frames_key']][:wanted['frames']])
                if not wanted['complete']:
                    self.assertNotIn('completion', result)
                    with self.assertRaises(IncompleteSidecar):
                        check(path)

    def test_manifest_preservation_and_exact_scalar_domains(self):
        """Preserve descriptive provenance while enforcing exact identities, counts and Boolean facts."""
        # Admit legitimate empty strings/signed headers and prove the borrowed object is unchanged.
        # Reject numeric substitutes for required Boolean values and inexact integer source sizes.
        changed = copy.deepcopy(self.catalog)  # Isolated metadata probe.
        manifest = changed['inflow_planes']['sources'][0]['initialization_manifest']  # Copied actual provider envelope.
        manifest['descriptive'] = {'empty': '', 'negative_header': -37, 'nested': [None, False, 0.25]}
        manifest['source']['source_bytes'] = MAX_INT
        before = copy.deepcopy(changed)  # Immutable expected semantic JSON object.
        self.assertEqual(validate_plane_catalog(changed).points, 12)
        self.assertEqual(changed, before)
        for value in (True, 1.0, -1, MAX_INT + 1):  # Source file size is an exact nonnegative bounded integer.
            manifest['source']['source_bytes'] = value
            with self.subTest(value=value), self.assertRaises(InvalidSidecar):
                validate_plane_catalog(changed)
        manifest['source']['source_bytes'] = 0
        for key, value in (('standalone_provider_prepared', 1), ('core_binding', 'performed')):  # Historical adapter facts cannot become a Core certificate.
            original = manifest['validation'][key]  # Restore the same original field between isolated probes.
            manifest['validation'][key] = value
            with self.subTest(key=key), self.assertRaises(InvalidSidecar):
                validate_plane_catalog(changed)
            manifest['validation'][key] = original
        manifest['source']['fingerprint']['cryptographic'] = 0
        with self.assertRaises(InvalidSidecar):
            validate_plane_catalog(changed)

    def test_open_provenance_and_generic_multiple_sources(self):
        """Admit future descriptive source kinds and multiple bound sources without provider inference."""
        # Keep the generic wire separate from the initial single-provider Core capability.
        changed = copy.deepcopy(self.catalog)  # Test-owned generic source table.
        descriptor = changed['inflow_planes']  # Mutable fixed recording metadata.
        source = copy.deepcopy(descriptor['sources'][0])  # Distinct bound source with an open future provenance kind.
        source.update(id=1, inflow_id='future-source')
        source['initialization_manifest']['identity']['inflow_id'] = 'future-source'
        source['initialization_manifest']['runtime_profile']['source_kind'] = 'future_descriptive_provider'
        source['initialization_manifest']['source'] = {'opaque': ['', -23, None]}
        descriptor['sources'].append(source)
        descriptor['planes'][1]['source_id'] = 1
        before = copy.deepcopy(changed)  # No normalization, sanitization or provider execution is allowed.
        self.assertEqual(validate_plane_catalog(changed).sources, 2)
        self.assertEqual(changed, before)
        source['inflow_id'] = descriptor['sources'][0]['inflow_id']
        source['initialization_manifest']['identity']['inflow_id'] = source['inflow_id']
        self.assertEqual(validate_plane_catalog(changed).sources, 2)  # Source identities are bound, not subject to an invented uniqueness predicate.
        descriptor['planes'][1]['source_id'] = 0
        with self.assertRaisesRegex(InvalidSidecar, 'orphan'):
            validate_plane_catalog(changed)
        descriptor['planes'][1]['source_id'] = 1
        for value in ('bad\0metadata', '\ud800', float('inf')):  # Extra provenance still obeys UTF8/NUL-free/finite rules.
            source['initialization_manifest']['extra'] = value
            with self.subTest(value=repr(value)), self.assertRaises(InvalidSidecar):
                validate_plane_catalog(changed)

    def test_assets_are_never_reopened(self):
        """Read only the selected sidecar even when its recorded source path cannot exist."""
        # Observe actual Path.open calls while leaving normal file reading intact.
        path = ROOT / 'source_asset_never_reopened.spose'  # Valid provenance intentionally naming an unavailable external asset.
        observed = []  # Small independent list of actual opened paths.
        original = Path.open  # Normal file operation, restored by the patch context.

        def traced_open(candidate, *args, **kwargs):
            """Record candidate, forward ordinary arguments and return its normal file handle."""
            # Record actual I/O identity, never inspect or resolve the manifest's asset path.
            observed.append(candidate)
            return original(candidate, *args, **kwargs)

        with patch.object(Path, 'open', new=traced_open):
            self.assertTrue(check(path)['complete'])
        self.assertEqual(observed, [path])

    def test_geometry_population_and_binary64_representation(self):
        """Admit fixed scalar geometry without point allocation and retain exact requested IDs/counts."""
        # Exercise the aggregate cap at its boundary, then binary64 world-coordinate semantics.
        changed = copy.deepcopy(self.catalog)  # Original geometry copied for scalar setup-only probes.
        descriptor = changed['inflow_planes']  # Borrowed mutable test descriptor.
        descriptor['planes'] = descriptor['planes'][:1]
        plane = descriptor['planes'][0]  # One generic finite plane with a million fixed slots.
        plane.update(nu=1000, nv=1000)
        layout = validate_plane_catalog(changed)  # Must retain a count, not build a million-point array.
        self.assertEqual((layout.points, layout.counts, layout.frame_bytes), (MAX_SAMPLES, (MAX_SAMPLES,), 80 + 125000 + 24000000))
        plane['nu'] += 1
        with self.assertRaises(InvalidSidecar):
            validate_plane_catalog(changed)
        changed = copy.deepcopy(self.catalog)
        with patch('check_sidecar.MAX_SAMPLES', 12):
            self.assertEqual(validate_plane_catalog(changed).points, 12)
        with patch('check_sidecar.MAX_SAMPLES', 11), self.assertRaisesRegex(InvalidSidecar, 'aggregate'):
            validate_plane_catalog(changed)
        plane = changed['inflow_planes']['planes'][0]
        plane['lower_corner_world_m'][0] = 2**53 + 1
        before = copy.deepcopy(changed)  # Integer text value is preserved even though physical arithmetic uses binary64.
        self.assertEqual(validate_plane_catalog(changed).points, 12)
        self.assertEqual(changed, before)
        for value in (True, 3.0, MAX_INT + 1):  # Dimensions are exact integer domains, never physical-coordinate conversions.
            plane['nu'] = value
            with self.subTest(value=value), self.assertRaises(InvalidSidecar):
                validate_plane_catalog(changed)

    def test_complete_pending_defects_are_not_truncation_recovery(self):
        """Reject complete malformed planes early and recover only fully joined earlier frames."""
        # Validate independently knowable pending facts before reading a missing later partner.
        for name in ('pending_nonfinite_velocity', 'pending_wrong_phase', 'pending_wrong_ordinal',
                     'pending_wrong_generation', 'pending_bad_mask', 'pending_copied_time'):
            with self.subTest(name=name), self.assertRaises(InvalidSidecar) as caught:
                check(ROOT / (name + '.spose.partial'), allow_incomplete=True)
            self.assertNotIsInstance(caught.exception, IncompleteSidecar)
        for name in ('pending_planes', 'truncated_winches', 'pending_winches', 'truncated_pose', 'unjoined_pending_time'):
            path = ROOT / (name + '.spose.partial')  # Original valid prefix; absent stamps cannot prove cross-stream equality.
            result = check(path, allow_incomplete=True, dump_frames=True)
            wanted = self.expected['cases'][path.name]  # Independent previous-group retention expectation.
            self.assertFalse(result['complete'])
            self.assertEqual(result['validated_frames'], wanted['frames'])
            self.assert_values(result['frames'], self.expected[wanted['frames_key']][:wanted['frames']])

    def test_known_kind_or_stream_engages_plane_semantics(self):
        """Either kind8 or stream6 requires planes; genuinely unknown optional families remain bounded/opaque."""
        # Reuse a known earlier-family catalog with no planes and insert strict or unknown discriminators.
        prefix, _ = split((ROOT.parent / 'fixtures_pose_moments/moments_only.spose').read_bytes())  # Descriptorless-for-planes setup.
        for kind, stream in ((8, 42), (100, 6), (8, 6)):
            with self.subTest(kind=kind, stream=stream), self.assertRaisesRegex(InvalidSidecar, 'plane chunk without catalog'):
                self.inspect(prefix + chunk(kind, stream, 0, 0, b'opaque'), allow_incomplete=True)
        result = self.inspect(prefix + chunk(100, 42, 0, 0, b'opaque'), allow_incomplete=True)
        self.assertEqual(result['ignored_families'], ['unknown_optional'])
        self.assertIn('inflow_planes', result['implemented_families'])
        self.assertNotIn('inflow_planes', result['understood_families'])

    def test_whole_raw_group_admission_precedes_first_payload(self):
        """Reject excessive known raw populations before reading their first family payload."""
        # Test F-first, loads-first and planes-first groups while every individual chunk remains legal.
        for name in ('mixed_transient', 'without_fluid', 'planes_only'):
            with self.subTest(name=name):
                path = ROOT / (name + '.spose')  # Original positive source with a distinct first implemented family.
                prefix, chunks = split(path.read_bytes())
                info = check(path)  # Public validated complete-group byte extent.
                prefix = bytearray(prefix)
                struct.pack_into('<Q', prefix, 40, MAX_PENDING_RAW)
                prefix[:64] = repaired(prefix[:64], 52)
                fields = list(chunks[0][1])  # Header-only oversized aggregate probe.
                width = fields[10] // fields[8]  # Independent literal row width.
                fields[8] = MAX_PENDING_RAW // info['raw_group_frame_bytes'] + 1
                fields[10] = fields[11] = fields[8] * width
                self.assertLessEqual(fields[10], MAX_PENDING_RAW)
                probe = ReadProbe(bytes(prefix) + repaired(HEADER.pack(*fields), 68))  # Tracks whether any payload read was attempted.
                with patch.object(Path, 'open', return_value=probe), self.assertRaisesRegex(InvalidSidecar, 'pending raw group cap'):
                    check('probe.spose', allow_incomplete=True)
                self.assertEqual(probe.reads[-1], (len(prefix), 80))
        self.assertEqual(MAX_PENDING_RAW, 64 * 1024 * 1024)

    def test_dump_admission_charges_planes_before_retaining_rows(self):
        """Charge one logical validity byte per plane point and enforce both hard dump limits before reads."""
        # Distinguish actual packed wire storage from this checker's conservative decoded charge.
        path = ROOT / 'mixed_transient.spose'  # Complete all-family input with12 grid points.
        raw = path.read_bytes()
        prefix, chunks = split(raw)
        result = check(path, dump_frames=True)
        logical = 512 + 288 + (80 + 57*6) + (80 + 96*2 + 49*8) + (80 + 25*7) + (80 + 25*12)  # Existing charges plus one plane prefix and all points.
        self.assertEqual(result['raw_group_frame_bytes'], 2494)
        self.assertEqual(result['logical_retained_frame_bytes'], logical)
        self.assertEqual(result['logical_retained_bytes'], 3 * logical)
        records = chunks[0][1][8]  # Actual first group size from the independent header.
        for options, message in (({'max_dump_bytes': records * logical - 1}, 'logical frame dump byte cap'),
                                 ({'max_dump_frames': records - 1}, 'frame dump cap')):
            probe = ReadProbe(raw)  # Limit failures must occur before a family payload or decoded rows exist.
            with patch.object(Path, 'open', return_value=probe), self.assertRaisesRegex(InvalidSidecar, message):
                check('probe.spose', dump_frames=True, **options)
            self.assertEqual(probe.reads[-1], (len(prefix), 80))
        self.assertEqual((MAX_DUMP_BYTES, MAX_DUMP_FRAMES), (256 * 1024 * 1024, 100000))
        for options in ({'max_dump_bytes': MAX_DUMP_BYTES + 1}, {'max_dump_frames': MAX_DUMP_FRAMES + 1}):
            with self.assertRaises(InvalidSidecar):
                check(path, dump_frames=True, **options)

    def test_failed_final_pose_cannot_commit_partial_history(self):
        """A complete bad later structural row rolls back publication of the entire mixed group."""
        # Keep all plane rows original and corrupt the second structural row after the first decoded row.
        raw = bytearray((ROOT / 'mixed_transient.spose').read_bytes())  # Test-owned corruption only.
        _, chunks = split(raw)
        offset, fields, original = next(item for item in chunks if item[1][1] == 1)  # First structural batch follows every optional partner.
        self.assertEqual(fields[8], 2)
        payload = bytearray(original)  # Complete retained length, deliberately invalid finite-pose requirement.
        struct.pack_into('<d', payload, 512 + 64, float('nan'))
        fields = list(fields)
        fields[12] = crc(payload)
        raw[offset:offset + 80] = repaired(HEADER.pack(*fields), 68)
        raw[offset + 80:offset + 80 + len(payload)] = payload
        try:
            self.inspect(raw, allow_incomplete=True, dump_frames=True)
        except InvalidSidecar as error:
            trace = error.__traceback__  # Temporary failing stack used only for transaction-boundary observations.
            while trace is not None and trace.tb_frame.f_code.co_name != 'check':
                trace = trace.tb_next
            self.assertIsNotNone(trace)
            self.assertEqual(trace.tb_frame.f_locals['state'].frames, 0)
            self.assertEqual(trace.tb_frame.f_locals['dumped'], [])
            self.assertEqual(trace.tb_frame.f_locals['plane_chunks'], 0)
            del trace
        else:
            self.fail('complete malformed final partner was admitted')

    def test_pending_views_are_released_before_further_reads(self):
        """Release every borrowed view before another payload/header read in complete mixed groups."""
        # Observe real reader I/O boundaries without retaining any checker frame or raw view.
        probe = ReadProbe((ROOT / 'mixed_transient.spose').read_bytes())  # Independent complete mixed source.
        with patch.object(Path, 'open', return_value=probe):
            result = check('probe.spose')
        self.assertTrue(result['complete'])
        self.assertEqual((result['inflow_source_count'], result['plane_count'], result['plane_point_count'], result['plane_chunks']), (1, 2, 12, 2))
        self.assertEqual(probe.live_views, [])
        self.assertNotIn('frames', result)

    def test_default_memory_does_not_follow_duration(self):
        """Scan8/1024 fixed-size plane-only groups with no retained decoded duration history."""
        # Repeat original literal velocities/poses while changing only coherent accepted clocks.
        # Measure checker allocations after constructing each independent recovery input.
        prefix, chunks = split((ROOT / 'planes_only.spose').read_bytes())  # Fixed setup and actual original record bytes.
        plane_width = chunks[0][1][10] // chunks[0][1][8]  # Original plane-only case has one six-point block:225 bytes.
        pose_width = 64 + 56 * self.expected['physical_sample_count']  # Independent structural row width512.
        plane_template = chunks[0][2][:plane_width]  # Original world-velocity bits and masks.
        pose_template = chunks[1][2][:pose_width]  # Original world-pose bits.
        peaks = []  # Checker-only allocation peaks, separate from test file construction.
        with tempfile.TemporaryDirectory(prefix='serene-plane-memory-') as temporary:
            for count in (8, 1024):  # Equal working set, substantially different duration.
                data = bytearray(prefix)  # Test-owned construction outside measured allocation.
                previous = 0  # Canonical first backlink then exact prior pose chunk offset.
                for ordinal in range(count):  # Exact increasing accepted state/time and recorder frame ordinal.
                    plane, pose = bytearray(plane_template), bytearray(pose_template)  # Original data rows with mutable common source facts.
                    for row in (plane, pose):  # Identical actual accepted-state stamp on both partners.
                        struct.pack_into('<QIIdQ', row, 0, ordinal, 1, 2 if ordinal == 0 else 4, ordinal * .125, ordinal)
                    struct.pack_into('<I', plane, 72, 2 if ordinal == 0 else 3)
                    data.extend(chunk(8, 6, 2 * ordinal, ordinal, plane))
                    pose_offset = len(data)  # Exact position to use as the next structural backlink.
                    data.extend(chunk(1, 1, 2 * ordinal + 1, ordinal, pose, previous=previous, flags=1))
                    previous = pose_offset
                path = Path(temporary) / f'{count}.spose.partial'  # Genuine complete-group prefix without invented final metadata.
                path.write_bytes(data)
                del data, plane, pose
                tracemalloc.start()
                try:
                    result = check(path, allow_incomplete=True)
                    peaks.append(tracemalloc.get_traced_memory()[1])
                finally:
                    tracemalloc.stop()
                self.assertEqual(result['grouped_frames'], count)
                self.assertEqual(result['plane_chunks'], count)
                self.assertNotIn('frames', result)
        self.assertLessEqual(peaks[1], peaks[0] + 64 * 1024, f'default retained memory grew with duration: {peaks}')


if __name__ == '__main__':
    unittest.main()
