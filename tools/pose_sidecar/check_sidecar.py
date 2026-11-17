#!/usr/bin/env python3
"""Independent stdlib Serene .spose 1.0 checker. Does not use the C++ writer.

Default memory: bounded catalog/index maps plus one <=64 MiB chunk for structural
files, or one pending feature chunk and its adjacent pose chunk for winch files.
No duration-sized feature history is retained. Optional --dump-frames is explicitly
frame-capped and may retain both populations; use it only for small fixtures.
"""
from __future__ import annotations
import argparse
from dataclasses import dataclass, replace
import json
import math
from pathlib import Path
import struct
import sys
import zlib

# Catalog byte ceiling checked before JSON materialization.
MAX_CATALOG = 16 * 1024 * 1024
# Per-chunk stored and decoded byte ceiling.
MAX_CHUNK = 64 * 1024 * 1024
# Maximum catalog sample population.
MAX_SAMPLES = 1_000_000
# Maximum wire count/offset admitted to signed host offset arithmetic.
MAX_INT = (1 << 63) - 1
# Absent-last-frame sentinel in empty completion records.
U64_MAX = (1 << 64) - 1
# Canonical 64-byte bootstrap layout.
BOOT = struct.Struct('<8sHHIQQQQIIQ')
# Canonical 80-byte chunk header layout.
CHUNK = struct.Struct('<4sHHIIIIQQQQQIIQ')
# Canonical 64-byte frame stamp layout.
STAMP = struct.Struct('<QIIdQdQQQ')
# Fixed representation-consistency bound, independent of any solver tolerance.
WINCH_CONSISTENCY = 2.0 ** -46
assert BOOT.size == 64 and CHUNK.size == 80 and STAMP.size == 64

class InvalidSidecar(ValueError):
    """Malformed, corrupt or unsupported required wire semantics.

    A checker failure owns its message and unwinds the active file read; callers
    may present it as a failed validation, never as a recoverable valid prefix."""
    # Signal a semantic/CRC failure to check() callers or the CLI error boundary.

class IncompleteSidecar(InvalidSidecar):
    """Valid complete-chunk prefix without a checked publication trailer.

    This InvalidSidecar subtype distinguishes interrupted retention from malformed
    required semantics. check() raises it unless allow_incomplete was explicit."""
    # Identify missing publication bytes after the already validated prefix.

def require(condition, message):
    """Raise InvalidSidecar(message) when condition is false; otherwise return None.

    condition is a computed admission predicate; message is the caller-owned
    diagnostic text. This helper does not mutate the checked data."""
    # Translate a failed contract predicate to the checker exception.
    if not condition:
        raise InvalidSidecar(message)

def uint(value, name):
    """Return value after exact-int admission in [0, MAX_INT]; reject booleans.

    name identifies the field in diagnostics; value is returned unchanged."""
    # Check the wire integer type and signed-offset-compatible range.
    require(type(value) is int and 0 <= value <= MAX_INT, f'{name}: unsigned bounded integer required')
    return value

def string(value, name):
    """Return nonempty string value; name labels a type/empty diagnostic.

    The catalog parser already owns the string; no coercion or normalization occurs."""
    # Require the registered string shape and preserve the decoded value.
    require(isinstance(value, str) and bool(value), f'{name}: nonempty string required')
    return value

def finite(value, name):
    """Return numeric value only when its exact Python type is int/float and finite.

    name labels failures; oversized integer-to-float checks are reported as invalid
    scalars rather than escaping OverflowError. Booleans are not numbers here."""
    # Check the admitted numeric types and finiteness.
    # Convert an overflow during classification into an ordinary admission failure.
    try:
        # Result of exact numeric-type and finiteness admission.
        valid = type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        # Result of exact numeric-type and finiteness admission.
        valid = False
    require(valid, f'{name}: finite number required')
    return value

def object_(value, name):
    """Return value when it is a dict, otherwise diagnose the field named name.

    Borrows and returns the parsed object without copying or mutation."""
    # Require the JSON object representation.
    require(isinstance(value, dict), f'{name}: object required')
    return value

def array(value, name):
    """Return value when it is a list, otherwise diagnose the field named name.

    Borrows and returns the parsed array without copying or mutation."""
    # Require the JSON array representation.
    require(isinstance(value, list), f'{name}: array required')
    return value

def crc(data):
    """Return the unsigned 32-bit CRC-32 of borrowed bytes-like data."""
    # Mask the stdlib CRC result to the wire unsigned width.
    return zlib.crc32(data) & 0xffffffff

def header_crc(data, offset):
    """Return the CRC of header data with its four-byte field at offset zeroed.

    data is borrowed bytes and offset is a byte offset; neither is mutated."""
    # Construct the checksum view with a zeroed checksum field, then calculate CRC-32.
    return crc(data[:offset] + b'\0' * 4 + data[offset + 4:])

