#!/usr/bin/env python3
"""Independent stdlib Serene .spose 1.0 checker. Does not use the C++ writer.

Default memory: bounded catalog/index maps plus one <=64 MiB chunk for structural
files, or one pending feature chunk and its adjacent pose chunk for winch files.
F/load/moment/plane groups share a64MiB raw cap; one opaque optional chunk may add64MiB.
No duration-sized feature history is retained. Optional --dump-frames is bounded
by100000 frames and256MiB conservative logical data, excluding Python overhead.
"""
from __future__ import annotations
import argparse
from bisect import bisect_left, bisect_right
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
# Shared new-overlay known-group raw ceiling; an additional skipped chunk may coexist.
MAX_PENDING_RAW = 64 * 1024 * 1024
# Hard caps on explicitly retained logical frame data; Python object overhead is separate.
MAX_DUMP_BYTES = 256 * 1024 * 1024
MAX_DUMP_FRAMES = 100_000
# Canonical unavailable F row; comparison is byte-exact, including positive zero signs.
UNAVAILABLE_FLUID_POSE = struct.pack('<7d', 0, 0, 0, 1, 0, 0, 0)
# Descriptor registry; the current reader implements all five optional families below.
OPTIONAL_FAMILIES = {4: 'winch_geometry', 5: 'fluid_poses', 6: 'applied_loads',
                     7: 'elastic_moments', 8: 'inflow_planes'}
# Exact unaligned per-attachment interval layout: status/reserved, accepted clocks, seven opaque IDs.
APPLIED_INTERVAL = struct.Struct('<IIQdQd7Q')
assert BOOT.size == 64 and CHUNK.size == 80 and STAMP.size == 64 and APPLIED_INTERVAL.size == 96

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

@dataclass(frozen=True)
class FluidLayout:
    """Owned fixed F population and generation admitted from the immutable catalog.

    The scanner uses these extents to pre-admit whole groups and validate packed
    masks/unaligned poses. No per-frame values or borrowed source pointers remain.
    """
    generation: int  # Owning Core generation, distinct from the capture-plan identity.
    samples: int  # Dense F rows in each complete feature record.
    mask_bytes: int  # Packed LSB-first validity extent, including checked padding bits.
    frame_bytes: int  # Complete copied-stamp/source/mask/pose raw record extent.


def validate_fluid_catalog(cat):
    """Return an admitted FluidLayout or None when the descriptor is absent.

    cat borrows the validated physical catalog. Validate exact F identities,
    occurrence ownership, resolved sides, native references and explicit segment
    connectivity; retained geometry is independent of physical N/port samples.
    Invalid metadata raises InvalidSidecar without changing cat.
    """
    # Check all descriptor text/numbers, then admit its fixed stream tuple/generation.
    # Validate owned lines and unique resolved sample addresses, then segment membership.
    # Keep only scalar extents required by the bounded raw-frame scanner.
    if 'fluid_poses' not in cat:
        return None
    descriptor = object_(cat['fluid_poses'], 'fluid_poses')  # Borrowed F metadata.
    pending = [descriptor]  # Iterative setup-only walk; bounded by the admitted catalog.
    while pending:
        value = pending.pop()  # One nested descriptive or normative JSON value.
        if isinstance(value, dict):
            pending.extend(value.keys())
            pending.extend(value.values())
        elif isinstance(value, list):
            pending.extend(value)
        elif isinstance(value, str):
            require(value and '\0' not in value, 'fluid metadata string is empty or contains NUL')
            try:
                value.encode('utf-8')
            except UnicodeEncodeError as error:
                raise InvalidSidecar('fluid metadata is not valid UTF-8') from error
        elif type(value) in (int, float):
            finite(value, 'fluid metadata number')
    for field, wanted in (('kind', 5), ('stream_id', 3), ('layout_version', 1)):
        require(uint(descriptor.get(field), 'fluid.' + field) == wanted, 'unsupported fluid descriptor tuple')
    require(descriptor.get('encoding') == 'raw_f64_le', 'unsupported fluid encoding')
    generation = uint(descriptor.get('source_preparation_generation'), 'fluid generation')
    require(generation > 0, 'fluid generation must be positive')
    owners = {entry['id']: entry for entry in cat['occurrences']}  # Existing exact owner IDs.
    lines = array(descriptor.get('lines'), 'fluid.lines')  # Dense independent F lines.
    samples = array(descriptor.get('samples'), 'fluid.samples')  # Dense F payload rows.
    segments = array(descriptor.get('segments'), 'fluid.segments')  # Explicit continuous runs.
    require(all(0 < len(items) <= MAX_SAMPLES for items in (lines, samples, segments)),
            'fluid population bounds exceeded')
    identities = set()  # Unique occurrence/spanline bindings.
    native_counts = []  # Line-local native element limits for sample admission.
    for index, entry in enumerate(lines):
        object_(entry, 'fluid line')
        require(uint(entry.get('id'), 'fluid line ID') == index, 'fluid line IDs must be dense')
        owner = uint(entry.get('occurrence_id'), 'fluid owner')
        require(owner in owners and owners[owner]['kind'] == 'flexible', 'invalid fluid owner')
        spanline = string(entry.get('spanline_id'), 'fluid spanline_id')
        key = (owner, spanline)  # No normalization of occurrence-local source identity.
        require(key not in identities, 'duplicate fluid line identity')
        identities.add(key)
        for field in ('source_geometry_fingerprint', 'effective_geometry_fingerprint', 'approximation_policy'):
            string(entry.get(field), 'fluid.' + field)
        count = uint(entry.get('native_element_count'), 'fluid native element count')
        require(0 < count <= MAX_SAMPLES, 'fluid native element count exceeds positive count bounds')
        native_counts.append(count)
    counts = [0] * len(lines)  # Samples owned by each line.
    addresses = set()  # Each resolved (line,s,side) selects exactly one native owner.
    for index, entry in enumerate(samples):
        object_(entry, 'fluid sample')
        require(uint(entry.get('id'), 'fluid sample ID') == index, 'fluid sample IDs must be dense')
        line = uint(entry.get('line_id'), 'fluid sample line')
        require(line < len(lines), 'fluid sample line out of bounds')
        coordinate = finite(entry.get('material_coordinate_m'), 'fluid material coordinate')
        require(coordinate >= 0, 'negative fluid material coordinate')
        side = entry.get('side')  # Resolved physical limiting side, never automatic/none.
        require(side in ('left', 'right'), 'invalid resolved fluid side')
        native = uint(entry.get('native_element'), 'fluid native element')
        require(native < native_counts[line], 'fluid native element out of bounds')
        key = (line, coordinate, side)  # Owner is deliberately excluded from uniqueness.
        require(key not in addresses, 'duplicate fluid address or contradictory native owner')
        addresses.add(key)
        counts[line] += 1
    require(all(count >= 2 for count in counts), 'fluid line requires at least two samples')
    used = bytearray(len(samples))  # Per-sample membership, never per-frame storage.
    for index, entry in enumerate(segments):
        object_(entry, 'fluid segment')
        require(uint(entry.get('id'), 'fluid segment ID') == index, 'fluid segment IDs must be dense')
        line = uint(entry.get('line_id'), 'fluid segment line')
        require(line < len(lines), 'fluid segment line out of bounds')
        members = array(entry.get('sample_ids'), 'fluid segment sample IDs')
        require(2 <= len(members) <= len(samples), 'invalid fluid segment length')
        previous = None  # Prior material coordinate within this one explicit segment.
        for member in members:
            uint(member, 'fluid segment sample ID')
            require(member < len(samples), 'fluid segment sample out of bounds')
            row = samples[member]  # Already admitted immutable sample descriptor.
            coordinate = row['material_coordinate_m']
            require(row['line_id'] == line and (previous is None or coordinate > previous),
                    'fluid segment owner/order mismatch')
            previous = coordinate
            used[member] = 1
    require(all(used), 'orphan fluid sample')
    mask_bytes = (len(samples) + 7) // 8  # No alignment padding precedes raw doubles.
    return FluidLayout(generation, len(samples), mask_bytes, 80 + mask_bytes + 56 * len(samples))


