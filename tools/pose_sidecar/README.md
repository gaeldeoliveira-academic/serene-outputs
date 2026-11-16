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
