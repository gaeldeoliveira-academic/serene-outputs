# J07 proposed public API and Core seam

Review sketch only; no header or implementation has been changed. Keep existing
`pose::Stamp`, `Options`, `Completion`, `Statistics` and feature-free call syntax.
New declarations would live with the recorder API after joint approval, using
standard-library types only. No Core/Chrono/Qt type crosses the library boundary.

```cpp
// Proposed names/values, subject to the contract review gate.
enum class WinchGeometryStatus : std::uint32_t {
    valid_positive_tension = 0,
    zero_tension_limit = 1
};
enum class WinchSourcePhase : std::uint32_t {
    initialized = 0, static_certified = 1,
    transient_initial = 2, transient_accepted = 3
};

/// One complete accepted arrangement, owned by the caller's prepared buffer.
/// Supplies raw world geometry to the recorder; never an algebraic solver input.
struct WinchGeometry {
    std::array<double, 12> world_xyz_m; // Axyz,Bxyz,Cxyz,Dxyz, finite metres.
    WinchGeometryStatus geometry;      // Positive tension or labelled limiting geometry.
};

/// Synchronous borrowed view of every arrangement in immutable catalog order.
/// Core assembles it only after validating every report against the bound context.
struct WinchFrameView {
    Stamp stamp;                         // Independently asserted source state/events.
    std::uint64_t preparation_generation; // Core context generation, not plan identity.
    WinchSourcePhase phase;              // Source lifecycle phase of this complete packet.
    std::span<const WinchGeometry> geometries; // Exactly catalog F rows; borrowed this call.
};

// Existing method: valid for a feature-free catalog only.
Status Recorder::append_frame(const Stamp& structural_stamp,
                              std::span<const double> poses) noexcept;

/// Validate both populations and stamps, then copy one complete paired frame.
/// structural_stamp and poses borrow the usual P*7 physical pose scalars.
/// winches supplies a matching source stamp/generation/phase and all F arrangements.
/// Returns Status failure for invalid lifecycle, population, nonfinite values,
/// mismatch or I/O; failure latches under the existing recorder policy.
/// Retains no span, caller pointer or reference after return. Owner-serialized.
Status Recorder::append_frame(const Stamp& structural_stamp,
                              std::span<const double> poses,
                              const WinchFrameView& winches) noexcept;
```

`Recorder::open` remains the setup route: it parses and owns/adapts the catalog,
validates topology and feature count, and reserves the combined bounded batch.
The supplied catalog string is still consumed synchronously. An old overload
against a feature-enabled catalog fails explicitly; supplying features against a
feature-free catalog also fails. There is no separate `append_features`, late
catalog mutation, partial arrangement update, lazy buffer resize or consumer-side
geometry solve. Invalid full input is checked before either batch is accepted.

The duplicated `WinchFrameView.stamp` makes a stale feature packet detectable at
the recorder boundary. Both stamps are compared as encoded bits, including time,
factor and events; the recorder assigns one ordinal and copies the resulting full
structural stamp into each stream. Core must validate all WinchReport identities
before constructing the common view; a common stamp does not prove that the rows
were gathered correctly. The source phase and generation must match the catalog
and the phase table in the candidate. Capture ID and context generation are never
compared to one another.

For enabled files, existing `Options::max_batch_bytes` bounds the **sum** of raw
pose and feature storage. `max_batch_frames` caps both populations together.
Existing statistics describe logical accepted/buffered/persisted paired frames;
`pose_chunks` remains structural and `batch_capacity_bytes` reports combined raw
batch bytes. The feature-free meanings and path remain unchanged. The final API
documentation must state this extension explicitly rather than imply a whole-process
allocation guarantee. Separate feature counters are unnecessary for the one-to-one
profile; an aware checker reports paired counts from the file.

## J08 setup and active path (Core-owned, not implemented in J07)

1. Select all active, prepared winch laws for capture, including zero effective
   tension. Use deterministic prepared order, independent of scientific requests.
   Main/Core must approve this population rule. If no winches are present, keep
   the feature-free recorder path. Do not silently omit an invalid selected law.
2. Bind each occurrence/force, all A/B/D physical sample references, context
   generation and capture plan/run identity during inactive setup. Retain C as
   algebraic provenance only. Preallocate one `WinchGeometry` row per arrangement.
3. Preserve J06's atomic refresh before capture and scientific consumers. At a
   capture event, validate each accepted report's generation, phase, geometry,
   certificate or accepted step and actual time against that capture event.
   Map transient report factor1 to wire factor0 deliberately. Static factor is
   carried as supplied; initialized diagnostics are separately labelled.
4. Copy A/B/C/D from the complete accepted report into the prepared feature rows.
   For the proposed exact duplicate invariant, also copy A/B/D into corresponding
   flattened physical-port XYZ after checking shared-port agreement. Keep their
   physical quaternions intact. Do not alter Core's state or scientific values.
5. Invoke one recorder overload. Distinguish callback delivery, recorder copied/
   flushed counts and numerical outcome. Preserve optional-sink failure behavior.
   A final off-stride target remains target-only, without inventing a scheduled event.

This seam does not change scientific channel selection or `.serene` layout.
Scientific readback should use existing reader APIs, run metadata and exact
accepted axes; no Python companion implementation is needed for this feature.
