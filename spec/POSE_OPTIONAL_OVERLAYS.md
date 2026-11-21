# Optional visualization groups

Accepted on 20 November 2026 after Core producer and viewer consumer review, with Serene Main approval. Bootstrap/catalog versions remain1.0/1; package versions are independent. Scientific archives are unchanged.

This freezes the common registry, ordering, recovery and memory contract. Only the F record layout is frozen in this slice; kinds6–8 retain their identities while their detailed records require separate review. F-only producers reject descriptors they cannot emit.

## Shared identities, ordering and visibility


Retain bootstrap1.0/catalog1/required mask0, base structural sample meanings,
kind1/stream1 pose, kinds2/3 stream0 index/completion, and winch4/stream2/layout1.
The permanent registry is, each optional flags0/raw encoding0/layout1:

| Descriptor | Kind | Stream | Delivery |
| --- | ---: | ---: | --- |
| fluid_poses | 5 | 3 | F slice first |
| applied_loads | 6 | 4 | Completed held physical density slice next |
| elastic_moments | 7 | 5 | Sided elastic N-cut components |
| inflow_planes | 8 | 6 | Finite masked world velocity grids |

Synthetic unknown100/stream42 remains an unknown test family. New kind OR new
stream engages strict validation in a reader implementing that family. Wrong
kind/stream pairing, layout, encoding, flags or missing descriptor cannot fall
through to optional skipping. A binary's implemented family set is explicit.
Families not implemented by that binary remain opaque optional data; report
understood and ignored families instead of claiming their semantics were checked.

An enabled descriptor contributes exactly one batch per structural batch:
enabled5,6,7,8 in that order, then optional winch4 immediately followed by pose1.
Omitted descriptors mean absent features. Declared but unavailable samples or
an absent completed applied interval still produce their full fixed-width rows.
Unknown optional chunks may precede/between overlays and precede the final
winch/pose suffix. Nothing may occur between winch4 and pose1. Unknown chunks
after the linked index remain invalid. Duplicates, known order reversals and
known chunks without descriptors fail. With no winches, pose terminates the group.

All participating chunks have identical positive record count R and first
recording ordinal. Every feature record begins with the byte-identical64-byte
structural stamp, including physical sample count P, followed by:

| Offset | Type | Meaning |
| ---: | --- | --- |
| 64 | u64 | Positive source Core preparation generation, <=INT64_MAX |
| 72 | u32 | Source phase0 initialized,1 static_certified,2 transient_initial,3 transient_accepted |
| 76 | u32 | Reserved zero |

Descriptors and every enabled stream, including winches, must agree on source
generation. Core validates owner/binding/effective-field identities before
submitting a common packet. Never persist process addresses or use the capture
plan counter as the Core generation. Phases map exactly as J07: prepared/id0/
event1/factor0; certified static/event8 and exact certified time/id/factor;
transient initial/id0/initialbit or positive transient accepted/no initialbit.
Prepared/transient wire factor0 is not a model's evaluation factor. Initial
capture may be omitted; actual times and off-stride target bits remain exact.

A fully aware reader validates the entire group and commits only at its final
pose, after every declared stream passed. A partial reader commits only the
group it understands and checks outer bounds/CRC on skipped streams. Neither
kind of reader may claim validation of opaque semantics. Truncated headers or
payloads/EOF while pending expose only earlier complete understood groups.
Complete malformed payloads, CRC failures, duplicated/omitted known batches,
wrong source facts, an intervening forbidden chunk or early index are errors.
Empty failed files may declare any supported streams and contain zero groups.
Structural previous pointers and index/completion still count only pose batches.

Recorder accepted counters advance after complete borrowed-frame validation and
copying. Persisted counters/first-last structural links advance after every group
chunk and its existing checked flush succeed. A failed later chunk leaves the
prior complete group recoverable and latches failure. Keep checked close,
no-clobber publication and wire-complete-but-unpublished .partial outcomes.

## Memory contract

Preserve16MiB catalog and64MiB per-chunk ceilings. For a new-overlay group, add
a64MiB aggregate raw known-group ceiling. With per-frame widths S_pose, S_winch
and S_5..8, B=sum(enabled widths), the recorder admits
R=min(max_batch_frames, floor(effective_raw_budget/B)). At least one whole frame
group must fit or setup fails before creating a file. Default max_batch_bytes
remains4MiB and hard maximum64MiB, now explicitly shared by every active stream.
No duration-sized buffer or index is retained.

Add a separate Options.max_workspace_bytes, default/hard maximum128MiB. Its
payload accounting charges all raw batches, retained physical-reference tables
(actual capacity; currently three u64 per winch), plus a fixed2048-byte allowance
for active encoding/index scratch. Subtract reference/scratch charges before
deriving effective_raw_budget, so a tighter workspace can reduce batch count.
Expose all charges and admitted frames in Statistics. Initialization JSON/maps,
caller-owned borrowed arrays, path/general object storage, allocator and I/O
implementation overhead are outside this payload budget; it is not process RSS
or a global allocation guarantee. Do not silently redefine the existing raw
batch counter or numerical-Core restrictions.

A fully aware reader checks R*B against its pending/raw budget before allocating or reading its first known payload. It checks retained logical payload and frame limits before allocating or appending decoded history. No frame from a torn group is published.
Known pending plus current payload stay within64MiB; one unknown raw chunk up to
64MiB can coexist while checked/skipped, giving a128MiB combined ceiling. Release
unknown bytes before reading another chunk. A partial reader bounds its known
subset plus current skipped payload similarly. Preserve legacy base/J07-only
admission rules (which can allow two separate64MiB chunks) when no new descriptor
is understood. Default checker operation retains no decoded history. Explicit
dumps and the viewer must separately limit retained logical payload to256MiB and
100000 frames, counting all understood overlays and stamps. Python object/
container overhead and GPU allocations are separate and must be stated by their
owners; the viewer must bound GPU uploads independently and expose duration cost.

