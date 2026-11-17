# J07 contract review handoff

17 November 2026. **Contract candidate ready for joint review. Recorder implementation
remains gated. No identifiers are frozen and J07 is not complete.**

Read `CONTRACT_CANDIDATE.md`, `API_SKETCH.md` and `BUILD_INSTALL_PLAN.md` together.
The fixture directory contains independent literal byte examples and separate
legacy versus prospective feature-aware expectations. `compatibility_probe.json`
records actual old-reader executions and exact tool identities. The manifest
identifies this draft revision; a hash is not a contract acceptance decision.

## Precise review questions

1. **Main/viewer:** approve bootstrap1.0/mask0 plus proposed optional kind4,
   stream2, layout1, raw encoding and the `winch_geometry` descriptor? Old
   structural readers pass the exact candidate and reject minor1. They ignore
   new feature semantics; the feature-aware reader must enforce them explicitly.
2. **Core/viewer:** approve immediately adjacent feature-before-pose batches,
   full copied structural stamps, one-to-one capture frames, and paired-prefix
   visibility? Scientific cadence remains independent, and feature seek may scan
   headers. No extra completion/index population is introduced.
3. **Core:** approve all active prepared winch laws, including zero effective
   tension, in immutable prepared order, independent of scientific selection?
   Confirm owner occurrence/force keys and physical A/B/D references are available
   at setup, and generation versus capture-plan IDs remain distinct.
4. **Core:** approve the common accepted-packet source for duplicate A/B/D XYZ,
   with bitwise equality to referenced physical poses and rejection of disagreeing
   shared ports? Current source inspection does not prove universal D/signed-zero
   agreement. This needs focused J08 proof, not an assumed tolerance.
5. **Main/Core:** approve package0.2.4 and one fresh combined Release/Debug install,
   followed by the narrow J08 version/manifest and current-source-selection
   guard/dispatch refresh? Both the empty Stage07 path and nonempty I06a path
   currently select historical source pins; a new JSON path alone is insufficient.
   Keep old wip.9 recorder tests disabled until fresh matched J08 tests exist. Existing
   versions, codec options and frozen prefixes remain unchanged.

## What was actually checked

* Unmodified current Python checker: **15/15 expected legacy results**.
* Existing viewer `reader_tests.exe`: **15 fixture outcomes and five existing
  focused regression checks passed**, with exact structural bits/stamps/catalogs.
  No viewer build, GUI launch or rendering took place.
* Independent fixture-only byte audit: **five positive paired frames and120 XYZ
  scalars matched exactly**; empty failed recording has zero frames.
* Exact minor1 and unknown-required-feature negatives reject in both old readers;
  corrupted optional payload rejects. Truncated feature, truncated pose and EOF
  after a staged feature return only the two earlier structural frames, incomplete.
* Semantic negatives (mismatched state, wrong generation, NaN feature value,
  unknown geometry tag and omitted features) are accepted by old readers as opaque
  optional data. Their **future feature-aware rejection is not tested here**.

The small corpus uses literal asymmetric geometry, two scopes sharing force ID
`pull`, a zero-tension-limit tag, prepared/static/transient states, nonzero start
and off-stride target. It is not a force-kernel oracle or a simulation trajectory.

`legacy-viewer-harness/transient.spose` is an intentional missing-feature control
needed by the old executable's hardcoded focused mutation. It is NOT a positive
feature-format example. It has the extended catalog but no feature batches; the
new reader must reject it. The main candidate positive is
`fixtures/paired_transient.spose`. The harness has a separate legacy-only manifest
so passing it cannot be mistaken for new-stream semantic qualification.

## Work after gate approval

Implement only the approved spec/checker/recorder changes with documented APIs,
then exercise installed producer readback and focused positive/negative cases.
Add explicit duplicate role/identity, invalid reference, count/overflow/capacity,
phase, shared-port/signed-zero and reserved/layout checks; verify invalid input
cannot partially admit a packet. Exercise truncation, no-clobber and targeted
write/flush/close/publication faults, including a failure between F and P. Recheck
feature-free fixtures and old-reader behavior against the final identifiers.
Actual simulation capture is J08; rendering is J09. No solver/convergence campaign
or Python scientific-format implementation is needed for J07.

## Preserved ownership and state

Read J07/current status, acceptance gates, documentation brief, the winch report's
scientific/capture section, Core J06 handoff, current recorder/spec/checker, reader
rules and dependency guards. J06 handoff/current status supersede older pending
implementation statements in report prose awaiting its separate owner's alignment.

Starting Outputs status contained only untracked `fixtures_extensions_draft/history/`
and `tools/conformance/history/`. They were left untouched. Theory has concurrent
report/prompt edits belonging to Main and report owners; none was changed here.
No production source, normative specification, existing fixtures, AGENTS, package
version, build/install tree, sealed receipt, branch, index, commit or remote changed.
Only this new review candidate was authored; local author/probe helpers are scratch.
