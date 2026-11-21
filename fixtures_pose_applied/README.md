# Independent applied-load layout1 fixtures

This corpus is authored directly from the accepted applied-load layout, using
only older independently authored P8/F6/W2 catalog and frame literals. The author
imports no production recorder, checker or solver. These are wire and reader
oracles; candidate/discard/stale-file negatives do not prove numerical rollback.
The author refuses an existing destination and fingerprints the entire older F
corpus plus its author before and after generation.

`catalog.json` declares two run-scoped models and two distinct target attachments.
Their complete logical station populations contain five and three rows. A
continuous logical station retains two original native consumers with different
element/limiting-side addresses. A genuine left/right pair shares a material
coordinate but retains separate physical tokens and different signed densities.
The target occurrence, model identity, attachment identity, physical side and
resolved consumer side remain separate facts.

`expected.json` contains exact literal rows, each file's size and SHA-256, and
separate `valid`, `baseline_valid`, `winch_aware_valid` and `f_aware_valid`
classifications. The first describes the full applied-aware contract; the three
older subsets ignore applied semantics while checking framing/integrity and the
families they implement. `legacy-classifications.json`, when present, records
actual executions of the pinned subset readers and their exact hashes.

Each frame's new oracle is:

```
applied_loads: {
  generation, phase,
  attachments: [
    {interval: {status, source_accepted_id, source_time_s,
                end_accepted_id, end_time_s, publication,
                mechanical_revision, state_revision, geometry_revision,
                environment_revision, prepared_step_sequence, packet},
     samples: [{valid, source_position_world_m, force_density_world_N_per_m}]}
  ]
}
```

The base load record is658 bytes: common80 plus K5 block337 and K3 block241.
Attachment prefixes start at80 and417; sample rows start at177 and514. The second
prefix and both arrays therefore exercise unaligned fields. The K9 variant has
a947-byte record and a two-byte second-attachment mask. No padding is inserted.

The three source states preserve the independent physical timestamps1.25/1.29/
1.32 and accepted IDs0/4/7. Positive load intervals use actual unsaved sources
3/1.28 and6/1.31. Source points are deliberately different from endpoint F poses.
Publication, revisions, sequence and packet IDs are positive but deliberately
decrease across frames. Both attachments have identical96-byte prefixes within
each frame. No arithmetic or cross-frame monotonic token relationship is assumed.

Other positives cover omitted initial capture, prepared/static zero-status
records, a prepared timestamp100 preceding a positive transient capture, exact
prior accepted source/time joins including signed-zero clocks, valid zero
density, valid signed-zero scalars, canonical unavailable rows, loads without F
or winches, complete multiple-attachment model ranges, permuted global station
ranges, repeated original consumer addresses and metadata ID domains above one
million without large populations.

Negatives isolate catalog ownership/range/token rules, source/end and same-frame
prefix identities, masks/finiteness/canonical zero, group order/cardinality,
integrity and interruption. Complete malformed pending load records reject even
if a later partner is missing. A consistently stamped pending F/load group whose
final pose alone is absent exposes only earlier complete groups. The selected
old physical catalog supports the established per-case native compatibility
adapters; this author does not modify or execute a native viewer.

Million-entry memory rejection remains a separate synthetic admission test;
this intentionally small corpus does not allocate giant catalogs or payloads.
