# Script 22 Outputs owner handoff

23 November 2026.

**All four actual 120 s cases have passed independent archive, pose and real viewer readback.** The approved clocks are 0.04 s for scalar histories and 0.08 s for poses. All 28 scalar channels and every authored pose layer are retained; no SDK, reader or storage-format change was needed.

Actual sweep: `C:/Users/12650029/serene-release/serene-input-preparation/build/script22/equilibrium poses-12p5 sweep-01`.

| Qualified case | Accepted steps | Scalar samples | Pose frames | Scalar bytes | Pose bytes |
|---|---:|---:|---:|---:|---:|
| dt-0p020 | 6,000 | 3001 | 1501 | 4,536,976 | 180,477,294 |
| dt-0p010 | 12,000 | 3001 | 1501 | 5,421,346 | 180,477,295 |
| dt-0p005 | 24,000 | 3001 | 1501 | 7,214,255 | 180,477,297 |
| dt-0p001 | 120,000 | 3001 | 1501 | 21,663,921 | 180,477,298 |

Each qualified case completed at time 120 s with the expected accepted identity, scalar/pose strides and initial/target coverage. The independent installed Python reader verified every archive section and decoded all 28 histories with finite values. Input, context and log sections are included. The official pose checker validated the complete sidecar, and the real viewer reader traversed all frames with every catalog population present and zero ignored optional chunks.

Actual viewer charge is **182,868,331 bytes (174.397 MiB)** per complete recording, below the 268,435,456-byte retained-data cap. This is full-file headless admission and accessibility, not a total-RSS measurement, graphical rendering test or numerical convergence claim.

Independent readback uses immutable byte-for-byte snapshots of the live sweep summary after each selected row reports completed/exit 0. The snapshot identities and original paths are preserved; changing sibling statuses cannot invalidate an already completed selected artifact.

Evidence root: `C:/Users/12650029/serene-academic/serene-theory/tmp/script22_outputs_20261123/full120/`. For each qualified case, read `readback/<case>.execution.json`, `readback/<case>.bounded-readback.json`, `readback/<case>.full-integrity.json`, `viewer/<case>.viewer.json`, and `producer_capacity/<case>-manifest-capacity.json`. These retain exact inputs, artifact hashes, commands, clocks, metadata, complete populations and producer reservations.

| Qualified case | Exporter requested bytes | Provenance requested bytes | Total prepared payload bytes |
|---|---:|---:|---:|
| dt-0p020 | 1,208,879 | 5,392,385 | 45,747,455 |
| dt-0p010 | 1,208,879 | 5,392,385 | 45,747,455 |
| dt-0p005 | 1,208,879 | 5,392,385 | 45,747,455 |
| dt-0p001 | 1,208,879 | 5,392,385 | 45,747,455 |

These are normal-path producer manifest counters, independently matched to published artifact identities. They close exporter/provenance admission for the qualified cases; their scope is requested payload, not peak process memory. The prepared-only fine report discussed below remains historical evidence and is not relabelled.

**All four storage checks and normal-path export/provenance gates are complete**, including 120,000 accepted fine steps. Numerical comparisons and scientific acceptance remain with Core/Main.

Final aggregate receipts and SHA256 identities:

- `readback/full120-readback-summary.json`: `9bc065a69d77c247f97b688786fa11bec3d35766a4ea833b24aa4daf427c00c6`.
- `viewer/actual-full120-viewer-summary.json`: `423538cd452433e7f4935c1bb4cc964e584ffa11fd7ccba379e6c7fb0214117f`.
- `producer_capacity/terminal-capacity-review.json`: `f3ac2241a3b36adb557ffa3b737c07e85fe6d3b217f0dc17a52df61ea7f06d4c`.

The finest case's actual archive is 21,663,921 bytes. Its planned maximum archive extent, 2,242,014,254 bytes, is a checked capacity bound, not an allocated buffer or the final file size. The normal run retained its 512 MiB context cap and 64 MiB native workspace budget. All four reported the same 45,747,455-byte total prepared payload; full accounting and its limits are preserved in the capacity review.

## Earlier pilot and capacity evidence

23 November 2026. **All four actual short pilots pass independent archive and pose readback. No Outputs SDK, Python reader or storage-format change is needed for the approved layout.** Final approved clocks are **0.04 s scalar histories and 0.08 s poses**, with every authored layer preserved. Main polls this handoff; Core-2 owns production and input-preparation owns authored cases. No commits or pushes.

### Actual artifacts and qualification boundary

