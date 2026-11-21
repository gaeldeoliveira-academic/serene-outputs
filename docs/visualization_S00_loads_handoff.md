# Stage00 applied physical loads handoff

20 November 2026. Package **0.2.6** adds optional applied physical force density
to the independent raw pose sidecar. Both MSVC configurations, independent
readers and four installed consumers passed the qualification below. Core
numerical capture and viewer rendering have separate owners and qualification.

## Package identity and scope

The Outputs baseline remains clean `main` HEAD
`fec96a4b01802e6ed78808bdf4cd39fe25bc6160`, package0.2.4. Stage00 changes remain
uncommitted on that branch. No staging, commit, push or branch change is implied.
The earlier qualified F package0.2.5, its source snapshot, installed prefix and
manifest are preserved byte-for-byte.

The accepted [load contract](../spec/POSE_APPLIED_LOADS.md) has SHA-256
`a97634c57b25affe2a8d5c7220e2b3cf0bbf34d76f5de6b0758ce3be0e462cd5`.
Original freeze/draft bytes remain under `spec/drafts/visualization_S00/loads-v1`;
the separate editorial receipt records status-wording alignment only.
[Common groups](../spec/POSE_OPTIONAL_OVERLAYS.md) and
[F geometry](../spec/POSE_FLUID_GEOMETRY.md) are unchanged. Bootstrap1.0,
catalog1 and required mask0 remain unchanged. Applied loads use optional
kind6/stream4/layout1/raw encoding0. The order is F, loads, winch, pose with
disabled populations omitted and winch/pose adjacent. The pose chunk commits
the complete understood group; structural backlinks, indexes and completion
still describe structural batches.

Fresh combined installation for matching Release and Debug consumers:

`C:/Users/12650029/serene-academic/serene-outputs/build/visualization_S00/loads/combined-install`

The immutable manifest and matching source snapshot are under
`build/visualization_S00/loads/qualification-v1`. Do not replace the earlier F
prefix or mix this header with an earlier library. Recompile consumers using
`find_package(SereneOutputs 0.2.6 EXACT CONFIG REQUIRED COMPONENTS pose_recorder)`.
Add `archive` only when needed. Pose-only import remains independent of model,
codec, ZFP and archive targets. Use `/MD` Release and `/MDd` Debug; source API
extension is not a binary ABI compatibility promise.

## Producer interface and physical meaning

New public values are `AppliedIntervalStatus`, `AppliedSourceEvaluation`,
`AppliedInterval`, `AppliedLoadSample`, `AppliedAttachmentFrameView` and
`AppliedLoadsFrameView`. `FrameGroupView` gains a trailing default-null
`applied_loads` pointer. Views borrow caller storage only during synchronous
`append_frame`. The catalog and every nested view must contain the complete
declared attachment population. All stamps, phases, prefixes, extents and rows
are checked before any population is copied. Older overloads reject newly
enabled overlays. This package rejects later moment/plane descriptors.

Store actual source F world positions in metres and physical world force
density in N/m. This is the packet integrated and transferred for a completed
weak interval, not an endpoint reevaluation or generalized native force. F pose
recording is independent and may be disabled. Source points may differ from
the current displayed F poses. The catalog preserves actual models,
attachments, spanline IDs, logical nodes, native consumers and fingerprints.
Permuted global station ranges and multiple attachments in one model are
admitted by the wire; Core's first producer has its separately qualified shape.

Each attachment has an unaligned96-byte interval prefix, its packed LSB mask,
then six binary64 scalars per station. All attachments in the admitted
synchronous coordinator have byte-identical prefixes. Source/end accepted
identities are ordered and endpoint identity/time matches the structural stamp.
The source cannot precede the preceding saved transient end; equal IDs require
bit-identical time, and later source IDs require strictly later time. Prepared
display times supply no such bound. Sparse captures and a first positive
capture are valid. Publication, revisions, prepared sequence and packet are
positive opaque identities: no derived arithmetic or cross-frame ordering.

Unavailable status0 has an all-zero prefix, mask and rows and is restricted to
prepared/static/initial phases. Positive transient captures require completed
status1. Valid signed zeros are preserved; invalid rows are exactly six positive
zeros. Core must reject stale/missing/failed products and cannot disguise them
as legitimate unavailability. Its adapter must own distinct candidate and
completed buffers, snapshot source geometry/density before transfer, and publish
only after checked native success and coordinator acceptance by a nonfallible
swap. A recorder test cannot prove that numerical publication behavior.

Review clarified numeric address equality: interpret `material_coordinate_m`
as finite IEEE754 binary64, including integer JSON spellings, when comparing
logical physical addresses. Preserve the original catalog metadata. Every
integer identity, token and count remains exact. Regression checks distinguish
2^53/2^53+1 (same binary64 coordinate) from2^53/2^53+2 and prove that distinct
large physical tokens remain distinct. No frozen fixture byte or wire field was
changed for this clarification.

