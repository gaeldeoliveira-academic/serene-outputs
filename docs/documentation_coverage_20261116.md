# Documentation coverage — 16 November 2026

This pass documents the maintained first-party Outputs implementation through the
existing 0.2.3 pose-recorder working tree. It changes comments/docstrings and
maintainer/build prose only. It is not a new package, wire contract or binary
qualification. No build, test execution, fixture regeneration, dependency change,
Git staging, commit or push was performed.

## Covered scope

Reviewed and updated 75 existing files, plus this handoff and the new root
`AGENTS.md`:

| Area | Files | Coverage |
| --- | ---: | --- |
| `include/serene/outputs/` |10| All public model, typed diagnostics, immutable codec profile, codec/hash, archive, bundle, document preflight and pose-recorder declarations/fields |
| `src/` |10| All eight implementations and private spool/parser headers, including ownership, checked planning, exact-real verification, graph validation, streaming I/O and failure cleanup |
| `tests/` |34| C++ fixtures/helpers/cases, Python fixture/oracle/readback drivers, five installed-consumer CMake projects and the retained codec command driver |
| Maintained `tools/` |16| Two conformance authors, two C++ inspectors and their CMake files; three pose tools; seven build/consumer command drivers |
| Build/prose |5| Root CMake, dependency/component CMake, README and `cmake/BUILDING.md` |

The maintained tools are `tools/conformance/{author_fixtures.py,custom_author.py,
inspect_records.cpp,CMakeLists.txt,custom_inspector/inspect_container.cpp,
custom_inspector/CMakeLists.txt}`, `tools/pose_sidecar/{author_fixtures.py,
check_sidecar.py,test_checker.py}`, `tools/build_{extensions,capacity_planner,
document_preflight,pose_recorder}.cmd` and
`tools/qualify_{extensions,capacity_planner,document_preflight}_consumer.cmd`.
The historical HDF5 conformance author/inspector are documented as such; this does
not introduce a production HDF5 dependency or compatibility reader.

Coverage review used a complete maintained-file inventory, declaration/function
inspection, per-field Doxygen attachment checks, function-body activity checks and
local-variable/loop reviews. Defaulted/deleted special members are documented at
their declarations because they have no handwritten body. No maintained owned
code area remains excluded from this pass. Existing pose-tool README guidance was
reviewed and needed no change.

## Contracts clarified

- Preparations own catalogs/workspace; views, callbacks and sessions have explicit
  borrowed lifetimes. Copied summaries own their facts independently of the
  reference returned by an owner getter. Requested workspace accounting is not
  total process memory or a global allocation guarantee.
- Fixed-accuracy acceptance compares exact binary64 values on the 33-limb integer
  lattice before raw/ZFP size selection. Rounded error reporting is separate.
  Frame-record layout decoding alone does not validate tags, ranges or profiles.
- Diagnostic counts/extrema are verified, while mean/median remain bounded
  producer assertions. Missing metrics use masks/canonical zeros. Included-empty
  sources/traces remain distinguishable from unavailable/disabled evidence.
- Trusted spool binding, source-used/section ownership, bounded JSON admission and
  tile copying remain separate from recompression. Bundles preserve non-original
  source roles when replacing the common original-role association.
- Archive/bundle destruction removes owned unpublished scratch; pose failures
  retain `.partial` evidence. Checked flush/close and no-clobber publication do
  not promise power-loss durability. Pose values use world metres and Hamilton
  wxyz sample-local-to-world orientation; capture scheduling belongs to Core.
- Current package guidance includes the independently importable pose component,
  keeps default archive/codec/model selection explicit and uses fresh example
  build paths. Historical wrappers still name their original qualification paths.

## Verification and independent review

The baseline was the current working tree on `main`, including all uncommitted
pose-recorder additions and build/doc integration. No comparison restored files
from HEAD. Before applying edits, all 82 captured product files matched their starting byte
digests. The copy procedure then verified all 77 staged/product file pairs byte
for byte, including the two new documentation files. The seven historical
generator/sealer inputs remained unchanged. Baseline CRLF/LF conventions,
including sparse inherited CRLF lines, are preserved.

All 75 changed baseline files pass the bounded documentation-only checks:
46 C++ files retain identical string-aware lexical tokens, logical preprocessor
directives and physical continuation lines; nine Python files retain equal ASTs
with only genuine module/class/function docstrings removed, plus identical
executable token spelling; ten CMake files and eight batch drivers retain every
noncomment executable line. The remaining two files are Markdown prose.
Temporary check scripts and captured baselines stay in ignored Theory scratch,
not in this package. No permanent validation implementation was added.

Serene Main independently sampled the staged pose/bundle API documentation,
codec algorithm and AGENTS guidance. The two wording findings were corrected:
summary-copy ownership and a missing space. The worker reviewed private spool/
parser algorithms and tests/tooling separately from the owner's public/writer/
codec work. No concrete executable defect was identified by this documentation
pass; this is not a fresh scientific or platform audit.

Python function/class `__doc__` and source-line metadata can change. The pose
checker module docstring is byte-preserved because argparse uses it for help.
Existing binaries, installed headers/libraries and source-qualified receipts
continue to identify their earlier exact snapshots; comment equality does not
claim binary identity or qualify a rebuilt package.

## Preserved exclusions and AGENTS review

Unchanged exclusions are third-party/vendor inputs, all fixture bytes and
manifests (`fixtures/`, `fixtures_custom/`, `fixtures_extensions_draft/`,
`fixtures_pose_sidecar/`), accepted specifications, historical handoffs/JSON
receipts, all `history/` trees, build/install copies, temporary comparisons and
other unrelated starting-worktree additions. In particular, these seven Python
files were captured and confirmed byte-identical:

- `tools/conformance/extensions_draft/author.py`
- `tools/conformance/extensions_draft/audit.py`
- `tools/conformance/extensions_draft/seal.py`
- `tools/conformance/extensions_draft/write_draft.py`
- `tools/seal_extensions.py`
- `tools/seal_capacity_planner.py`
- `tools/seal_document_preflight.py`

Main confirmed these are dated/pinned accepted corpus/receipt generators, not
reusable current implementation tooling. Their identities were not rewritten to
accommodate documentation changes. No vendor/generated/frozen area is counted as
completed documentation coverage. Existing `.gitattributes` and `.gitignore`
remain unchanged.

No applicable repo/ancestor AGENTS file existed. The new canonical `AGENTS.md`
records actual component ownership, C++23/CMake/MSVC runtime conventions, eic12,
offline dependencies, current-branch/no-publication workflow, fresh output paths,
frozen evidence exclusions and the requested documentation/checking convention.
It distinguishes real budget/lifetime requirements from relaxed development
allocation restrictions and does not grant broader filesystem authorization.
