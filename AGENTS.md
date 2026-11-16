# Working in serene-outputs

## Ownership and contracts

This repository owns the C++23 scientific-output model, codec, native archive
writer/verified spool merger and the independent raw pose sidecar recorder.
`serene-core` owns capture scheduling and solver state; `serene-schemas` owns input
schema generation. Coordinate sibling changes instead of adding competing schema,
solver, GUI or codec authorities here. Pose recording is headless and has no
Chrono, Qt, rendering or scientific archive dependency.

`spec/FORMAT.md` is the preserved 1.0 contract; `spec/FORMAT-1.1.md` is the accepted
extension contract. `spec/POSE_SIDECAR.md` describes the separate visualization
sidecar. Package versions and wire versions are distinct. Preserve explicit
retention states, exact unsigned identities, validity masks, ownership, admitted
budgets and no-clobber publication. In particular, included-empty evidence is not
unavailable, and fixed accuracy must never silently become reversible or clamp a
tolerance. Bundle copying verifies encoded bytes without recompression.

## Workflow and preserved evidence

Stay on the current branch. Do not stage, commit, push or switch branches unless
the human explicitly authorizes that particular publication action. Preserve all
unrelated working-tree changes and establish a starting-worktree baseline before
broad edits. This instruction file does not grant additional filesystem access.

Use `rg` for searches and CMake for builds. Windows/MSVC is primary; load
`C:/Users/12650029/serene-academic/serene-math/load_compilers.bat` and explicitly
select MSVC as the maintained wrappers do. Match `/MD` Release and `/MDd` Debug.
Use the existing `eic12` Python environment for development tools. Linux compiler
support is a portability goal, not proof that a particular release was qualified.
See `cmake/BUILDING.md` for components and offline pinned dependency setup.

Use fresh build, install and generated-output paths. Do not overwrite accepted
`build/install-*` prefixes or their qualification artifacts. Existing stage
wrappers retain historical output paths; inspect them before invoking. Test only
the changed behavior and preserve previously accepted evidence rather than rerun
historical qualification without a concrete reason.

Do not hand-edit `third_party/`, fixture bytes/manifests, `history/`, nested
history, build/install copies, or sealed qualification receipts. The old
`tools/conformance/extensions_draft/` and `tools/seal_extensions.py`,
`tools/seal_capacity_planner.py`, `tools/seal_document_preflight.py` are historical
receipt/corpus generators. Treat their identities as frozen. Maintained conformance
and pose-checker tooling is separate; changing it does not retrospectively change
an old receipt. Follow narrow `.gitattributes` byte-preservation rules.

## Documentation convention

Document every maintained public/private class and field: purpose, owned versus
borrowed state, caller examples, units/order, optional states and relevant
lifecycle, failure, capacity and thread-safety requirements. Document every
function, overload and special member with arguments, effects and return/error
semantics. C++ API documentation belongs immediately above declarations; private
helper documentation belongs above definitions. Python class/function docstrings
must be genuine first statements.

Begin each function body with a concise normal comment describing its activities
in execution order. Mark nontrivial activity boundaries and explain local variables,
loop cursors and captured state where declared. Comments must explain verified
behavior rather than echo names or promise unimplemented guarantees. Defaulted
or deleted special members have no body; document their ownership policy at the
declaration. Keep exact-real acceptance distinct from rounded error reporting,
and producer-asserted summaries distinct from independently verified statistics.

For documentation-only work, change no executable tokens, directives, literal
values, diagnostic/registration strings, assertions, dependencies or defaults.
Verify against the starting working tree with a string-aware C++ token/directive
comparison and Python AST comparison excluding only genuine docstrings. Preserve
Python executable token spelling as well. Review coverage and representative
algorithms separately; a passing token check does not prove complete documentation.
Do not rebuild qualified artifacts or rewrite historical source hashes for comments.
Source line metadata and Python `__doc__` can change; do not claim binary identity.

## Resource and failure semantics

Construction/planning may allocate owned catalogs and working buffers. Active
operations retain their documented checked capacities, source lifetimes and
failure boundaries. Output allocation restrictions were relaxed during development;
do not infer a global zero-allocation or total-process-memory guarantee from a
local requested-workspace counter. Do not remove a real parser/extent limit or
borrow-lifetime requirement on that basis. Tests distinguish requested bytes,
allocation churn and sampled process memory.

Archive/bundle sessions clean up owned unpublished temporary files on destruction;
pose recorder failures retain `.partial` evidence. Flush/close plus no-clobber
publication is not a power-loss durability promise. Document concrete defects for
later authorized implementation when working under a documentation-only scope.