Actual sweep:
`C:/Users/12650029/serene-release/serene-input-preparation/build/script22/equilibrium-native-pilots-05/sweep-summary.json`.
Its SHA256 is `c4401d7324e60aa8e864dff57b40f0189d2ad6ce70cb9b9535f49c761766da21`.
Producer executable SHA256: `dc7a6896906fa38178887642d434c3b464fd29a3d2c4d49b1f94feb7d956cc0f`.

These are **actual 0..0.08 s pilots**, each with three scalar samples and three transient pose frames at the earlier common 0.04 s cadence. All four processes completed with exit 0. They are not 120 s results and do not exercise the newly approved 0.08 s pose cadence.

| Case | Final accepted step | Scalar archive bytes | Pose file bytes | Independent checks |
|---|---:|---:|---:|---|
| dt-0p020 | 4 | 3,047,653 | 997,674 | Passed |
| dt-0p010 | 8 | 3,048,246 | 997,674 | Passed |
| dt-0p005 | 16 | 3,049,459 | 997,676 | Passed |
| dt-0p001 | 80 | 3,058,984 | 997,677 | Passed |

The coarsest pair is in `dt-0p020/results/outputs.serene` and `poses.spose` below that sweep. SHA256 identities:

- Scalar: `eab7733d7139359c1134ce8de34277e521c1cb72438785010a43c2918be6dc78`.
- Pose: `5ee9040dd1a0641877d5ad2eb188c04766c27d58b64ee1505ae166cbf36d9026`.
- Pose catalog: `8032b074c9d51e0cd365262d96efa02934297349b1274409caeaa9c573d82c4e`.

Independent installed Python 0.3.0 reads these native 1.0 archives. All four pass `verify_all()` (every section digest/ownership and retained evidence), and all 28 histories decode to three finite values. Each has 19 requests, included input/context/log sections, and recorded successful numerical/observation outcomes. Separate bounded checks preserve exact scalar IDs, effective requests, units, clocks and accepted identities. Numerical accuracy, stability and convergence are outside these storage checks.

The maintained pose checker validates complete files and all known streams. The real viewer `read_spose` additionally admits all four pilots and traverses every frame with every declared population present, no ignored optional chunks, initial time/step zero and expected target events. This is headless reader evidence, not graphical rendering or wall-clock playback.

All owner evidence below is rooted at:
`C:/Users/12650029/serene-academic/serene-theory/tmp/script22_outputs_20261123/`.

- `pilots05/actual-pilots-bounded-readback-v1.json`: all four scalar/pose clocks, selected per-blade identities, effective requests and full pose integrity.
- `pilots05/actual-pilots-bounded-execution-v1.json`: isolated invocation, exit 0, empty stderr.
- `pilots05/actual-pilots-full-integrity-v1.json`: full archive verification, complete scalar catalog and artifact hashes.
- `pilots05/viewer/actual-pilots-summary.json`: all four actual viewer results, populations/occurrences, hashes and final-cadence projection.
- `pilots05/viewer/dt-0p020.cadence008.capacity.json`: exact catalog byte accounting projected to 1501/1502 frames. Earlier cadence004 receipts remain unchanged.

### Actual layout and viewer capacity

All pilots contain 459 structural pose samples, 183 structural segments, 708 fluid samples, 354 fluid segments, three load attachments with 207 stations each (621 total), and 990 elastic moment cuts. Occurrences retain tower, nacelle, hub and all three blades, plus the root assembly and ground anchor. Nacelle and hub appear as assemblies; they were not discarded based on occurrence kind. No winch or inflow-plane stream is authored in this catalog.

| Enabled frame family | Raw payload bytes/frame | Viewer retained logical bytes/frame |
|---|---:|---:|
| Structural poses | 25,768 | 25,768 |
| Fluid poses | 39,817 | 40,436 |
| Applied loads and provenance | 30,254 | 30,797 |
| Elastic moments | 23,964 | 24,830 |
| **Total** | **119,803** | **121,831** |

The actual eager-reader cap is **268,435,456 retained logical bytes (256 MiB)**, plus a 100,000-frame limit. It is not a file-size cap or total process RSS limit. Packed masks become bytes in retained memory; stream overhead and attachment provenance are included above. Static section geometry/catalogs, containers, GPU resources and other allocations have additional costs.

| Pose schedule | Charged frames | Retained logical bytes | Margin below cap | Result |
|---|---:|---:|---:|---|
| Earlier 0.04 s, 120 s | 3001 | 365,614,831 | -97,179,375 | Does not fit |
| **Approved 0.08 s, 120 s** | **1501** | **182,868,331 (174.397 MiB)** | **85,567,125** | **Fits** |
| Approved plus one prepared frame | 1502 | 182,990,162 (174.513 MiB) | 85,445,294 | Fits |