def consume_fluid(raw, pose_raw, layout, collect):
    """Validate a borrowed F record and optionally return its owned logical rows.

    pose_raw=None checks self-contained feature facts before its pose arrives.
    Otherwise copied structural stamps must match byte for byte. layout supplies
    immutable extents/generation; collect is enabled only after dump admission.
    """
    # Check the exact common stamp/source prefix and its accepted lifecycle.
    # Check mask padding and finite poses through bytewise unpacking at unaligned offsets.
    # Materialize original values only for an explicitly admitted diagnostic dump.
    if pose_raw is not None:
        require(raw[:64] == pose_raw[:64], 'fluid/pose stamp mismatch')
    generation, phase, reserved = struct.unpack_from('<QII', raw, 64)
    require(generation == layout.generation and reserved == 0, 'fluid generation/reserved mismatch')
    _, analysis, events, _, state_id, _, _, _, _ = STAMP.unpack_from(raw)
    require((phase == 0 and analysis == 0) or (phase == 1 and analysis == 2) or
            (phase == 2 and analysis == 1 and state_id == 0 and events & 2 != 0) or
            (phase == 3 and analysis == 1 and state_id > 0 and events & 2 == 0),
            'fluid phase/stamp mismatch')
    if layout.samples % 8:
        require(raw[80 + layout.mask_bytes - 1] >> (layout.samples % 8) == 0, 'fluid mask padding nonzero')
    rows = [] if collect else None  # Optional owned rows; no default decoded history.
    for index in range(layout.samples):
        valid = bool(raw[80 + index // 8] & (1 << (index % 8)))  # Explicit logical validity.
        begin = 80 + layout.mask_bytes + 56 * index  # Byte offset may be unaligned.
        pose = struct.unpack_from('<7d', raw, begin)  # Owned tiny current row only.
        require(all(math.isfinite(value) for value in pose), 'nonfinite fluid pose')
        if valid:
            require(abs(sum(value * value for value in pose[3:]) - 1) <= 1e-6, 'nonunit fluid quaternion')
        else:
            require(raw[begin:begin + 56] == UNAVAILABLE_FLUID_POSE, 'noncanonical unavailable fluid pose')
        if collect:
            rows.append({'valid': valid, 'world_pose': list(pose)})
    return {'generation': generation, 'phase': phase, 'samples': rows} if collect else None


@dataclass(frozen=True)
class AppliedLayout:
    """Owned scalar load layout admitted once, with no retained station/consumer map.

    generation identifies Core preparation. counts is the attachment-order tuple
    of station counts; stations and frame_bytes bound each complete raw record.
    Only setup allocates this immutable descriptor; no history is stored here.
    """
    generation: int  # Exact positive Core preparation identity.
    counts: tuple  # Attachment-order station populations, independent of F display samples.
    stations: int  # Total logical rows carried by each record.
    frame_bytes: int  # Common80 plus each96-byte interval, packed mask and six-f64 rows.


def applied_text(value, name):
    """Validate a required load identity string and return its original spelling.

    name labels failures. No Unicode normalization, coercion or ID substitution
    occurs; lone surrogates and NULs cannot enter a UTF8 source identity.
    """
    # Check required string shape/NULs, then prove UTF-8 encoding without replacing the value.
    string(value, name)
    require('\0' not in value, name + ': NUL is forbidden')
    try:
        value.encode('utf-8')
    except UnicodeEncodeError as error:
        raise InvalidSidecar(name + ': valid UTF-8 required') from error
    return value


def validate_applied_catalog(cat):
    """Admit the independent applied-load population and return its fixed layout.

    cat borrows the validated physical catalog. All maps/sets below are setup-only
    and bounded by catalog/array/consumer ceilings. Native/model node domain bounds
    are positive INT64 identities, not allocated arrays and not capped at1e6.
    Invalid metadata raises InvalidSidecar; absent descriptor returns None.
    """
    # Admit scalar family/source facts and finite descriptive metadata.
    # Resolve run-scoped models, complete attachment ranges and exact logical identities.
    # Check complete native consumer maps, then retain only attachment population counts.
    if 'applied_loads' not in cat:
        return None
    descriptor = object_(cat['applied_loads'], 'applied_loads')  # Borrowed source catalog.
    pending = [descriptor]  # Bounded iterative walk of additional descriptive metadata.
    while pending:
        value = pending.pop()  # Next original descriptive value or key, with no normalization.
        if isinstance(value, dict):
            pending.extend(value.keys())
            pending.extend(value.values())
        elif isinstance(value, list):
            pending.extend(value)
        elif isinstance(value, str):
            try:
                value.encode('utf-8')
            except UnicodeEncodeError as error:
                raise InvalidSidecar('applied metadata is not valid UTF-8') from error
        elif type(value) in (int, float):
            finite(value, 'applied metadata number')
    for field, wanted in (('kind', 6), ('stream_id', 4), ('layout_version', 1)):  # Exact implemented discriminator fields.
        require(uint(descriptor.get(field), 'applied.' + field) == wanted, 'unsupported applied descriptor tuple')
    for field, wanted in (('encoding', 'raw_f64_le'), ('product', 'completed_weak_force_density'),
                          ('force_unit', 'N/m'), ('position_unit', 'm'), ('vector_frame', 'world')):  # Closed product/unit/frame meanings.
        require(descriptor.get(field) == wanted, 'unsupported applied ' + field)
    generation = uint(descriptor.get('source_preparation_generation'), 'applied generation')  # Exact prepared source/step lifetime.
    require(generation > 0, 'applied generation must be positive')
    coordinators, models, attachments, stations = (  # Borrowed bounded catalog populations, never native-sized domains.
        array(descriptor.get(field), 'applied.' + field)
        for field in ('coordinators', 'models', 'attachments', 'stations'))
    require(all(0 < len(entries) <= MAX_SAMPLES for entries in (coordinators, models, attachments, stations)),
            'applied population bounds exceeded')
    require(len(coordinators) == 1, 'applied profile requires one coordinator')
    coordinator = object_(coordinators[0], 'applied coordinator')  # Sole synchronous source owner admitted by this layout.
    require(uint(coordinator.get('id'), 'coordinator ID') == 0, 'coordinator IDs must be dense')
    scope = object_(coordinator.get('scope'), 'coordinator scope')  # Run definition scope, separate from physical targets.
    require(scope.get('kind') == 'run' and applied_text(scope.get('run_id'), 'coordinator run') == cat['run_id'] and
            coordinator.get('coupling') == 'fixed_step_weak_held', 'invalid applied coordinator scope/coupling')
    instances, model_ids = set(), set()  # Unique actual model bindings, with no process-address interpretation.
    node_counts = []  # Actual model-wide domain bounds; this does not allocate that population.
    for index, model in enumerate(models):  # Dense catalog ordinal and original source-model manifest.
        object_(model, 'applied model')
        require(uint(model.get('id'), 'model ID') == index, 'applied model IDs must be dense')
        coordinator_id = uint(model.get('coordinator_id'), 'model coordinator')  # Exact source namespace reference.
        require(coordinator_id == 0, 'model coordinator out of bounds')
        instance = uint(model.get('instance'), 'model instance')  # Original positive model instance token.
        run_id = applied_text(model.get('run_id'), 'model run_id')  # Source run spelling, checked against the file.
        model_id = applied_text(model.get('model_id'), 'model_id')  # Original run-local authored model identity.
        require(instance > 0 and run_id == cat['run_id'], 'invalid applied model instance/run')
        require((coordinator_id, instance) not in instances and (run_id, model_id) not in model_ids,
                'duplicate applied model identity')
        instances.add((coordinator_id, instance))
        model_ids.add((run_id, model_id))
        for field in ('model_type', 'model_version'):  # Required implementation-contract descriptions.
            applied_text(model.get(field), 'model.' + field)
        require(uint(model.get('layout_revision'), 'model layout revision') > 0, 'invalid model layout revision')
        count = uint(model.get('logical_node_count'), 'model logical node count')  # Domain bound only; no count-sized allocation.
        require(count > 0, 'model logical node count must be positive')
        node_counts.append(count)
    owners = {owner['id']: owner for owner in cat['occurrences']}  # Existing flexible occurrence owners.
    attachment_ids, attachment_indices = set(), set()  # Model-local identity pairs, distinct from dense IDs.
    used = bytearray(len(stations))  # Prove an exact partition without a per-domain-size allocation.
    ranges = [[] for _ in models]  # Selected model logical intervals, sorted only during setup.
    counts = []  # Dense attachment-order fixed raw populations.
    total_consumers = 0  # Aggregate native-consumer admission, not a native-element domain bound.
    # Resolve complete selected attachment ranges without requiring global station-range order.
    for index, attachment in enumerate(attachments):  # Dense payload ordinal and exact prepared target binding.
        object_(attachment, 'applied attachment')
        require(uint(attachment.get('id'), 'attachment ID') == index, 'applied attachment IDs must be dense')
        model_id = uint(attachment.get('model_catalog_id'), 'attachment model')  # Numeric model-table reference, not the authored string.
        require(model_id < len(models), 'attachment model out of bounds')
        model_index = uint(attachment.get('model_attachment_index'), 'model attachment index')  # Original model-local attachment ordinal.
        identity = applied_text(attachment.get('attachment_id'), 'attachment_id')  # Original model-local attachment spelling.
        require((model_id, identity) not in attachment_ids and (model_id, model_index) not in attachment_indices,
                'duplicate model attachment identity')
        attachment_ids.add((model_id, identity))
        attachment_indices.add((model_id, model_index))
        owner = uint(attachment.get('target_occurrence_id'), 'attachment occurrence')  # Existing flexible physical target identity.
        require(owner in owners and owners[owner]['kind'] == 'flexible', 'invalid applied attachment owner')
        for field in ('spanline_id', 'source_geometry_fingerprint', 'effective_geometry_fingerprint',
                      'model_nodes_fingerprint', 'approximation_policy'):  # Original/effective geometry and qualified policy identities.
            applied_text(attachment.get(field), 'attachment.' + field)
        native_count = uint(attachment.get('native_element_count'), 'attachment native element count')  # Native owner domain, not a buffer size.
        require(native_count > 0, 'attachment native element count must be positive')
        node_begin = uint(attachment.get('model_node_begin'), 'attachment model node begin')  # Original model-wide logical range origin.
        count = uint(attachment.get('station_count'), 'attachment station count')  # Complete selected logical population.
        begin = uint(attachment.get('station_begin'), 'attachment station begin')  # Global station-table range origin, possibly permuted.
        require(count > 0 and begin + count <= len(stations) and node_begin + count <= node_counts[model_id],
                'attachment station/model range out of bounds')
        ranges[model_id].append((node_begin, node_begin + count))
        counts.append(count)
        addresses = set()  # Exact logical addresses within this attachment; side is not a deduplication key.
        consumers = set()  # Actual prepared request indices, required dense after the whole attachment.
        for local in range(count):  # Original logical ordinal inside this attachment.
            ordinal = begin + local  # Corresponding dense global station-table index.
            station = object_(stations[ordinal], 'applied station')  # Borrowed physical address and full native consumer list.
            require(not used[ordinal] and uint(station.get('id'), 'station ID') == ordinal and
                    uint(station.get('attachment_catalog_id'), 'station attachment') == index and
                    uint(station.get('logical_node_index'), 'station logical node') == node_begin + local,
                    'station partition/identity mismatch')
            used[ordinal] = 1
            # Coordinates use the producer's binary64 domain for address equality;
            # the borrowed catalog and exact integer identity/count fields stay unchanged.
            coordinate = float(finite(station.get('material_coordinate_m'), 'station material coordinate'))
            require(coordinate >= 0 and station.get('physical_side') in ('continuous', 'left', 'right'),
                    'invalid applied logical coordinate/side')
            token = uint(station.get('physical_side_token'), 'station physical side token')  # Attachment-local continuity identity, possibly zero.
            require((coordinate, token) not in addresses, 'duplicate/contradictory applied logical address')
            addresses.add((coordinate, token))
            native = array(station.get('native_consumers'), 'station native consumers')  # All actual native copies of this logical point.
            total_consumers += len(native)
            require(native and total_consumers <= MAX_SAMPLES, 'native consumer population bounds exceeded')
            for consumer in native:  # Original request index and resolved owner/limit; no coordinate deduplication.
                object_(consumer, 'native consumer')
                consumer_id = uint(consumer.get('consumer_index'), 'native consumer index')  # Original attachment station-request index.
                require(consumer_id not in consumers and
                        uint(consumer.get('native_element'), 'native consumer element') < native_count and
                        consumer.get('limit_side') in ('left', 'right'), 'invalid native consumer identity/side')
                consumers.add(consumer_id)
        require(max(consumers) == len(consumers) - 1, 'native consumer indices must be dense from zero')
    require(all(used), 'orphan applied station')
    # Prove complete global coverage and nonoverlap of each model's selected logical ranges.
    for intervals in ranges:  # Setup-only selected intervals for one original model.
        intervals.sort()
        require(all(before[1] <= after[0] for before, after in zip(intervals, intervals[1:])),
                'selected model logical ranges overlap')
    width = 80 + sum(96 + (count + 7) // 8 + 48 * count for count in counts)  # Full common/prefix/mask/value wire extent.
    return AppliedLayout(generation, tuple(counts), len(stations), width)


def consume_applied(raw, pose_raw, layout, previous_stamp, collect):
    """Validate one complete applied record, optionally returning original owned data.

    raw borrows one admitted record. pose_raw is None until its structural partner
    exists. previous_stamp is the previous saved structural stamp, not a source
    point; only a previous transient constrains the accepted interval. Opaque IDs
    have no arithmetic or cross-frame monotonic rules. collect is pre-admitted.
    Return an owned generation/phase/attachment dictionary when collect is true,
    otherwise None. Any failed check raises InvalidSidecar before caller commit.
    The caller has already checked the complete record extent and structural stamp.
    """
    # Validate common source facts, then identical per-attachment interval prefixes.
    # Check accepted source/end clocks against the present and prior saved transient.
    # Decode finite point/force rows and explicit masks without alignment assumptions.
    if pose_raw is not None:
        require(raw[:64] == pose_raw[:64], 'applied/pose stamp mismatch')
    generation, phase, reserved = struct.unpack_from('<QII', raw, 64)  # Common source lifetime, lifecycle and reserved field.
    require(generation == layout.generation and reserved == 0, 'applied generation/reserved mismatch')
    _, analysis, events, _, state_id, _, _, _, _ = STAMP.unpack_from(raw)  # Structural facts used to admit this source phase.
    require((phase == 0 and analysis == 0) or (phase == 1 and analysis == 2) or
            (phase == 2 and analysis == 1 and state_id == 0 and events & 2 != 0) or
            (phase == 3 and analysis == 1 and state_id > 0 and events & 2 == 0),
            'applied phase/stamp mismatch')
    result = [] if collect else None  # Explicitly bounded decoded attachments only when requested.
    first_prefix = raw[80:176]  # Borrowed96-byte synchronous coordinator identity, possibly unaligned later.
    offset = 80  # First packed attachment follows the common copied stamp/source prefix.
    for count in layout.counts:  # Complete logical population of this catalog-ordered attachment.
        mask_bytes = (count + 7) // 8  # Packed LSB-first validity extent, including high padding bits.
        width = 96 + mask_bytes + 48 * count  # Complete attachment prefix/mask/value extent.
        prefix = raw[offset:offset + 96]  # Borrowed exact synchronous-coordinator interval bytes.
        require(prefix == first_prefix, 'applied attachment interval prefix mismatch')
        # Availability/reserved, actual accepted source/end IDs and seconds, then opaque
        # evaluation publication/revisions, prepared sequence and held packet identities.
        status, r0, source_id, source_time, end_id, end_time, publication, mechanical, state, geometry, environment, sequence, packet = APPLIED_INTERVAL.unpack_from(raw, offset)
        require(status in (0, 1) and r0 == 0, 'invalid applied interval status/reserved')
        if status == 0:
            require(phase in (0, 1, 2) and not any(raw[offset:offset + width]),
                    'unavailable applied interval must be canonical zero in prepared/static/initial phase')
        else:
            require(phase == 3 and source_id <= MAX_INT and
                    all(0 < value <= MAX_INT for value in (end_id, publication, mechanical, state, geometry, environment, sequence, packet)),
                    'invalid completed applied source identity/phase')
            require(math.isfinite(source_time) and math.isfinite(end_time) and source_id < end_id and source_time < end_time,
                    'invalid applied accepted interval')
            require(end_id == state_id and raw[offset + 32:offset + 40] == raw[16:24],
                    'applied endpoint differs from structural stamp')
            if previous_stamp is not None and previous_stamp['analysis'] == 1:
                previous_id, previous_time = previous_stamp['state_id'], previous_stamp['time']  # Prior saved transient endpoint facts.
                require(source_id >= previous_id and source_time >= previous_time,
                        'applied source precedes previous saved transient')
                if source_id == previous_id:
                    require(raw[offset + 16:offset + 24] == struct.pack('<d', previous_time),
                            'applied source time differs from matching saved accepted ID')
                else:
                    require(source_time > previous_time, 'later applied source ID requires later time')
        if count % 8:
            require(raw[offset + 96 + mask_bytes - 1] >> (count % 8) == 0, 'applied mask padding nonzero')
        # Validate each row before materializing its diagnostic entry; the caller owns whole-group publication.
        rows = [] if collect else None  # Optional owned logical rows, absent in default streaming mode.
        for index in range(count):  # Original local logical-node order, independent of display F sampling.
            valid = bool(raw[offset + 96 + index // 8] & (1 << (index % 8)))  # Explicit availability, distinct from zero load.
            begin = offset + 96 + mask_bytes + index * 48  # Unaligned source XYZ/force-density row offset.
            values = struct.unpack_from('<6d', raw, begin)  # Six finite scalars: world metres then world N/m.
            require(all(math.isfinite(value) for value in values), 'nonfinite applied position/force')
            if not valid:
                require(not any(raw[begin:begin + 48]), 'noncanonical unavailable applied row')
            if collect:
                rows.append({'valid': valid, 'source_position_world_m': list(values[:3]),
                             'force_density_world_N_per_m': list(values[3:])})
        if collect:
            result.append({'interval': {'status': status, 'source_accepted_id': source_id,
                'source_time_s': source_time, 'end_accepted_id': end_id, 'end_time_s': end_time,
                'publication': publication, 'mechanical_revision': mechanical, 'state_revision': state,
                'geometry_revision': geometry, 'environment_revision': environment,
                'prepared_step_sequence': sequence, 'packet': packet}, 'samples': rows})
        offset += width
    return {'generation': generation, 'phase': phase, 'attachments': result} if collect else None


@dataclass(frozen=True)
class MomentLayout:
    """Owned fixed elastic-cut population admitted from immutable catalog metadata.

    check() uses these scalar extents to bound raw batches, validate unaligned
    triples and join accepted groups. Setup alone builds the temporary mapping
    indexes; no provider objects, display geometry or frame history survive here.
    The layout remains valid until the recording closes because topology is fixed.
    """
    generation: int  # Positive source preparation identity shared by enabled families.
    cuts: int  # Dense catalog-order signed current-N moment triples per record.
    mask_bytes: int  # Packed LSB-first validity bytes, including checked high padding.
    frame_bytes: int  # Common80 plus packed mask and24 bytes per cut, with no padding.


def validate_moment_catalog(cat):
    """Return MomentLayout for an admitted elastic descriptor, or None if absent.

    cat borrows an already validated physical catalog and remains unchanged.
    Setup indexes enforce exact identities, binary64 material/eta relationships,
    sided breaks and explicitly mapped base-edge coverage. They neither bind a
    provider nor prove physical continuity or phase capability. All populations
    are bounded before count-sized allocation; malformed input raises InvalidSidecar.
    """
    # Admit the exact product and finite UTF8 metadata, then bounded tables.
    # Resolve native providers/cuts and optional structural-row ownership.
    # Prove declared sided separation, complete cut coverage and interval nonoverlap.
    # Retain only scalar extents needed by the bounded streaming validator.
    if 'elastic_moments' not in cat:
        return None
    descriptor = object_(cat['elastic_moments'], 'elastic_moments')  # Borrowed immutable descriptor.
    pending = [descriptor]  # Catalog-bounded iterative metadata traversal, without recursion.
    while pending:
        value = pending.pop()  # Current authored scalar/container; no coercion is written back.
        if isinstance(value, dict):
            pending.extend(value.keys())
            pending.extend(value.values())
        elif isinstance(value, list):
            pending.extend(value)
        elif isinstance(value, str):
            applied_text(value, 'moment metadata string')
        elif type(value) in (int, float):
            finite(value, 'moment metadata number')
    for field, wanted in (('kind', 7), ('stream_id', 5), ('layout_version', 1)):  # Implemented exact integer discriminators.
        require(uint(descriptor.get(field), 'moment.' + field) == wanted, 'unsupported moment descriptor tuple')
    for field, wanted in (('encoding', 'raw_f64_le'), ('quantity', 'elastic_section_moment'),
                          ('components', ['Mx', 'My', 'Mz']), ('unit', 'N.m'),
                          ('expression_frame', 'current_structural_N'), ('moment_point', 'current_structural_N'),
                          ('action_convention', 'positive_span_on_negative_span'),
                          ('display_interpolation', 'linear_scalar_on_declared_base_edge')):  # Closed quantity/basis/display meanings.
        require(descriptor.get(field) == wanted, 'unsupported moment ' + field)
    generation = uint(descriptor.get('source_preparation_generation'), 'moment generation')  # Actual owning source lifetime.
    require(generation > 0, 'moment generation must be positive')
    lines, providers, cuts, breaks, intervals = (  # Borrowed dense tables; breaks use exact referenced IDs instead of dense row IDs.
        array(descriptor.get(field), 'moment.' + field)
        for field in ('lines', 'provider_addresses', 'cuts', 'sided_breaks', 'display_intervals'))
    require(all(0 < len(rows) <= MAX_SAMPLES for rows in (lines, providers, cuts, intervals))
            and len(breaks) <= MAX_SAMPLES and len(cuts) >= 2, 'moment population bounds exceeded')
    owners = {row['id']: row for row in cat['occurrences']}  # Existing stable physical occurrence IDs.
    base_samples = cat['samples']  # Already validated dense structural/port population.
    base_segments = {row['id']: row for row in cat['segments']}  # Stable IDs are not array ordinals.
    structural_rows = array(cat['structural_lines'], 'structural_lines') if 'structural_lines' in cat else None  # Optional existing Core metadata.
    structural_matches = {}  # Setup-only exact owner/local-ID index; unrelated row details remain descriptive.
    if structural_rows is not None:
        for row in structural_rows:  # Optional source rows, validated further only when actually matched.
            if isinstance(row, dict) and type(row.get('occurrence_id')) is int and isinstance(row.get('local_id'), str):
                structural_matches.setdefault((row['occurrence_id'], row['local_id']), []).append(row)
    line_keys = set()  # Unique occurrence/local spanline identities.
    matched_rows = []  # Matched structural row ID/range, or None when the optional table is absent.
    native_counts = []  # Bounded per-line native element domains, never native-sized allocations.
    for index, line in enumerate(lines):  # Dense moment-line position and borrowed source identity.
        object_(line, 'moment line')
        require(uint(line.get('id'), 'moment line ID') == index, 'moment line IDs must be dense')
        owner = uint(line.get('occurrence_id'), 'moment line occurrence')  # Existing flexible occurrence ID.
        require(owner in owners and owners[owner]['kind'] == 'flexible', 'invalid moment line owner')
        local_id = applied_text(line.get('spanline_id'), 'moment spanline ID')  # Authored structural local ID; no literal N requirement.
        require((owner, local_id) not in line_keys, 'duplicate moment line identity')
        line_keys.add((owner, local_id))
        for field in ('source_geometry_fingerprint', 'effective_geometry_fingerprint', 'approximation_policy'):  # Distinct prepared geometry/provenance strings.
            applied_text(line.get(field), 'moment line.' + field)
        native_count = uint(line.get('native_element_count'), 'moment native element count')  # This profile applies its positive1m count cap.
        require(0 < native_count <= MAX_SAMPLES, 'moment native element count bounds exceeded')
        native_counts.append(native_count)
        matched = None  # Absent table uses explicit base segment/edge ownership alone.
        if structural_rows is not None:
            matches = structural_matches.get((owner, local_id), [])  # Only matched row fields acquire authority.
            require(len(matches) == 1, 'moment line needs exactly one matching structural row')
            row = matches[0]  # Matched existing structural row; its ID is independent of moment ordinal.
            row_id = uint(row.get('id'), 'structural line ID')  # Stable producer row identity.
            uint(row.get('occurrence_id'), 'structural line occurrence')
            applied_text(row.get('local_id'), 'structural line local ID')
            begin = uint(row.get('sample_begin'), 'structural line sample begin')  # Dense base range start.
            count = uint(row.get('sample_count'), 'structural line sample count')  # Selected base range extent.
            require(begin + count <= MAX_INT and begin + count <= len(base_samples), 'structural line sample range out of bounds')
            matched = (row_id, begin, begin + count)
        matched_rows.append(matched)
    provider_keys = set()  # Each native element has at most one prepared provider address.
    bounds = []  # Binary64 closed provider intervals used for exact endpoint rules.
    for index, provider in enumerate(providers):  # Dense address ID and borrowed native provenance.
        object_(provider, 'moment provider')
        require(uint(provider.get('id'), 'moment provider ID') == index, 'moment provider IDs must be dense')
        line_id = uint(provider.get('line_id'), 'moment provider line')  # Moment-line ordinal, distinct from structural row ID.
        require(line_id < len(lines), 'moment provider line out of bounds')
        native = uint(provider.get('native_element'), 'moment provider native element')  # Actual selected native element index.
        require(native < native_counts[line_id] and (line_id, native) not in provider_keys,
                'invalid/duplicate moment provider native element')
        provider_keys.add((line_id, native))
        applied_text(provider.get('provider_id'), 'moment provider provenance')
        begin = float(finite(provider.get('material_begin_m'), 'moment provider begin'))  # Binary64 lower material boundary, metres.
        end = float(finite(provider.get('material_end_m'), 'moment provider end'))  # Binary64 upper material boundary, metres.
        require(0 <= begin < end and math.isfinite(end - begin), 'invalid moment provider interval')
        bounds.append((begin, end))
    addresses = set()  # Exact binary64 line/coordinate/side addresses, excluding native/provider from uniqueness.
    coordinates = []  # Dense binary64 material coordinates, preserving original JSON in cat.
    coordinate_sides = {}  # Per-line/coordinate mapping from explicit side to cut ID.
    for index, cut in enumerate(cuts):  # Dense row order and borrowed actual cut binding.
        object_(cut, 'moment cut')
        require(uint(cut.get('id'), 'moment cut ID') == index, 'moment cut IDs must be dense')
        line_id = uint(cut.get('line_id'), 'moment cut line')  # Moment-line ordinal.
        provider_id = uint(cut.get('provider_address_id'), 'moment cut provider')  # Dense prepared address ordinal.
        native = uint(cut.get('native_element'), 'moment cut native element')  # Explicit exact native element identity.
        require(line_id < len(lines) and provider_id < len(providers), 'moment cut reference out of bounds')
        provider = providers[provider_id]  # Admitted same-line native provider address.
        require(provider['line_id'] == line_id and provider['native_element'] == native, 'moment cut/provider owner mismatch')
        coordinate = float(finite(cut.get('material_coordinate_m'), 'moment cut coordinate'))  # Binary64 authoritative display coordinate, metres.
        eta = float(finite(cut.get('native_eta'), 'moment native eta'))  # Actual native coordinate; never recomputed from material position.
        side = cut.get('side')  # Explicit resolved limiting side, not automatic/none.
        begin, end = bounds[provider_id]  # Closed native material interval, already binary64 and finite.
        require(begin <= coordinate <= end and side in ('left', 'right'), 'invalid moment cut coordinate/side')
        require((coordinate == begin and side == 'right' and eta == -1)
                or (coordinate == end and side == 'left' and eta == 1)
                or (begin < coordinate < end and -1 < eta < 1), 'moment endpoint/eta relationship mismatch')
        key = (line_id, coordinate, side)  # Native owner cannot distinguish duplicate material addresses.
        require(key not in addresses, 'duplicate moment material address')
        addresses.add(key)
        coordinates.append(coordinate)
        coordinate_sides.setdefault((line_id, coordinate), {})[side] = index
    declared_pairs = set()  # Validated opposite-side pairs, each required exactly once.
    paired_cuts = set()  # A cut cannot be reused by another sided break.
    line_breaks = [[] for _ in lines]  # Sorted binary64 discontinuity positions for bounded interval lookup.
    for pair in breaks:  # Explicit display separation independent of the sampled values.
        object_(pair, 'moment sided break')
        require(set(pair) == {'left_cut_id', 'right_cut_id'}, 'moment sided break must contain only cut references')
        left = uint(pair.get('left_cut_id'), 'moment left cut')  # Required left limiting endpoint ID.
        right = uint(pair.get('right_cut_id'), 'moment right cut')  # Required right limiting endpoint ID.
        require(left < len(cuts) and right < len(cuts) and left != right
                and left not in paired_cuts and right not in paired_cuts, 'invalid/reused moment break cut')
        require(cuts[left]['line_id'] == cuts[right]['line_id'] and coordinates[left] == coordinates[right]
                and cuts[left]['side'] == 'left' and cuts[right]['side'] == 'right', 'moment break owner/coordinate/side mismatch')
        declared_pairs.add((left, right))
        paired_cuts.update((left, right))
        line_breaks[cuts[left]['line_id']].append(coordinates[left])
    required_pairs = {(sides['left'], sides['right']) for sides in coordinate_sides.values() if len(sides) == 2}  # Every opposite-sided coordinate pair.
    require(declared_pairs == required_pairs, 'missing moment sided break')
    for positions in line_breaks:  # Setup-only sorted material breaks per moment line.
        positions.sort()
    used = bytearray(len(cuts))  # Prove full declared-cut coverage, independent of frame masks.
    edge_ranges = {}  # Material intervals grouped by exact moment line/stable segment/edge.
    for index, interval in enumerate(intervals):  # Dense explicit display declaration and original row.
        object_(interval, 'moment display interval')
        require(uint(interval.get('id'), 'moment interval ID') == index, 'moment interval IDs must be dense')
        line_id = uint(interval.get('line_id'), 'moment interval line')  # Exact moment-line ordinal.
        segment_id = uint(interval.get('base_segment_id'), 'moment base segment')  # Stable base ID, never a positional index.
        edge = uint(interval.get('edge_index'), 'moment base edge')  # Consecutive sample position within this one segment.
        require(line_id < len(lines) and segment_id in base_segments, 'moment display line/segment out of bounds')
        segment = base_segments[segment_id]  # Existing admitted structural segment.
        members = segment['sample_ids']  # Existing physical sample IDs in immutable display order.
        require(segment['occurrence_id'] == lines[line_id]['occurrence_id'] and edge + 1 < len(members),
                'moment mapped edge owner/range mismatch')
        sample_ids = (members[edge], members[edge + 1])  # The two exact dense base endpoints.
        endpoints = [base_samples[member] for member in sample_ids]  # Borrowed structural N endpoint descriptors.
        require(all(row['kind'] == 'structural_node' and row['occurrence_id'] == lines[line_id]['occurrence_id']
                    for row in endpoints), 'moment edge endpoints must be structural samples of the line owner')
        base_begin, base_end = [float(finite(row.get('material_coordinate_m'), 'moment base coordinate')) for row in endpoints]  # Binary64 base bounds.
        require(base_begin < base_end and math.isfinite(base_end - base_begin), 'moment base edge is not finite and increasing')
        matched = matched_rows[line_id]  # Optional matched structural ID and checked physical sample range.
        if matched is not None:
            row_id, begin, end = matched  # Existing structural table identity and half-open dense range.
            require(all(begin <= member < end for member in sample_ids), 'moment mapped edge outside structural line range')
            for row in [segment, *endpoints]:  # Only supplied line references acquire the matched structural identity rule.
                if 'line_id' in row:
                    require(uint(row['line_id'], 'mapped structural line ID') == row_id, 'moment mapped structural line identity mismatch')
        ids = array(interval.get('cut_ids'), 'moment interval cuts')  # Exactly two distinct dense moment-cut references.
        require(len(ids) == 2, 'moment interval needs two cut IDs')
        lower, upper = [uint(value, 'moment interval cut') for value in ids]  # Lower/upper explicit cut IDs.
        require(lower < len(cuts) and upper < len(cuts) and lower != upper, 'invalid moment interval cut references')
        require(cuts[lower]['line_id'] == cuts[upper]['line_id'] == line_id
                and cuts[lower]['provider_address_id'] == cuts[upper]['provider_address_id'], 'moment interval crosses line/provider')
        a, b = coordinates[lower], coordinates[upper]  # Exact binary64 interval material bounds, metres.
        require(base_begin <= a < b <= base_end, 'moment interval outside/reverses mapped edge')
        positions = line_breaks[line_id]  # Sorted authored breaks; binary search avoids an interval-by-break quadratic walk.
        require(bisect_right(positions, a) == bisect_left(positions, b), 'moment interval crosses sided break')
        if (line_id, a) in coordinate_sides and len(coordinate_sides[(line_id, a)]) == 2:
            require(lower == coordinate_sides[(line_id, a)]['right'], 'moment interval starts with left limit')
        if (line_id, b) in coordinate_sides and len(coordinate_sides[(line_id, b)]) == 2:
            require(upper == coordinate_sides[(line_id, b)]['left'], 'moment interval ends with right limit')
        used[lower] = used[upper] = 1
        edge_ranges.setdefault((line_id, segment_id, edge), []).append((a, b))
    require(all(used), 'unmapped moment cut')
    for ranges in edge_ranges.values():  # Setup-only intervals belonging to one declared base edge.
        ranges.sort()
        require(all(before[1] <= after[0] for before, after in zip(ranges, ranges[1:])), 'overlapping moment display intervals')
    mask_bytes = (len(cuts) + 7) // 8  # Exact packed extent before unaligned binary64 triples.
    return MomentLayout(generation, len(cuts), mask_bytes, 80 + mask_bytes + 24 * len(cuts))


def consume_moment(raw, pose_raw, layout, collect):
    """Validate one borrowed moment record and optionally return owned signed rows.

    raw has its admitted layout.frame_bytes extent. pose_raw=None validates an
    independently complete pending record; otherwise structural stamps must join
    byte-for-byte. layout supplies fixed generation/extents, and collect requests
    a previously admitted diagnostic dump. Invalid data raises InvalidSidecar;
    false collect returns None without retaining decoded rows or raw views.
    """
    # Check source lifetime, accepted lifecycle and the optional exact structural join.
    # Validate packed padding and finite unaligned triples, preserving valid signed zeros.
    # Decode rows only for explicit dumps; unavailable rows must be canonical positive zero.
    if pose_raw is not None:
        require(raw[:64] == pose_raw[:64], 'moment/pose stamp mismatch')
    generation, phase, reserved = struct.unpack_from('<QII', raw, 64)  # Common source identity, actual phase and reserved zero.
    require(generation == layout.generation and reserved == 0, 'moment generation/reserved mismatch')
    _, analysis, events, _, state_id, _, _, _, _ = STAMP.unpack_from(raw)  # Independently validated structural lifecycle fields.
    require((phase == 0 and analysis == 0) or (phase == 1 and analysis == 2)
            or (phase == 2 and analysis == 1 and state_id == 0 and events & 2 != 0)
            or (phase == 3 and analysis == 1 and state_id > 0 and events & 2 == 0), 'moment phase/stamp mismatch')
    if layout.cuts % 8:
        require(raw[80 + layout.mask_bytes - 1] >> (layout.cuts % 8) == 0, 'moment mask padding nonzero')
    rows = [] if collect else None  # Optional owned original triples; absent during streaming-only validation.
    for index in range(layout.cuts):  # Dense immutable cut order, independent of base sample IDs.
        valid = bool(raw[80 + index // 8] & (1 << (index % 8)))  # Explicit availability, independent of magnitude/sign.
        begin = 80 + layout.mask_bytes + 24 * index  # Byte offset; C7/C8 rows begin81 and C9 rows82.
        values = struct.unpack_from('<3d', raw, begin)  # Tiny temporary current-N Mx/My/Mz tuple in N.m.
        require(all(math.isfinite(value) for value in values), 'nonfinite elastic moment')
        if not valid:
            require(not any(raw[begin:begin + 24]), 'noncanonical unavailable moment')
        if collect:
            rows.append({'valid': valid, 'moment_N_Nm': list(values)})
    return {'generation': generation, 'phase': phase, 'moments': rows} if collect else None


@dataclass(frozen=True)
class PlaneLayout:
    """Owned fixed plane extents admitted from an immutable source/geometry catalog.

    check() uses catalog-order counts to bound batches and locate per-plane masks
    without retaining geometry, provider objects or point arrays. Source identities
    are validated only at setup; this object owns no history or external assets.
    It remains valid for the lifetime of the recording's fixed topology.
    """
    generation: int  # Positive common Core preparation identity.
    sources: int  # Dense source-row count, reported without implying provider capability.
    counts: tuple  # Dense plane-order point counts nu*nv; no per-point map is retained.
    points: int  # Aggregate fixed slots across all planes, bounded before allocation.
    frame_bytes: int  # Single common80 prefix plus every per-plane packed mask and24*N values.


def validate_plane_catalog(cat):
    """Return PlaneLayout for valid inflow metadata, or None when absent.

    cat borrows the already validated physical catalog and is never normalized.
    Validate Main's exact minimum source-manifest envelope and finite realized
    affine geometry using binary64 arithmetic. Extra provenance remains open;
    this function neither reopens assets nor validates provider physics, coverage,
    Core readiness or supported analyses. Invalid metadata raises InvalidSidecar.
    """
    # Check all metadata strings/numbers, then exact family and bounded table shapes.
    # Validate source/run joins and the conditional native/mean provenance envelope.
    # Admit aggregate point counts, orthonormal geometry and finite affine corners.
    # Retain only fixed plane counts, source count and the exact packed raw extent.
    if 'inflow_planes' not in cat:
        return None
    descriptor = object_(cat['inflow_planes'], 'inflow_planes')  # Borrowed fixed recording metadata.
    pending = [descriptor]  # Iterative traversal bounded by the existing catalog byte ceiling.
    while pending:
        value = pending.pop()  # Original descriptive or normative value; no rewritten provenance.
        if isinstance(value, dict):
            pending.extend(value.keys())
            pending.extend(value.values())
        elif isinstance(value, list):
            pending.extend(value)
        elif isinstance(value, str):
            require('\0' not in value, 'plane metadata string contains NUL')
            try:
                value.encode('utf-8')
            except UnicodeEncodeError as error:
                raise InvalidSidecar('plane metadata is not valid UTF-8') from error
        elif type(value) in (int, float):
            finite(value, 'plane metadata number')
    for field, wanted in (('kind', 8), ('stream_id', 6), ('layout_version', 1)):  # Exact integer family discriminators.
        require(uint(descriptor.get(field), 'plane.' + field) == wanted, 'unsupported plane descriptor tuple')
    for field, wanted in (('encoding', 'raw_f64_le'), ('quantity', 'absolute_unperturbed_world_velocity'), ('units', 'm/s')):  # Fixed stored physical meaning.
        require(descriptor.get(field) == wanted, 'unsupported plane ' + field)
    generation = uint(descriptor.get('source_preparation_generation'), 'plane generation')  # Owning preparation lifetime, not capture-plan identity.
    require(generation > 0, 'plane generation must be positive')
    sources = array(descriptor.get('sources'), 'plane sources')  # Dense copied provenance rows; generic wire may name multiple sources.
    planes = array(descriptor.get('planes'), 'planes')  # Dense fixed geometry and payload order.
    require(0 < len(sources) <= len(planes) <= MAX_SAMPLES, 'plane/source population bounds exceeded')
    for index, source in enumerate(sources):  # Dense source ID and borrowed original manifest envelope.
        object_(source, 'plane source')
        require(uint(source.get('id'), 'plane source ID') == index, 'plane source IDs must be dense')
        inflow_id = applied_text(source.get('inflow_id'), 'plane inflow ID')  # Actual selected provider-definition identity.
        run_id = applied_text(source.get('run_id'), 'plane run ID')  # Selected recording run, not a direct-definition null identity.
        require(run_id == cat['run_id'], 'plane source run mismatch')
        manifest = object_(source.get('initialization_manifest'), 'plane initialization manifest')  # Copied provider object; semantic preservation is producer-owned.
        require(manifest.get('format') == 'serene-inflow.initialization.v1', 'unsupported plane manifest format')
        identity = object_(manifest.get('identity'), 'plane manifest identity')  # Required source/run binding envelope.
        require(applied_text(identity.get('inflow_id'), 'manifest inflow ID') == inflow_id
                and applied_text(identity.get('run_id'), 'manifest run ID') == run_id, 'plane manifest identity mismatch')
        validation = object_(manifest.get('validation'), 'plane manifest validation')  # Historical standalone preparation facts, never a Core certificate.
        require(validation.get('standalone_provider_prepared') is True
                and validation.get('core_binding') == 'not_performed', 'plane manifest preparation/binding mismatch')
        profile = object_(manifest.get('runtime_profile'), 'plane runtime profile')  # Recorded ordinary provider semantics.
        source_kind = applied_text(profile.get('source_kind'), 'plane source kind')  # Open provenance name; not executable capability admission.
        if source_kind == 'native_general_kaimal_wnd':
            native = object_(manifest.get('source'), 'plane native source')  # Native file provenance required only for the recognized native kind.
            applied_text(native.get('path'), 'plane native path')
            uint(native.get('source_bytes'), 'plane native source bytes')
            fingerprint = object_(native.get('fingerprint'), 'plane native fingerprint')  # Noncryptographic entire-file identity with exact labels.
            require(fingerprint.get('algorithm') == 'fnv1a64' and fingerprint.get('encoding') == 'hex_lowercase_16'
                    and fingerprint.get('scope') == 'entire_source_file' and fingerprint.get('cryptographic') is False,
                    'plane native fingerprint labels/type mismatch')
            digest = applied_text(fingerprint.get('value'), 'plane native fingerprint value')  # Original16-character lowercase hexadecimal representation.
            require(len(digest) == 16 and all(character in '0123456789abcdef' for character in digest),
                    'plane native fingerprint value mismatch')
            require(profile.get('transverse') == 'turbulence_only_clamp', 'plane native transverse policy mismatch')
        elif source_kind == 'none':
            require('source' not in manifest, 'mean-only plane manifest must omit native source')
    used = bytearray(len(sources))  # Prove every declared source supplies a selected plane.
    identities = set()  # Unique request strings; no normalization or source-ordinal substitution.
    counts = []  # Plane-order fixed populations retained only after complete admission.
    total = 0  # Aggregate point count, not a per-plane allowance.
    width = 80  # One common prefix per frame, never one per plane.
    for index, plane in enumerate(planes):  # Dense payload-plane position and original realized geometry.
        object_(plane, 'plane')
        require(uint(plane.get('id'), 'plane ID') == index, 'plane IDs must be dense')
        identity = applied_text(plane.get('plane_id'), 'plane request ID')  # Original user/request identity, distinct from dense integer ID.
        require(identity not in identities, 'duplicate plane request ID')
        identities.add(identity)
        source_id = uint(plane.get('source_id'), 'plane source reference')  # Exact source-table ordinal.
        require(source_id < len(sources), 'plane source out of bounds')
        used[source_id] = 1
        nu, nv = (uint(plane.get(field), 'plane.' + field) for field in ('nu', 'nv'))  # Endpoint-inclusive dimensions, exact integers.
        require(nu >= 2 and nv >= 2 and nu <= MAX_SAMPLES // nv, 'plane dimensions/product bounds exceeded')
        count = nu * nv  # Checked population; multiplication cannot exceed the1m aggregate cap.
        require(count <= MAX_SAMPLES - total, 'plane aggregate point cap exceeded')
        total += count
        require(plane.get('sample_order') == 'u_fastest', 'unsupported plane sample order')
        vectors = []  # Three tiny binary64 triples: lower corner then the two proper basis vectors.
        for field in ('lower_corner_world_m', 'e1_world', 'e2_world'):  # Realized wire geometry, independent of request/default metadata.
            values = array(plane.get(field), 'plane.' + field)  # Borrowed three-component source vector.
            require(len(values) == 3, 'plane geometry requires three components')
            vectors.append(tuple(float(finite(value, 'plane.' + field)) for value in values))
        lower, e1, e2 = vectors  # Binary64 corner in world metres and two world directions.
        require(abs(sum(value * value for value in e1) - 1) <= 1e-12
                and abs(sum(value * value for value in e2) - 1) <= 1e-12
                and abs(sum(a * b for a, b in zip(e1, e2))) <= 1e-12, 'plane basis representation tolerance exceeded')
        du, dv = (float(finite(plane.get(field), 'plane.' + field)) for field in ('du_m', 'dv_m'))  # Positive realized sampling spacings, metres.
        require(du > 0 and dv > 0, 'plane spacing must be positive')
        u_extent, v_extent = (nu - 1) * du, (nv - 1) * dv  # Complete endpoint-inclusive extents before any point allocation.
        require(math.isfinite(u_extent) and math.isfinite(v_extent), 'nonfinite plane extent')
        for u in (0.0, u_extent):  # Both affine extremes of the fast axis.
            for v in (0.0, v_extent):  # Both affine extremes of the slow axis.
                for axis in range(3):  # Explicit component products and left-associated world-coordinate sums.
                    first, second = u * e1[axis], v * e2[axis]  # Products must remain finite even if a later sum cancels them.
                    partial = lower[axis] + first  # Intermediate point coordinate before adding the slow-axis contribution.
                    require(all(math.isfinite(value) for value in (first, second, partial, partial + second)),
                            'unrepresentable plane point geometry')
        counts.append(count)
        width += count // 8 + (count % 8 != 0) + 24 * count
    require(all(used), 'orphan plane source')
    return PlaneLayout(generation, len(sources), tuple(counts), total, width)


def consume_plane(raw, pose_raw, layout, collect):
    """Validate one borrowed complete plane record and optionally return owned rows.

    raw has admitted layout.frame_bytes extent; pose_raw is None for independently
    complete pending records, otherwise its structural stamp must match bitwise.
    layout supplies source generation and each fixed plane population. collect is
    true only after dump admission. Return owned generation/phase/plane rows then,
    otherwise None. Invalid masks, lifecycle or values raise InvalidSidecar.
    """
    # Validate common source facts and the available structural partner.
    # Walk each independent packed mask and unaligned world-velocity population.
    # Preserve valid bits and require positive-zero unavailable rows without deriving coverage.
    if pose_raw is not None:
        require(raw[:64] == pose_raw[:64], 'plane/pose stamp mismatch')
    generation, phase, reserved = struct.unpack_from('<QII', raw, 64)  # Exact source generation, lifecycle and reserved zero.
    require(generation == layout.generation and reserved == 0, 'plane generation/reserved mismatch')
    _, analysis, events, _, state_id, _, _, _, _ = STAMP.unpack_from(raw)  # Structural lifecycle facts validated independently by FrameState.
    require((phase == 0 and analysis == 0) or (phase == 1 and analysis == 2)
            or (phase == 2 and analysis == 1 and state_id == 0 and events & 2 != 0)
            or (phase == 3 and analysis == 1 and state_id > 0 and events & 2 == 0), 'plane phase/stamp mismatch')
    planes = [] if collect else None  # Optional owned decoded plane rows; no default duration-sized history.
    offset = 80  # First plane begins immediately after the single common prefix.
    for plane_id, count in enumerate(layout.counts):  # Dense plane ordinal and its immutable fixed row count.
        mask_bytes = count // 8 + (count % 8 != 0)  # This plane's own mask; no concatenated-population masking.
        if count % 8:
            require(raw[offset + mask_bytes - 1] >> (count % 8) == 0, 'plane mask padding nonzero')
        rows = [] if collect else None  # Optional owned values in original u-fastest order.
        for index in range(count):  # Dense u+nu*v sample index, not a physical-pose ID.
            valid = bool(raw[offset + index // 8] & (1 << (index % 8)))  # Explicit availability, independent of magnitude or ground geometry.
            begin = offset + mask_bytes + 24 * index  # Byte offset may be unaligned within or across planes.
            velocity = struct.unpack_from('<3d', raw, begin)  # Tiny temporary world XYZ velocity tuple, m/s.
            require(all(math.isfinite(value) for value in velocity), 'nonfinite plane velocity')
            if not valid:
                require(not any(raw[begin:begin + 24]), 'noncanonical unavailable plane velocity')
            if collect:
                rows.append({'valid': valid, 'world_velocity_mps': list(velocity)})
        if collect:
            planes.append({'plane_id': plane_id, 'samples': rows})
        offset += mask_bytes + 24 * count
    return {'generation': generation, 'phase': phase, 'planes': planes} if collect else None


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

def check(path, *, allow_incomplete=False, dump_frames=False, max_dump_frames=1000,
          max_pending_raw_bytes=MAX_PENDING_RAW, max_dump_bytes=MAX_DUMP_BYTES):
    """Validate path in file order and return an owned result dictionary.

    path is an input filename. allow_incomplete permits only a valid complete-chunk
    prefix without the final trailer; corrupt required semantics always fail.
    dump_frames retains poses/features only after frame and logical-byte admission.
    max_dump_frames is at most 100000; max_dump_bytes is at most 256 MiB. This
    checker's conservative logical charge is each complete pose/winch wire record
    plus 80+57*K for F, 80+96*A+49*K for applied loads, 80+25*C for moments
    and 80+25*N for planes
    (one validity byte per decoded row); it excludes Python
    object overhead and is not a universal minimum for other implementations.
    max_pending_raw_bytes may lower the 64 MiB whole-known-group admission when
    F, applied loads, moments or planes are understood. One additional bounded opaque chunk may coexist; legacy
    base/J07-only inputs retain their original per-chunk admission. Default mode
    retains no decoded history. I/O errors propagate."""
    # Validate bootstrap bounds/checksums and parse the owned UTF-8 catalog.
    # Scan chunks in order, committing only fully validated pose-chunk candidates.
    # Check seek index, completion and footer, or report an explicitly allowed valid prefix.
    # Normalized Path wrapper for the caller input filename.
    path = Path(path)
    require(type(max_dump_frames) is int and 0 <= max_dump_frames <= MAX_DUMP_FRAMES, 'invalid dump cap')
    require(type(max_dump_bytes) is int and 0 <= max_dump_bytes <= MAX_DUMP_BYTES, 'invalid dump byte cap')
    require(type(max_pending_raw_bytes) is int and 0 < max_pending_raw_bytes <= MAX_PENDING_RAW,
            'invalid pending raw cap')
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
        fluid = validate_fluid_catalog(cat)  # Optional implemented F contract, independent of N rows.
        applied = validate_applied_catalog(cat)  # Self-contained logical stations; F recording may be absent.
        moment = validate_moment_catalog(cat)  # Independent sided cuts and explicit structural display mapping.
        plane = validate_plane_catalog(cat)  # Fixed realized grids and copied provider provenance, without provider capability inference.
        if winch is not None:
            require(winch.frame_bytes <= chunk_limit, 'winch frame exceeds chunk limit')
            result.update(feature_count=len(winch.ports), feature_chunks=0, paired_frames=0,
                          raw_payload_memory_bound_bytes=2 * chunk_limit)
        if fluid is not None:
            require(fluid.frame_bytes <= chunk_limit, 'fluid frame exceeds chunk limit')
            require(winch is None or winch.generation == fluid.generation,
                    'known descriptor generation mismatch')
            result.update(fluid_sample_count=fluid.samples, fluid_chunks=0, grouped_frames=0,
                          pending_raw_group_limit_bytes=max_pending_raw_bytes,
                          raw_payload_memory_bound_bytes=max_pending_raw_bytes + chunk_limit)
        if applied is not None:
            require(applied.frame_bytes <= chunk_limit, 'applied frame exceeds chunk limit')
            require((winch is None or winch.generation == applied.generation) and
                    (fluid is None or fluid.generation == applied.generation), 'known descriptor generation mismatch')
            result.update(applied_attachment_count=len(applied.counts), applied_station_count=applied.stations,
                          applied_chunks=0, grouped_frames=0, pending_raw_group_limit_bytes=max_pending_raw_bytes,
                          raw_payload_memory_bound_bytes=max_pending_raw_bytes + chunk_limit)
        if moment is not None:
            require(moment.frame_bytes <= chunk_limit, 'moment frame exceeds chunk limit')
            require(all(other is None or other.generation == moment.generation for other in (winch, fluid, applied)),
                    'known descriptor generation mismatch')
            result.update(moment_cut_count=moment.cuts, moment_chunks=0, grouped_frames=0,
                          pending_raw_group_limit_bytes=max_pending_raw_bytes,
                          raw_payload_memory_bound_bytes=max_pending_raw_bytes + chunk_limit)
        if plane is not None:
            require(plane.frame_bytes <= chunk_limit, 'plane frame exceeds chunk limit')
            require(all(other is None or other.generation == plane.generation for other in (winch, fluid, applied, moment)),
                    'known descriptor generation mismatch')
            result.update(inflow_source_count=plane.sources, plane_count=len(plane.counts), plane_point_count=plane.points,
                          plane_chunks=0, grouped_frames=0, pending_raw_group_limit_bytes=max_pending_raw_bytes,
                          raw_payload_memory_bound_bytes=max_pending_raw_bytes + chunk_limit)
        # Exact raw group extent and separately charged retained decoded extent.
        group_bytes = frame_bytes + (winch.frame_bytes if winch is not None else 0) + (fluid.frame_bytes if fluid is not None else 0) + (applied.frame_bytes if applied is not None else 0) + (moment.frame_bytes if moment is not None else 0) + (plane.frame_bytes if plane is not None else 0)
        logical_frame_bytes = frame_bytes + (winch.frame_bytes if winch is not None else 0) + (80 + 57 * fluid.samples if fluid is not None else 0) + (80 + 96 * len(applied.counts) + 49 * applied.stations if applied is not None else 0) + (80 + 25 * moment.cuts if moment is not None else 0) + (80 + 25 * plane.points if plane is not None else 0)
        if fluid is not None or applied is not None or moment is not None or plane is not None:
            result['raw_group_frame_bytes'] = group_bytes
        result['implemented_families'] = ['poses', 'winch_geometry', 'fluid_poses', 'applied_loads', 'elastic_moments', 'inflow_planes']
        result['understood_families'] = ['poses'] + (['winch_geometry'] if winch is not None else []) + (['fluid_poses'] if fluid is not None else []) + (['applied_loads'] if applied is not None else []) + (['elastic_moments'] if moment is not None else []) + (['inflow_planes'] if plane is not None else [])
        # Every registered family is implemented; truly unknown optional chunks remain opaque.
        ignored_families = set()
        result['ignored_families'] = sorted(ignored_families)
        if dump_frames:
            result.update(logical_retained_frame_bytes=logical_frame_bytes,
                          logical_retained_bytes=0, logical_retained_limit_bytes=max_dump_bytes,
                          logical_retained_accounting='pose/winch wire extents + F(80+57*K) + applied(80+96*A+49*K) + moments(80+25*C) + planes(80+25*N); excludes Python object overhead')
        # Accepted frame state; updated only after the complete pose chunk validates.
        state = FrameState()
        # Next required global chunk sequence number.
        # Number of fully validated pose chunks.
        # Number of skipped, CRC-checked optional chunks.
        sequence = pose_chunks = optional_chunks = 0
        # Complete feature chunks are counted only with their validated pose partner.
        feature_chunks = fluid_chunks = applied_chunks = moment_chunks = plane_chunks = 0
        # One bounded raw feature batch waiting for the immediately adjacent pose batch.
        pending_feature = None
        pending_fluid = None  # Complete raw F batch awaiting the rest of its known group.
        pending_applied = None  # Complete raw applied batch awaiting its optional winch/required pose suffix.
        pending_moment = None  # Complete raw cut batch awaiting its optional winch/required pose suffix.
        pending_plane = None  # Complete raw grid batch awaiting its optional winch/required pose suffix.
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
            if fluid is not None:
                result.update(grouped_frames=state.frames, fluid_chunks=fluid_chunks)
            if applied is not None:
                result.update(grouped_frames=state.frames, applied_chunks=applied_chunks)
            if moment is not None:
                result.update(grouped_frames=state.frames, moment_chunks=moment_chunks)
            if plane is not None:
                result.update(grouped_frames=state.frames, plane_chunks=plane_chunks)
            result['ignored_families'] = sorted(ignored_families)
            if dump_frames:
                result['frames'] = dumped
                result['logical_retained_bytes'] = state.frames * logical_frame_bytes
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
                if fluid is not None:
                    result.update(grouped_frames=state.frames, fluid_chunks=fluid_chunks)
                if applied is not None:
                    result.update(grouped_frames=state.frames, applied_chunks=applied_chunks)
                if moment is not None:
                    result.update(grouped_frames=state.frames, moment_chunks=moment_chunks)
                if plane is not None:
                    result.update(grouped_frames=state.frames, plane_chunks=plane_chunks)
                result['ignored_families'] = sorted(ignored_families)
                if dump_frames:
                    result['frames'] = dumped
                    result['logical_retained_bytes'] = state.frames * logical_frame_bytes
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
            fluid_family = kind == 5 or stream == 3  # Either reserved discriminator engages F checks.
            applied_family = kind == 6 or stream == 4  # Never skip a malformed implemented load-family tuple.
            moment_family = kind == 7 or stream == 5  # Either known discriminator prevents opaque fallback.
            plane_family = kind == 8 or stream == 6  # Either reserved plane discriminator requires the exact implemented tuple.
            if fluid_family:
                require(fluid is not None, 'fluid chunk without catalog descriptor')
                require((kind, stream, layout, encoding, flags) == (5, 3, 1, 0, 0),
                        'unsupported fluid chunk semantics')
                require(phase == 'poses' and pending_fluid is None and pending_applied is None and pending_moment is None and pending_plane is None and pending_feature is None,
                        'invalid fluid chunk ordering')
                require(records > 0 and first == state.frames and prev == 0 and
                        stored == decoded == records * fluid.frame_bytes and
                        state.frames + records <= MAX_INT, 'fluid count/byte mismatch')
                # Whole known raw groups are admitted before the first payload read/allocation.
                require(records * group_bytes <= max_pending_raw_bytes, 'pending raw group cap exceeded')
            if applied_family:
                require(applied is not None, 'applied chunk without catalog descriptor')
                require((kind, stream, layout, encoding, flags) == (6, 4, 1, 0, 0),
                        'unsupported applied chunk semantics')
                require(phase == 'poses' and pending_applied is None and pending_moment is None and pending_plane is None and pending_feature is None,
                        'invalid applied chunk ordering')
                require(records > 0 and first == state.frames and prev == 0 and
                        stored == decoded == records * applied.frame_bytes and
                        state.frames + records <= MAX_INT, 'applied count/byte mismatch')
                if fluid is not None:
                    require(pending_fluid is not None and (pending_fluid[1], pending_fluid[2]) == (records, first),
                            'applied batch missing or mismatching fluid group')
                require(records * group_bytes <= max_pending_raw_bytes, 'pending raw group cap exceeded')
            if moment_family:
                require(moment is not None, 'moment chunk without catalog descriptor')
                require((kind, stream, layout, encoding, flags) == (7, 5, 1, 0, 0), 'unsupported moment chunk semantics')
                require(phase == 'poses' and pending_moment is None and pending_plane is None and pending_feature is None, 'invalid moment chunk ordering')
                require(records > 0 and first == state.frames and prev == 0
                        and stored == decoded == records * moment.frame_bytes
                        and state.frames + records <= MAX_INT, 'moment count/byte mismatch')
                if fluid is not None:
                    require(pending_fluid is not None and (pending_fluid[1], pending_fluid[2]) == (records, first),
                            'moment batch missing or mismatching fluid group')
                if applied is not None:
                    require(pending_applied is not None and (pending_applied[1], pending_applied[2]) == (records, first),
                            'moment batch missing or mismatching applied group')
                require(records * group_bytes <= max_pending_raw_bytes, 'pending raw group cap exceeded')
            if plane_family:
                require(plane is not None, 'plane chunk without catalog descriptor')
                require((kind, stream, layout, encoding, flags) == (8, 6, 1, 0, 0), 'unsupported plane chunk semantics')
                require(phase == 'poses' and pending_plane is None and pending_feature is None, 'invalid plane chunk ordering')
                require(records > 0 and first == state.frames and prev == 0
                        and stored == decoded == records * plane.frame_bytes
                        and state.frames + records <= MAX_INT, 'plane count/byte mismatch')
                for earlier, pending in ((fluid, pending_fluid), (applied, pending_applied), (moment, pending_moment)):  # Required earlier known families in order5/6/7/8/4/1.
                    if earlier is not None:
                        require(pending is not None and (pending[1], pending[2]) == (records, first),
                                'plane batch missing or mismatching earlier group')
                del earlier, pending  # No loop alias may retain an old raw batch after the group commits.
                require(records * group_bytes <= max_pending_raw_bytes, 'pending raw group cap exceeded')
            if winch_family:
                require(winch is not None, 'winch chunk without catalog descriptor')
                require((kind, stream, layout, encoding, flags) == (4, 2, 1, 0, 0),
                        'unsupported winch chunk semantics')
                require(phase == 'poses' and pending_feature is None, 'invalid winch chunk ordering')
                require(records > 0 and first == state.frames and prev == 0 and
                        stored == decoded == records * winch.frame_bytes and
                        state.frames + records <= MAX_INT, 'winch count/byte mismatch')
                if fluid is not None:
                    require(pending_fluid is not None and (pending_fluid[1], pending_fluid[2]) == (records, first),
                            'winch batch missing or mismatching fluid group')
                if applied is not None:
                    require(pending_applied is not None and (pending_applied[1], pending_applied[2]) == (records, first),
                            'winch batch missing or mismatching applied group')
                if moment is not None:
                    require(pending_moment is not None and (pending_moment[1], pending_moment[2]) == (records, first),
                            'winch batch missing or mismatching moment group')
                if plane is not None:
                    require(pending_plane is not None and (pending_plane[1], pending_plane[2]) == (records, first),
                            'winch batch missing or mismatching plane group')
            # Whether kind, layout, encoding and stream select implemented structural semantics.
            known = kind in (1, 2, 3) and layout == 1 and encoding == 0 and stream == (1 if kind == 1 else 0)
            require(known or winch_family or fluid_family or applied_family or moment_family or plane_family or not flags & 1, 'unsupported required chunk semantics')
            if pending_feature is not None:
                require(known and kind == 1 and flags == 1, 'winch batch must immediately precede required poses')
            if (pending_fluid is not None or pending_applied is not None or pending_moment is not None or pending_plane is not None) and known:
                require(kind == 1, 'metadata inside pending overlay group')
            if known and kind == 1:
                # Validate size/order and known-group population before reading its final buffer.
                require(phase == 'poses' and records > 0 and first == state.frames and prev == last_pose,
                        'invalid pose chunk ordering/index')
                require(records * frame_bytes == stored == decoded and state.frames + records <= MAX_INT,
                        'pose count/byte mismatch')
                if fluid is not None:
                    require(flags == 1 and pending_fluid is not None and
                            (pending_fluid[1], pending_fluid[2]) == (records, first),
                            'pose batch missing or mismatching fluid group')
                if applied is not None:
                    require(flags == 1 and pending_applied is not None and
                            (pending_applied[1], pending_applied[2]) == (records, first),
                            'pose batch missing or mismatching applied group')
                if moment is not None:
                    require(flags == 1 and pending_moment is not None and
                            (pending_moment[1], pending_moment[2]) == (records, first),
                            'pose batch missing or mismatching moment group')
                if plane is not None:
                    require(flags == 1 and pending_plane is not None and
                            (pending_plane[1], pending_plane[2]) == (records, first),
                            'pose batch missing or mismatching plane group')
                if winch is not None:
                    require(pending_feature is not None and
                            (pending_feature[1], pending_feature[2]) == (records, first),
                            'pose batch missing or mismatching winch pair')
            if dump_frames and (fluid_family or applied_family or moment_family or plane_family or winch_family or (known and kind == 1)):
                # Reject retained-history requests before any raw or decoded batch allocation.
                require(state.frames + records <= max_dump_frames, 'frame dump cap exceeded')
                require((state.frames + records) * logical_frame_bytes <= max_dump_bytes,
                        'logical frame dump byte cap exceeded')
            # Owned bounded chunk bytes after complete read and CRC verification.
            payload = f.read(stored)
            if len(payload) != stored:
                return incomplete('truncated chunk payload')
            require(crc(payload) == checksum, 'payload checksum mismatch')
            sequence += 1
            if fluid_family:
                # Independently malformed complete F cannot hide behind a later truncated pose.
                candidate = replace(state)
                fluid_views = memoryview(payload)  # Borrowed complete raw F batch.
                for i in range(records):
                    row = fluid_views[i*fluid.frame_bytes:(i+1)*fluid.frame_bytes]
                    candidate.consume_stamp(row, samples)
                    consume_fluid(row, None, fluid, False)
                del row, fluid_views
                pending_fluid = (payload, records, first)
                del payload
                continue
            if applied_family:
                # Check complete load semantics before any potentially torn following record.
                candidate = replace(state)
                applied_views = memoryview(payload)
                fluid_views = memoryview(pending_fluid[0]) if pending_fluid is not None else None
                for i in range(records):
                    row = applied_views[i*applied.frame_bytes:(i+1)*applied.frame_bytes]
                    previous_stamp = candidate.last_stamp  # Earlier saved structural fact, before this candidate advances.
                    candidate.consume_stamp(row, samples)
                    consume_applied(row, None, applied, previous_stamp, False)
                    if fluid_views is not None:
                        require(row[:80] == fluid_views[i*fluid.frame_bytes:i*fluid.frame_bytes + 80],
                                'applied/fluid common prefix mismatch')
                del row, applied_views, fluid_views
                pending_applied = (payload, records, first)
                del payload
                continue
            if moment_family:
                # Validate every complete moment before a potentially torn following partner.
                # Check earlier known prefixes now; pose-only equality waits for an actual pose.
                candidate = replace(state)  # Trial clocks; complete pending records cannot commit accepted history.
                moment_views = memoryview(payload)  # Borrowed complete raw cut batch.
                fluid_views = memoryview(pending_fluid[0]) if pending_fluid is not None else None  # Optional already admitted F batch.
                applied_views = memoryview(pending_applied[0]) if pending_applied is not None else None  # Optional already admitted applied batch.
                for i in range(records):  # Candidate batch row with fixed catalog extents.
                    row = moment_views[i*moment.frame_bytes:(i+1)*moment.frame_bytes]  # Borrowed unaligned moment record.
                    candidate.consume_stamp(row, samples)
                    consume_moment(row, None, moment, False)
                    if fluid_views is not None:
                        require(row[:80] == fluid_views[i*fluid.frame_bytes:i*fluid.frame_bytes + 80],
                                'moment/fluid common prefix mismatch')
                    if applied_views is not None:
                        require(row[:80] == applied_views[i*applied.frame_bytes:i*applied.frame_bytes + 80],
                                'moment/applied common prefix mismatch')
                del row, moment_views, fluid_views, applied_views
                pending_moment = (payload, records, first)
                del payload
                continue
            if plane_family:
                # Independently validate complete plane rows before reading a potentially torn suffix.
                # Compare every available earlier common prefix; never invent an absent pose equality.
                candidate = replace(state)  # Trial accepted clocks; pending groups cannot publish history.
                plane_views = memoryview(payload)  # Borrowed complete packed grid batch.
                fluid_views = memoryview(pending_fluid[0]) if pending_fluid is not None else None  # Earlier complete F rows.
                applied_views = memoryview(pending_applied[0]) if pending_applied is not None else None  # Earlier complete load rows.
                moment_views = memoryview(pending_moment[0]) if pending_moment is not None else None  # Earlier complete cut rows.
                for i in range(records):  # Fixed record ordinal within this bounded group.
                    row = plane_views[i*plane.frame_bytes:(i+1)*plane.frame_bytes]  # Borrowed potentially unaligned plane record.
                    candidate.consume_stamp(row, samples)
                    consume_plane(row, None, plane, False)
                    for earlier, earlier_views in ((fluid, fluid_views), (applied, applied_views), (moment, moment_views)):  # Each present common80 prefix must match exactly.
                        if earlier_views is not None:
                            require(row[:80] == earlier_views[i*earlier.frame_bytes:i*earlier.frame_bytes + 80],
                                    'plane/earlier common prefix mismatch')
                del row, plane_views, fluid_views, applied_views, moment_views, earlier, earlier_views
                pending_plane = (payload, records, first)
                del payload
                continue
            if winch_family:
                # Validate the complete feature chunk itself even if a later pose is truncated.
                # Malformed known semantics are errors, not a recoverable pending prefix.
                candidate = replace(state)
                feature_views = memoryview(payload)
                fluid_views = memoryview(pending_fluid[0]) if pending_fluid is not None else None
                applied_views = memoryview(pending_applied[0]) if pending_applied is not None else None
                moment_views = memoryview(pending_moment[0]) if pending_moment is not None else None  # Optional complete cut population.
                plane_views = memoryview(pending_plane[0]) if pending_plane is not None else None  # Optional complete grid population.
                for i in range(records):
                    candidate.consume_stamp(feature_views[i*winch.frame_bytes:(i+1)*winch.frame_bytes], samples)
                    consume_winch(feature_views[i*winch.frame_bytes:(i+1)*winch.frame_bytes], None, winch, False)
                    if fluid_views is not None:
                        require(feature_views[i*winch.frame_bytes:i*winch.frame_bytes + 80] ==
                                fluid_views[i*fluid.frame_bytes:i*fluid.frame_bytes + 80],
                                'fluid/winch common prefix mismatch')
                    if applied_views is not None:
                        require(feature_views[i*winch.frame_bytes:i*winch.frame_bytes + 80] ==
                                applied_views[i*applied.frame_bytes:i*applied.frame_bytes + 80],
                                'applied/winch common prefix mismatch')
                    if moment_views is not None:
                        require(feature_views[i*winch.frame_bytes:i*winch.frame_bytes + 80] ==
                                moment_views[i*moment.frame_bytes:i*moment.frame_bytes + 80],
                                'moment/winch common prefix mismatch')
                    if plane_views is not None:
                        require(feature_views[i*winch.frame_bytes:i*winch.frame_bytes + 80] ==
                                plane_views[i*plane.frame_bytes:i*plane.frame_bytes + 80],
                                'plane/winch common prefix mismatch')
                del feature_views, fluid_views, applied_views, moment_views, plane_views
                pending_feature = (payload, records, first)
                del payload  # The pending pair is the sole owner until its pose arrives.
                continue
            if not known:
                require(phase == 'poses', 'optional chunk after seek index')
                optional_chunks += 1
                # Fixed registry labels plus one generic unknown label keep reporting bounded.
                ignored_families.add(OPTIONAL_FAMILIES.get(kind, 'unknown_optional'))
                del payload  # Do not retain a skipped raw chunk while reading the next one.
                continue
            require(stored == decoded, 'raw stored/decoded length mismatch')
            if kind == 1:
                # Copy of accepted frame state committed only after the whole chunk validates.
                candidate = replace(state)
                # Borrowed byte view over the current payload; never retained in the result.
                views = memoryview(payload)
                # Feature bytes remain borrowed from the single pending batch.
                feature_views = memoryview(pending_feature[0]) if pending_feature is not None else None
                fluid_views = memoryview(pending_fluid[0]) if pending_fluid is not None else None
                applied_views = memoryview(pending_applied[0]) if pending_applied is not None else None
                moment_views = memoryview(pending_moment[0]) if pending_moment is not None else None  # Borrowed complete cut records pending this pose group.
                plane_views = memoryview(pending_plane[0]) if pending_plane is not None else None  # Borrowed complete plane records pending this final partner.
                # Dumps are committed with the whole pair, never one earlier row of a failed batch.
                batch_dump = [] if dump_frames else None
                # Current dense sample/frame index within the bounded population.
                for i in range(records):
                    # Optional owned decoded frame produced by the candidate state.
                    previous_stamp = candidate.last_stamp  # Capture the prior saved state before advancing this candidate row.
                    item = candidate.consume(views[i*frame_bytes:(i+1)*frame_bytes], samples, dump_frames)
                    if winch is not None:
                        details = consume_winch(feature_views[i*winch.frame_bytes:(i+1)*winch.frame_bytes],
                                                views[i*frame_bytes:(i+1)*frame_bytes], winch, dump_frames)
                        if dump_frames:
                            item.update(details)
                    if fluid is not None:
                        details = consume_fluid(fluid_views[i*fluid.frame_bytes:(i+1)*fluid.frame_bytes],
                                                views[i*frame_bytes:(i+1)*frame_bytes], fluid, dump_frames)
                        if dump_frames:
                            item['fluid_poses'] = details
                    if applied is not None:
                        details = consume_applied(applied_views[i*applied.frame_bytes:(i+1)*applied.frame_bytes],
                                                  views[i*frame_bytes:(i+1)*frame_bytes], applied, previous_stamp, dump_frames)
                        if dump_frames:
                            item['applied_loads'] = details
                    if moment is not None:
                        details = consume_moment(moment_views[i*moment.frame_bytes:(i+1)*moment.frame_bytes],
                                                 views[i*frame_bytes:(i+1)*frame_bytes], moment, dump_frames)  # Owned cut rows only after dump admission.
                        if dump_frames:
                            item['elastic_moments'] = details
                    if plane is not None:
                        details = consume_plane(plane_views[i*plane.frame_bytes:(i+1)*plane.frame_bytes],
                                                views[i*frame_bytes:(i+1)*frame_bytes], plane, dump_frames)  # Owned grid rows only after explicit retention admission.
                        if dump_frames:
                            item['inflow_planes'] = details
                    if dump_frames:
                        batch_dump.append(item)
                # Accepted frame state; updated only after the complete pose chunk validates.
                state = candidate
                if dump_frames:
                    dumped.extend(batch_dump)
                if winch is not None:
                    feature_chunks += 1
                if fluid is not None:
                    fluid_chunks += 1
                if applied is not None:
                    applied_chunks += 1
                if moment is not None:
                    moment_chunks += 1
                if plane is not None:
                    plane_chunks += 1
                # Release all raw views before reading another batch, bounding peak retention.
                del views, feature_views, fluid_views, applied_views, moment_views, plane_views
                pending_feature = None
                pending_fluid = None
                pending_applied = None
                pending_moment = None
                pending_plane = None
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
    parser.add_argument('--max-dump-bytes', type=int, default=MAX_DUMP_BYTES)
    parser.add_argument('--max-pending-raw-bytes', type=int, default=MAX_PENDING_RAW)
    # Parsed input path and explicit checker options.
    args = parser.parse_args(argv)
    try:
        # Owned checker result dictionary populated only with validated state.
        result = check(args.path, allow_incomplete=args.allow_incomplete,
                       dump_frames=args.dump_frames, max_dump_frames=args.max_dump_frames,
                       max_dump_bytes=args.max_dump_bytes, max_pending_raw_bytes=args.max_pending_raw_bytes)
    # Expected parse/validation/I/O exception reported at its public boundary.
    except (InvalidSidecar, OSError) as e:
        print(json.dumps({'valid': False, 'error': str(e)}), file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
