#include <algorithm>
#include <array>
#include <cstdlib>
#include <new>
#ifdef _MSC_VER
#include <malloc.h>
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <serene/outputs/pose_recorder.hpp>


namespace allocation_audit {
thread_local bool active{}; ///< Count only the synchronous append window on this test thread.
thread_local std::uint64_t calls{}; ///< C++ new-family calls in the current window, excluding malloc/OS allocation.
/// Allocate size bytes with an optional alignment and count this C++ allocation when armed.
/// Return owned storage or throw bad_alloc; callers release using the matching free path.
void* acquire(std::size_t size, std::size_t alignment = 0) {
    // Count the requested C++ allocation and obtain suitably aligned nonempty storage.
    if (active) ++calls;
    size = size ? size : 1;
    void* memory{}; // Owned allocation result, transferred to the caller on success.
    if (alignment) {
#ifdef _MSC_VER
        memory = _aligned_malloc(size, alignment);
#else
        memory = std::aligned_alloc(alignment, ((size + alignment - 1) / alignment) * alignment);
#endif
    } else memory = std::malloc(size);
    if (!memory) throw std::bad_alloc();
    return memory;
}
/// Release owned aligned storage; nullptr is harmless and no allocation is performed.
void release_aligned(void* memory) noexcept {
    // Match the platform-specific aligned allocation family.
#ifdef _MSC_VER
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}
} // namespace allocation_audit
/// Replace scalar new for the executable's scoped C++ allocation observation; throw on failure.
void* operator new(std::size_t size) {
    // Obtain ordinary storage through the counted test allocator.
    return allocation_audit::acquire(size);
}
/// Replace array new with the same counted ownership and failure contract as scalar new.
void* operator new[](std::size_t size) {
    // Obtain ordinary array storage without counting the forwarding operation twice.
    return allocation_audit::acquire(size);
}
/// Release ordinary scalar storage supplied by the replacement allocator.
void operator delete(void* memory) noexcept {
    // Release the allocation; null pointers remain harmless.
    std::free(memory);
}
/// Release ordinary array storage supplied by the replacement allocator.
void operator delete[](void* memory) noexcept {
    // Release array storage with its matching allocation family.
    std::free(memory);
}
/// Release sized scalar storage; the size argument does not affect malloc ownership.
void operator delete(void* memory, std::size_t) noexcept {
    // Release ordinary storage using the matching allocator.
    std::free(memory);
}
/// Release sized array storage; the size argument does not affect malloc ownership.
void operator delete[](void* memory, std::size_t) noexcept {
    // Release ordinary array storage using the matching allocator.
    std::free(memory);
}
/// Allocate over-aligned scalar storage and count one C++ allocation; throw on failure.
void* operator new(std::size_t size, std::align_val_t alignment) {
    // Preserve the caller's requested alignment in the allocation family.
    return allocation_audit::acquire(size, static_cast<std::size_t>(alignment));
}
/// Allocate over-aligned array storage and count one C++ allocation; throw on failure.
void* operator new[](std::size_t size, std::align_val_t alignment) {
    // Preserve alignment without routing through a second counted new call.
    return allocation_audit::acquire(size, static_cast<std::size_t>(alignment));
}
/// Release over-aligned scalar storage; alignment is represented by its allocation family.
void operator delete(void* memory, std::align_val_t) noexcept {
    // Match the aligned allocation family.
    allocation_audit::release_aligned(memory);
}
/// Release over-aligned array storage using its matching allocation family.
void operator delete[](void* memory, std::align_val_t) noexcept {
    // Release aligned array storage without retaining it in the test.
    allocation_audit::release_aligned(memory);
}
/// Release sized over-aligned scalar storage; size and alignment need not be retained.
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    // Match aligned allocation ownership.
    allocation_audit::release_aligned(memory);
}
/// Release sized over-aligned array storage using the matching platform allocator.
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    // Match aligned array allocation ownership.
    allocation_audit::release_aligned(memory);
}

namespace pose = serene::outputs::pose;
using Json = nlohmann::json;

