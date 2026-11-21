"""Independent elastic-moment checker tests against immutable literal wire oracles.

No author, recorder or provider implementation is imported. Local framing helpers
change only explicit metadata, clocks or corruption probes; expected physical
values remain the independent corpus literals. These tests qualify wire/catalog
admission and bounded reader storage, not moment recovery, transport or rendering.
"""
import copy
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import tracemalloc
import unittest
from unittest.mock import patch
import zlib

from check_sidecar import (check, InvalidSidecar, IncompleteSidecar,
                           validate_moment_catalog, MAX_PENDING_RAW, MAX_DUMP_BYTES,
                           MAX_DUMP_FRAMES, MAX_INT, MAX_SAMPLES)

ROOT = Path(__file__).resolve().parents[2] / 'fixtures_pose_moments'  # Frozen independently authored corpus.
HEADER = struct.Struct('<4sHHIIIIQQQQQIIQ')  # Local documented framing, independent of checker structs.


def crc(raw):
    """Return unsigned standard CRC32 of borrowed bytes without changing them."""
    # Calculate the documented checksum independently of the production checker.
    return zlib.crc32(raw) & 0xffffffff


def repaired(raw, offset):
    """Return owned header bytes with the checksum at byte offset replaced.

    raw is borrowed and offset names its four-byte CRC field. This helper encodes
    only framing; it does not compute an expected physical value.
    """
    # Copy the input, clear its checksum field and replace it with the actual CRC.
    data = bytearray(raw)  # Test-owned mutable header.
    struct.pack_into('<I', data, offset, 0)
    struct.pack_into('<I', data, offset, crc(data))
    return bytes(data)


def split(raw):
    """Split a complete borrowed fixture into its prefix and chunk triples.

    Returns owned (prefix, [(offset, header_tuple, payload), ...]) for these small
    positive test inputs. The final footer is ignored; no fixture bytes are changed.
    """
    # Read the catalog extent, then walk complete documented chunk boundaries.
    offset = 64 + struct.unpack_from('<Q', raw, 24)[0]  # First chunk after bootstrap/catalog.
    prefix = raw[:offset]  # Owned immutable setup bytes.
    chunks = []  # Small local framing table, outside measured reader allocations.
    while raw[offset:offset + 4] == b'PCHK':
        header = HEADER.unpack_from(raw, offset)  # Original scalar chunk facts.
        payload = raw[offset + 80:offset + 80 + header[10]]  # Original stored bytes.
        chunks.append((offset, header, payload))
        offset += 80 + header[10]
    return prefix, chunks


def chunk(kind, stream, sequence, ordinal, payload, *, records=1, previous=0, flags=0):
    """Return a raw-layout1 test chunk from explicit framing and borrowed payload.

    kind/stream select a family, sequence is the global chunk ordinal, ordinal is
    its first frame, records is the batch count, previous the structural backlink,
    and flags the required bit. Values are never inferred from physical geometry.
    """
    # Encode explicit scalar fields and checksum both header and original payload.
    header = HEADER.pack(b'PCHK', kind, flags, 80, 0, stream, 1, sequence,
                         records, ordinal, len(payload), len(payload), crc(payload), 0, previous)  # Local wire header.
    return repaired(header, 68) + payload


class ReadProbe(io.BytesIO):
    """Owned test byte stream recording read boundaries and borrowed-view lifetime.

    Tests replace Path.open only during a check() call. The normal context closes
    the stream; read observations remain available afterward. No fixture is edited.
    Stack inspection records only live memoryview variable names at I/O boundaries,
    avoiding retention of the checker's frames, raw buffers or exception objects.
    """
    def __init__(self, raw):
        """Own raw input bytes and initialize empty bounded test observations."""
        # Initialize ordinary BytesIO and the separate observation lists.
        super().__init__(raw)
        self.reads = []  # Requested (file offset, byte extent) pairs during this small probe.
        self.live_views = []  # Names of unexpected borrowed views alive before a further read.

    def read(self, size=-1):
        """Record the requested size/current offset, then perform the ordinary read.

        size follows BytesIO semantics. Only a direct check() caller is inspected;
        the returned bytes and normal cursor/lifetime behavior remain unchanged.
        """
        # Record extent, inspect borrowed views without retaining the caller, then read.
        self.reads.append((self.tell(), size))
        caller = sys._getframe(1)  # Borrowed immediate caller frame, released before actual I/O.
        if caller.f_code.co_name == 'check':
            self.live_views.extend(name for name, value in caller.f_locals.items() if isinstance(value, memoryview))
        del caller
        return super().read(size)