## Bounded storage and recovery

`Statistics::overlay_reference_bytes` reports actual retained attachment-layout
vector capacity, separately from the unchanged physical-reference charge.
Only station counts/byte offsets survive setup; topology and JSON are not
retained during capture. Include both reference charges and2048-byte scratch
in the128MiB default/hard workspace cap; raw groups remain4MiB default/64MiB
hard. Catalog/count/product/minimum-group checks precede retained layout/raw
allocation and file creation. This is requested payload/workspace accounting,
not total process memory or a global allocation-free guarantee.

The full checker admits a whole known raw group before reading it, permits one
bounded skipped unknown chunk, and publishes no partial decoded group. Dumped
logical history retains its256MiB/100000-frame admission; Python object overhead
is excluded. Applied logical charge is80+96*A+49*K bytes per retained frame,
in addition to other understood populations. Kind6 OR stream4 invokes strict
validation; later7/8 remain explicitly opaque to this checker.

Failures preserve `.partial` evidence and distinguish accepted, persisted,
recoverable and published frames. Complete structural bytes can be recoverable
after a failed flush while persistence statistics remain at the prior group.
Final close/publication failure can leave a complete but unpublished sidecar.
No-clobber publication preserves an existing destination. These are not
power-loss durability guarantees.

## Qualification evidence

Release and Debug each passed all9 selected CTests: base, winch, F and applied
recorder/checker pairs plus the small scientific-archive check. Each run recorded
1852 applied producer assertions,1435 F assertions and1120 winch assertions.
The four Python checker suites recorded23+12+10+10=55 tests per configuration,
including the complete151-case applied corpus and71-case F corpus.

Applied producer evidence covers actual files from mixed and individual-feature
catalogs, generic/permuted attachment ranges, initial/prepared/static/positive
phases, sparse and exact-prior joins, opaque identity reuse, signed-zero clocks,
unavailable masks, malformed catalogs/packets, source-copy lifetime, exact raw
and workspace boundaries,1001 accepted frames with duration-independent
retained capacity,13 full-write/short-write failure positions,7 flush failures,
abandonment, close/publication failure and competing no-clobber destinations.
The independent readback checks exact nested rows and binary64 representations,
unaligned prefix offsets, group joins and structural-only backlinks.

All four external consumers passed against a copied, relocated installation:
combined and pose-only in Release and Debug. Every installed public header
matched the maintained source; all eight component libraries matched their
configuration build. Pose-only import excludes archive/model/codec/ZFP targets.
Every consumer emitted the exact three-frame mixed sidecar oracle. The unchanged
scientific Python companion opened both combined archives, verified all blocks,
and returned force-density values[12.5,12.5] at times[1.29,1.32], with exact
uint64 accepted IDs[4,7]. The initial unavailable interval has no fabricated
scientific force row. Python source/native-module hashes and installation hashes
were unchanged throughout the consumer checks.

The standalone receipt is retained at
`serene-theory/tmp/s00_20261120/loads-installed-consumers/20261120T175314629432Z/result.json`
and copied into the immutable qualification evidence. Build logs retain every
attempt. The first builds preceded the final documentation pass; the final
build/test/install actions use the final documented sources. Separate
string-aware C++ token/directive, Python executable-token/AST and consumer CMake
checks establish the documentation-only preservation scope. They do not claim
binary identity across changed source line metadata.

Independent corpus:151 cases,2,035,098 binary bytes; manifest SHA-256
`8ec353a7249ed53c00a83e08e96645d4a4daf811e96997c485a26d0ed7ab37fc`.
It was authored before recorder implementation and contains literal positive,
negative, incomplete, mask-boundary and mixed-population records. Two
attachments K5/K3 give658-byte load records and1875-byte complete mixed groups.
The second prefix starts at417 and doubles at177/514, exercising unaligned
access. A K9 attachment separately exercises mask padding. Literal identities
and geometry fingerprints are fixture evidence, not numerical qualification.

All453 pinned Python subset classifications (base, winch-aware, F-aware) and302
pinned Qt base/winch per-case checks passed. Older readers validate only their
understood subset; a successful opaque skip does not validate loads. Original
base/winch/F corpora and frozen F package identities are preserved.

## Remaining owner work

Core must implement and qualify physical capture, complete candidate publication,
no extra numerical evaluations, exact prepared identities and budget admission.
Viewer must validate the understood group, bound CPU/GPU storage and draw source
point arrows with world directions, N/m scale and source/end interval labels.
No connected load curve is implied by this catalog. Moments and planes retain
their accepted specifications but require subsequent independent fixture,
recorder, installed-package and owner-integration slices.

This package qualification is Windows x64/MSVC only. Linux and macOS have not
been run. Scientific format semantics and the companion Python implementation
are unchanged; the existing reader independently opened and verified both
installed combined-consumer archives as described above.