namespace {
std::uint64_t checks{}; ///< Successful public-contract assertions in this invocation.
/// Count a successful condition, or throw the borrowed diagnostic at the first failure.
void require(bool condition, const char* diagnostic) {
    // Preserve the first failure and all files already created by this fresh invocation.
    if (!condition) throw std::runtime_error(diagnostic);
    ++checks;
}

/// Own one literal interval and its station rows for packet() and append() scenarios.
/// Samples use catalog order, world metres and world N/m; unavailable rows retain +0.
/// Short-lived public views borrow the sample buffer synchronously, so callers may
/// mutate or destroy this storage after append() returns. Test use is single-threaded.
struct Attachment {
    pose::AppliedInterval interval; ///< Actual completed source/end and opaque provenance facts.
    std::vector<pose::AppliedLoadSample> samples; ///< Logical station order, independent of F display sampling.
};

/// Own one dense catalog plane's literal world-velocity rows in u-fastest order.
/// append() borrows samples only until it returns; validity is authoritative, and unavailable rows are +0.
struct Plane {
    std::uint64_t id{}; ///< Dense plane ordinal, distinct from the catalog's descriptive string plane_id.
    std::vector<pose::InflowVelocity> samples; ///< Owned absolute unperturbed world velocities in m/s.
};

/// Own a complete independent source packet for positive and malformed submissions.
/// packet() constructs the storage, then append() exposes synchronous borrowed views.
/// Copies isolate each mutation scenario; deliberately malformed copies need not obey
/// the recorder contract. No buffer is shared with a recorder after submission returns.
struct Packet {
    pose::Stamp stamp; ///< Actual structural accepted/source identity.
    std::uint64_t generation{}; ///< Core preparation generation, never a capture-plan identity.
    pose::SourcePhase phase{}; ///< Prepared/static/transient source lifecycle.
    std::vector<double> physical; ///< P8 xyz/wxyz scalar population.
    std::vector<pose::WinchGeometry> winches; ///< Literal A/B/C/D arrangement packets when enabled.
    std::vector<pose::FluidPose> fluid; ///< Literal endpoint F poses when enabled.
    std::vector<Attachment> attachments; ///< Optional two independently authored complete load populations.
    std::vector<Plane> planes; ///< Owned plane populations; metadata remains in the setup catalog.
    std::vector<pose::ElasticMoment> moments; ///< Signed current-N Mx/My/Mz triples in dense cut order.
};

/// Copy the borrowed expected-row JSON into an owned Packet without deriving source facts.
/// Optional loads contain two attachments; malformed/missing JSON or a failed extent check throws.
Packet packet(const Json& row) {
    // Read source identity, physical values and optional older populations.
    // Copy every interval scalar and every physical source point/density exactly.
    Packet result; // Owned submission buffers, independent of the borrowed JSON document.
    const auto& stamp = row.at("stamp"); // Actual accepted source identity in the literal oracle.
    result.stamp = {static_cast<pose::Analysis>(stamp.at("analysis").get<std::uint32_t>()),
        stamp.at("events").get<std::uint32_t>(), stamp.at("time").get<double>(),
        stamp.at("state_id").get<std::uint64_t>(), stamp.at("load_factor").get<double>()};
    const auto& planes = row.at("inflow_planes"); // Plane population supplies the common preparation identity.
    result.generation = planes.at("generation").get<std::uint64_t>();
    result.phase = static_cast<pose::SourcePhase>(planes.at("phase").get<std::uint32_t>());
    for (const auto& source : planes.at("planes")) { // One dense catalog plane with its own validity population.
        Plane value; // Owned point rows and exact dense identity, independent of display geometry.
        value.id = source.at("plane_id").get<std::uint64_t>();
        for (const auto& point : source.at("samples")) { // Current u-fastest sample, preserving zero signs.
            pose::InflowVelocity item; // Three world components and authoritative validity.
            item.valid = point.at("valid").get<bool>();
            item.world_velocity_mps = point.at("world_velocity_mps").get<std::array<double, 3>>();
            value.samples.push_back(item);
        }
        result.planes.push_back(std::move(value));
    }
    require(!result.planes.empty() && result.planes.size() <= 2, "bounded literal plane count");
    for (const auto& sample : row.at("poses")) // One structural xyz/wxyz row in catalog order.
        for (const auto& value : sample) result.physical.push_back(value.get<double>()); // Preserve scalar order.
    if (row.contains("features")) for (const auto& source : row.at("features")) { // Optional literal winch arrangement.
        pose::WinchGeometry value{}; // Owned arrangement status and twelve world-coordinate scalars.
        value.geometry = static_cast<pose::WinchGeometryStatus>(source.at("geometry_status").get<std::uint32_t>());
        std::size_t index{}; // Flattened A/B/C/D then xyz cursor, checked by array::at.
        for (const auto& point : source.at("world_xyz_m")) // Current A/B/C/D world point.
            for (const auto& scalar : point) value.world_xyz_m.at(index++) = scalar.get<double>(); // Current xyz component.
        result.winches.push_back(value);
    }
    if (row.contains("fluid_poses")) for (const auto& source : row.at("fluid_poses").at("samples")) { // One F catalog sample.
        pose::FluidPose value{}; // Owned xyz/wxyz and validity; no interpolation from structural samples.
        value.valid = source.at("valid").get<bool>();
        value.world_pose = source.at("world_pose").get<std::array<double, 7>>();
        result.fluid.push_back(value);
    }
    // Copy all signed moment bits independently of base/F geometry and applied source density.
    if (row.contains("elastic_moments")) for (const auto& source : row.at("elastic_moments").at("moments")) { // One literal current-N cut in dense catalog order.
        pose::ElasticMoment value{}; // Owned three-component sample and its authoritative availability.
        value.valid = source.at("valid").get<bool>();
        value.moment_N_Nm = source.at("moment_N_Nm").get<std::array<double, 3>>();
        result.moments.push_back(value);
    }
    // Preserve complete source/end provenance separately from end-of-step display geometry.
    if (row.contains("applied_loads")) for (const auto& source : row.at("applied_loads").at("attachments")) { // One complete attachment in catalog order.
        Attachment value; // Owned interval and all logical station rows for this attachment.
        const auto& interval = source.at("interval"); // Literal accepted interval and opaque revision tokens.
        value.interval.status = static_cast<pose::AppliedIntervalStatus>(interval.at("status").get<std::uint32_t>());
        value.interval.source_accepted_id = interval.at("source_accepted_id").get<std::uint64_t>();
        value.interval.source.time_s = interval.at("source_time_s").get<double>();
        value.interval.end_accepted_id = interval.at("end_accepted_id").get<std::uint64_t>();
        value.interval.end_time_s = interval.at("end_time_s").get<double>();
        value.interval.source.publication = interval.at("publication").get<std::uint64_t>();
        value.interval.source.mechanical_revision = interval.at("mechanical_revision").get<std::uint64_t>();
        value.interval.source.state_revision = interval.at("state_revision").get<std::uint64_t>();
        value.interval.source.geometry_revision = interval.at("geometry_revision").get<std::uint64_t>();
        value.interval.source.environment_revision = interval.at("environment_revision").get<std::uint64_t>();
        value.interval.prepared_step_sequence = interval.at("prepared_step_sequence").get<std::uint64_t>();
        value.interval.packet = interval.at("packet").get<std::uint64_t>();
        // Copy validity, source geometry and density without substituting F endpoint values.
        for (const auto& sample : source.at("samples")) { // Current logical load station, including sided rows.
            pose::AppliedLoadSample item; // Owned six world-space scalars plus independent availability.
            item.valid = sample.at("valid").get<bool>();
            item.source_position_world_m = sample.at("source_position_world_m").get<std::array<double, 3>>();
            item.force_density_world_N_per_m = sample.at("force_density_world_N_per_m").get<std::array<double, 3>>();
            value.samples.push_back(item);
        }
        result.attachments.push_back(std::move(value));
    }
    require(result.attachments.empty() || result.attachments.size() == 2, "literal optional two-attachment source");
    return result;
}

/// Return two views borrowing row sample buffers, or empty views when loads are absent.
/// Interval metadata is copied into the views; row must outlive their synchronous use.
std::array<pose::AppliedAttachmentFrameView, 2> attachment_views(const Packet& row) {
    // Return harmless empty views when absent; otherwise pair copied intervals with borrowed rows.
    if (row.attachments.empty()) return {};
    return {{{row.attachments[0].interval, row.attachments[0].samples},
             {row.attachments[1].interval, row.attachments[1].samples}}};
}

/// Return fixed-capacity views borrowing each supplied plane's owned sample storage.
/// The caller selects the active prefix; row must outlive synchronous append and contain at most two planes.
std::array<pose::InflowPlaneView, 2> plane_views(const Packet& row) {
    // Pair dense identities with borrowed velocity rows without allocating during view construction.
    std::array<pose::InflowPlaneView, 2> result{}; // Fixed local view capacity for this independent corpus.
    for (std::size_t index = 0; index < row.planes.size(); ++index) // Dense literal plane cursor.
        result.at(index) = {row.planes[index].id, row.planes[index].samples};
    return result;
}

/// Submit row's populations to the mutable recorder and return its unmodified Status.
/// The row stays alive through the call; absent older populations use null group pointers.
/// Recorder failure effects and copied-buffer ownership are exercised by main().
serene::outputs::Status append(pose::Recorder& recorder, const Packet& row) {
    // Assemble short-lived nested views, then submit one jointly stamped source group.
    const auto attachments = attachment_views(row); // Local view array borrowing row's sample buffers.
    const pose::AppliedLoadsFrameView applied{row.stamp, row.generation, row.phase, attachments}; // Required load view.
    const pose::WinchFrameView winches{row.stamp, row.generation, row.phase, row.winches}; // Optional older winch view.
    const pose::FluidFrameView fluid{row.stamp, row.generation, row.phase, row.fluid}; // Optional endpoint F view.
    const pose::ElasticMomentFrameView moments{row.stamp, row.generation, row.phase, row.moments}; // Required complete cut population.
    const auto planes = plane_views(row); // Fixed local views borrow all complete plane populations.
    const pose::InflowPlanesFrameView inflow{row.stamp, row.generation, row.phase,
        std::span<const pose::InflowPlaneView>(planes).first(row.planes.size())}; // Exact enabled plane extent.
    allocation_audit::calls = 0;
    allocation_audit::active = true;
    const auto status = recorder.append_frame({row.stamp, row.physical, row.winches.empty() ? nullptr : &winches,
        row.fluid.empty() ? nullptr : &fluid, row.attachments.empty() ? nullptr : &applied,
        row.moments.empty() ? nullptr : &moments, &inflow}); // Synchronous append, including any automatic flush.
    allocation_audit::active = false;
    if (status) require(allocation_audit::calls == 0, "successful append has no C++ new-family allocation");
    return status;
}

/// Return owned catalog text from the independent fixture at path without production decoding.
/// Missing/truncated input or a catalog outside the 16 MiB test bound throws.
std::string fixture_catalog_text(const std::filesystem::path& path) {
    // Decode the fixed bootstrap length and read that bounded literal UTF-8 document.
    std::ifstream input(path, std::ios::binary); // Read-only literal fixture; no generator/checker dependency.
    std::array<unsigned char, 64> header{}; // Fixed bootstrap containing the little-endian catalog extent.
    input.read(reinterpret_cast<char*>(header.data()), header.size());
    require(bool(input), "fixture bootstrap available");
    std::uint64_t size{}; // Catalog byte count accumulated without host-endian assumptions.
    for (unsigned index = 0; index < 8; ++index) size |= std::uint64_t(header[24 + index]) << (8 * index); // Length-byte cursor.
    require(size > 0 && size <= 16 * 1024 * 1024, "bounded independent catalog");
    std::string text(static_cast<std::size_t>(size), '\0'); // Owned bounded UTF-8 catalog bytes.
    input.read(text.data(), static_cast<std::streamsize>(size));
    require(bool(input), "fixture catalog available");
    return text;
}

/// Own fault settings borrowed by fail()/limit() for one single-threaded recorder scenario.
/// Setup leaves faults disarmed; main() arms them after a persisted prefix exists.
/// The object must outlive the recorder and counts only the selected operation.
struct Fault {
    bool enabled{}; ///< Arm after one prior complete group and one buffered final row.
    pose::IoOperation operation{pose::IoOperation::write}; ///< Selected I/O boundary.
    std::uint64_t seen{}; ///< Matching calls observed after arming.
    std::uint64_t occurrence{1}; ///< One-based failure point within finalization.
    bool short_write{}; ///< Retain a physically truncated record at the selected write.
};
/// Inspect mutable Fault context and the current operation; the ordinal argument is unused.
/// Return true only for the selected outright failure; count matching armed calls for limit().
bool fail(void* context, pose::IoOperation operation, std::uint64_t) noexcept {
    // Ignore unarmed/unselected calls, count a matching boundary, then decide outright failure.
    auto& fault = *static_cast<Fault*>(context); // Borrowed settings guaranteed live by the enclosing scenario.
    if (!fault.enabled || fault.operation != operation) return false;
    ++fault.seen;
    return !fault.short_write && fault.seen == fault.occurrence;
}
/// Return the permitted byte count for requested bytes using borrowed Fault context.
/// The ordinal argument is unused; fail() has already counted this write boundary.
/// Only the selected short-write scenario is shortened, retaining physically torn evidence.
std::size_t limit(void* context, std::uint64_t, std::size_t requested) noexcept {
    // Inspect the matching fault count and cap only the selected armed write.
    const auto& fault = *static_cast<const Fault*>(context); // Read-only settings shared with fail().
    return fault.enabled && fault.short_write && fault.seen == fault.occurrence
        ? std::min<std::size_t>(17, requested) : requested;
}

/// Compare borrowed stats with expected raw bytes, admitted frames, physical-reference bytes,
/// overlay-reference bytes and effective raw-byte budget; throw on the first mismatch.
/// These are retained payload capacities, not total process memory or numerical-core allocation.
void workspace(const pose::Statistics& stats, std::uint64_t raw, std::uint64_t frames,
               std::uint64_t physical, std::uint64_t overlay, std::uint64_t budget) {
    // Check whole-group capacity, each retained table, fixed scratch, then the aggregate budget.
    require(stats.batch_capacity_bytes == raw && stats.effective_batch_frames == frames, "whole-group raw admission");
    require(stats.physical_reference_bytes == physical && stats.overlay_reference_bytes == overlay, "separate retained-table charges");
    require(stats.encoding_workspace_bytes == 2048 && stats.index_workspace_bytes == 256, "fixed scratch charge");
    require(stats.workspace_capacity_bytes == raw + physical + overlay + 2048, "aggregate payload workspace");
    require(stats.effective_raw_budget_bytes == budget, "effective raw budget after both tables");
}
} // namespace


