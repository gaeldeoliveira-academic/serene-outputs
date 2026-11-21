# Independent pose-sidecar checker and fixtures

These stdlib tools implement `spec/POSE_SIDECAR.md` independently of the C++ recorder.
They do not load core, Chrono, scientific ArchiveSession or native bindings.

From the repository root:

```text
python tools/pose_sidecar/author_fixtures.py
python tools/pose_sidecar/test_checker.py
python tools/pose_sidecar/check_sidecar.py fixtures_pose_sidecar/transient.spose --dump-frames
python tools/pose_sidecar/check_sidecar.py fixtures_pose_sidecar/incomplete.spose.partial --allow-incomplete
```

`author_fixtures.py` deterministically rewrites only its named synthetic fixture files.
`fixtures_pose_sidecar/expected.json` contains literal expected frames and fixture hashes;
it is generated from independent author data, not from the checker or production writer.
The offset body port differs from the structural tip; its final quaternion deliberately
changes sign without changing orientation. The anchor port is a separate actual location.
The transient has a nonzero start, accepted IDs 0/4/7 and a final batch of one frame.
Static load factor and prepared-state semantics are exercised in separate files.

Import `check_sidecar.check(path, allow_incomplete=False, dump_frames=False,
max_dump_frames=1000)` for writer/reader qualification. Default operation retains the
bounded catalog, one chunk and constant counters; it returns no pose history.
`dump_frames=True` is explicitly capped for small fixtures and returns `frames`, each
containing `stamp` and `poses` (one seven-value list per sample). Exceeding the cap fails.
Strict mode rejects incomplete files. Explicit recovery mode returns only a validated
complete-chunk prefix after truncation; corruption/unsupported required semantics still
fail. CRC is integrity checking, not authentication. Successful complete decoding does
not imply run or capture success: inspect the independent completion outcome fields.

This is a small conformance checker, not the production viewer or general archive API.
It does not enforce visual continuity, infer connections or recover missing frames.

## Stage00 optional F geometry

The new independent literal corpus is `fixtures_pose_fluid`; the author is
`author_fluid_fixtures.py`, which accepts a new destination and refuses existing
ones. It imports only earlier independent fixture definitions, never production
recorder/checker logic. The frozen common/F contracts are in
`spec/POSE_OPTIONAL_OVERLAYS.md` and `spec/POSE_FLUID_GEOMETRY.md`.

`test_fluid_checker.py` exercises fully F-aware semantics and allocation
admission. Compatibility with pinned base and winch-aware readers checks their
explicitly narrower subsets. The generic historical Qt driver requires one
catalog and one filename-selected oracle per invocation; the qualification
uses a separate per-case adapter with unchanged fixture bytes and executables.
The convenience multi-case harnesses in the author output do not qualify mixed
catalog preservation by that historical driver. Read the F handoff for actual
commands, limits, failure outcomes and exact artifact identities.

## Applied physical loads

`fixtures_pose_applied` and `author_applied_fixtures.py` provide an independent
151-case literal corpus authored before the recorder. The production checker
does not import the author. `test_applied_checker.py` checks both full semantics
and bounded admission. Pinned base, winch-aware and F-aware readers qualify only
their explicitly understood subsets; they cannot validate opaque load semantics.
Logical material-coordinate addresses compare finite binary64 values, including
integer-spelled JSON coordinates; integer IDs, tokens and counts remain exact.
Original catalog metadata is preserved. See the load handoff for receipts.

## Signed elastic moments

The independent `fixtures_pose_moments` corpus and `author_moment_fixtures.py`
precede recorder implementation. They cover off-node cuts, in-element and native
interface sided breaks, explicit base-edge mappings, full signed triples,
validity and mixed-stream recovery. `test_moment_checker.py` checks the current
reader against these literal files without importing production encoding helpers.

Pinned older Python and Qt readers validate their narrower understood subsets.
Three deliberately invalid JSON catalogs require a valid control catalog in the
old Qt test harness so that it can reach the actual invalid file and check its
rejection. Original file bytes and original expected classifications remain
unchanged; the initial harness failures and the adapted results are retained.
All four pinned Python readers also reject those three invalid JSON files.

## Fixed inflow planes

`fixtures_pose_planes` and `author_plane_fixtures.py` contain 223 independent
literal cases authored before recorder implementation. Their original oracle
and five pinned Python reader classifications remain immutable. The production
checker does not import the author. `test_plane_checker.py` verifies typed source
identity/provenance, finite realized geometry, separate packed masks, world
velocity bits, bounded retention and atomic mixed-stream recovery.

The fully aware checker recognizes kind8 OR stream6 and validates it strictly.
The historical moment-only `opaque_future_planes.spose` is deliberately malformed
for the now-understood plane family: the live regression test rejects it while
preserving the original file, moment-only oracle and pinned-reader acceptance.
Truly unknown optional kinds remain opaque under the common group rules.

Older native readers are exercised against each unchanged literal file using
their original subset classifications. A strict-JSON-invalid catalog may use a
valid control catalog only to initialize the old test harness; it does not replace
the actual submitted file or change its rejection expectation. These subset
checks establish compatibility, not plane or provider semantic validation.