class MomentCheckerTests(unittest.TestCase):
    """Independent corpus and resource/transaction regression checks for moments.

    Each test borrows class-owned original JSON or creates isolated temporary
    inputs. expected/catalog never mutate, and no test invokes a fixture author,
    build, native provider or renderer. Public check() results are compared with
    literal source values, including every stored binary64 sign bit.
    """
    @classmethod
    def setUpClass(cls):
        """Load immutable expected outcomes and catalog once for the suite."""
        # Read only the frozen manifest and baseline catalog into class-owned objects.
        cls.expected = json.loads((ROOT / 'expected.json').read_text(encoding='utf-8'))  # Independent original frame/value oracles.
        cls.catalog = json.loads((ROOT / 'catalog.json').read_text(encoding='utf-8'))  # Original immutable catalog used for isolated probes.

    def inspect(self, raw, **options):
        """Check owned test bytes through a temporary file and return the result.

        raw is copied only to a test-owned file; options are forwarded unchanged
        to check(). The temporary directory is removed even when validation fails.
        """
        # Create one isolated input and close its lifetime after the public check.
        with tempfile.TemporaryDirectory(prefix='serene-moment-checker-') as temporary:
            path = Path(temporary) / 'input.spose'  # Sole test-owned temporary input.
            path.write_bytes(raw)
            return check(path, **options)

    def assert_values(self, actual, expected):
        """Compare owned decoded values recursively, using exact bits for floats.

        actual is checker output and expected the independent literal oracle.
        Dict/list shapes and Boolean masks are exact; no tolerance, normalization,
        reconstructed quantity or renderer-derived expectation enters comparison.
        """
        # Compare scalar/container identities, then recurse through the original structure.
        self.assertEqual(actual, expected)
        if isinstance(expected, float):
            self.assertEqual(struct.pack('<d', actual), struct.pack('<d', expected))
        elif isinstance(expected, bool):
            self.assertIs(type(actual), bool)
        elif isinstance(expected, dict):
            for key, value in expected.items():  # Original key and independently authored expected child.
                self.assert_values(actual[key], value)
        elif isinstance(expected, list):
            for value, wanted in zip(actual, expected):  # Corresponding complete logical rows/scalars.
                self.assert_values(value, wanted)

    def test_frozen_independent_corpus(self):
        """Bind all205 original artifacts to their hashes and compare entire retained frames."""
        # Check frozen file identities before exercising complete/incomplete/invalid outcomes.
        # Compare every valid whole-group literal value, including signed binary64 zeros.
        self.assertEqual(len(self.expected['cases']), 205)
        for name, wanted in self.expected['cases'].items():  # Original artifact name and independent expected disposition.
            with self.subTest(name=name):
                path = ROOT / name  # Read-only independent fixture.
                raw = path.read_bytes()  # Small owned bytes for the identity check only.
                self.assertEqual(len(raw), wanted['bytes'])
                self.assertEqual(hashlib.sha256(raw).hexdigest(), wanted['sha256'])
                if name == 'opaque_future_planes.spose':
                    # Historical moment-only reader accepted this deliberately opaque plane descriptor.
                    # The full plane-aware reader must reject it; preserve the original oracle unchanged.
                    self.assertTrue(wanted['valid'])
                    with self.assertRaises(InvalidSidecar) as caught:
                        check(path, allow_incomplete=True)
                    self.assertNotIsInstance(caught.exception, IncompleteSidecar)
                    continue
                if not wanted['valid']:
                    with self.assertRaises(InvalidSidecar) as caught:
                        check(path, allow_incomplete=True)
                    self.assertNotIsInstance(caught.exception, IncompleteSidecar)
                    continue
                result = check(path, allow_incomplete=True, dump_frames=True)  # Only validated complete groups can enter this result.
                self.assertEqual(result['complete'], wanted['complete'])
                self.assertEqual(result['validated_frames'], wanted['frames'])
                self.assertEqual(result['grouped_frames'], wanted['frames'])
                self.assertEqual(result['moment_chunks'], result['pose_chunks'])
                for key in ('applied_chunks', 'fluid_chunks', 'feature_chunks'):  # Optional enabled partners commit atomically with moments/poses.
                    if key in result:
                        self.assertEqual(result[key], result['pose_chunks'])
                self.assert_values(result['frames'], self.expected[wanted['frames_key']][:wanted['frames']])
                if not wanted['complete']:
                    self.assertNotIn('completion', result)
                    with self.assertRaises(IncompleteSidecar):
                        check(path)

    def test_binary64_coordinates_preserve_original_json_and_exact_ids(self):
        """Use producer binary64 endpoint equality while preserving authored tokens and ID domains."""
        # Form a minimal valid provider/cut interval around the exact-integer precision boundary.
        # Admit rounded physical endpoints, reject collapsed bounds, and keep scalar IDs exact.
        changed = copy.deepcopy(self.catalog)  # Test-owned catalog; original bytes and parsed values remain untouched.
        descriptor = changed['elastic_moments']  # Mutable independent moment metadata probe.
        origin = 2 ** 53  # First binary64 integer magnitude where adjacent integers may coincide.
        descriptor['source_preparation_generation'] = MAX_INT
        descriptor['provider_addresses'] = [dict(descriptor['provider_addresses'][0], material_begin_m=origin, material_end_m=origin + 4)]
        descriptor['cuts'] = [dict(descriptor['cuts'][0], material_coordinate_m=origin + 1),
                              dict(descriptor['cuts'][-1], id=1, native_element=0, provider_address_id=0, material_coordinate_m=origin + 4)]
        descriptor['sided_breaks'] = []
        descriptor['display_intervals'] = [dict(descriptor['display_intervals'][0], base_segment_id=MAX_INT, cut_ids=[0, 1])]
        changed['segments'][0]['id'] = MAX_INT
        changed['samples'][0]['material_coordinate_m'] = origin
        changed['samples'][1]['material_coordinate_m'] = origin + 5
        before = copy.deepcopy(changed)  # Prove validation does not normalize borrowed JSON.
        layout = validate_moment_catalog(changed)  # Lower cut rounds to provider start; upper base coordinate rounds to provider end.
        self.assertEqual((layout.cuts, layout.frame_bytes, layout.generation), (2, 129, MAX_INT))
        self.assertEqual(changed, before)
        self.assertIs(type(descriptor['cuts'][0]['material_coordinate_m']), int)
        descriptor['provider_addresses'][0]['material_end_m'] = origin + 1
        with self.assertRaisesRegex(InvalidSidecar, 'provider interval'):
            validate_moment_catalog(changed)
        for value in (True, 0.0, MAX_INT + 1):  # Exact ordinal identity tokens must never be converted to binary64.
            changed = copy.deepcopy(self.catalog)
            changed['elastic_moments']['cuts'][0]['id'] = value
            with self.subTest(value=value), self.assertRaises(InvalidSidecar):
                validate_moment_catalog(changed)
        changed = copy.deepcopy(self.catalog)
        changed['elastic_moments']['lines'][0]['native_element_count'] = MAX_SAMPLES
        self.assertEqual(validate_moment_catalog(changed).cuts, 7)
        changed['elastic_moments']['lines'][0]['native_element_count'] += 1
        with self.assertRaisesRegex(InvalidSidecar, 'native element count'):
            validate_moment_catalog(changed)

    def test_optional_structural_rows_use_independent_stable_identity(self):
        """Resolve only the actual optional structural row, without equating its ID to moment ordinal."""
        # Add one valid matched structural row and irrelevant descriptive rows.
        # Validate exact supplied line references and reject duplicate matches or wrong ownership.
        changed = copy.deepcopy(self.catalog)  # Isolated optional metadata probe.
        changed['structural_lines'] = [None, {'id': 'unrelated description'},
            {'id': 999, 'occurrence_id': 21.0, 'local_id': 'N'},
            {'id': 7013, 'occurrence_id': 21, 'local_id': 'N', 'sample_begin': 0, 'sample_count': 2}]
        for row in [changed['segments'][0], *changed['samples'][:2]]:  # The exact mapped segment and its two endpoint samples.
            row['line_id'] = 7013
        self.assertEqual(validate_moment_catalog(changed).cuts, 7)
        changed['samples'][0]['line_id'] = 0
        with self.assertRaisesRegex(InvalidSidecar, 'structural line identity'):
            validate_moment_catalog(changed)
        changed['samples'][0]['line_id'] = 7013
        changed['structural_lines'].append(dict(changed['structural_lines'][-1]))
        with self.assertRaisesRegex(InvalidSidecar, 'exactly one matching'):
            validate_moment_catalog(changed)

    def test_declared_sides_and_provider_provenance_are_not_inferred(self):
        """Keep explicit discontinuities and actual eta without inventing physical continuity or phase proofs."""
        # Admit literal non-recomputed eta and valid phase subsets.
        # Reject explicit wrong-side/cross-provider intervals even when geometry could look continuous.
        for name in ('native_eta_not_recomputed', 'selected_native_elements_only', 'generic_metadata_not_authority',
                     'prepared', 'static', 'first_positive', 'permuted_display_intervals'):
            with self.subTest(name=name):
                self.assertTrue(check(ROOT / (name + '.spose'))['complete'])
        changed = copy.deepcopy(self.catalog)  # Descriptive metadata cannot add a wire phase-membership rule.
        changed['elastic_moments']['source_phases'] = [1]
        self.assertEqual(validate_moment_catalog(changed).cuts, 7)
        for name in ('interval_upper_right', 'interval_lower_left', 'interval_cross_break', 'interval_cross_provider',
                     'interval_overlap', 'pair_missing', 'unmapped_cut'):
            with self.subTest(name=name), self.assertRaises(InvalidSidecar):
                check(ROOT / (name + '.spose'), allow_incomplete=True)
        for value in ('', 'bad\0identity', '\ud800'):  # Strict moment metadata text, including unknown descriptive strings.
            changed = copy.deepcopy(self.catalog)
            changed['elastic_moments']['description'] = value
            with self.subTest(value=repr(value)), self.assertRaises(InvalidSidecar):
                validate_moment_catalog(changed)

    def test_complete_pending_defects_are_not_torn_group_recovery(self):
        """Reject independently detectable complete moment defects before a missing later partner."""
        # Separate complete malformed pending records from genuinely unjoined valid prefixes.
        for name in ('pending_nonfinite_moment', 'pending_wrong_phase', 'pending_wrong_ordinal',
                     'pending_wrong_generation', 'pending_copied_time_disagrees_with_present_F'):
            with self.subTest(name=name), self.assertRaises(InvalidSidecar) as caught:
                check(ROOT / (name + '.spose.partial'), allow_incomplete=True)
            self.assertNotIsInstance(caught.exception, IncompleteSidecar)
        for name in ('pending_moments', 'truncated_winches', 'pending_winches', 'truncated_pose', 'unjoined_pending_time'):
            path = ROOT / (name + '.spose.partial')  # Literal incomplete suffix after earlier complete groups.
            result = check(path, allow_incomplete=True, dump_frames=True)
            wanted = self.expected['cases'][path.name]  # Independent retained-prefix expectation.
            self.assertFalse(result['complete'])
            self.assertEqual(result['validated_frames'], wanted['frames'])
            self.assert_values(result['frames'], self.expected[wanted['frames_key']][:wanted['frames']])

    def test_known_kind_or_stream_cannot_fall_back_to_opaque(self):
        """Either kind7 or stream5 engages strict moments; only unknown future families stay opaque."""
        # Reuse a descriptorless earlier prefix and insert known/unknown discriminators.
        prefix, _ = split((ROOT.parent / 'fixtures_pose_applied/loads_only.spose').read_bytes())  # Earlier valid setup without moments.
        for kind, stream in ((7, 42), (100, 5), (7, 5)):  # Known kind, known stream and exact moment pair.
            with self.subTest(kind=kind, stream=stream), self.assertRaisesRegex(InvalidSidecar, 'moment chunk without catalog'):
                self.inspect(prefix + chunk(kind, stream, 0, 0, b'opaque'), allow_incomplete=True)
        result = self.inspect(prefix + chunk(100, 42, 0, 0, b'opaque'), allow_incomplete=True)  # Truly unknown payload is CRC/bounds checked only.
        self.assertEqual(result['ignored_families'], ['unknown_optional'])
        self.assertIn('elastic_moments', result['implemented_families'])
        self.assertIn('inflow_planes', result['implemented_families'])

    def test_whole_raw_group_admission_precedes_first_payload(self):
        """Reject oversized aggregate known groups from their first header, before any payload read."""
        # Probe each possible first family, preserving legal per-chunk bounds but exceeding the complete raw-group cap.
        for name in ('mixed_transient', 'without_fluid', 'moments_only'):
            with self.subTest(name=name):
                path = ROOT / (name + '.spose')  # Original literal F/load/moment-first example.
                prefix, chunks = split(path.read_bytes())  # Owned framing for a header-only admission probe.
                info = check(path)  # Validated public raw group extent.
                prefix = bytearray(prefix)
                struct.pack_into('<Q', prefix, 40, 64 * 1024 * 1024)
                prefix[:64] = repaired(prefix[:64], 52)
                fields = list(chunks[0][1])  # First family header with no appended payload.
                width = fields[10] // fields[8]  # Independently read original row extent.
                records = MAX_PENDING_RAW // info['raw_group_frame_bytes'] + 1  # First aggregate-overflow batch count.
                fields[8] = records
                fields[10] = fields[11] = records * width
                self.assertLessEqual(fields[10], 64 * 1024 * 1024)
                probe = ReadProbe(bytes(prefix) + repaired(HEADER.pack(*fields), 68))  # Records exact requested reads.
                with patch.object(Path, 'open', return_value=probe), self.assertRaisesRegex(InvalidSidecar, 'pending raw group cap'):
                    check('probe.spose', allow_incomplete=True)
                self.assertEqual(probe.reads[-1], (len(prefix), 80))
        self.assertEqual(MAX_PENDING_RAW, 64 * 1024 * 1024)

    def test_dump_caps_charge_moment_rows_before_retained_allocation(self):
        """Bound explicit history by all known populations and one logical validity byte per moment cut."""
        # Check distinct raw/logical charges, then reject lowered caps before first payload reads.
        # Enforce hard100000-frame/256MiB limits independently of file duration.
        path = ROOT / 'mixed_transient.spose'  # Full independent mixed-family source.
        raw = path.read_bytes()  # Owned read-probe source, outside reader allocations.
        prefix, chunks = split(raw)
        result = check(path, dump_frames=True)  # Owned admitted diagnostic history.
        logical = (64 + 56 * 8) + (80 + 104 * 2) + (80 + 57 * 6) + (80 + 96 * 2 + 49 * 8) + (80 + 25 * 7)  # Conservative per-frame accounting.
        self.assertEqual(result['raw_group_frame_bytes'], 2124)
        self.assertEqual(result['logical_retained_frame_bytes'], logical)
        self.assertEqual(result['logical_retained_bytes'], 3 * logical)
        records = chunks[0][1][8]  # First batch population from the independent original header.
        for options, message in (({'max_dump_bytes': records * logical - 1}, 'logical frame dump byte cap'),
                                 ({'max_dump_frames': records - 1}, 'frame dump cap')):
            probe = ReadProbe(raw)  # Admission must end on the first known header.
            with patch.object(Path, 'open', return_value=probe), self.assertRaisesRegex(InvalidSidecar, message):
                check('probe.spose', dump_frames=True, **options)
            self.assertEqual(probe.reads[-1], (len(prefix), 80))
        self.assertEqual((MAX_DUMP_BYTES, MAX_DUMP_FRAMES), (256 * 1024 * 1024, 100000))
        for options in ({'max_dump_bytes': MAX_DUMP_BYTES + 1}, {'max_dump_frames': MAX_DUMP_FRAMES + 1}):
            with self.assertRaises(InvalidSidecar):
                check(path, dump_frames=True, **options)

    def test_failed_final_pose_cannot_commit_earlier_group_rows(self):
        """A bad second pose row leaves accepted clocks and dump history before the entire group."""
        # Corrupt a complete later row in the first structural batch and repair its framing CRC.
        # Inspect the failing scanner boundary without retaining a traceback past this assertion.
        raw = bytearray((ROOT / 'mixed_transient.spose').read_bytes())  # Test-owned corruption input.
        _, chunks = split(raw)
        offset, fields, original = next(item for item in chunks if item[1][1] == 1)  # First structural counterpart after all complete optional partners.
        self.assertEqual(fields[8], 2)
        payload = bytearray(original)  # Mutable pose batch; all prior feature bytes remain original.
        struct.pack_into('<d', payload, (64 + 56 * 8) + 64, float('nan'))
        fields = list(fields)
        fields[12] = crc(payload)
        raw[offset:offset + 80] = repaired(HEADER.pack(*fields), 68)
        raw[offset + 80:offset + 80 + len(payload)] = payload
        try:
            self.inspect(raw, allow_incomplete=True, dump_frames=True)
        except InvalidSidecar as error:
            trace = error.__traceback__  # Temporary failure stack, used only to observe the transaction boundary.
            while trace is not None and trace.tb_frame.f_code.co_name != 'check':
                trace = trace.tb_next
            self.assertIsNotNone(trace)
            self.assertEqual(trace.tb_frame.f_locals['state'].frames, 0)
            self.assertEqual(trace.tb_frame.f_locals['dumped'], [])
            self.assertEqual(trace.tb_frame.f_locals['moment_chunks'], 0)
            del trace
        else:
            self.fail('complete malformed final partner was admitted')

    def test_pending_views_are_released_before_further_reads(self):
        """Full mixed groups release all borrowed record views before any subsequent I/O."""
        # Track I/O boundaries through two complete mixed groups and their metadata suffix.
        probe = ReadProbe((ROOT / 'mixed_transient.spose').read_bytes())  # Small complete source; no file rewrite.
        with patch.object(Path, 'open', return_value=probe):
            result = check('probe.spose')
        self.assertTrue(result['complete'])
        self.assertEqual(result['moment_chunks'], 2)
        self.assertEqual(probe.live_views, [])
        self.assertNotIn('frames', result)

    def test_default_memory_does_not_follow_duration(self):
        """Stream8/1024 fixed-size moment-only groups without retaining decoded rows or history."""
        # Construct longer inputs by changing only common accepted clocks in original literal rows.
        # Measure checker allocations after construction and require a bounded duration-independent peak.
        prefix, chunks = split((ROOT / 'moments_only.spose').read_bytes())  # Independent setup and literal row templates.
        moment_width = self.expected['moment_frame_bytes']  # Frozen C7 complete raw extent249.
        pose_width = 64 + 56 * self.expected['physical_sample_count']  # Frozen complete structural row extent512.
        moment_template = chunks[0][2][:moment_width]  # Original signed moment values, never recomputed.
        pose_template = chunks[1][2][:pose_width]  # Original physical poses, never inferred from moments.
        peaks = []  # Measured checker-only allocation peaks for the two durations.
        with tempfile.TemporaryDirectory(prefix='serene-moment-memory-') as temporary:
            for count in (8, 1024):  # Equal working sets with substantially different file durations.
                data = bytearray(prefix)  # Test-owned construction outside measured reader allocation.
                previous = 0  # Prior structural chunk offset, initially canonical zero.
                for ordinal in range(count):  # Exact recorder ordinal and accepted clock sequence.
                    moment, pose = bytearray(moment_template), bytearray(pose_template)  # Mutable common stamps with unchanged values.
                    for row in (moment, pose):  # Coherent copies of the same actual accepted endpoint.
                        struct.pack_into('<QIIdQ', row, 0, ordinal, 1, 2 if ordinal == 0 else 4, ordinal * .125, ordinal)
                    struct.pack_into('<I', moment, 72, 2 if ordinal == 0 else 3)
                    data.extend(chunk(7, 5, 2 * ordinal, ordinal, moment))
                    pose_offset = len(data)  # Exact structural chunk position for the next backlink.
                    data.extend(chunk(1, 1, 2 * ordinal + 1, ordinal, pose, previous=previous, flags=1))
                    previous = pose_offset
                path = Path(temporary) / f'{count}.spose.partial'  # Fresh recovery input; no fake completion claim.
                path.write_bytes(data)
                del data, moment, pose
                tracemalloc.start()
                try:
                    result = check(path, allow_incomplete=True)  # Default mode returns counters/catalog, never duration-sized rows.
                    peaks.append(tracemalloc.get_traced_memory()[1])
                finally:
                    tracemalloc.stop()
                self.assertEqual(result['grouped_frames'], count)
                self.assertEqual(result['moment_chunks'], count)
                self.assertNotIn('frames', result)
        self.assertLessEqual(peaks[1], peaks[0] + 64 * 1024, f'default retained memory grew with duration: {peaks}')


if __name__ == '__main__':
    unittest.main()