/// Return one complete raw group's byte width from borrowed independent catalog metadata.
/// Formulae are the frozen layouts, not recorder implementation helpers; checked fixture sizes are small.
std::uint64_t group_width(const Json& catalog) {
    // Count each enabled fixed-width population, including masks and attachment interval prefixes.
    std::uint64_t width = 64 + 56 * catalog.at("samples").size(); // Common structural stamp plus physical xyz/wxyz rows.
    if (catalog.contains("winch_geometry")) width += 80 + 104 * catalog.at("winch_geometry").at("arrangements").size();
    if (catalog.contains("fluid_poses")) {
        const auto count = catalog.at("fluid_poses").at("samples").size(); // F population independent of cut/load counts.
        width += 80 + (count + 7) / 8 + 56 * count;
    }
    if (catalog.contains("applied_loads")) {
        width += 80;
        for (const auto& attachment : catalog.at("applied_loads").at("attachments")) { // Complete attachment-local rows.
            const auto count = attachment.at("station_count").get<std::uint64_t>(); // Logical physical station count.
            width += 96 + (count + 7) / 8 + 48 * count;
        }
    }
    if (catalog.contains("elastic_moments")) {
        const auto count = catalog.at("elastic_moments").at("cuts").size(); // Dense signed moment population.
        width += 80 + (count + 7) / 8 + 24 * count;
    }
    if (catalog.contains("inflow_planes")) {
        width += 80;
        for (const auto& plane : catalog.at("inflow_planes").at("planes")) { // One independently packed rectangle.
            const auto count = plane.at("nu").get<std::uint64_t>() * plane.at("nv").get<std::uint64_t>(); // Small valid literal product.
            width += (count + 7) / 8 + 24 * count;
        }
    }
    return width;
}

