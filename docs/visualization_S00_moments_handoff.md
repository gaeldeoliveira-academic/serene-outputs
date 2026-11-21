# Stage00 signed elastic moments handoff

20 November 2026. Package0.2.7 adds optional signed elastic moments to the raw
pose sidecar. Both MSVC configurations, independent readers and four relocated
installed consumers passed the qualification below. Core numerical capture and
viewer rendering remain separately owned work with separate qualification.

## Contracts and package

Accepted [moment catalog and record](../spec/POSE_ELASTIC_MOMENTS.md) SHA-256:
`461b8cba73fd8cafd2aca93977c1a561c7b702bd35e34c66dc251abfed9729ac`.
Main approved the optional structural-line matching clarification after checking
Core's producer. Its previous bytes and separate approval receipt remain under
`spec/drafts/visualization_S00/moments-v1`; original freeze records are unchanged.
The common group contract, bootstrap1.0, catalog1 and required mask0 remain
unchanged. Moments use optional kind7/stream5/layout1/raw encoding0. The complete
order is F, loads, moments, winch, pose with disabled populations omitted and
winch/pose adjacent. Structural backlinks, index and completion retain their
original meanings. This writer rejects the future plane descriptor; the reader
reports its limited knowledge when skipping a future optional plane stream.

Fresh matching Release/Debug installation:

`C:/Users/12650029/serene-academic/serene-outputs/build/visualization_S00/moments/combined-install`

Request `find_package(SereneOutputs 0.2.7 EXACT CONFIG REQUIRED COMPONENTS
pose_recorder)`; add `archive` only when needed. Recompile with the matching
headers and configuration libraries, using `/MD` Release and `/MDd` Debug.
There is no cross-package binary ABI promise. Prior F0.2.5 and loads0.2.6
prefixes, manifests and source/evidence snapshots remain immutable. The Outputs
baseline stays `main`, HEAD`fec96a4b01802e6ed78808bdf4cd39fe25bc6160`; all Stage00
changes are uncommitted, with no staging, commit, push or branch change.

## Physical and API boundary

`ElasticMoment` owns `moment_N_Nm[3]` and validity. `ElasticMomentFrameView` borrows
the complete dense cut population with its exact structural stamp, preparation
generation and source phase. `FrameGroupView` gains a trailing default-null
`elastic_moments` pointer. All views borrow only during synchronous append.
The recorder validates every enabled population before copying any of them;
older overloads cannot silently drop a newly enabled stream.

Store signed Mx, My and Mz about the actual current structural N point, in that
cut's actual current N axes, in N.m. Positive-span material acts on negative-span
material for both sides. Core must use one coherent elastic wrench about its
actual point P, shift once by `(pP-pN) cross F`, and rotate once into current N.
Applied force density, inertia, damping, selected-output histories and endpoint
reevaluations do not substitute for that wrench. All three components and each
requested phase require actual provider qualification; an unproved component
must not become zero. No mandatory prepared phase0 or new `source_phases` wire
field is introduced. Recorder fixtures do not establish this numerical capture.

Cuts have independent material/side/native-provider addresses, including off-node
locations. Explicit display intervals map two cuts on one provider to an existing
base segment edge. Opposite-sided pairs require explicit `sided_breaks`, including
breaks within one element. Intervals cannot cross a break; an interval ending
there uses the left cut, and one starting there uses the right cut. Every cut
participates in an interval, positive-length overlaps on one line/edge reject,
and touching endpoints are allowed. There is no nearest-station mapping,
fabricated native coverage or assumption that a cut is a base pose sample.

Base segment IDs are stable IDs, not array positions. When `structural_lines`
is present, match a moment line by occurrence and exact local spanline ID;
validate its matched range and any supplied base sample/segment line references.
Structural row IDs and dense moment-line IDs are independent. When absent, the
explicit validated base edge and occurrence ownership suffice. Do not require
literal `N`, F recording, or fingerprint equality. Material coordinates, native
eta and base-edge coordinates compare as finite binary64; integer IDs remain
exact. Endpoint ownership/eta is checked without recomputing native eta.

Rows preserve finite binary64 bits, including valid signed zeros. Invalid rows
have a clear packed LSB mask bit and three positive zeros. The exact width is
`80 + ceil(C/8) + 24*C`, without alignment padding. The viewer derives each
cut's full three-component norm before scalar interpolation: opposed
`(3,4,12)` and `(-3,-4,-12)` still give midpoint resultant13. The file stores no
extra norm or glyph orientation. Display interpolation is an explicit
approximation; it does not reconstruct the physical off-node N pose.

## Bounds and failure semantics

Catalog topology maps are setup-only. Runtime retains the scalar moment count
and generation plus the admitted raw buffer; no new persistent reference table
charge is introduced. Actual raw vector capacity participates in the shared
4MiB default/64MiB hard raw budget and128MiB workspace, including existing
physical/applied reference capacities and2048-byte scratch. Whole-group checks
precede raw allocation and file creation. Break lookups use ordered/binary
search, and interval overlaps use sorted adjacent checks; valid large catalogs
do not trigger pairwise interval/break scans.