def validate_catalog(cat, sample_count):
    """Validate parsed catalog cat against bootstrap sample_count and return cat.

    Checks closed world conventions, occurrence identity/acyclic ownership, dense
    sample order, segment membership and physical-port references. Uses bounded
    catalog-sized indexes; the input object is retained unchanged and not normalized."""
    # Validate version/world labels and build unique occurrence/path indexes.
    # Walk parent chains iteratively, then validate sample, segment and port identities.
    # Check scoped aliases/connections against those exact catalog identities.
    object_(cat, 'catalog')
    require(type(cat.get('catalog_version')) is int and cat['catalog_version'] == 1, 'unsupported catalog version')
    # Current required field or scoped identity key.
    for key in ('recording_id', 'run_id', 'preparation_id'):
        string(cat.get(key), key)
    # Borrowed world-convention object.
    world = object_(cat.get('world'), 'world')
    # Current required field or scoped identity key.
    # Exact supported world-convention spelling.
    for key, wanted in [('axes', 'right_handed_xyz'), ('length_unit', 'm'), ('orientation', 'hamilton_wxyz_local_to_world')]:
        require(world.get(key) == wanted, f'unsupported world {key}')
    # Owned ID-to-descriptor index over the parsed occurrence catalog.
    occurrences = {}
    # Seen occurrence paths for uniqueness.
    paths = set()
    # Current borrowed occurrence descriptor.
    for occurrence in array(cat.get('occurrences'), 'occurrences'):
        object_(occurrence, 'occurrence')
        # Occurrence ID being checked or traversed.
        oid = uint(occurrence.get('id'), 'occurrence.id')
        require(oid not in occurrences, 'duplicate occurrence ID')
        # Exact authored occurrence path used only for uniqueness, without normalization.
        path = string(occurrence.get('path'), 'occurrence.path')
        require(path not in paths, 'duplicate occurrence path')
        paths.add(path)
        require(occurrence.get('kind') in ('assembly', 'flexible', 'rigid', 'anchor'), 'unsupported occurrence kind')
        require('parent_id' in occurrence, 'missing occurrence parent_id')
        if occurrence['parent_id'] is not None:
            uint(occurrence['parent_id'], 'parent_id')
        occurrences[oid] = occurrence
    # Colour walk is O(number of occurrences), not recursive and not O(depth squared).
    # Iterative parent-walk marks: one active, two completely checked.
    colour = {}
    # Occurrence ID being checked or traversed.
    for oid in occurrences:
        # Current parent chain to mark complete after reaching an admitted ancestor.
        trail = []
        # Current ancestor ID, or None for a root.
        node = oid
        while node is not None and colour.get(node, 0) != 2:
            require(node in occurrences, 'missing occurrence parent')
            require(colour.get(node, 0) != 1, 'occurrence hierarchy cycle')
            colour[node] = 1
            trail.append(node)
            # Current ancestor ID, or None for a root.
            node = occurrences[node]['parent_id']
        # Current ancestor ID, or None for a root.
        for node in trail:
            colour[node] = 2
    # Borrowed dense sample catalog defining every frame row.
    samples = array(cat.get('samples'), 'samples')
    require(len(samples) == sample_count, 'sample count/catalog mismatch')
    # Seen (occurrence, physical port) identities.
    port_ids = set()
    # Current dense sample/frame index within the bounded population.
    # Borrowed sample descriptor in frame order.
    for i, sample in enumerate(samples):
        object_(sample, 'sample')
        require(uint(sample.get('id'), 'sample.id') == i, 'sample IDs must be dense frame order')
        # Occurrence ID being checked or traversed.
        oid = uint(sample.get('occurrence_id'), 'sample.occurrence_id')
        require(oid in occurrences, 'missing sample occurrence')
        # Closed sample or chunk kind selecting the applicable layout.
        kind = sample.get('kind')
        require(kind in ('structural_node', 'body_port', 'anchor_port'), 'unsupported sample kind')
        if kind == 'structural_node':
            finite(sample.get('material_coordinate_m'), 'material_coordinate_m')
            require(sample.get('side') in ('none', 'left', 'right'), 'invalid material side')
        else:
            # Current required field or scoped identity key.
            key = (oid, string(sample.get('port_id'), 'port_id'))
            require(key not in port_ids, 'duplicate physical port identity')
            port_ids.add(key)
            require((kind == 'anchor_port') == (occurrences[oid]['kind'] == 'anchor'), 'port/owner kind mismatch')
    # Seen segment IDs.
    segment_ids = set()
    # Borrowed segment descriptor.
    for seg in array(cat.get('segments'), 'segments'):
        object_(seg, 'segment')
        # Current segment identity.
        sid = uint(seg.get('id'), 'segment.id')
        require(sid not in segment_ids, 'duplicate segment ID')
        segment_ids.add(sid)
        # Occurrence ID being checked or traversed.
        oid = uint(seg.get('occurrence_id'), 'segment.occurrence_id')
        require(oid in occurrences, 'missing segment occurrence')
        # Sample IDs already used by the current segment.
        seen = set()
        # Structural sample referenced by the current segment.
        for sample_id in array(seg.get('sample_ids'), 'segment.sample_ids'):
            uint(sample_id, 'segment sample ID')
            require(sample_id not in seen, 'duplicate sample in segment')
            seen.add(sample_id)
            require(sample_id < len(samples), 'segment sample out of bounds')
            require(samples[sample_id]['kind'] == 'structural_node' and samples[sample_id]['occurrence_id'] == oid,
                    'segment must reference structural samples of its occurrence')
    def port_ref(value, name):
        """Require value to address a physical port sample; name labels failures.

        Borrows the enclosing validated samples list and returns None."""
        # Check unsigned range and reject structural-node references in port slots.
        uint(value, name)
        require(value < len(samples) and samples[value]['kind'] != 'structural_node', f'{name}: physical port required')
    # Seen scoped alias identities.
    alias_ids = set()
    # Borrowed alias descriptor.
    for alias in array(cat.get('port_aliases'), 'port_aliases'):
        object_(alias, 'alias')
        # Occurrence ID defining the alias/connection scope.
        scope = uint(alias.get('scope_id'), 'alias.scope_id')
        require(scope in occurrences, 'missing alias scope')
        # Current required field or scoped identity key.
        key = (scope, string(alias.get('alias'), 'alias'))
        require(key not in alias_ids, 'duplicate scoped alias')
        alias_ids.add(key)
        port_ref(alias.get('sample_id'), 'alias.sample_id')
    # Seen connection IDs.
    connection_ids = set()
    # Borrowed connection descriptor.
    for conn in array(cat.get('connections'), 'connections'):
        object_(conn, 'connection')
        # Current connection identity.
        cid = uint(conn.get('id'), 'connection.id')
        require(cid not in connection_ids, 'duplicate connection ID')
        connection_ids.add(cid)
        string(conn.get('kind'), 'connection.kind')
        # Occurrence ID defining the alias/connection scope.
        scope = uint(conn.get('scope_id'), 'connection.scope_id')
        require(scope in occurrences, 'missing connection scope')
        require('junction_id' in conn, 'missing junction_id')
        if conn['junction_id'] is not None:
            string(conn['junction_id'], 'junction_id')
        # Endpoint role selecting first/second registered fields.
        for prefix in ('first', 'second'):
            string(conn.get(prefix + '_endpoint_id'), prefix + '_endpoint_id')
            port_ref(conn.get(prefix + '_sample_id'), prefix + '_sample_id')
    return cat