This exact catalog permits at most 2203 retained frames. Raw all-stream payload at 1501 frames is 179,824,303 bytes. The bootstrap's 4 MiB per-stream chunk limit bounds a batch to 105 frames; the viewer also enforces a 64 MiB grouped raw payload bound. A short pilot's observed batch size is not its producer's reserved capacity. No force, body, section or sample was dropped and no viewer limit changed.

A **SYNTHETIC capacity-only replay** now exercises the real loader across all 1501 frames through 120 s, with the exact coarsest catalog and every physical payload field/mask retained. It preserves the initial pilot frame and repeats the final pilot fields for later synthetic frames, rewriting timing/event and associated load-interval records. Opaque provider identities are repeated; this is not a physical simulation or new provider evaluation. Both the complete official checker and the real viewer reader passed. The viewer traversed all 1501 frames, charging exactly 182,868,331 bytes. Batches contain at most 64 frames: largest stream 2,548,288 bytes, largest complete group 7,667,392 bytes, 24 groups total.

Replay file: 180,469,928 bytes; SHA256 `dc16b82a6d157431685bc13b9c4ecd4bc4ce67c3350a0641c1c8f83e163acfc9`. Recipe, source identities, scope and readback are in `pilots05/viewer/SYNTHETIC-capacity-recipe.json`, `SYNTHETIC-capacity-checker.json` and `SYNTHETIC-capacity-viewer.json`. The original pilot is unchanged. This proves reader capacity for the preserved layout, not actual 120 s physics, production export or every-frame graphical rendering.

### Approved final clocks and channel identities

Final runs require **3001 scalar samples** and **1501 transient pose frames**, including time zero and 120 s. Optional prepared poses are separate and remain charged. Pose frame k coincides with scalar row 2k (zero based); compare stored physical times and accepted IDs, not equal row indices or interpolated values.

| Integration dt | Scalar stride (0.04 s) | Pose stride (0.08 s) | Terminal accepted step |
|---:|---:|---:|---:|
| 0.02 s | 2 | 4 | 6000 |
| 0.01 s | 4 | 8 | 12000 |
| 0.005 s | 8 | 16 | 24000 |
| 0.001 s | 40 | 80 | 120000 |

For each blade i=1,2,3, the bounded check preserves these exact tuples, the complete actual scalar ID and producer-perceived effective request:

| Group | Request | Component | Unit |
|---|---|---|---|
| turbine_aero | blade_i_aero_force_density_x | force_density_world.x | N/m |
| turbine_structure | blade_i_root_moment | moment.x | N m |
| turbine_structure | blade_i_tip_deflection | x | m |

The complete catalog contains aero XYZ (9), blade-tip displacement XYZ (9), root moments XY (6), rotor speed in rad/s (1), and blade pitch coordinates in rad (3). Aero is force density at logical fluid node 0, not integrated force. Root moments use current structural axes, outboard-on-inboard action and the structural spanline moment point. Tip displacement uses world components relative to reference configuration. Pose positions are absolute world metres; orientations are Hamilton wxyz local-to-world. Physical comparisons require the corresponding reference and frame conventions.

### Fine-step capacity boundary

The earlier `equilibrium-native-fine-prepare-05` successfully prepared the finest case: phase `prepared_inactive`, preparation passed, exit 0, 120,000-step capacity, six prepared bodies and capacity for 3001 scalar samples. That historical receipt records **0.04 s poses / stride 40 / 3001 frames**. The subsequent actual run qualified above records the revised **0.08 s / stride 80 / 1501 transient frames**.

Reported prepared scalar storage is 756,671 bytes: 672,224 values, 24,008 times, **zero retained identity bytes**, 224 staging and 60,215 bindings. Do not mistake a hypothetical persisted time-plus-identity size for measured Core retained storage. Pose prepared payload is 4,854,342 bytes, including 25,704 flat scratch bytes. Live pose writer workspace/batch/index capacities remain zero; started/finalized/published are false. No archive/pose/run manifest was published by prepare-only.

**The prepared-only report did not establish full native exporter/provenance admission.** At the recorded source inspection, `serene-core/src/application.cpp` returned from `--prepare-only` at line 985, before `PreparedNativeExport` creation/preparation and the combined export/provenance cap check at lines 1039-1041. That provider report has a 512 MiB context cap and requested 64 MiB output workspace, not actual exporter/provenance allocations. The compiled-source aggregate matches the coarse pilot; source corroboration is identified separately from binary execution evidence. The earlier successful coarse pilot export covered only four accepted steps and three samples. The later actual runs at the top now close this admission gap.

