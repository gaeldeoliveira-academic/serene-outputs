# Independent Stage00 F-pose candidate fixtures

These are literal byte-layout and reader-group oracles, not numerical beam or
fluid solutions. `tools/pose_sidecar/author_fluid_fixtures.py` generates a new
corpus and refuses replacement. The candidate is not a frozen implementation
receipt. No production recorder or checker is imported by that author.

The main `expected.json` carries exact asymmetric frame dictionaries and each
file's size/SHA-256. `valid` describes the proposed F-aware contract;
`baseline_valid` and `winch_aware_valid` describe the two explicitly narrower
readers. Successful opaque skipping does not validate F geometry or metadata.

The base population has eight physical samples, two winch arrangements and six F samples.
The two F lines belong to different flexible occurrences but both use local
spanline ID `F`. The first line has two separated intervals meeting at sided
material coordinate2; coincident world positions retain different local-F
orientations. Source/effective fingerprints remain separate. Record widths are
512 structural bytes,288 winch bytes and417 F bytes, giving1217 per full group.
The F mask is LSB-first, six used bits, then six seven-double world poses.

`fluid_k7.spose`, `fluid_k8.spose` and `fluid_k9.spose` extend the second F line
with declared stations and distinct literal poses. Their F records contain
473, 529 and 586 bytes respectively. Their one-, one- and two-byte masks place
the first binary64 row at deliberately unaligned offsets 81, 81 and 82.
The respective literal oracles are `k7_frames`, `k8_frames` and `k9_frames`.
`padding_mask_k7.spose` and `padding_mask_k9.spose` independently set an unused
high bit in the final mask byte and must be rejected by an F-aware reader.

Expected frames have `fluid_poses` containing `generation`, `phase` and `samples`;
each sample has `valid` and `world_pose` in xyz/wxyz order. Unavailable rows have
the exact positive-zero/identity-quaternion representation. A separate valid
zero-position sample proves that the mask distinguishes zero from unavailable.

The corpus includes a complete valid F batch whose later time differs from the
missing pose in `unjoined_time_pending_fluid.spose.partial`. That cross-population
join cannot yet be evaluated, so the two prior frames remain an incomplete prefix.
Other complete pending-F cases have defects independently visible before a pose
arrives and must be rejected by the F-aware reader.

`legacy-viewer-harness` and `winch-aware-viewer-harness` adapt outcomes for the
existing generic `reader_tests.exe` programs. Their `transient.spose` is a
deliberately feature-free control for historical hardcoded mutation checks and
is not an F-positive fixture. `mixed_transient.spose` is the positive mixed
example. The harnesses omit only initial-disabled capture because the old driver
selects expected frames by a hardcoded filename rule. They do not fit the separate
winch-specific executable's hardcoded40-case count. No executable is modified.