@dataclass(frozen=True)
class WinchLayout:
    """Owned bounded feature admission map derived from the immutable catalog.

    check() uses this map to join each A/B/D to a canonical physical sample. It
    stores no geometry, solver objects or per-frame history. All entries remain
    valid for the file lifetime because catalog topology cannot change.
    """
    generation: int  # Core context generation, distinct from capture-plan identity.
    ports: tuple[tuple[int, int, int], ...]  # A/B/D physical sample IDs per arrangement.
    frame_bytes: int  # Exact raw extent of one copied-stamp/source/geometry record.


def validate_winch_catalog(cat):
    """Return WinchLayout for an admitted descriptor, or None when absent.

    cat borrows the already validated structural catalog. Checks exact optional
    stream identity, dense arrangement order, canonical occurrence/force keys,
    role topology and physical provenance. Invalid metadata raises InvalidSidecar.
    """
    # Admit the optional descriptor and build bounded occurrence/reference indexes.
    # Validate each exact arrangement identity and its four roles/three edges.
    # Return only the generation and physical sample references needed by the scan.
    if 'winch_geometry' not in cat:
        return None
    descriptor = object_(cat['winch_geometry'], 'winch_geometry')  # Borrowed metadata.
    for name, wanted in (('layout_version', 1), ('kind', 4), ('stream_id', 2)):
        require(uint(descriptor.get(name), 'winch_geometry.' + name) == wanted,
                'unsupported winch descriptor tuple')
    require(descriptor.get('encoding') == 'raw_f64_le', 'unsupported winch descriptor encoding')
    generation = uint(descriptor.get('source_preparation_generation'), 'winch generation')
    require(generation > 0, 'winch generation must be positive')
    rows = array(descriptor.get('arrangements'), 'winch arrangements')  # Immutable order.
    require(0 < len(rows) <= MAX_SAMPLES, 'winch arrangement count out of bounds')
    occurrences = {item['id']: item for item in cat['occurrences']}  # Exact owner lookup.
    samples = cat['samples']  # Borrowed existing physical samples, including ports.
    identities = set()  # Duplicate occurrence/force identity detector.
    ports = []  # Small catalog-sized map retained by the returned layout.
    expected_edges = [([0, 2], 'bridle'), ([2, 1], 'bridle'), ([2, 3], 'main_cable')]
    for index, row in enumerate(rows):
        object_(row, 'winch arrangement')
        require(uint(row.get('index'), 'winch index') == index, 'winch indices must be dense')
        owner = uint(row.get('owner_occurrence_id'), 'winch owner')  # Assembly occurrence.
        require(owner in occurrences and occurrences[owner]['kind'] == 'assembly',
                'winch owner must be an assembly occurrence')
        force = string(row.get('force_id'), 'winch force_id')  # Exact local force spelling.
        path = occurrences[owner]['path']  # Canonical occurrence path from structural metadata.
        require('\0' not in force and '\0' not in path, 'winch identity contains NUL')
        try:
            key = f"winch_with_saddle.v1.o{len(path.encode('utf-8'))}_{path}.f{len(force.encode('utf-8'))}_{force}"
        except UnicodeEncodeError as error:
            raise InvalidSidecar('winch identity is not valid UTF-8') from error
        require(row.get('key') == key, 'winch canonical key mismatch')
        require((owner, force) not in identities, 'duplicate winch identity')
        identities.add((owner, force))
        require(row.get('feature_type') == 'winch_with_saddle', 'unsupported winch feature type')
        points = array(row.get('points'), 'winch points')  # Four immutable role descriptors.
        require(len(points) == 4, 'winch requires exactly four roles')
        physical = []  # A/B/D references in role order; C contributes no physical identity.
        for local, role in enumerate(('A', 'B', 'C', 'D')):
            point = object_(points[local], 'winch point')  # This exact local role.
            require(point.get('role') == role, 'winch roles must be A/B/C/D exactly once')
            if role == 'C':
                require(point.get('kind') == 'algebraic' and point.get('feature') == 'pulley',
                        'winch C requires algebraic pulley provenance')
                require(set(point) == {'role', 'kind', 'feature'},
                        'winch C must not fabricate physical identity or pose')
                continue
            require(point.get('kind') == 'physical_port', 'winch physical port role required')
            sample_id = uint(point.get('sample_id'), 'winch sample_id')  # Canonical reference.
            require(sample_id < len(samples), 'winch sample reference out of bounds')
            wanted_kind = 'anchor_port' if role == 'D' else 'body_port'
            require(samples[sample_id]['kind'] == wanted_kind, 'winch physical port kind mismatch')
            physical.append(sample_id)
        require(physical[0] != physical[1] and
                samples[physical[0]]['occurrence_id'] == samples[physical[1]]['occurrence_id'],
                'winch A/B must be distinct ports of one body occurrence')
        edges = array(row.get('edges'), 'winch edges')  # Explicit visualization topology.
        require(len(edges) == 3, 'winch requires exactly three edges')
        for edge, (wanted_points, wanted_kind) in zip(edges, expected_edges):
            object_(edge, 'winch edge')
            ids = array(edge.get('points'), 'winch edge points')  # Two local point indices.
            require(len(ids) == 2 and all(type(value) is int for value in ids) and
                    ids == wanted_points and edge.get('kind') == wanted_kind,
                    'winch edge topology mismatch')
        ports.append(tuple(physical))
    return WinchLayout(generation, tuple(ports), 80 + 104 * len(ports))


