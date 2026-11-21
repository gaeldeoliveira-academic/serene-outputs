#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <serene/outputs/pose_recorder.hpp>

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
    const auto& moments = row.at("elastic_moments"); // Moment population supplies the common preparation identity.
    result.generation = moments.at("generation").get<std::uint64_t>();
    result.phase = static_cast<pose::SourcePhase>(moments.at("phase").get<std::uint32_t>());
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
    for (const auto& source : moments.at("moments")) { // One literal current-N cut in dense catalog order.
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
    return recorder.append_frame({row.stamp, row.physical, row.winches.empty() ? nullptr : &winches,
        row.fluid.empty() ? nullptr : &fluid, row.attachments.empty() ? nullptr : &applied, &moments});
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
    return width;
}

/// Return whether a borrowed manifest entry describes invalid setup rather than malformed wire bytes.
/// Missing descriptor alone is a valid old-family setup and is handled separately as an extra-view test.
bool bad_catalog_case(const std::string& name, const Json& info) {
    // Select independent catalog rejection categories while excluding payload/order-only defects.
    const auto reason = info.value("rejection_reason", std::string{}); // Author's isolated contract category.
    return name != "missing_descriptor.spose" && (reason == "moment catalog contract"
        || reason == "missing moment catalog field" || reason == "optional structural-line identity/range predicate"
        || reason == "line-coordinate-side uniqueness across provider/native ownership"
        || reason == "moment line identity uniqueness" || reason == "explicit base edge occurrence ownership"
        || reason == "moment base edge coordinate bounds" || reason == "binary64 line-coordinate-side uniqueness"
        || reason == "nonfinite JSON metadata");
}