/// Return whether a borrowed manifest entry describes invalid setup rather than malformed wire bytes.
/// Missing descriptor alone is a valid old-family setup and is handled separately as an extra-view test.
bool bad_catalog_case(const std::string& name, const Json& info) {
    // Select independent catalog rejection categories while excluding payload/order-only defects.
    const auto reason = info.value("rejection_reason", std::string{}); // Author's isolated contract category.
    return name != "missing_descriptor.spose" && (reason == "plane catalog contract" || reason == "nonfinite JSON metadata");
}

/// Produce bounded plane-enabled scenarios for independent Python readback.
/// argv contains NEW_OUTPUT_DIR, catalog.json and expected.json; no solver or fixture author is invoked.
/// Return zero after assertions, two for bad arguments, or one after reporting the first exception.
int main(int argc, char** argv) {
    // Load independent source literals and exercise every admitted catalog/phase/availability variant.
    // Reject malformed borrowed groups before copy, then test exact budgets and deterministic I/O boundaries.
    // Retain all resulting files for separate semantic, bit-fidelity and structural-link inspection.
    try {
        if (argc != 4) return 2;
        // Prove the scoped allocator observation is active before testing recorder calls.
        allocation_audit::calls = 0;
        allocation_audit::active = true;
        void* probe = ::operator new(1); // Direct ordinary allocation whose ownership is immediately released.
        allocation_audit::active = false;
        ::operator delete(probe);
        require(allocation_audit::calls == 1, "allocation observation self-check");
        const std::filesystem::path directory = argv[1], fixture = std::filesystem::path(argv[2]).parent_path(); // Fresh owner and read-only corpus.
        require(std::filesystem::create_directory(directory), "output directory must be new");
        std::ifstream catalog_input(argv[2]), expected_input(argv[3]); // Immutable catalog and literal row inputs.
        Json catalog, expected; // Owned setup/manifest documents retained for every scenario.
        catalog_input >> catalog;
        expected_input >> expected;
        const auto text = catalog.dump(); // Common immutable full mixed catalog bytes.
        const auto first = packet(expected.at("transient_frames").at(0)); // Initial row with valid moments and unavailable loads.
        const auto second = packet(expected.at("transient_frames").at(1)); // Sparse accepted positive endpoint.
        const auto third = packet(expected.at("transient_frames").at(2)); // Later endpoint with distinct moment/source facts.
        pose::Options options; // Two complete mixed two-plane groups, sharing every raw population's capacity.
        options.max_batch_frames = 2;
        options.max_batch_bytes = 4988;
        // Borrow directory/text/options for main's lifetime; name chooses a fresh destination
        // and optional hook borrows a longer-lived scenario Fault. Return Recorder::open's result.
        const auto open = [&](const std::string& name, pose::FaultHook hook = {}) {
            // Open one independent scenario under the shared catalog and two-group limit.
            return pose::Recorder::open(directory / (name + ".spose"), text, options, hook);
        };
        std::uint64_t physical_bytes{}, overlay_bytes{}; // Actual retained winch and applied/plane-layout charges learned once.
        require(group_width(catalog) == 2494, "independent mixed group width");

        // Mutate all nested borrowed buffers after acceptance, proving synchronous ownership on readback.
        {
            auto recorder = open("mixed_transient"); // Main bit-fidelity and borrow-lifetime success case.
            require(bool(recorder), "mixed plane setup");
            physical_bytes = recorder->statistics().physical_reference_bytes;
            overlay_bytes = recorder->statistics().overlay_reference_bytes;
            require(physical_bytes >= 48 && physical_bytes % 24 == 0 && overlay_bytes >= 64 && overlay_bytes % 16 == 0,
                    "actual retained table capacities charged");
            workspace(recorder->statistics(), 4988, 2, physical_bytes, overlay_bytes, 4988);
            auto borrowed = first; // Caller storage deliberately corrupted after append returns.
            require(bool(append(*recorder, borrowed)), "initial source group");
            std::fill(borrowed.physical.begin(), borrowed.physical.end(), std::numeric_limits<double>::quiet_NaN());
            for (auto& value : borrowed.fluid) value.world_pose.fill(std::numeric_limits<double>::quiet_NaN()); // Every F row.
            for (auto& value : borrowed.winches) value.world_xyz_m.fill(std::numeric_limits<double>::quiet_NaN()); // Every arrangement.
            for (auto& value : borrowed.moments) { // Every current-N triple and availability bit.
                value.moment_N_Nm.fill(std::numeric_limits<double>::quiet_NaN());
                value.valid = false;
            }
            for (auto& plane : borrowed.planes) { // Caller may also destroy or mutate plane IDs and every point immediately.
                plane.id = 99;
                for (auto& value : plane.samples) { // All velocity bits and validity were synchronously copied.
                    value.world_velocity_mps.fill(std::numeric_limits<double>::quiet_NaN());
                    value.valid = false;
                }
            }
            for (auto& attachment : borrowed.attachments) { // Nested interval metadata and all borrowed source rows.
                attachment.interval.packet = 999;
                for (auto& value : attachment.samples) { // Source geometry and density must already have been copied.
                    value.valid = true;
                    value.source_position_world_m.fill(std::numeric_limits<double>::quiet_NaN());
                    value.force_density_world_N_per_m.fill(std::numeric_limits<double>::quiet_NaN());
                }
            }
            require(recorder->statistics().accepted_frames == 1 && recorder->statistics().persisted_frames == 0, "borrowed group retained independently");
            require(append(*recorder, second) && recorder->statistics().persisted_frames == 2, "first complete six-stream batch");
            require(append(*recorder, third) && recorder->finalize(), "mixed finalize");
            require(recorder->statistics().accepted_frames == 3 && recorder->statistics().persisted_frames == 3
                    && recorder->statistics().pose_chunks == 2, "mixed counters");
            require(!append(*recorder, third) && !recorder->finalize() && !open("mixed_transient"), "terminal state and no clobber");
        }

        // Generic wire tables permit two used source ordinals without inventing source-identity uniqueness.
        {
            auto generic = catalog; // Independent live catalog variant; frozen fixture bytes remain unchanged.
            auto duplicate = generic.at("inflow_planes").at("sources").at(0); // Exact copied provider object and identity, with only its dense ordinal changed.
            duplicate["id"] = 1;
            generic["inflow_planes"]["sources"].push_back(std::move(duplicate));
            generic["inflow_planes"]["planes"][1]["source_id"] = 1;
            auto recorder = pose::Recorder::open(directory / "generic_two_sources.spose", generic.dump(), options); // Same fixed geometry/populations and raw budget.
            require(bool(recorder), "generic two-source catalog admitted");
            workspace(recorder->statistics(), 4988, 2, physical_bytes, overlay_bytes, 4988);
            require(append(*recorder, first) && append(*recorder, second) && append(*recorder, third)
                    && recorder->finalize(), "generic two-source literal groups published");
        }

        // JSON integer -0 denotes exact zero for new plane fields, independently of a parser's signed storage tag.
        {
            auto negative_zero = catalog; // Supplementary new-family-only metadata; every older identifier remains unchanged.
            negative_zero["inflow_planes"]["sources"][0]["id"] = "__plane_source_zero_token__";
            negative_zero["inflow_planes"]["planes"][0]["id"] = "__plane_id_zero_token__";
            negative_zero["inflow_planes"]["planes"][0]["source_id"] = "__plane_source_reference_zero_token__";
            negative_zero["inflow_planes"]["sources"][0]["initialization_manifest"]["source"]["source_bytes"] = "__plane_source_bytes_zero_token__";
            auto raw = negative_zero.dump(); // Owned serialized template; replacement below must precede open without another JSON roundtrip.
            for (const std::string_view token : {"\"__plane_source_zero_token__\"", "\"__plane_id_zero_token__\"",
                    "\"__plane_source_reference_zero_token__\"", "\"__plane_source_bytes_zero_token__\""}) { // Unique quoted placeholders for four exact integer fields.
                const auto position = raw.find(token); // Sole occurrence belonging to the intended new-family field.
                require(position != std::string::npos && raw.find(token, position + token.size()) == std::string::npos,
                        "unique raw negative-zero placeholder");
                raw.replace(position, token.size(), "-0");
            }
            auto recorder = pose::Recorder::open(directory / "generic_negative_zero_integers.spose", raw, options); // Pass the literal -0 spelling directly.
            require(bool(recorder), "new plane fields admit exact JSON negative integer zero");
            workspace(recorder->statistics(), 4988, 2, physical_bytes, overlay_bytes, 4988);
            require(append(*recorder, first) && append(*recorder, second) && append(*recorder, third)
                    && recorder->finalize(), "negative-zero catalog literal groups published");
        }

        // All valid corpus catalogs are admitted, including descriptive future source kinds and missing external assets.
        // Partial-file oracles contribute only their earlier complete rows; newly emitted files finalize normally.
        for (const auto& [name, info] : expected.at("cases").items()) { // Independent fixture filename and declared outcome.
            if (!info.at("valid").get<bool>()) continue;
            const auto selected_text = fixture_catalog_text(fixture / name); // Literal setup text, without a writer/generator import.
            const auto selected = Json::parse(selected_text); // Owned metadata for independent group-width accounting.
            const auto path = directory / ("valid_" + name); // Unique produced artifact corresponding to this corpus case.
            auto limits = options; // One independent two-row raw budget for the actual enabled populations.
            const auto width = group_width(selected); // All enabled layouts including C7/C8/C9 mask alignment.
            limits.max_batch_bytes = 2 * width;
            auto recorder = pose::Recorder::open(path, selected_text, limits); // Variant owner.
            require(bool(recorder), "independent valid plane catalog admitted");
            const auto retained = recorder->statistics().overlay_reference_bytes; // Actual applied plus plane layout capacities.
            const auto minimum = 16 * (selected.at("inflow_planes").at("planes").size()
                + (selected.contains("applied_loads") ? selected.at("applied_loads").at("attachments").size() : 0)); // Required retained layout entries.
            require(retained >= minimum && retained % 16 == 0, "all actual overlay layout entries charged");
            workspace(recorder->statistics(), 2 * width, 2, selected.contains("winch_geometry") ? physical_bytes : 0, retained, 2 * width);
            const auto& rows = expected.at(info.at("frames_key").get<std::string>()); // Literal full-population source rows.
            const auto count = info.at("frames").get<std::size_t>(); // Complete expected prefix, independent of torn suffix bytes.
            for (std::size_t index = 0; index < count; ++index) // Row ordinal in this admitted literal source sequence.
                require(bool(append(*recorder, packet(rows.at(index)))), "independent valid plane row");
            require(bool(recorder->finalize({static_cast<pose::Outcome>(info.at("run_outcome").get<unsigned>()),
                static_cast<pose::Outcome>(info.at("capture_outcome").get<unsigned>())})), "independent valid plane finalize");
        }
        for (const auto& [name, info] : expected.at("cases").items()) { // Every independent setup-specific catalog negative.
            if (!bad_catalog_case(name, info)) continue;
            const auto path = directory / ("bad_catalog_" + name); // No output or partial file may be created here.
            require(!pose::Recorder::open(path, fixture_catalog_text(fixture / name), options), "independent invalid catalog rejected");
            require(!std::filesystem::exists(path) && !std::filesystem::exists(path.string() + ".partial"), "bad catalog rejected before file creation");
        }

        // Each malformed view fails before any population, accepted count or write counter advances.
        for (int scenario = 0; scenario < 43; ++scenario) { // One isolated whole-group admission defect.
            auto row = second; // Fresh owned packet; resize point storage before borrowing any spans.
            if (scenario == 3) row.planes[0].samples.push_back(row.planes[0].samples.front());
            auto attachments = attachment_views(row); // Copied load intervals and borrowed complete stations.
            auto planes = plane_views(row); // Dense IDs and all caller-owned point spans.
            std::array<pose::InflowPlaneView, 3> extra{planes[0], planes[1], planes[1]}; // Explicit overpopulation case.
            pose::AppliedLoadsFrameView loads{row.stamp, row.generation, row.phase, attachments}; // Earlier source provenance.
            pose::FluidFrameView fluid{row.stamp, row.generation, row.phase, row.fluid}; // Endpoint F population.
            pose::WinchFrameView winches{row.stamp, row.generation, row.phase, row.winches}; // Old arrangement population.
            pose::ElasticMomentFrameView moments{row.stamp, row.generation, row.phase, row.moments}; // Signed current-N cuts.
            pose::InflowPlanesFrameView inflow{row.stamp, row.generation, row.phase, planes}; // New target of malformed view tests.
            pose::FrameGroupView group{row.stamp, row.physical, &winches, &fluid, &loads, &moments, &inflow}; // Atomic source boundary.
            if (scenario == 0) group.inflow_planes = nullptr;
            if (scenario == 1) planes[0].samples = planes[0].samples.first(5);
            if (scenario == 2) planes[0].samples = {};
            if (scenario == 4) inflow.preparation_generation = 0;
            if (scenario == 5) inflow.preparation_generation = 3;
            if (scenario == 6) inflow.preparation_generation = std::numeric_limits<std::uint64_t>::max();
            if (scenario == 7) inflow.phase = pose::SourcePhase::static_certified;
            if (scenario == 8) inflow.phase = static_cast<pose::SourcePhase>(4);
            if (scenario == 9) inflow.stamp.analysis = pose::Analysis::static_equilibrium;
            if (scenario == 10) inflow.stamp.events = pose::Event::target;
            if (scenario == 11) inflow.stamp.time += .001;
            if (scenario == 12) inflow.stamp.state_id = 5;
            if (scenario == 13) inflow.stamp.load_factor = -0.0;
            if (scenario == 14) row.planes[0].samples[0].world_velocity_mps[0] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 15) row.planes[0].samples[0].world_velocity_mps[1] = std::numeric_limits<double>::infinity();
            if (scenario == 16) row.planes[0].samples[0].world_velocity_mps[2] = -std::numeric_limits<double>::infinity();
            if (scenario == 17) row.planes[0].samples[0].valid = false;
            if (scenario == 18) row.planes[0].samples[1].world_velocity_mps[0] = -0.0;
            if (scenario == 19) row.planes[0].samples[1].world_velocity_mps[2] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 20) row.physical[0] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 21) row.fluid[0].world_pose[3] = 2;
            if (scenario == 22) row.winches[0].world_xyz_m[0] = std::numeric_limits<double>::infinity();
            if (scenario == 23) row.attachments[0].samples[0].force_density_world_N_per_m[0] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 24) group.fluid_poses = nullptr;
            if (scenario == 25) group.applied_loads = nullptr;
            if (scenario == 26) group.winches = nullptr;
            if (scenario == 27) group.structural_poses = group.structural_poses.first(55);
            if (scenario == 28) group.stamp.time += .001;
            if (scenario == 29) inflow.phase = pose::SourcePhase::transient_initial;
            if (scenario == 30) inflow.stamp.time = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 31) inflow.stamp.load_factor = 1;
            if (scenario == 32) inflow.planes = {};
            if (scenario == 33) inflow.planes = inflow.planes.first(1);
            if (scenario == 34) inflow.planes = extra;
            if (scenario == 35) planes[0].plane_id = 1;
            if (scenario == 36) planes[1].plane_id = std::numeric_limits<std::uint64_t>::max();
            if (scenario == 37) std::swap(planes[0], planes[1]);
            if (scenario == 38) group.elastic_moments = nullptr;
            if (scenario == 39) row.moments[0].moment_N_Nm[2] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 40) planes[1].samples = planes[1].samples.first(5);
            if (scenario == 41) row.planes[1].samples[5].world_velocity_mps[2] = std::numeric_limits<double>::infinity();
            if (scenario == 42) row.planes[1].samples[0].world_velocity_mps[2] = 1;
            auto recorder = open("bad_packet_" + std::to_string(scenario)); // Buffered predecessor exercises failure before a potential flush.
            require(recorder && append(*recorder, first), "bad packet predecessor");
            const auto before = recorder->statistics(); // Counters before malformed borrowed-group admission.
            const auto rejected = recorder->append_frame(group); // Actual returned domain, not merely a false conversion.
            require(rejected.code == serene::outputs::StatusCode::invalid_contract, "malformed plane group contract status");
            require(recorder->statistics().accepted_frames == before.accepted_frames
                    && recorder->statistics().buffered_frames == before.buffered_frames
                    && recorder->statistics().persisted_frames == before.persisted_frames
                    && recorder->statistics().written_bytes == before.written_bytes, "whole group fails before admission/copy/write");
            require(recorder->state() == pose::State::failed && !recorder->finalize(), "invalid packet latches failure");
        }

        // Legacy overloads and a group missing its new trailing member cannot discard declared planes.
        {
            auto base = open("old_base_overload"), winch = open("old_winch_overload"); // Separate legacy call paths.
            const pose::WinchFrameView view{first.stamp, first.generation, first.phase, first.winches}; // Borrowed old overload argument.
            require(base && !base->append_frame(first.stamp, first.physical), "old base overload rejects planes");
            require(winch && !winch->append_frame(first.stamp, first.physical, view), "old winch overload rejects planes");
            auto plain = catalog; // Valid old-family setup that must reject an unexpected plane view.
            plain.erase("inflow_planes");
            auto extra = pose::Recorder::open(directory / "unexpected_planes.spose", plain.dump(), options);
            require(extra && !append(*extra, first), "undeclared plane view rejects");
        }
        {
            auto recorder = open("abandoned"); // Preserve one complete batch while the final accepted row remains buffered.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "abandoned complete prefix");
        }
        {
            // Exact aggregate admission subtracts both actual retained tables and the fixed scratch allowance.
            auto limits = options; // Independent limit changes reset before each distinct budget contract.
            limits.max_batch_frames = 100;
            limits.max_workspace_bytes = physical_bytes + overlay_bytes + 2048 + 2494;
            auto exact = pose::Recorder::open(directory / "workspace_one.spose", text, limits); // Exactly one complete six-stream row fits.
            require(bool(exact), "exact workspace admits one complete group");
            workspace(exact->statistics(), 2494, 1, physical_bytes, overlay_bytes, 2494);
            require(append(*exact, first) && exact->finalize(), "one workspace-limited group");
            --limits.max_workspace_bytes;
            require(!pose::Recorder::open(directory / "workspace_too_small.spose", text, limits), "one byte below aggregate workspace rejects");
            limits = options;
            limits.max_batch_bytes = 2494;
            auto raw = pose::Recorder::open(directory / "raw_one.spose", text, limits); // Raw-only group boundary, independent of reference charges.
            require(bool(raw), "exact raw group admits");
            workspace(raw->statistics(), 2494, 1, physical_bytes, overlay_bytes, 2494);
            require(append(*raw, first) && raw->finalize(), "one raw-limited group");
            --limits.max_batch_bytes;
            require(!pose::Recorder::open(directory / "raw_too_small.spose", text, limits), "short raw group rejects");
            limits = options;
            limits.max_batch_frames = std::numeric_limits<std::uint64_t>::max();
            require(!pose::Recorder::open(directory / "frame_limit_overflow.spose", text, limits), "out-of-range frame budget rejected");
            limits = options;
            limits.max_workspace_bytes = std::numeric_limits<std::uint64_t>::max();
            require(!pose::Recorder::open(directory / "workspace_limit_overflow.spose", text, limits), "out-of-range workspace budget rejected");
            limits = options;
            limits.max_workspace_bytes = physical_bytes + overlay_bytes + 2047;
            require(!pose::Recorder::open(directory / "scratch_too_small.spose", text, limits), "all table charges precede scratch admission");
        }

        // Six group chunks use twelve writes; index/completion/footer contribute five later writes.
        for (const bool short_write : {false, true}) for (std::uint64_t occurrence = 1; occurrence <= 17; ++occurrence) {
            Fault fault; // Owned context outlives recorder; occurrence selects the finalization boundary.
            fault.short_write = short_write;
            fault.occurrence = occurrence;
            const auto name = std::string(short_write ? "short_write_" : "write_") + std::to_string(occurrence); // Unique evidence stem.
            auto recorder = open(name, {&fault, fail, limit}); // Unarmed until an honest prior group has persisted.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "write fault prefix");
            fault.enabled = true;
            const auto failed = recorder->finalize(); // Actual write error returned at the selected boundary.
            require(failed.code == serene::outputs::StatusCode::io_failure && recorder->state() == pose::State::failed, "selected write failure latches");
            require(recorder->statistics().accepted_frames == 3
                    && recorder->statistics().persisted_frames == (occurrence <= 12 ? 2 : 3)
                    && recorder->statistics().pose_chunks == (occurrence <= 12 ? 1 : 2), "write whole-group commit counters");
        }
        for (std::uint64_t occurrence = 1; occurrence <= 9; ++occurrence) { // Six group flushes plus index/completion/footer.
            Fault fault; // Owned seam context with selected one-based finalization flush.
            fault.operation = pose::IoOperation::flush;
            fault.occurrence = occurrence;
            auto recorder = open("flush_" + std::to_string(occurrence), {&fault, fail, limit}); // Bytes may be recoverable beyond committed counters.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "flush fault prefix");
            fault.enabled = true;
            const auto failed = recorder->finalize(); // Actual flush error, with readable bytes evaluated separately in Python.
            require(failed.code == serene::outputs::StatusCode::io_failure && recorder->state() == pose::State::failed, "selected flush failure latches");
            require(recorder->statistics().accepted_frames == 3
                    && recorder->statistics().persisted_frames == (occurrence <= 6 ? 2 : 3)
                    && recorder->statistics().pose_chunks == (occurrence <= 6 ? 1 : 2), "flush whole-group commit counters");
        }
        for (const auto operation : {pose::IoOperation::close, pose::IoOperation::publish}) { // Terminal failures after complete wire output.
            Fault fault; // Retained hook context until recorder destruction.
            fault.operation = operation;
            auto recorder = open(operation == pose::IoOperation::close ? "close" : "publish", {&fault, fail, limit}); // Complete unpublished evidence.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "terminal fault prefix");
            fault.enabled = true;
            const auto failed = recorder->finalize(); // Terminal domain distinguishes close I/O from publication failure.
            require(failed.code == (operation == pose::IoOperation::close ? serene::outputs::StatusCode::io_failure
                    : serene::outputs::StatusCode::publication_failure) && recorder->statistics().accepted_frames == 3
                    && recorder->statistics().persisted_frames == 3, "terminal failure retains complete wire facts");
        }
        {
            auto recorder = open("collision"); // Open before a competing owner claims the final destination.
            require(recorder && append(*recorder, first), "collision setup");
            { std::ofstream competing(directory / "collision.spose"); competing << "do not replace"; } // Exact no-clobber sentinel.
            require(recorder->finalize().code == serene::outputs::StatusCode::publication_failure, "competing destination preserved");
        }
        {
            // Reuse fixed packet buffers as accepted IDs advance; retained payload capacity stays duration-independent.
            auto recorder = open("long_bounded"); // Long sequence with exact prior-source joins and cycling opaque load identities.
            require(bool(recorder), "long run setup");
            auto row = first; // Owned buffers reused for every captured source group.
            double prior_time = 1.25; // Exact previous accepted time used for the next completed interval.
            for (std::uint64_t index = 0; index < 1001; ++index) { // Accepted identity and recording ordinal in this case.
                if (index == 1) row.attachments = second.attachments;
                row.stamp = {pose::Analysis::transient, index == 0 ? 2U : (index == 1000 ? 8U : 4U),
                    1.25 + .01 * static_cast<double>(index), index, 0};
                row.phase = index ? pose::SourcePhase::transient_accepted : pose::SourcePhase::transient_initial;
                row.planes[0].samples[0].world_velocity_mps[0] = 1.0 + .125 * static_cast<double>(index);
                if (index) for (auto& attachment : row.attachments) { // Common actual interval facts for both complete attachments.
                    attachment.interval.source_accepted_id = index - 1;
                    attachment.interval.source.time_s = prior_time;
                    attachment.interval.end_accepted_id = index;
                    attachment.interval.end_time_s = row.stamp.time;
                    attachment.interval.source.publication = 9 - index % 7;
                    attachment.interval.prepared_step_sequence = 3 + index % 5;
                    attachment.interval.packet = 17 - index % 11;
                }
                require(bool(append(*recorder, row)), "bounded accepted plane group");
                prior_time = row.stamp.time;
            }
            workspace(recorder->statistics(), 4988, 2, physical_bytes, overlay_bytes, 4988);
            require(recorder->finalize() && recorder->statistics().persisted_frames == 1001
                    && recorder->statistics().pose_chunks == 501, "bounded duration-independent plane storage");
        }
        std::cout << "plane recorder checks passed: " << checks << "; output=" << directory.string() << '\n';
        return 0;
    } catch (const std::exception& failure) { // First contract/input exception; retain all evidence already written.
        std::cerr << "plane recorder: " << failure.what() << '\n';
        return 1;
    }
}