def consume_winch(raw, pose_raw, layout, collect):
    """Validate one complete feature record against its adjacent structural frame.

    Both byte views are borrowed only during this call; pose_raw=None checks a
    pending record's own semantics before its partner arrives. layout owns the admitted
    generation/reference map. Return an owned feature dictionary only for capped
    dumps; otherwise None. Positions are never overwritten or normalized.
    """
    # Join exact structural stamps and the owning generation/source phase.
    # Validate raw finite XYZ/status entries and their fixed representation consistency.
    # Optionally preserve the original values for a bounded diagnostic dump.
    if pose_raw is not None:
        require(raw[:64] == pose_raw[:64], 'winch/pose stamp mismatch')
    generation, phase, reserved = struct.unpack_from('<QII', raw, 64)
    require(generation == layout.generation and reserved == 0, 'winch generation/reserved mismatch')
    _, analysis, events, _, state_id, _, _, _, _ = STAMP.unpack_from(raw)
    valid_phase = ((phase == 0 and analysis == 0) or (phase == 1 and analysis == 2) or
                   (phase == 2 and analysis == 1 and state_id == 0 and events & 2 != 0) or
                   (phase == 3 and analysis == 1 and state_id > 0 and events & 2 == 0))
    require(valid_phase, 'winch source phase/stamp mismatch')
    features = [] if collect else None  # Optional decoded rows, never retained by default.
    for index, physical in enumerate(layout.ports):
        status, reserved, *xyz = struct.unpack_from('<II12d', raw, 80 + 104 * index)
        require(status in (0, 1) and reserved == 0, 'winch geometry status/reserved mismatch')
        require(all(math.isfinite(value) for value in xyz), 'nonfinite winch coordinate')
        for local, sample_id in zip((0, 1, 3), physical) if pose_raw is not None else ():
            p = struct.unpack_from('<3d', pose_raw, 64 + 56 * sample_id)  # Canonical physical XYZ.
            f = xyz[3 * local:3 * local + 3]  # Original accepted feature XYZ, unchanged.
            require(all(math.isfinite(value) for value in p), 'nonfinite physical port coordinate')
            scale = max(1.0, *(abs(value) for value in f), *(abs(value) for value in p))
            bound = WINCH_CONSISTENCY * scale  # Fixed roundoff consistency, not solver accuracy.
            require(all(abs(fv - pv) <= bound for fv, pv in zip(f, p)),
                    'winch/physical coordinate consistency exceeded')
        if collect:
            features.append({'geometry_status': status,
                             'world_xyz_m': [xyz[begin:begin + 3] for begin in (0, 3, 6, 9)]})
    return {'source_generation': generation, 'source_phase': phase, 'features': features} if collect else None

