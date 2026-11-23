# Independent modal M1 archive corpus

This tooling authors small native 1.2 archive fixtures without importing a
Serene writer, reader or solver. `oracle.py` first constructs and checks the
scientific packet. The packet is manufactured archive evidence, not a blade
finite-element model or a production frequency prediction.

The exact six-coordinate pencil is
`M = diag(1,4,16,1,4,16)` and `K = diag(-4,0,64,4,64,576)`.
Its spectrum is `[-4,0,4,4,16,36] s^-2`. The retained vectors are
`e0`, `e1/2`, `e2/4`, and `-e3`, each with unit generalized mass and mutually
mass-orthogonal. Requesting one elastic mode returns both independent modes in
the repeated cutoff cluster. The upper distinct separator is `16 s^-2`.

Coordinates are world XYZ translation followed by owner-local XYZ infinitesimal
rotation. The owner reference point is `(1,2,3) m`; the local-to-world quaternion
is scalar-first `(0,0,0,1)`. The local offset `(0,2,0) m` becomes world
`(0,-2,0) m`. The fourth mode therefore has world angular coefficient `(1,0,0)`
and offset-point translational coefficient `(0,0,-2)`. These exact values detect
frame, quaternion-order, port-offset and eigenvector-sign mistakes. Distinct
left/right material observations retain their identities at the same reference
position, while a clamped-root observation has six zero coefficients.

The JSON packet records values per unit modal coordinate. Physical vibration
amplitude is independent. Signed zero is preserved in the generated shape
coefficients. Oracle checks prove the diagonal eigenpairs and mass normalization;
they do not certify a real assembled blade, its constraints or its solver.

Run the oracle only into a fresh destination:

```powershell
& C:/Users/12650029/AppData/Local/anaconda3/envs/eic12/python.exe -B `
  tools/conformance/modal_m1/oracle.py path/to/fresh/manufactured_packet.json
```

`author.py` reads the preserved independent `custom_author.py` wire helper,
whose identity is recorded in the new corpus manifest. It uses public zfpy for
reversible frames, never a Serene production codec or writer. Author and inspect
a fresh corpus with:

```powershell
& C:/Users/12650029/AppData/Local/anaconda3/envs/eic12/python.exe -B `
  tools/conformance/modal_m1/author.py --output path/to/fresh/corpus `
  --spec spec/FORMAT-1.2.md
& C:/Users/12650029/AppData/Local/anaconda3/envs/eic12/python.exe -B `
  tools/conformance/modal_m1/audit.py path/to/fresh/corpus
```

An existing identical `manufactured_packet.json` is allowed because the physical
packet precedes binary authoring. Every other destination artifact must be absent.
The manifest records exact source/contract and artifact hashes. A later contract
revision requires a new invocation identity; it does not alter an earlier receipt.

The current Core-aligned corpus is `fixtures_modal_m1/revision_4`. It has twelve
positive cases. The original seven cover mixed classified spectrum,
frequencies only, known-empty shapes, unavailable shapes, incomplete-search
failure with no modes, a signed-zero eigenvalue, and an indeterminate negative
eigenvalue. The mixed case contains nine raw and fifteen reversible ZFP frames.
Mode IDs exceed `2^53` and cluster IDs exceed `2^63`, exercising unsigned identity
preservation. Display scaling is present for one mode and never changes its shape.

Five additional cases preserve counted coverage independently of returned Ritz
estimates, preserve counted repeated-cluster membership even where a Ritz gap
would split it, record a completed search with failed numerical certification,
record invalid zero-quota options, and retain the material coordinate/side of a
named material-backed port. The perturbed Ritz estimate is `nextafter(4,+inf)`;
the same manufactured mass/stiffness/vector gives an actual normalized projected
residual of `1.1102230246251565e-16`. That nonzero value is stored in the per-mode
and maximum diagnostics. Counted reference eigenvalues, the diagonal pencil and
the residual definition appear in the expected JSON oracle only, not extra wire
fields. Dense grouping remains source evidence; the archive does not regroup
returned Ritz estimates.

Twenty-five negative cases cover version/features, dimensions/count limits, class
and frequency consistency, cluster partition and upper separator, incomplete
search mislabeled success, wrong frame/triad, invalid quaternion, malformed frame
extent, data hashes and directory truncation. Manifest phases identify the first
intended production-reader operation: `open`, `run` or `decode`. A full audit may
also detect damage in an unvisited payload. These labels are expected outcomes,
not a claim that the production implementation has passed them.
The added cases exercise required grouping/material-presence fields, an unknown
grouping basis, absent required dense uncertainty, an excessive certified residual,
unpaired failure coverage and a failure incorrectly claiming certification.

`audit.py` has a separate explicit struct/JSON envelope inspector and uses the
public zfpy decoder. It checks all positive section hashes, exact descriptor
ownership, byte-preserving IDs/numerics, optional states, complete frame coverage,
and the rotated-offset physical example. Expected numeric hex strings are exact
little-endian bytes, not integer hex spellings. It verifies negative artifact
identities without accepting those files as valid archives.

Generated fixtures, manifests and expected binary values must come from their
authoring scripts; do not hand-edit them. Existing native 1.0/1.1 corpus bytes
and qualification receipts remain unchanged. Installed writer/reader results
belong to separate qualification evidence and must not be inferred from a
successfully authored fixture.

The initial files at `fixtures_modal_m1/`, `revision_2`, and `revision_3` remain
unchanged draft evidence. Revisions 1 and 2 precede the final Core-aligned schema.
Revision 3 has the current archive bytes but identifies the specification before
its final text-encoding/prose correction; revision 4 records the final specification
identity. `author_corpus_v1.py` and `author_corpus_v2.py` preserve the corresponding
earlier author source bytes. They are historical snapshots, not current entry points.
