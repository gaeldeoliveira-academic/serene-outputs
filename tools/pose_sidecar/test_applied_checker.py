#!/usr/bin/env python3
"""Independent completed-load reader tests; no recorder or fixture author imports.

Frozen corpus bytes and literal manifest rows supply the oracle. Small local
wire rewrites exercise admission and lifetime only, outside measured scans.
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
                           validate_applied_catalog, MAX_PENDING_RAW,
                           MAX_DUMP_BYTES, MAX_DUMP_FRAMES, MAX_INT)

ROOT = Path(__file__).resolve().parents[2] / 'fixtures_pose_applied'  # Read-only independent corpus.
HEADER = struct.Struct('<4sHHIIIIQQQQQIIQ')  # Locally stated fixed header, independent of decoder structs.


def crc(raw):
    """Return standard unsigned CRC32 of borrowed raw bytes."""
    # Compute the independent stdlib checksum without mutating the fixture.
    return zlib.crc32(raw) & 0xffffffff


def repaired(raw, offset):
    """Return owned header bytes with the checksum at offset recomputed."""
    # Copy the header, zero its checksum field, then write the independently computed checksum.
    data = bytearray(raw)  # Only test-owned bytes are changed.
    struct.pack_into('<I', data, offset, 0)
    struct.pack_into('<I', data, offset, crc(data))
    return bytes(data)


def split(raw):
    """Return a complete fixture's prefix and (offset, header, payload) chunks.

    The final footer is excluded. This helper does not validate semantics or
    manufacture expected geometry; callers use hash-bound positive fixtures.
    """
    # Locate the bounded catalog end, then copy each complete fixture chunk description.
    offset = 64 + struct.unpack_from('<Q', raw, 24)[0]  # First chunk offset after bootstrap/catalog bytes.
    prefix = raw[:offset]  # Owned unchanged bootstrap and catalog used by adversarial rewrites.
    chunks = []  # Small local input descriptions, never reader history.
    while raw[offset:offset + 4] == b'PCHK':
        header = HEADER.unpack_from(raw, offset)  # Original scalar wire fields, including stored extent.
        chunks.append((offset, header, raw[offset + 80:offset + 80 + header[10]]))
        offset += 80 + header[10]
    return prefix, chunks


def chunk(kind, stream, sequence, first, payload, *, records=1, previous=0, flags=0):
    """Return an owned test chunk around explicit borrowed payload bytes.

    kind/stream select wire identity; sequence is the global chunk ordinal;
    first/records describe the frame range; previous is a structural backlink;
    flags is the required/optional bitfield. No model or recorder helper is used.
    """
    # Pack the explicit header fields, repair its checksum and append unchanged payload bytes.
    header = HEADER.pack(b'PCHK', kind, flags, 80, 0, stream, 1, sequence,  # New test-owned fixed wire header.
                         records, first, len(payload), len(payload), crc(payload), 0, previous)
    return repaired(header, 68) + payload


class ReadProbe(io.BytesIO):
    """Test-owned byte stream that records every read requested by public check()."""
    def __init__(self, raw):
        """Own the supplied fixture bytes and a fresh list of requested extents."""
        # Initialize the byte-stream owner, then record reads separately from its payload.
        super().__init__(raw)
        self.reads = []  # (file offset, requested size), inspected after closure.

    def read(self, size=-1):
        """Record requested size, then return BytesIO.read(size) without another retained payload.

        size=-1 preserves the ordinary remaining-byte behavior; underlying errors propagate.
        """
        # Save only offset/extent evidence before forwarding the original read request.
        self.reads.append((self.tell(), size))
        return super().read(size)


class AppliedCheckerTests(unittest.TestCase):
    """Exercise completed-source identity, strict grouping and bounded retention.

    The class owns expected JSON and temporary adversarial inputs. Every corpus
    path remains read-only, and passing tests make no numerical rollback claim.
    """
    @classmethod
    def setUpClass(cls):
        """Read the independent literal manifest and setup catalog once."""
        # Own original expectations and catalog data; individual tests copy before mutation.
        cls.expected = json.loads((ROOT / 'expected.json').read_text(encoding='utf-8'))  # Hash-bound independent cases and literal rows.
        cls.catalog = json.loads((ROOT / 'catalog.json').read_text(encoding='utf-8'))  # Shared original setup metadata, kept unchanged.

    def inspect(self, raw, **options):
        """Return check()'s result for borrowed raw bytes with forwarded options.

        Own a temporary file for the call; reader errors propagate after cleanup.
        """
        # Write only a fresh temporary input, invoke the public checker and release the directory.
        with tempfile.TemporaryDirectory(prefix='serene-load-checker-') as temporary:
            path = Path(temporary) / 'test.spose'  # Test-owned transient input, never a corpus artifact.
            path.write_bytes(raw)
            return check(path, **options)

    def assert_frames(self, actual, expected):
        """Compare identities and all binary64 data, preserving signed-zero distinctions."""
        # Compare semantic structures, then every time/pose/force representation bit for bit.
        # Raise unittest failures on disagreement; successful comparison returns None.
        self.assertEqual(actual, expected)
        for got, wanted in zip(actual, expected):  # Decoded frame and its independent literal oracle.
            for key in ('time', 'load_factor'):  # Structural floating identity fields.
                self.assertEqual(struct.pack('<d', got['stamp'][key]), struct.pack('<d', wanted['stamp'][key]))
            for pose, reference in zip(got['poses'], wanted['poses']):  # Structural world xyz/wxyz rows.
                self.assertEqual(struct.pack('<7d', *pose), struct.pack('<7d', *reference))
            for row, reference in zip(got.get('fluid_poses', {}).get('samples', []),
                                      wanted.get('fluid_poses', {}).get('samples', [])):  # Optional independent F rows.
                self.assertEqual(struct.pack('<7d', *row['world_pose']), struct.pack('<7d', *reference['world_pose']))
            for row, reference in zip(got.get('features', []), wanted.get('features', [])):  # Optional A/B/C/D geometry.
                values = [value for point in row['world_xyz_m'] for value in point]  # Decoded Cartesian components in role order.
                reference_values = [value for point in reference['world_xyz_m'] for value in point]  # Original literal role components.
                self.assertEqual(struct.pack('<12d', *values), struct.pack('<12d', *reference_values))
            for attachment, reference in zip(got['applied_loads']['attachments'], wanted['applied_loads']['attachments']):  # Complete model-local populations.
                for key in ('source_time_s', 'end_time_s'):  # Actual interval clock representations.
                    self.assertEqual(struct.pack('<d', attachment['interval'][key]),
                                     struct.pack('<d', reference['interval'][key]))
                for sample, wanted_sample in zip(attachment['samples'], reference['samples']):  # Logical source-point/density row.
                    self.assertIs(type(sample['valid']), bool)
                    for key in ('source_position_world_m', 'force_density_world_N_per_m'):  # World XYZ metres and N/m vectors.
                        self.assertEqual(struct.pack('<3d', *sample[key]), struct.pack('<3d', *wanted_sample[key]))

    def test_frozen_independent_corpus(self):
        """Bind all151 cases to original hashes and compare complete literal output frames."""
        # Verify immutable input hashes, then distinguish complete, recoverable and invalid cases.
        # Compare original rows only from whole validated groups, including explicit recovery prefixes.
        self.assertEqual(len(self.expected['cases']), 151)
        for name, wanted in self.expected['cases'].items():  # Original artifact name and frozen expected outcome.
            with self.subTest(name=name):
                path = ROOT / name  # Read-only independent fixture.
                raw = path.read_bytes()  # Small owned bytes used solely to verify the frozen hash.
                self.assertEqual(len(raw), wanted['bytes'])
                self.assertEqual(hashlib.sha256(raw).hexdigest(), wanted['sha256'])
                if not wanted['valid']:
                    with self.assertRaises(InvalidSidecar) as caught:
                        check(path, allow_incomplete=True)
                    self.assertNotIsInstance(caught.exception, IncompleteSidecar)
                    continue
                result = check(path, allow_incomplete=True, dump_frames=True)  # Owned fully validated groups or earlier complete prefix.
                self.assertEqual(result['complete'], wanted['complete'])
                self.assertEqual(result['validated_frames'], wanted['frames'])
                self.assertEqual(result['grouped_frames'], wanted['frames'])
                self.assertEqual(result['applied_chunks'], result['pose_chunks'])
                for key in ('fluid_chunks', 'feature_chunks'):  # Optional known streams must commit with structural chunks.
                    if key in result:
                        self.assertEqual(result[key], result['pose_chunks'])
                self.assert_frames(result['frames'], self.expected[wanted['frames_key']][:wanted['frames']])
                if not wanted['complete']:
                    self.assertNotIn('completion', result)
                    with self.assertRaises(IncompleteSidecar):
                        check(path)

    def test_opaque_tokens_are_not_accepted_step_arithmetic(self):
        """Admit decreasing source tokens and sparse accepted IDs without invented token relations."""
        # Compare decreasing opaque tokens with increasing accepted endpoints, then exercise valid clock joins.
        result = check(ROOT / 'mixed_transient.spose', dump_frames=True)  # Original mixed-stream literal result.
        first, later = [row['applied_loads']['attachments'][0]['interval'] for row in result['frames'][1:]]  # Two completed source intervals.
        for key in ('publication', 'mechanical_revision', 'state_revision', 'geometry_revision',
                    'environment_revision', 'prepared_step_sequence', 'packet'):  # Opaque identities deliberately unrelated to accepted steps.
            self.assertGreater(first[key], later[key])
        self.assertGreater(later['end_accepted_id'], first['end_accepted_id'])
        for name in ('first_positive', 'prepared_then_positive', 'exact_prior_join', 'zero_time_join', 'zero_end_time'):  # Valid lifecycle/bitwise clock edge cases.
            self.assertTrue(check(ROOT / (name + '.spose'))['complete'])

    def test_stale_and_failed_candidates_are_not_unavailable_intervals(self):
        """Reject stale endpoints/status0 at positive captures and independently bad pending loads."""
        # Reject stale/failed interval facts even in recovery mode, then retain an otherwise valid unjoined prefix.
        names = ('stale_completed_payload', 'failed_candidate_endpoint', 'rollback_attempt_status_zero',  # Complete malformed interval cases.
                 'positive_status_unavailable', 'source_older_than_previous', 'equal_source_id_wrong_time',
                 'larger_source_id_equal_time', 'larger_source_id_earlier_time')
        for name in names:  # A complete malformed payload cannot become an incomplete success.
            with self.subTest(name=name), self.assertRaises(InvalidSidecar):
                check(ROOT / (name + '.spose'), allow_incomplete=True)
        for name in ('pending_nonfinite_density', 'pending_wrong_phase', 'pending_wrong_ordinal', 'pending_stale_end'):  # Independently invalid complete pending loads.
            with self.subTest(name=name), self.assertRaises(InvalidSidecar) as caught:
                check(ROOT / (name + '.spose.partial'), allow_incomplete=True)
            self.assertNotIsInstance(caught.exception, IncompleteSidecar)
        result = check(ROOT / 'unjoined_pending_time.spose.partial', allow_incomplete=True)  # Only prior whole groups may survive the missing partner.
        self.assertEqual(result['validated_frames'], self.expected['cases']['unjoined_pending_time.spose.partial']['frames'])

    def test_domain_bounds_are_not_array_allocation_counts(self):
        """Admit large native/model domains and generic attachment ranges without allocating those domains."""
        # Enlarge identity domains without enlarging rows; then check domain and aggregate-consumer boundaries.
        changed = copy.deepcopy(self.catalog)  # Test-owned metadata copy; the shared source remains unchanged.
        loads = changed['applied_loads']  # Mutable applied descriptor under test.
        loads['attachments'][0]['native_element_count'] = MAX_INT
        loads['models'][0]['logical_node_count'] = MAX_INT
        loads['stations'][0]['native_consumers'][0]['native_element'] = MAX_INT - 1
        loads['probe'] = {'empty_description': '', 'optional_nul_description': '\0', 'finite': 1e308}
        layout = validate_applied_catalog(changed)  # Scalar/count layout, independent of native domain extent.
        self.assertEqual(layout.stations, 8)
        self.assertEqual(layout.frame_bytes, 658)
        for field in ('native_element_count',):  # Normative domain identity to push beyond INT64_MAX.
            changed['applied_loads']['attachments'][0][field] = MAX_INT + 1
            with self.assertRaises(InvalidSidecar):
                validate_applied_catalog(changed)
        for name in ('large_metadata_domains', 'permuted_station_ranges', 'multiple_attachments_one_model', 'repeated_consumer_address'):  # Qualified generic catalog forms.
            self.assertTrue(check(ROOT / (name + '.spose'))['complete'])
        # Eight station rows fit this lowered cap; their nine native consumers must still fail aggregate admission.
        with patch('check_sidecar.MAX_SAMPLES', 8), self.assertRaisesRegex(InvalidSidecar, 'consumer population'):
            validate_applied_catalog(self.catalog)

    def test_required_text_and_finite_descriptive_metadata(self):
        """Keep required UTF8 identities strict while allowing harmless empty descriptive text."""
        # Reject malformed required text and nonfinite descriptive numbers in independent metadata copies.
        for value in ('', 'bad\0id', '\ud800'):  # Empty, embedded-NUL and invalid-UTF8 required identities.
            changed = copy.deepcopy(self.catalog)  # Isolated required-field mutation.
            changed['applied_loads']['attachments'][0]['spanline_id'] = value
            with self.subTest(value=repr(value)), self.assertRaises(InvalidSidecar):
                validate_applied_catalog(changed)
        for value in (float('inf'), float('nan'), 10 ** 400):  # Nonfinite or unrepresentable descriptive scalars.
            changed = copy.deepcopy(self.catalog)  # Isolated optional metadata mutation.
            changed['applied_loads']['probe'] = value
            with self.subTest(value=repr(value)), self.assertRaises(InvalidSidecar):
                validate_applied_catalog(changed)

    def test_material_address_uses_binary64_without_coercing_ids(self):
        """Match C++ coordinate equality while preserving original JSON and full-width identities."""
        # Probe rounded coordinate collisions, distinct representable coordinates and exact integer tokens.
        # Verify admission never rewrites the original parsed JSON values.
        changed = copy.deepcopy(self.catalog)  # Test-owned station-address probe.
        first, second = changed['applied_loads']['stations'][:2]  # Same-attachment logical addresses to compare.
        first['material_coordinate_m'] = 2 ** 53
        second['material_coordinate_m'] = 2 ** 53 + 1
        second['physical_side_token'] = first['physical_side_token']
        second['physical_side'] = first['physical_side']
        original = copy.deepcopy(changed)  # Neither successful nor rejected validation may rewrite source metadata.
        with self.assertRaisesRegex(InvalidSidecar, 'duplicate/contradictory applied logical address'):
            validate_applied_catalog(changed)
        self.assertEqual(changed, original)
        self.assertIs(type(second['material_coordinate_m']), int)
        # The next representable binary64 coordinate remains a distinct physical address.
        second['material_coordinate_m'] = 2 ** 53 + 2
        self.assertEqual(validate_applied_catalog(changed).stations, 8)
        self.assertEqual(second['material_coordinate_m'], 2 ** 53 + 2)
        # Equal represented coordinates with distinct exact tokens are also independent.
        second['material_coordinate_m'] = 2 ** 53 + 1
        first['physical_side_token'], second['physical_side_token'] = MAX_INT, MAX_INT - 1
        original = copy.deepcopy(changed)  # Exact metadata snapshot before successful validation.
        self.assertEqual(validate_applied_catalog(changed).stations, 8)
        self.assertEqual(changed, original)
        self.assertNotEqual(first['physical_side_token'], second['physical_side_token'])

    def test_group_cap_precedes_first_known_payload_read(self):
        """Pre-admit every known population before either an F-first or load-first payload read."""
        # Advertise a legal-sized first chunk whose complete group exceeds the budget.
        # Verify rejection occurs at its header, before a payload read is attempted.
        for name in ('mixed_transient', 'loads_only'):  # Exercise both possible first understood overlay families.
            with self.subTest(name=name):
                raw = (ROOT / (name + '.spose')).read_bytes()  # Original bounded fixture bytes.
                prefix, chunks = split(raw)  # Independent bootstrap/catalog and first-chunk framing.
                info = check(ROOT / (name + '.spose'))  # Admitted complete-group width from the public result.
                prefix = bytearray(prefix)  # Test-owned bootstrap mutation allowing the hard per-chunk limit.
                struct.pack_into('<Q', prefix, 40, 64 * 1024 * 1024)
                prefix[:64] = repaired(prefix[:64], 52)
                fields = list(chunks[0][1])  # Mutable first known header fields; no payload is appended.
                width = fields[10] // fields[8]  # Original first-family bytes per record.
                records = MAX_PENDING_RAW // info['raw_group_frame_bytes'] + 1  # First whole group count exceeding the aggregate cap.
                fields[8] = records
                fields[10] = fields[11] = records * width
                self.assertLessEqual(fields[10], 64 * 1024 * 1024)
                probe = ReadProbe(bytes(prefix) + repaired(HEADER.pack(*fields), 68))  # Records exact public checker read extents.
                with patch.object(Path, 'open', return_value=probe), self.assertRaisesRegex(InvalidSidecar, 'pending raw group cap'):
                    check('probe.spose', allow_incomplete=True)
                self.assertEqual(probe.reads[-1], (len(prefix), 80))

    def test_retained_caps_precede_allocation_and_account_all_stamps(self):
        """Bound retained logical data independently of raw groups before allocating the first batch."""
        # Check the independent per-frame logical charge and rejection before payload reads.
        # Verify caller caps cannot exceed the hard frame and logical-byte ceilings.
        path = ROOT / 'mixed_transient.spose'  # Independent complete mixed-stream source.
        raw = path.read_bytes()  # Probe-owned immutable input bytes.
        prefix, chunks = split(raw)  # Header boundary and first batch population.
        info = check(path, dump_frames=True)  # Explicitly retained bounded decoded result.
        logical = (64 + 56 * 8) + (80 + 104 * 2) + (80 + 57 * 6) + (80 + 96 * 2 + 49 * 8)  # Stamps, values and one logical validity byte per row.
        self.assertEqual(info['logical_retained_frame_bytes'], logical)
        self.assertEqual(info['logical_retained_bytes'], logical * info['validated_frames'])
        records = chunks[0][1][8]  # First known batch's frame count.
        for options, message in (({'max_dump_bytes': records * logical - 1}, 'logical frame dump byte cap'),
                                 ({'max_dump_frames': records - 1}, 'frame dump cap')):  # Caller cap and expected admission diagnostic.
            probe = ReadProbe(raw)  # Track whether admission reaches any payload read.
            with patch.object(Path, 'open', return_value=probe), self.assertRaisesRegex(InvalidSidecar, message):
                check('probe.spose', dump_frames=True, **options)
            self.assertEqual(probe.reads[-1], (len(prefix), 80))
        self.assertEqual((MAX_DUMP_BYTES, MAX_DUMP_FRAMES), (256 * 1024 * 1024, 100000))
        for options in ({'max_dump_bytes': MAX_DUMP_BYTES + 1}, {'max_dump_frames': MAX_DUMP_FRAMES + 1}):  # Requests exceeding documented hard limits.
            with self.assertRaises(InvalidSidecar):
                check(path, dump_frames=True, **options)

    def test_kind_or_stream_engages_load_validation(self):
        """Descriptorless kind6 or stream4 cannot fall through to opaque optional skipping."""
        # Use an F-only descriptor, reject either known load discriminator, then preserve future-family opacity.
        fluid_path = ROOT.parent / 'fixtures_pose_fluid' / 'without_winches.spose'  # Frozen earlier-family input, read-only.
        prefix, _ = split(fluid_path.read_bytes())  # Descriptorless-for-loads bootstrap/catalog only.
        for kind, stream in ((6, 42), (100, 4), (6, 4)):  # Known kind, known stream and exact load tuple respectively.
            with self.subTest(kind=kind, stream=stream), self.assertRaisesRegex(InvalidSidecar, 'applied chunk without catalog'):
                self.inspect(prefix + chunk(kind, stream, 0, 0, b'opaque'), allow_incomplete=True)
        for kind, stream, name in ((100, 42, 'unknown_optional'),):  # Truly unknown future family; all registered families are implemented.
            result = self.inspect(prefix + chunk(kind, stream, 0, 0, b'opaque'), allow_incomplete=True)  # Explicit subset-validation report.
            self.assertEqual(result['ignored_families'], [name])
            self.assertNotIn(name, result['implemented_families'])

    def test_default_memory_does_not_follow_duration(self):
        """Scan8/1024 fixed-size loads-only groups without retaining decoded frames or source histories."""
        # Construct two durations from frozen physical rows, changing only accepted clocks.
        # Measure default checker allocations after construction and compare their bounded peaks.
        prefix, chunks = split((ROOT / 'loads_only.spose').read_bytes())  # Original setup bytes and raw source templates.
        width = self.expected['load_frame_bytes']  # Literal complete applied-record extent.
        load_template = chunks[0][2][width:2 * width]  # Frozen positive frame; only accepted clocks change.
        pose_width = 64 + 56 * self.expected['physical_sample_count']  # Original structural record extent.
        pose_template = chunks[1][2][pose_width:2 * pose_width]  # Frozen positive physical pose row.
        offsets = self.expected['attachment_block_offsets']  # Independent interval-prefix positions for both attachments.
        peaks = []  # Measured reader allocations; file construction is excluded.
        with tempfile.TemporaryDirectory(prefix='serene-load-memory-') as temporary:
            for count in (8, 1024):  # Short and longer runs with identical per-group working-set sizes.
                data = bytearray(prefix)  # Test-owned file construction, outside the allocation measurement.
                previous = 0  # Structural backlink before the first generated group.
                for ordinal in range(count):  # Actual recording ordinal and constructed accepted interval.
                    load, pose = bytearray(load_template), bytearray(pose_template)  # Mutable clock fields with unchanged physical values.
                    source_time, end_time = ordinal * .125, (ordinal + 1) * .125  # Exact dyadic interval clocks in seconds.
                    for row in (load, pose):  # Both common stamps must describe the same accepted endpoint.
                        struct.pack_into('<QIIdQ', row, 0, ordinal, 1, 4, end_time, ordinal + 1)
                    struct.pack_into('<I', load, 72, 3)
                    for offset in offsets:  # Each attachment receives identical actual interval identities.
                        struct.pack_into('<QdQd', load, offset + 8, ordinal, source_time, ordinal + 1, end_time)
                    data.extend(chunk(6, 4, 2 * ordinal, ordinal, load))
                    pose_offset = len(data)  # Next structural chunk position used by backward linkage.
                    data.extend(chunk(1, 1, 2 * ordinal + 1, ordinal, pose, previous=previous, flags=1))
                    previous = pose_offset
                path = Path(temporary) / f'{count}.spose.partial'  # Fresh bounded-duration recovery input.
                path.write_bytes(data)
                del data, load, pose
                tracemalloc.start()
                try:
                    result = check(path, allow_incomplete=True)  # Default result retains counters/catalog only, never decoded rows.
                    peaks.append(tracemalloc.get_traced_memory()[1])
                finally:
                    tracemalloc.stop()
                self.assertEqual(result['grouped_frames'], count)
                self.assertEqual(result['applied_chunks'], count)
                self.assertNotIn('frames', result)
        self.assertLessEqual(peaks[1], peaks[0] + 64 * 1024,
                             f'default retention grew with duration: {peaks}')


if __name__ == '__main__':
    unittest.main()