/// Produce bounded moment-enabled scenarios for independent Python readback.
/// argv contains NEW_OUTPUT_DIR, catalog.json and expected.json; no solver or fixture author is invoked.
/// Return zero after assertions, two for bad arguments, or one after reporting the first exception.
int main(int argc, char** argv) {
    // Load independent source literals and exercise every admitted catalog/phase/availability variant.
    // Reject malformed borrowed groups before copy, then test exact budgets and deterministic I/O boundaries.
    // Retain all resulting files for separate semantic, bit-fidelity and structural-link inspection.
    try {
        if (argc != 4) return 2;
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
        pose::Options options; // Two complete mixed C7 groups, sharing every raw population's capacity.
        options.max_batch_frames = 2;
        options.max_batch_bytes = 4248;
        // Borrow directory/text/options for main's lifetime; name chooses a fresh destination
        // and optional hook borrows a longer-lived scenario Fault. Return Recorder::open's result.
        const auto open = [&](const std::string& name, pose::FaultHook hook = {}) {
            // Open one independent scenario under the shared catalog and two-group limit.
            return pose::Recorder::open(directory / (name + ".spose"), text, options, hook);
        };
        std::uint64_t physical_bytes{}, overlay_bytes{}; // Actual retained winch and applied-layout charges learned once.
        require(group_width(catalog) == 2124, "independent mixed group width");

        // Mutate all nested borrowed buffers after acceptance, proving synchronous ownership on readback.
        {
            auto recorder = open("mixed_transient"); // Main bit-fidelity and borrow-lifetime success case.
            require(bool(recorder), "mixed moment setup");
            physical_bytes = recorder->statistics().physical_reference_bytes;
            overlay_bytes = recorder->statistics().overlay_reference_bytes;
            require(physical_bytes >= 48 && physical_bytes % 24 == 0 && overlay_bytes >= 32 && overlay_bytes % 16 == 0,
                    "actual retained table capacities charged");
            workspace(recorder->statistics(), 4248, 2, physical_bytes, overlay_bytes, 4248);
            auto borrowed = first; // Caller storage deliberately corrupted after append returns.
            require(bool(append(*recorder, borrowed)), "initial source group");
            std::fill(borrowed.physical.begin(), borrowed.physical.end(), std::numeric_limits<double>::quiet_NaN());
            for (auto& value : borrowed.fluid) value.world_pose.fill(std::numeric_limits<double>::quiet_NaN()); // Every F row.
            for (auto& value : borrowed.winches) value.world_xyz_m.fill(std::numeric_limits<double>::quiet_NaN()); // Every arrangement.
            for (auto& value : borrowed.moments) { // Every current-N triple and availability bit.
                value.moment_N_Nm.fill(std::numeric_limits<double>::quiet_NaN());
                value.valid = false;
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
            require(append(*recorder, second) && recorder->statistics().persisted_frames == 2, "first complete five-stream batch");
            require(append(*recorder, third) && recorder->finalize(), "mixed finalize");
            require(recorder->statistics().accepted_frames == 3 && recorder->statistics().persisted_frames == 3
                    && recorder->statistics().pose_chunks == 2, "mixed counters");
            require(!append(*recorder, third) && !recorder->finalize() && !open("mixed_transient"), "terminal state and no clobber");
        }

        // All valid corpus catalogs are admitted except future planes, which this producer cannot emit.
        // Partial-file oracles contribute only their earlier complete rows; newly emitted files finalize normally.
        for (const auto& [name, info] : expected.at("cases").items()) { // Independent fixture filename and declared outcome.
            if (!info.at("valid").get<bool>()) continue;
            const auto selected_text = fixture_catalog_text(fixture / name); // Literal setup text, without a writer/generator import.
            const auto selected = Json::parse(selected_text); // Owned metadata for independent group-width accounting.
            const auto path = directory / ("valid_" + name); // Unique produced artifact corresponding to this corpus case.
            if (name == "opaque_future_planes.spose") {
                require(!pose::Recorder::open(path, selected_text, options), "future unsupported planes rejected at setup");
                require(!std::filesystem::exists(path) && !std::filesystem::exists(path.string() + ".partial"), "unsupported setup creates no file");
                continue;
            }
            auto limits = options; // One independent two-row raw budget for the actual enabled populations.
            const auto width = group_width(selected); // All enabled layouts including C7/C8/C9 mask alignment.
            limits.max_batch_bytes = 2 * width;
            auto recorder = pose::Recorder::open(path, selected_text, limits); // Variant owner.
            require(bool(recorder), "independent valid moment catalog admitted");
            workspace(recorder->statistics(), 2 * width, 2, selected.contains("winch_geometry") ? physical_bytes : 0,
                selected.contains("applied_loads") ? overlay_bytes : 0, 2 * width);
            const auto& rows = expected.at(info.at("frames_key").get<std::string>()); // Literal full-population source rows.
            const auto count = info.at("frames").get<std::size_t>(); // Complete expected prefix, independent of torn suffix bytes.
            for (std::size_t index = 0; index < count; ++index) // Row ordinal in this admitted literal source sequence.
                require(bool(append(*recorder, packet(rows.at(index)))), "independent valid moment row");
            require(bool(recorder->finalize({static_cast<pose::Outcome>(info.at("run_outcome").get<unsigned>()),
                static_cast<pose::Outcome>(info.at("capture_outcome").get<unsigned>())})), "independent valid moment finalize");
        }
        for (const auto& [name, info] : expected.at("cases").items()) { // Every independent setup-specific catalog negative.
            if (!bad_catalog_case(name, info)) continue;
            const auto path = directory / ("bad_catalog_" + name); // No output or partial file may be created here.
            require(!pose::Recorder::open(path, fixture_catalog_text(fixture / name), options), "independent invalid catalog rejected");
            require(!std::filesystem::exists(path) && !std::filesystem::exists(path.string() + ".partial"), "bad catalog rejected before file creation");
        }

        // Each malformed view must fail before any population, accepted count or write counter advances.
        for (int scenario = 0; scenario < 32; ++scenario) { // One isolated borrowed-group admission defect.
            auto row = second; // Fresh owned packet; construct spans only after the deliberate population resize.
            if (scenario == 3) row.moments.push_back(row.moments.front());
            auto attachments = attachment_views(row); // Copied intervals and borrowed station spans.
            pose::AppliedLoadsFrameView loads{row.stamp, row.generation, row.phase, attachments}; // Complete old load population.
            pose::FluidFrameView fluid{row.stamp, row.generation, row.phase, row.fluid}; // Optional endpoint F population.
            pose::WinchFrameView winches{row.stamp, row.generation, row.phase, row.winches}; // Complete old winch population.
            pose::ElasticMomentFrameView moments{row.stamp, row.generation, row.phase, row.moments}; // Defect target; borrowed actual cut rows.
            pose::FrameGroupView group{row.stamp, row.physical, &winches, &fluid, &loads, &moments}; // Atomic append boundary.
            if (scenario == 0) group.elastic_moments = nullptr;
            if (scenario == 1) moments.moments = moments.moments.first(6);
            if (scenario == 2) moments.moments = {};
            if (scenario == 4) moments.preparation_generation = 0;
            if (scenario == 5) moments.preparation_generation = 3;
            if (scenario == 6) moments.preparation_generation = std::numeric_limits<std::uint64_t>::max();
            if (scenario == 7) moments.phase = pose::SourcePhase::static_certified;
            if (scenario == 8) moments.phase = static_cast<pose::SourcePhase>(4);
            if (scenario == 9) moments.stamp.analysis = pose::Analysis::static_equilibrium;
            if (scenario == 10) moments.stamp.events = pose::Event::target;
            if (scenario == 11) moments.stamp.time += .001;
            if (scenario == 12) moments.stamp.state_id = 5;
            if (scenario == 13) moments.stamp.load_factor = -0.0;
            if (scenario == 14) row.moments[0].moment_N_Nm[0] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 15) row.moments[0].moment_N_Nm[1] = std::numeric_limits<double>::infinity();
            if (scenario == 16) row.moments[0].moment_N_Nm[2] = -std::numeric_limits<double>::infinity();
            if (scenario == 17) row.moments[0].valid = false;
            if (scenario == 18) { row.moments[0] = {}; row.moments[0].moment_N_Nm[0] = -0.0; }
            if (scenario == 19) { row.moments[0] = {}; row.moments[0].moment_N_Nm[2] = std::numeric_limits<double>::quiet_NaN(); }
            if (scenario == 20) row.physical[0] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 21) row.fluid[0].world_pose[3] = 2;
            if (scenario == 22) row.winches[0].world_xyz_m[0] = std::numeric_limits<double>::infinity();
            if (scenario == 23) row.attachments[0].samples[0].force_density_world_N_per_m[0] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 24) group.fluid_poses = nullptr;
            if (scenario == 25) group.applied_loads = nullptr;
            if (scenario == 26) group.winches = nullptr;
            if (scenario == 27) group.structural_poses = group.structural_poses.first(55);
            if (scenario == 28) group.stamp.time += .001;
            if (scenario == 29) moments.phase = pose::SourcePhase::transient_initial;
            if (scenario == 30) moments.stamp.time = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 31) moments.stamp.load_factor = 1;
            auto recorder = open("bad_packet_" + std::to_string(scenario)); // One valid buffered predecessor before rejection.
            require(recorder && append(*recorder, first), "bad packet predecessor");
            const auto before = recorder->statistics(); // Snapshot before rejected borrowed group admission.
            require(!recorder->append_frame(group), "malformed moment group rejected");
            require(recorder->statistics().accepted_frames == before.accepted_frames
                    && recorder->statistics().buffered_frames == before.buffered_frames
                    && recorder->statistics().persisted_frames == before.persisted_frames
                    && recorder->statistics().written_bytes == before.written_bytes, "whole group fails before admission/copy/write");
            require(recorder->state() == pose::State::failed && !recorder->finalize(), "invalid packet latches failure");
        }

        // Legacy overloads and a group missing its new trailing member cannot discard declared moments.
        {
            auto base = open("old_base_overload"), winch = open("old_winch_overload"); // Separate legacy call paths.
            const pose::WinchFrameView view{first.stamp, first.generation, first.phase, first.winches}; // Borrowed old overload argument.
            require(base && !base->append_frame(first.stamp, first.physical), "old base overload rejects moments");
            require(winch && !winch->append_frame(first.stamp, first.physical, view), "old winch overload rejects moments");
            auto plain = catalog; // Valid old-family setup that must reject an unexpected moment view.
            plain.erase("elastic_moments");
            auto extra = pose::Recorder::open(directory / "unexpected_moments.spose", plain.dump(), options);
            require(extra && !append(*extra, first), "undeclared moment view rejects");
        }
        {
            auto recorder = open("abandoned"); // Preserve one complete batch while the final accepted row remains buffered.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "abandoned complete prefix");
        }
        {
            // Exact aggregate admission subtracts both actual retained tables and the fixed scratch allowance.
            auto limits = options; // Independent limit changes reset before each distinct budget contract.
            limits.max_batch_frames = 100;
            limits.max_workspace_bytes = physical_bytes + overlay_bytes + 2048 + 2124;
            auto exact = pose::Recorder::open(directory / "workspace_one.spose", text, limits); // Exactly one complete five-stream row fits.
            require(bool(exact), "exact workspace admits one complete group");
            workspace(exact->statistics(), 2124, 1, physical_bytes, overlay_bytes, 2124);
            require(append(*exact, first) && exact->finalize(), "one workspace-limited group");
            --limits.max_workspace_bytes;
            require(!pose::Recorder::open(directory / "workspace_too_small.spose", text, limits), "one byte below aggregate workspace rejects");
            limits = options;
            limits.max_batch_bytes = 2124;
            auto raw = pose::Recorder::open(directory / "raw_one.spose", text, limits); // Raw-only group boundary, independent of reference charges.
            require(bool(raw), "exact raw group admits");
            workspace(raw->statistics(), 2124, 1, physical_bytes, overlay_bytes, 2124);
            require(append(*raw, first) && raw->finalize(), "one raw-limited group");
            --limits.max_batch_bytes;
            require(!pose::Recorder::open(directory / "raw_too_small.spose", text, limits), "short raw group rejects");
            limits = options;
            limits.max_workspace_bytes = physical_bytes + overlay_bytes + 2047;
            require(!pose::Recorder::open(directory / "scratch_too_small.spose", text, limits), "all table charges precede scratch admission");
        }

        // Five group chunks use ten writes; index/completion/footer contribute five later writes.
        for (const bool short_write : {false, true}) for (std::uint64_t occurrence = 1; occurrence <= 15; ++occurrence) {
            Fault fault; // Owned context outlives recorder; occurrence selects the finalization boundary.
            fault.short_write = short_write;
            fault.occurrence = occurrence;
            const auto name = std::string(short_write ? "short_write_" : "write_") + std::to_string(occurrence); // Unique evidence stem.
            auto recorder = open(name, {&fault, fail, limit}); // Unarmed until an honest prior group has persisted.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "write fault prefix");
            fault.enabled = true;
            require(!recorder->finalize() && recorder->state() == pose::State::failed, "selected write failure latches");
            require(recorder->statistics().accepted_frames == 3
                    && recorder->statistics().persisted_frames == (occurrence <= 10 ? 2 : 3)
                    && recorder->statistics().pose_chunks == (occurrence <= 10 ? 1 : 2), "write whole-group commit counters");
        }
        for (std::uint64_t occurrence = 1; occurrence <= 8; ++occurrence) { // Five group flushes plus index/completion/footer.
            Fault fault; // Owned seam context with selected one-based finalization flush.
            fault.operation = pose::IoOperation::flush;
            fault.occurrence = occurrence;
            auto recorder = open("flush_" + std::to_string(occurrence), {&fault, fail, limit}); // Bytes may be recoverable beyond committed counters.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "flush fault prefix");
            fault.enabled = true;
            require(!recorder->finalize() && recorder->state() == pose::State::failed, "selected flush failure latches");
            require(recorder->statistics().accepted_frames == 3
                    && recorder->statistics().persisted_frames == (occurrence <= 5 ? 2 : 3)
                    && recorder->statistics().pose_chunks == (occurrence <= 5 ? 1 : 2), "flush whole-group commit counters");
        }
        for (const auto operation : {pose::IoOperation::close, pose::IoOperation::publish}) { // Terminal failures after complete wire output.
            Fault fault; // Retained hook context until recorder destruction.
            fault.operation = operation;
            auto recorder = open(operation == pose::IoOperation::close ? "close" : "publish", {&fault, fail, limit}); // Complete unpublished evidence.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "terminal fault prefix");
            fault.enabled = true;
            require(!recorder->finalize() && recorder->statistics().accepted_frames == 3
                    && recorder->statistics().persisted_frames == 3, "terminal failure retains complete wire facts");
        }
        {
            auto recorder = open("collision"); // Open before a competing owner claims the final destination.
            require(recorder && append(*recorder, first), "collision setup");
            { std::ofstream competing(directory / "collision.spose"); competing << "do not replace"; } // Exact no-clobber sentinel.
            require(!recorder->finalize(), "competing destination preserved");
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
                row.moments[2].moment_N_Nm[0] = -7.25 - static_cast<double>(index);
                if (index) for (auto& attachment : row.attachments) { // Common actual interval facts for both complete attachments.
                    attachment.interval.source_accepted_id = index - 1;
                    attachment.interval.source.time_s = prior_time;
                    attachment.interval.end_accepted_id = index;
                    attachment.interval.end_time_s = row.stamp.time;
                    attachment.interval.source.publication = 9 - index % 7;
                    attachment.interval.prepared_step_sequence = 3 + index % 5;
                    attachment.interval.packet = 17 - index % 11;
                }
                require(bool(append(*recorder, row)), "bounded accepted moment group");
                prior_time = row.stamp.time;
            }
            workspace(recorder->statistics(), 4248, 2, physical_bytes, overlay_bytes, 4248);
            require(recorder->finalize() && recorder->statistics().persisted_frames == 1001
                    && recorder->statistics().pose_chunks == 501, "bounded duration-independent moment storage");
        }
        std::cout << "moment recorder checks passed: " << checks << "; output=" << directory.string() << '\n';
        return 0;
    } catch (const std::exception& failure) { // First contract/input exception; retain all evidence already written.
        std::cerr << "moment recorder: " << failure.what() << '\n';
        return 1;
    }
}