@dataclass
class FrameState:
    """Constant-size accepted-frame validation state, excluding optional dumps.

    check() copies this state before each pose chunk and commits it only after
    every frame validates. Stamp dictionaries are owned; the raw frame bytes are
    borrowed only during consume(). A target event makes the state terminal."""
    # Store only counters, prior transient identity/time and the last accepted stamp.
    # Number of fully accepted frames.
    frames: int = 0
    # Current or incoming analysis kind; None before an actual analysis frame.
    analysis: int | None = None
    # Last accepted transient state identity, absent before first transient frame.
    transient_id: int | None = None
    # Last accepted transient time in seconds, absent initially.
    transient_time: float | None = None
    # Whether a target event prohibits any later frame.
    terminal: bool = False
    # Owned last accepted stamp dictionary, absent before any frame.
    last_stamp: dict | None = None

    def consume_stamp(self, raw, sample_count):
        """Validate a borrowed 64-byte structural stamp and advance candidate clocks.

        Used for both staged features and structural records. The caller commits
        this copied state only after the corresponding complete pair validates.
        Returns an owned scalar stamp; no raw-byte view is retained.
        """
        # Validate exact ordinal/count, analysis-specific events and monotone clocks.
        # Update only this candidate state, then retain the last immutable scalar facts.
        ordinal, analysis, events, time, state_id, load_factor, count, r0, r1 = STAMP.unpack_from(raw)
        require(not self.terminal, 'frame after target')
        require(ordinal == self.frames, 'nonconsecutive frame ordinal')
        require(count == sample_count and r0 == 0 and r1 == 0, 'invalid frame count/reserved fields')
        require(state_id <= MAX_INT and math.isfinite(time) and math.isfinite(load_factor), 'invalid stamp scalar')
        if analysis == 0:
            require(self.frames == 0 and events == 1 and state_id == 0 and load_factor == 0, 'invalid prepared stamp')
        elif analysis == 1:
            require(self.analysis in (None, 1) and events != 0 and events & ~14 == 0 and load_factor == 0,
                    'invalid transient stamp')
            if events & 2:
                require(self.transient_id is None and state_id == 0, 'initial must be first transient step zero')
            if self.transient_id is not None:
                require(state_id > self.transient_id and time > self.transient_time, 'transient clocks must increase')
            self.analysis = 1
            self.transient_id = state_id
            self.transient_time = time
        elif analysis == 2:
            require(self.analysis is None and events == 8, 'invalid/static mixed analysis stamp')
            self.analysis = 2
        else:
            raise InvalidSidecar('unsupported frame analysis')
        self.terminal = bool(events & 8)
        stamp = dict(ordinal=ordinal, analysis=analysis, events=events, time=time,
                     state_id=state_id, load_factor=load_factor)  # Owned last-stamp facts.
        self.last_stamp = stamp
        self.frames += 1
        return stamp

    def consume(self, raw, sample_count, collect):
        """Validate one complete borrowed pose frame and advance candidate state.

        sample_count fixes the frame shape; collect requests a capped owned dump.
        Invalid clocks/poses raise InvalidSidecar before the caller commits a batch.
        """
        # Admit the stamp using shared clock rules, then check each world pose.
        # Return original values only when the caller requested the bounded dump.
        stamp = self.consume_stamp(raw, sample_count)  # Admitted source-state facts.
        # Optional owned pose rows, absent when no frame dump was requested.
        poses = [] if collect else None
        # Current dense sample/frame index within the bounded population.
        for i in range(sample_count):
            # Current seven-double world pose in xyz then Hamilton wxyz order.
            pose = struct.unpack_from('<7d', raw, 64 + 56*i)
            # Scalar pose component or rejected nonfinite JSON spelling.
            require(all(math.isfinite(x) for x in pose), 'nonfinite pose')
            # Scalar pose component or rejected nonfinite JSON spelling.
            require(abs(sum(x*x for x in pose[3:]) - 1) <= 1e-6, 'nonunit quaternion')
            if collect:
                poses.append(list(pose))
        return dict(stamp=stamp, poses=poses) if collect else None