Detailed historical pointers, hashes and reproducible read-only inspection: `pilots05/fine_prepare/fine_prepare_inspection.json`, SHA256 `4992a1da9b7e307d206839c2315822f70fe2bce9a85baf7e15245b163d5fee6f`, with `inspect_fine_prepare.py` beside it. The completed normal-path campaign supplies separate new evidence; this prepared-only result remains unchanged.

### Reproduce final readback

The frozen bounded helper already supports distinct clocks; no implementation change is needed. SHA256 `8ff07a3c9e4de4fbbd9b80b34ceb93a2b796a3a69a9ab1931a46a6048296568b`. Use an actual Script 22b **run** summary and fresh receipts. Expected end defaults to 120 s. Omit `--case` to process all four serially, coarsest first; add `--case dt-0p020` for an initial check.

```powershell
$task = 'C:/Users/12650029/serene-academic/serene-theory/tmp/script22_outputs_20261123'
$py = 'C:/Users/12650029/serene-academic/serene-theory/tmp/modal_m1_20261122/new-reader-v1/Scripts/python.exe'
$site = 'C:/Users/12650029/serene-academic/serene-theory/tmp/modal_m1_20261122/new-reader-v1/Lib/site-packages'
$checker = 'C:/Users/12650029/serene-academic/serene-outputs/tools/pose_sidecar/check_sidecar.py'
$actualSweep = '<actual complete 120 s Script 22b sweep-summary.json>'
$env:PATH = 'C:/Windows/System32'
Remove-Item Env:PYTHONPATH,Env:PYTHONHOME,Env:HDF5_PLUGIN_PATH,Env:CMAKE_PREFIX_PATH -ErrorAction SilentlyContinue
& $py -I -B "$task/check_script22_outputs.py" --sweep $actualSweep --reader-site $site --pose-checker $checker --scalar-interval-s .04 --pose-interval-s .08 --receipt "$task/final120-004-008-readback-01.json"
```

This bounded check owns at most 4096 values per scalar/time/identity array (32 KiB), plus separately bounded decoder/catalog/batch state, and uses a stamp-only second pose scan. It verifies nine selected scalar histories and touched frame digests; it does not invoke `verify_all` on every scientific section. The additional full-integrity check above was executed on the small pilots. These are not total-RSS or allocation-free guarantees. Missing artifacts fail/remain pending; the helper never launches Core or manufactures missing samples.

Real full-file viewer command, repeated serially per case with fresh receipts:

```powershell
$env:PATH = 'C:/Qt/6.12.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;C:/Windows/System32'
$pose = '<actual complete 120 s coarsest poses.spose>'
& "$task/viewer_probe/build-v1/script22_viewer_probe.exe" $pose "$task/final120-coarsest-viewer-01.json" 1501 120 6000
```

Use final steps 12000/24000/120000 for the remaining cases, always 1501 transient frames and 120 s. The probe links existing qualified viewer libraries; executable SHA256 `df00c6ff31c77ed8d522d84603ef3c3596e2d1ccbfcdc399427878e2f820e66f`. It traverses every retained frame/population but renders no images. The ordinary viewer smoke mode skips timed playback beyond 0.1 s, so eager-read/target navigation is not 120 s wall-clock playback.

For a changed catalog, rerun `cadence004/measure_pilot_bytes.py --frames 1501` with its documented pilot/checker/viewer-source arguments and a fresh receipt. SHA256 `4687eb71db7c28a4b352b72ddb8a81f055abf4c9ae0e26c310b63671abb58156`. Inspect its fit booleans; successful measurement alone is not a capacity pass. Opaque unknown streams are refused for projection. Exact same-catalog file-size projection additionally needs the planned frames-per-chunk.

### Compatibility and historical evidence

Selected Outputs 0.2.8 planes SDK manifest pin: `d31ea2491b02466aba563cfd0e68ee0653d2612072c07d42b7fdb9b374a8beac`. Independent Python 0.3.0 wheel SHA256: `adda76abf657f699239096503c0f5193d5d9585032c4eaabbf2dd26d6beeb383`. Existing SDK/reader support suffices; no rebuild, dependency installation or format redesign was needed.

Historical controls and failed receipts remain unchanged. The old overlay control's reader accepted its data but the Script-22 zero-start assertion rejected its 1.25 s start; it remains failed evidence. Historical tower/static checks and synthetic absent-artifact controls are not promoted to actual Script 22 results. The preceding handoff is preserved at `pilots05/handoff-before-pilots05.md` when this update is published.

The production status at the top supersedes the earlier pending gates only for cases explicitly qualified there. Historical pilot, replay and prepared-only receipts remain unchanged.
