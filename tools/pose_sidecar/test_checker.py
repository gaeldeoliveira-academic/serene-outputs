#!/usr/bin/env python3
"""Focused independent reader/fixture checks; no production builds or solver required."""
import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest
from author_fixtures import CATALOG, FRAMES, make_file, crc_header, chunk
from check_sidecar import check, InvalidSidecar, IncompleteSidecar

# Read-only checked-in pose-fixture directory.
FIX = Path(__file__).resolve().parents[2] / 'fixtures_pose_sidecar'

class CheckerTests(unittest.TestCase):
    """Independent checker regression cases over literal and mutated tiny files.

    Owns a temporary directory per test via setUp/cleanup. Uses the independent
    author only to create intentional byte inputs, never the production writer."""
    # Keep all test-created files scoped to the per-case temporary directory.
    def setUp(self):
        """Create self.tmp and register cleanup before each test; return None."""
        # Allocate a fresh owned temporary directory and register deterministic test cleanup.
        # Owned temporary-directory guard registered for cleanup after this test.
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
    def read(self, raw, **kwargs):
        """Write borrowed raw bytes into the case file and return check() output.

        kwargs are forwarded checker options; self.tmp owns the file lifetime."""
        # Persist the supplied tiny byte fixture, then invoke the independent checker.
        # Temporary case filename rewritten only within the current test directory.
        path = Path(self.tmp.name)/'case.spose'
        path.write_bytes(raw)
        return check(path, **kwargs)
    def test_published_fixture_expectations(self):
        """Check pinned positive/negative expectations, exact dumped frame dictionaries and completion outcomes.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Check pinned positive/negative expectations, exact dumped frame dictionaries and completion outcomes.
        # Pinned independent expectations loaded from fixture JSON.
        expected = json.loads((FIX/'expected.json').read_text())
        # Current fixture filename in the expectation table.
        # Expected validity/count/outcome data for the current case.
        for name, exp in expected['cases'].items():
            with self.subTest(name=name):
                if not exp['valid']:
                    with self.assertRaisesRegex(InvalidSidecar, exp['error_contains']):
                        check(FIX/name)
                    continue
                # Owned independent-checker result under test.
                got = check(FIX/name, allow_incomplete=exp.get('allow_incomplete', False), dump_frames=True)
                self.assertEqual(got['complete'], exp['complete'])
                self.assertEqual(got['validated_frames'], exp['frames'])
                # Optional result count field being compared.
                for key in ('pose_chunks', 'optional_chunks'):
                    if key in exp:
                        self.assertEqual(got[key], exp[key])
                if name == 'transient.spose':
                    self.assertEqual(got['frames'], expected['transient_frames'])
                if name == 'static.spose':
                    self.assertEqual(got['frames'], expected['static_frames'])
                if name == 'prepared.spose':
                    self.assertEqual(got['frames'], expected['prepared_frames'])
                if 'run_outcome' in exp:
                    self.assertEqual(got['completion']['run'], exp['run_outcome'])
                    self.assertEqual(got['completion']['capture'], exp['capture_outcome'])
    def test_incomplete_requires_explicit_mode(self):
        """Require explicit recovery mode for an interrupted but valid prefix.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Require explicit recovery mode for an interrupted but valid prefix.
        with self.assertRaises(IncompleteSidecar):
            check(FIX/'incomplete.spose.partial')
    def test_corrupt_is_not_recovery_success(self):
        """Ensure recovery mode cannot convert a checksum failure into success.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Ensure recovery mode cannot convert a checksum failure into success.
        with self.assertRaisesRegex(InvalidSidecar, 'checksum'):
            check(FIX/'bad_payload_checksum.spose', allow_incomplete=True)
    def test_default_has_no_frame_history(self):
        """Verify default checking omits owned frame-history dumps.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Verify default checking omits owned frame-history dumps.
        self.assertNotIn('frames', check(FIX/'transient.spose'))
    def test_dump_is_bounded(self):
        """Reject an explicitly requested dump exceeding its frame cap.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject an explicitly requested dump exceeding its frame cap.
        with self.assertRaisesRegex(InvalidSidecar, 'dump cap'):
            check(FIX/'transient.spose', dump_frames=True, max_dump_frames=2)
    def test_catalog_cycle(self):
        """Reject a cycle introduced into the occurrence parent graph.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject a cycle introduced into the occurrence parent graph.
        # Private deep copy of the catalog for an isolated semantic mutation.
        cat = copy.deepcopy(CATALOG)
        cat['occurrences'][0]['parent_id'] = 21
        with self.assertRaisesRegex(InvalidSidecar, 'cycle'):
            self.read(make_file(FRAMES, cat)[0])
    def test_bad_connection_ref(self):
        """Reject a structural node used where a physical port is required.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject a structural node used where a physical port is required.
        # Private deep copy of the catalog for an isolated semantic mutation.
        cat = copy.deepcopy(CATALOG)
        cat['connections'][0]['first_sample_id'] = 0
        with self.assertRaisesRegex(InvalidSidecar, 'physical port'):
            self.read(make_file(FRAMES, cat)[0])
    def test_missing_connection_catalog(self):
        """Reject omission of the required connection catalog.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject omission of the required connection catalog.
        # Private deep copy of the catalog for an isolated semantic mutation.
        cat = copy.deepcopy(CATALOG)
        del cat['connections']
        with self.assertRaisesRegex(InvalidSidecar, 'connections'):
            self.read(make_file(FRAMES, cat)[0])
    def test_missing_alias_catalog(self):
        """Reject omission of the required alias catalog.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject omission of the required alias catalog.
        # Private deep copy of the catalog for an isolated semantic mutation.
        cat = copy.deepcopy(CATALOG)
        del cat['port_aliases']
        with self.assertRaisesRegex(InvalidSidecar, 'port_aliases'):
            self.read(make_file(FRAMES, cat)[0])
    def test_catalog_version_must_be_integer(self):
        """Reject a floating-point spelling of the integer catalog version.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject a floating-point spelling of the integer catalog version.
        # Private deep copy of the catalog for an isolated semantic mutation.
        cat = copy.deepcopy(CATALOG)
        cat['catalog_version'] = 1.0
        with self.assertRaisesRegex(InvalidSidecar, 'catalog version'):
            self.read(make_file(FRAMES, cat)[0])
    def test_duplicate_path(self):
        """Reject duplicate occurrence paths even when IDs remain distinct.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject duplicate occurrence paths even when IDs remain distinct.
        # Private deep copy of the catalog for an isolated semantic mutation.
        cat = copy.deepcopy(CATALOG)
        cat['occurrences'][2]['path'] = cat['occurrences'][1]['path']
        with self.assertRaisesRegex(InvalidSidecar, 'duplicate occurrence path'):
            self.read(make_file(FRAMES, cat)[0])
    def test_duplicate_segment_sample(self):
        """Reject repeated structural sample membership inside one segment.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject repeated structural sample membership inside one segment.
        # Private deep copy of the catalog for an isolated semantic mutation.
        cat = copy.deepcopy(CATALOG)
        cat['segments'][0]['sample_ids'] = [0, 0]
        with self.assertRaisesRegex(InvalidSidecar, 'duplicate sample'):
            self.read(make_file(FRAMES, cat)[0])
    def test_anchor_owner_kind(self):
        """Reject an anchor-port sample assigned to a flexible occurrence.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject an anchor-port sample assigned to a flexible occurrence.
        # Private deep copy of the catalog for an isolated semantic mutation.
        cat = copy.deepcopy(CATALOG)
        cat['samples'][3]['occurrence_id'] = 21
        with self.assertRaisesRegex(InvalidSidecar, 'port/owner'):
            self.read(make_file(FRAMES, cat)[0])
    def test_bad_quaternion(self):
        """Reject a finite quaternion whose squared norm is outside tolerance.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject a finite quaternion whose squared norm is outside tolerance.
        # Private deep copy of frames for an isolated stamp/pose mutation.
        frames = copy.deepcopy(FRAMES)
        frames[0]['poses'][2][3] = 0.1
        with self.assertRaisesRegex(InvalidSidecar, 'nonunit'):
            self.read(make_file(frames)[0])
    def test_nonfinite_position(self):
        """Reject a NaN world-position component.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject a NaN world-position component.
        # Private deep copy of frames for an isolated stamp/pose mutation.
        frames = copy.deepcopy(FRAMES)
        frames[0]['poses'][1][0] = float('nan')
        with self.assertRaisesRegex(InvalidSidecar, 'nonfinite pose'):
            self.read(make_file(frames)[0])
    def test_repeated_clock(self):
        """Reject repeated transient sample times after a previous accepted frame.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject repeated transient sample times after a previous accepted frame.
        # Private deep copy of frames for an isolated stamp/pose mutation.
        frames = copy.deepcopy(FRAMES)
        frames[1]['stamp']['time'] = frames[0]['stamp']['time']
        with self.assertRaisesRegex(InvalidSidecar, 'clocks'):
            self.read(make_file(frames)[0])
    def test_combined_events_one_frame(self):
        """Admit combined initial/observation/target bits without duplicating a frame.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Admit combined initial/observation/target bits without duplicating a frame.
        # Private deep copy of frames for an isolated stamp/pose mutation.
        frames = copy.deepcopy(FRAMES[:1])
        frames[0]['stamp']['events'] = 2|4|8
        # Owned independent-checker result under test.
        got = self.read(make_file(frames)[0], dump_frames=True)
        self.assertEqual(got['validated_frames'], 1)
        self.assertEqual(got['frames'][0]['stamp']['events'], 14)
    def test_after_target_fails(self):
        """Reject any frame after a terminal target event.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject any frame after a terminal target event.
        # Private deep copy of frames for an isolated stamp/pose mutation.
        frames = copy.deepcopy(FRAMES)
        frames[0]['stamp']['events'] |= 8
        with self.assertRaisesRegex(InvalidSidecar, 'after target'):
            self.read(make_file(frames)[0])
    def test_oversize_before_reading_payload(self):
        """Reject a repaired-header oversized payload claim before payload access.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject a repaired-header oversized payload claim before payload access.
        # Owned baseline or mutable file bytes used for the targeted corruption.
        # Pose-chunk offsets returned by the independent author.
        raw, offsets = make_file(FRAMES)
        # Owned baseline or mutable file bytes used for the targeted corruption.
        raw = bytearray(raw)
        # Byte offset of the targeted chunk header.
        at = offsets[0]
        struct.pack_into('<Q', raw, at+48, 1<<60)
        raw[at:at+80] = crc_header(raw[at:at+80], 68)
        with self.assertRaisesRegex(InvalidSidecar, 'bounds'):
            self.read(raw)
    def test_bad_previous_link(self):
        """Reject a repaired-header pose chunk linked to the wrong predecessor.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject a repaired-header pose chunk linked to the wrong predecessor.
        # Owned baseline or mutable file bytes used for the targeted corruption.
        # Pose-chunk offsets returned by the independent author.
        raw, offsets = make_file(FRAMES)
        # Owned baseline or mutable file bytes used for the targeted corruption.
        raw = bytearray(raw)
        # Byte offset of the targeted chunk header.
        at = offsets[1]
        struct.pack_into('<Q', raw, at+72, 1)
        raw[at:at+80] = crc_header(raw[at:at+80], 68)
        with self.assertRaisesRegex(InvalidSidecar, 'ordering/index'):
            self.read(raw)
    def test_required_unknown_chunk(self):
        """Reject an unknown stream when its required flag is set.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject an unknown stream when its required flag is set.
        # Owned baseline or mutable file bytes used for the targeted corruption.
        # Pose-chunk offsets returned by the independent author.
        raw, offsets = make_file(FRAMES, optional=True)
        # Optional chunk starts immediately after first two-frame batch.
        # Byte offset of the targeted chunk header.
        at = offsets[0] + 80 + 2*(64+56*4)
        # Owned baseline or mutable file bytes used for the targeted corruption.
        raw = bytearray(raw)
        struct.pack_into('<H', raw, at+6, 1)
        raw[at:at+80] = crc_header(raw[at:at+80], 68)
        with self.assertRaisesRegex(InvalidSidecar, 'unsupported required'):
            self.read(raw)
    def test_footer_and_trailing_bytes(self):
        """Reject both extra trailing bytes and a corrupted footer.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Reject both extra trailing bytes and a corrupted footer.
        # Owned baseline or mutable file bytes used for the targeted corruption.
        raw = (FIX/'transient.spose').read_bytes()
        with self.assertRaisesRegex(InvalidSidecar, 'trailing'):
            self.read(raw+b'x')
        # Private mutable footer-corruption copy.
        bad = bytearray(raw)
        bad[-20] ^= 1
        with self.assertRaisesRegex(InvalidSidecar, 'footer/checksum'):
            self.read(bad)
    def test_empty_complete(self):
        """Admit a completed file with zero frames and zero pose offsets.

        Returns None on success; unittest assertions/expected exceptions establish the case."""
        # Admit a completed file with zero frames and zero pose offsets.
        # Owned independent-checker result under test.
        got = self.read(make_file([])[0])
        self.assertTrue(got['complete'])
        self.assertEqual(got['validated_frames'], 0)
        self.assertEqual(got['first_pose_offset'], 0)

if __name__ == '__main__':
    unittest.main()