def check(path, *, allow_incomplete=False, dump_frames=False, max_dump_frames=1000):
    """Validate path in file order and return an owned result dictionary.

    path is an input filename. allow_incomplete permits only a valid complete-chunk
    prefix without the final trailer; corrupt required semantics always fail.
    dump_frames retains poses and any features only under nonnegative max_dump_frames.
    Default payload storage is one chunk, or at most two chunk-limit buffers for
    paired features, plus catalog/reference maps and counters; I/O errors propagate."""
    # Validate bootstrap bounds/checksums and parse the owned UTF-8 catalog.
    # Scan chunks in order, committing only fully validated pose-chunk candidates.
    # Check seek index, completion and footer, or report an explicitly allowed valid prefix.
    # Normalized Path wrapper for the caller input filename.
    path = Path(path)
    require(type(max_dump_frames) is int and max_dump_frames >= 0, 'invalid dump cap')
    # Owned checker result dictionary populated only with validated state.
    result = dict(format='Serene pose sidecar', version='1.0', complete=False,
                  validated_frames=0, pose_chunks=0, optional_chunks=0)
    # Optional bounded list of owned decoded frame dictionaries.
    dumped = []
    # Read-only file handle owned by the active context manager.
    with path.open('rb') as f:
        # Exact fixed-header bytes read from the current file position.
        raw = f.read(64)
        require(len(raw) == 64, 'truncated bootstrap')
        # Wire discriminator required for the current bootstrap/chunk/footer.
        # Wire major format version.
        # Wire minor format version.
        # Wire header byte size.
        # Bootstrap required-feature bits.
        # Admitted UTF-8 catalog byte extent.
        # Bootstrap-declared sample count validated against the catalog.
        # Bootstrap-declared per-chunk byte ceiling.
        # Expected catalog CRC-32.
        # Expected bootstrap CRC-32.
        # Reserved zero header/footer field.
        magic, major, minor, size, features, cat_bytes, samples, chunk_limit, cat_crc, hdr_crc, reserved = BOOT.unpack(raw)
        require(magic == b'SRNPOSE\0' and size == 64, 'invalid bootstrap magic/size')
        require(header_crc(raw, 52) == hdr_crc, 'bootstrap checksum mismatch')
        require(major == 1 and minor == 0 and features == 0, 'unsupported version/required feature')
        require(reserved == 0, 'bootstrap reserved nonzero')
        require(0 < cat_bytes <= MAX_CATALOG and samples <= MAX_SAMPLES and 64 <= chunk_limit <= MAX_CHUNK,
                'bootstrap bounds exceeded')
        # Exact frame extent: fixed stamp plus one pose per sample.
        frame_bytes = 64 + 56*samples
        require(frame_bytes <= chunk_limit, 'frame exceeds chunk limit')
        # Owned bounded catalog bytes, verified before parsing.
        cat_raw = f.read(cat_bytes)
        require(len(cat_raw) == cat_bytes and crc(cat_raw) == cat_crc, 'catalog truncation/checksum mismatch')
        try:
            # Owned parsed catalog retained in the returned result.
            # Check every floating token, including unrecognized descriptive metadata.
            cat = json.loads(cat_raw.decode('utf-8'),
                             parse_float=lambda token: finite(float(token), 'catalog JSON number'),
                             parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))
        # Expected parse/validation/I/O exception reported at its public boundary.
        except (ValueError, UnicodeDecodeError, RecursionError) as e:
            raise InvalidSidecar(f'invalid UTF-8 JSON catalog: {e}') from e
        validate_catalog(cat, samples)
        result.update(sample_count=samples, catalog=cat)
        # Optional immutable feature map; no source geometry or history is retained.
        winch = validate_winch_catalog(cat)
        if winch is not None:
            require(winch.frame_bytes <= chunk_limit, 'winch frame exceeds chunk limit')
            result.update(feature_count=len(winch.ports), feature_chunks=0, paired_frames=0,
                          raw_payload_memory_bound_bytes=2 * chunk_limit)
        # Accepted frame state; updated only after the complete pose chunk validates.
        state = FrameState()
        # Next required global chunk sequence number.
        # Number of fully validated pose chunks.
        # Number of skipped, CRC-checked optional chunks.
        sequence = pose_chunks = optional_chunks = 0
        # Complete feature chunks are counted only with their validated pose partner.
        feature_chunks = 0
        # One bounded raw feature batch waiting for the immediately adjacent pose batch.
        pending_feature = None
        # Byte offset of the first pose chunk, zero for empty recordings.
        # Byte offset of the last accepted pose chunk, also the next previous-link expectation.
        # Byte offset of the admitted seek-index chunk.
        # Byte offset of the admitted completion chunk.
        first_pose = last_pose = index_offset = completion_offset = 0
        # Required publication order: poses, completion, then footer.
        phase = 'poses'
        # Owned completion outcome/count summary, absent until its chunk validates.
        completion = None
        def incomplete(reason):
            """Return the current valid-prefix result or raise IncompleteSidecar(reason).

            reason explains the missing bytes. Captures only accepted chunk state;
            optional dumped frames are included only when requested."""
            # Publish accepted-prefix counters and the interruption reason.
            # Require explicit recovery mode before returning an incomplete result.
            result.update(validated_frames=state.frames, pose_chunks=pose_chunks,
                          optional_chunks=optional_chunks, last_stamp=state.last_stamp, reason=reason)
            if winch is not None:
                result.update(paired_frames=state.frames, feature_chunks=feature_chunks)
            if dump_frames:
                result['frames'] = dumped
            if not allow_incomplete:
                raise IncompleteSidecar(reason)
            return result
        while True:
            # Byte offset of the next chunk or footer.
            offset = f.tell()
            require(offset <= MAX_INT, 'file offset overflow')
            if phase == 'footer':
                # Exact final 32-byte publication footer.
                footer = f.read(32)
                if len(footer) != 32:
                    return incomplete('missing/truncated footer')
                # Wire discriminator required for the current bootstrap/chunk/footer.
                # Footer/completion pointer to the admitted seek-index chunk.
                # Footer pointer to the admitted completion chunk.
                # Expected payload/footer CRC-32.
                # Reserved zero header/footer field.
                magic, idx, comp, checksum, reserved = struct.unpack('<8sQQII', footer)
                require(magic == b'POSEEND\0' and reserved == 0 and header_crc(footer, 24) == checksum,
                        'invalid footer/checksum')
                require(idx == index_offset and comp == completion_offset, 'footer offsets mismatch')
                require(f.read(1) == b'', 'trailing bytes after footer')
                result.update(complete=True, validated_frames=state.frames, pose_chunks=pose_chunks,
                              optional_chunks=optional_chunks, last_stamp=state.last_stamp,
                              completion=completion, index_offset=index_offset,
                              first_pose_offset=first_pose, last_pose_offset=last_pose)
                if winch is not None:
                    result.update(paired_frames=state.frames, feature_chunks=feature_chunks)
                if dump_frames:
                    result['frames'] = dumped
                return result
            # Exact fixed-header bytes read from the current file position.
            raw = f.read(80)
            if len(raw) != 80:
                return incomplete('missing/truncated next chunk')
            # Wire discriminator required for the current bootstrap/chunk/footer.
            # Closed sample or chunk kind selecting the applicable layout.
            # Chunk required/optional bitfield.
            # Wire header byte size.
            # Chunk payload encoding identifier.
            # Chunk stream identifier.
            # Chunk schema identifier.
            # Wire sequence number compared with the expected global counter.
            # Frame/record population declared by this chunk.
            # First frame ordinal declared by this chunk.
            # Stored chunk payload byte extent.
            # Decoded payload extent, equal to stored for supported raw chunks.
            # Expected payload/footer CRC-32.
            # Expected chunk-header CRC-32.
            # Previous pose-chunk byte offset or canonical zero for metadata chunks.
            magic, kind, flags, size, encoding, stream, layout, seq, records, first, stored, decoded, checksum, hcrc, prev = CHUNK.unpack(raw)
            require(magic == b'PCHK' and size == 80 and header_crc(raw, 68) == hcrc, 'invalid chunk header/checksum')
            require(flags & ~1 == 0 and seq == sequence, 'invalid chunk flags/sequence')
            # Scalar pose component or rejected nonfinite JSON spelling.
            require(all(x <= MAX_INT for x in (seq, records, first, stored, decoded, prev)), 'chunk integer overflow')
            require(stored <= chunk_limit and decoded <= chunk_limit and offset + 80 + stored <= MAX_INT,
                    'chunk bounds exceeded')
            # Known winch discriminators engage strict validation before optional skipping.
            winch_family = kind == 4 or stream == 2
            if winch_family:
                require(winch is not None, 'winch chunk without catalog descriptor')
                require((kind, stream, layout, encoding, flags) == (4, 2, 1, 0, 0),
                        'unsupported winch chunk semantics')
                require(phase == 'poses' and pending_feature is None, 'invalid winch chunk ordering')
                require(records > 0 and first == state.frames and prev == 0 and
                        stored == decoded == records * winch.frame_bytes and
                        state.frames + records <= MAX_INT, 'winch count/byte mismatch')
            # Whether kind, layout, encoding and stream select implemented structural semantics.
            known = kind in (1, 2, 3) and layout == 1 and encoding == 0 and stream == (1 if kind == 1 else 0)
            require(known or winch_family or not flags & 1, 'unsupported required chunk semantics')
            if pending_feature is not None:
                require(known and kind == 1 and flags == 1, 'winch batch must immediately precede required poses')
            # Owned bounded chunk bytes after complete read and CRC verification.
            payload = f.read(stored)
            if len(payload) != stored:
                return incomplete('truncated chunk payload')
            require(crc(payload) == checksum, 'payload checksum mismatch')
            sequence += 1
            if winch_family:
                # Validate the complete feature chunk itself even if a later pose is truncated.
                # Malformed known semantics are errors, not a recoverable pending prefix.
                candidate = replace(state)
                feature_views = memoryview(payload)
                for i in range(records):
                    candidate.consume_stamp(feature_views[i*winch.frame_bytes:(i+1)*winch.frame_bytes], samples)
                    consume_winch(feature_views[i*winch.frame_bytes:(i+1)*winch.frame_bytes], None, winch, False)
                del feature_views
                pending_feature = (payload, records, first)
                del payload  # The pending pair is the sole owner until its pose arrives.
                continue
            if not known:
                require(phase == 'poses', 'optional chunk after seek index')
                optional_chunks += 1
                del payload  # Do not retain a skipped raw chunk while reading the next one.
                continue
            require(stored == decoded, 'raw stored/decoded length mismatch')
            if kind == 1:
                require(phase == 'poses' and records > 0 and first == state.frames and prev == last_pose,
                        'invalid pose chunk ordering/index')
                require(records * frame_bytes == stored and state.frames + records <= MAX_INT, 'pose count/byte mismatch')
                if winch is not None:
                    require(pending_feature is not None and
                            (pending_feature[1], pending_feature[2]) == (records, first),
                            'pose batch missing or mismatching winch pair')
                if dump_frames:
                    require(state.frames + records <= max_dump_frames, 'frame dump cap exceeded')
                # Copy of accepted frame state committed only after the whole chunk validates.
                candidate = replace(state)
                # Borrowed byte view over the current payload; never retained in the result.
                views = memoryview(payload)
                # Feature bytes remain borrowed from the single pending batch.
                feature_views = memoryview(pending_feature[0]) if pending_feature is not None else None
                # Dumps are committed with the whole pair, never one earlier row of a failed batch.
                batch_dump = [] if dump_frames else None
                # Current dense sample/frame index within the bounded population.
                for i in range(records):
                    # Optional owned decoded frame produced by the candidate state.
                    item = candidate.consume(views[i*frame_bytes:(i+1)*frame_bytes], samples, dump_frames)
                    if winch is not None:
                        details = consume_winch(feature_views[i*winch.frame_bytes:(i+1)*winch.frame_bytes],
                                                views[i*frame_bytes:(i+1)*frame_bytes], winch, dump_frames)
                        if dump_frames:
                            item.update(details)
                    if dump_frames:
                        batch_dump.append(item)
                # Accepted frame state; updated only after the complete pose chunk validates.
                state = candidate
                if dump_frames:
                    dumped.extend(batch_dump)
                if winch is not None:
                    feature_chunks += 1
                # Release all raw views before reading another batch, bounding peak retention.
                del views, feature_views
                pending_feature = None
                pose_chunks += 1
                if first_pose == 0:
                    # Byte offset of the first pose chunk, zero for empty recordings.
                    first_pose = offset
                # Byte offset of the last accepted pose chunk, also the next previous-link expectation.
                last_pose = offset
            elif kind == 2:
                require(phase == 'poses' and records == 1 and first == 0 and prev == 0 and stored == 32,
                        'invalid seek index chunk')
                require(struct.unpack('<4Q', payload) == (pose_chunks, state.frames, first_pose, last_pose),
                        'seek index differs from validated pose chunks')
                # Byte offset of the admitted seek-index chunk.
                index_offset = offset
                # Required publication order: poses, completion, then footer.
                phase = 'completion'
            else:
                require(phase == 'completion' and records == 1 and first == 0 and prev == 0 and stored == 64,
                        'invalid completion chunk')
                # Closed completion run outcome.
                # Closed completion capture outcome.
                # Completion recorder state, required to indicate checked publication.
                # Reserved zero field from the frame/completion layout.
                # Completion accepted-frame count.
                # Completion persisted-frame count.
                # Completion pose-chunk count.
                # Completion last-frame ordinal, or U64_MAX for an empty recording.
                # Footer/completion pointer to the admitted seek-index chunk.
                run, capture, recorder, r0, accepted, persisted, nchunks, last, idx, r1 = struct.unpack('<4I6Q', payload)
                require(run <= 3 and capture <= 3 and recorder == 1 and r0 == r1 == 0, 'invalid completion outcomes')
                require((accepted, persisted, nchunks, last, idx) ==
                        (state.frames, state.frames, pose_chunks, state.frames-1 if state.frames else U64_MAX, index_offset),
                        'completion differs from validated prefix')
                # Owned completion outcome/count summary, absent until its chunk validates.
                completion = dict(run=run, capture=capture, recorder=recorder,
                                  accepted_frames=accepted, persisted_frames=persisted)
                # Byte offset of the admitted completion chunk.
                completion_offset = offset
                # Required publication order: poses, completion, then footer.
                phase = 'footer'
            del payload  # Completed chunks never survive into the next bounded read.

def main(argv=None):
    """Parse argv and print one JSON checker result; return process status 0 or 2.

    argv=None uses the process arguments. Invalid sidecars and I/O errors produce
    stderr JSON; argparse owns usage errors. Module __doc__ supplies CLI help."""
    # Parse file/recovery/dump options without changing defaults.
    # Run the independent checker and translate expected failures to stderr/status two.
    # Print the successful result as finite JSON and return zero.
    # CLI parser whose description uses the unchanged module docstring.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', type=Path)
    parser.add_argument('--allow-incomplete', action='store_true')
    parser.add_argument('--dump-frames', action='store_true')
    parser.add_argument('--max-dump-frames', type=int, default=1000)
    # Parsed input path and explicit checker options.
    args = parser.parse_args(argv)
    try:
        # Owned checker result dictionary populated only with validated state.
        result = check(args.path, allow_incomplete=args.allow_incomplete,
                       dump_frames=args.dump_frames, max_dump_frames=args.max_dump_frames)
    # Expected parse/validation/I/O exception reported at its public boundary.
    except (InvalidSidecar, OSError) as e:
        print(json.dumps({'valid': False, 'error': str(e)}), file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