The checker admits the whole known raw group before its first payload, validates
complete pending defects even if a later partner is torn, and commits state and
decoded rows only after the complete structural batch succeeds. Kind7 or stream5
invokes strict validation. Pending views are released before further reads.
Moment decoded-history charge is80+25*C per frame, within the existing256MiB /
100000-frame limits. Payload accounting excludes allocator/general-object,
Python-object and GPU overhead; this is not total process memory accounting.

Failures latch and retain `.partial` evidence. Accepted, persisted, recoverable
and published counts remain distinct. A failed flush can leave readable complete
bytes without advancing persisted counters. Close/publication failure can leave
a complete unpublished file; no-clobber publication preserves a competing
destination. No power-loss durability promise is made.

## Observed qualification

Release and Debug each passed all11 selected CTests: base, winch, F, applied-load
and moment recorder/checker pairs plus the small scientific-archive test. Each
configuration recorded2345 moment producer assertions,1852 applied assertions,
1435 F assertions and1120 winch assertions. The five Python suites passed
23+12+10+10+11=66 tests per configuration, including the complete moment corpus.

Actual producer files exercise the valid catalog variants and phase subsets,
C7/C8/C9 mask boundaries, exact borrowed population/source identity checks,
source-copy lifetime, invalid catalogs before file creation, all-before-copy
failure latching, raw/workspace boundary rejection and1001 accepted frames with
duration-independent retained capacity. Independent readback checks complete
literal rows, every relevant binary64 representation, group stamps and
structural-only links. Fault probes cover15 full-write/short-write positions,
8 flush positions, abandonment, close/publication failures and a competing
destination. Their recovery counts preserve the distinctions described above.

All four external consumers passed against a copied, relocated package:
combined and pose-only in Release and Debug. Each emitted the exact three-frame
mixed oracle with two moment chunks. Public headers matched maintained sources,
and all eight installed component libraries matched their build configurations.
Pose-only import excludes model/codec/archive/ZFP targets and emits no scientific
archive. The unchanged Python scientific companion opened both combined archives,
verified all blocks, and read `installed_moment` / `moment.cut4.Mz` values
[-12,-12] at times[1.29,1.32] with exact uint64 accepted IDs[4,7]. The initial
frame remains present in the sidecar; this consumer's scientific channel contains
only the two positive accepted states by its explicit construction.

The standalone receipt is retained at
`serene-theory/tmp/s00_20261120/moment-installed-consumers/20261120T182212003423Z/result.json`,
SHA-256`387e74c9b2a72dd69e515ea75f7a74ac41eae9dc88a489293a2849dd4ad7bd34`.
The immutable package/source/installed/test evidence manifest is under
`build/visualization_S00/moments/qualification-v1`. It retains every actual
build attempt, current test executables, independent fixture/readback evidence,
installed-consumer inputs/outputs and matching source/installation snapshots.
The scientific companion source/native-module identities and both previous
qualified package trees are verified unchanged before sealing.

The independent corpus was authored before the recorder:205 cases,31 valid and
174 invalid,3,425,874 binary bytes. Manifest SHA-256:
`df505adaff2c5009c0ca3c37f5c1d46e16700d4313316b8795800a038db4b72a`.
Seven-cut primary records are249 bytes and full mixed groups2124 bytes. Eight
and nine cuts give273/298 bytes and first-double offsets81/82; C7 starts at81.
Two providers, in-element and native-interface breaks, off-node cuts, explicit
intervals, full signed triples, masks, source phases and mixed recovery are
covered. Literal provider identities and fingerprints are not numerical proof.

All820 pinned Python subset classifications and410 pinned Qt base/winch checks
matched their original expectations. Three negative files intentionally contain
invalid JSON NaN. The old Qt harness initially failed to parse their independent
catalog oracle before reaching the file. Its adapter now uses a valid control
catalog only for these already-rejected files; original file bytes and expected
classifications are unchanged. All four pinned Python readers also reject them.
The initial six harness failures and the successful adapted run are preserved.
No older reader is claimed to validate opaque moment semantics.

Python and C++ cross-review found no actionable discrepancies in admission,
grouping, memory bounds, borrowing or new documentation. These are review
outcomes, separate from actual execution evidence. Linux/macOS, Core numerical
capture and viewer rendering are not qualified by this Windows/MSVC package.

## Owner integration

Core must qualify current-N transport and rotation at genuinely off-node cuts,
including bent/twisted geometry, a nonzero lever arm and nonzero Mz, actual
inward endpoint limits and in-element sided cuts. Preserve source identity,
capability/phase gates and allocation rules; add no extra solve or native state
refresh for capture. Viewer must validate its understood groups, bound CPU/GPU
storage, preserve discontinuities, derive full norms before interpolation, and
label the base-polyline display approximation. Neither output-selection changes
nor graph resultants/reactions are supplied by this recorder slice.
