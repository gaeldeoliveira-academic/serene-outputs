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
    std::vector<Attachment> attachments; ///< Two independently authored complete load populations.
};

/// Copy the borrowed expected-row JSON into an owned Packet without deriving source facts.
/// Require two load attachments; malformed/missing JSON or a failed extent check throws.
Packet packet(const Json& row) {
    // Read source identity, physical values and optional older populations.
    // Copy every interval scalar and every physical source point/density exactly.
    Packet result; // Owned submission buffers, independent of the borrowed JSON document.
    const auto& stamp = row.at("stamp"); // Actual accepted source identity in the literal oracle.
    result.stamp = {static_cast<pose::Analysis>(stamp.at("analysis").get<std::uint32_t>()),
        stamp.at("events").get<std::uint32_t>(), stamp.at("time").get<double>(),
        stamp.at("state_id").get<std::uint64_t>(), stamp.at("load_factor").get<double>()};
    const auto& applied = row.at("applied_loads"); // Load population and its common preparation identity.
    result.generation = applied.at("generation").get<std::uint64_t>();
    result.phase = static_cast<pose::SourcePhase>(applied.at("phase").get<std::uint32_t>());
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
    // Preserve complete source/end provenance separately from end-of-step display geometry.
    for (const auto& source : applied.at("attachments")) { // One complete attachment in catalog order.
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
    require(result.attachments.size() == 2, "literal two-attachment source");
    return result;
}

/// Return two views borrowing sample buffers from row, which must contain two attachments.
/// Interval metadata is copied into the views; row must outlive their synchronous use.
std::array<pose::AppliedAttachmentFrameView, 2> attachment_views(const Packet& row) {
    // Pair each copied interval with its borrowed station population in catalog order.
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
    return recorder.append_frame({row.stamp, row.physical, row.winches.empty() ? nullptr : &winches,
        row.fluid.empty() ? nullptr : &fluid, &applied});
}

/// Return owned catalog JSON from the independent fixture at path without production decoding.
/// Missing/truncated input or a catalog outside the 16 MiB test bound throws.
Json fixture_catalog(const std::filesystem::path& path) {
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
    return Json::parse(text);
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

/// Produce bounded load-enabled scenarios from independent catalog/expected inputs for Python readback.
/// argv: NEW_OUTPUT_DIR, catalog.json, expected.json. No solver, generator or build is invoked.
/// Return 0 after all assertions, 2 for wrong argument count, or 1 after reporting an exception.
int main(int argc, char** argv) {
    // Load independent literal packets and establish fresh bounded recorder destinations.
    // Exercise complete source groups, strict setup/packet admission and publication failures.
    // Leave every resulting file for the separately implemented Python wire/semantic checks.
    try {
        if (argc != 4) return 2;
        const std::filesystem::path directory = argv[1], fixture = std::filesystem::path(argv[2]).parent_path(); // Fresh output owner and read-only corpus root.
        require(std::filesystem::create_directory(directory), "output directory must be new");
        std::ifstream catalog_input(argv[2]), expected_input(argv[3]); // Independent setup and literal row inputs.
        Json catalog, expected; // Owned documents retained through every recorder scenario.
        catalog_input >> catalog;
        expected_input >> expected;
        const auto text = catalog.dump(); // Common immutable mixed-population setup text.
        const auto first = packet(expected.at("transient_frames").at(0)); // Initial accepted state, unavailable interval.
        const auto second = packet(expected.at("transient_frames").at(1)); // First saved positive interval, with an unsaved source.
        const auto third = packet(expected.at("transient_frames").at(2)); // Later sparse endpoint with decreasing opaque tokens.
        pose::Options options; // Common two-row raw budget; scenario copies alter only their chosen admission bound.
        options.max_batch_frames = 2;
        options.max_batch_bytes = 3750; // Two1875-byte F/load/winch/physical groups.
        // Borrow directory/text/options for the enclosing main lifetime; name selects a fresh
        // destination and hook optionally borrows scenario fault settings. Return open's result.
        const auto open = [&](const std::string& name, pose::FaultHook hook = {}) {
            // Combine the scenario name with its fresh parent and open using the shared catalog.
            return pose::Recorder::open(directory / (name + ".spose"), text, options, hook);
        };
        std::uint64_t physical_bytes{}, overlay_bytes{}; // Actual retained winch/layout capacities learned from initial setup.
        // Prove synchronous ownership by corrupting every borrowed population after the first append.
        {
            auto recorder = open("mixed_transient"); // Common mixed-population success and borrow-lifetime scenario.
            require(bool(recorder), "mixed setup");
            physical_bytes = recorder->statistics().physical_reference_bytes;
            overlay_bytes = recorder->statistics().overlay_reference_bytes;
            require(physical_bytes >= 48 && physical_bytes % 24 == 0 && overlay_bytes >= 32 && overlay_bytes % 16 == 0,
                    "actual physical/layout capacities charged");
            workspace(recorder->statistics(), 3750, 2, physical_bytes, overlay_bytes, 3750);
            auto borrowed = first; // Caller-owned copy deliberately corrupted after the recorder has accepted it.
            require(bool(append(*recorder, borrowed)), "initial unavailable group");
            std::fill(borrowed.physical.begin(), borrowed.physical.end(), std::numeric_limits<double>::quiet_NaN());
            for (auto& value : borrowed.fluid) value.world_pose.fill(std::numeric_limits<double>::quiet_NaN()); // Every F row.
            for (auto& value : borrowed.winches) value.world_xyz_m.fill(std::numeric_limits<double>::quiet_NaN()); // Every winch row.
            for (auto& attachment : borrowed.attachments) { // Every borrowed interval and station population.
                attachment.interval.packet = 999;
                for (auto& value : attachment.samples) { // Current caller-owned source station to invalidate.
                    value.valid = true;
                    value.source_position_world_m.fill(std::numeric_limits<double>::quiet_NaN());
                    value.force_density_world_N_per_m.fill(std::numeric_limits<double>::quiet_NaN());
                }
            }
            require(recorder->statistics().accepted_frames == 1 && recorder->statistics().persisted_frames == 0, "all nested views copied");
            require(append(*recorder, second) && recorder->statistics().persisted_frames == 2, "first complete mixed group");
            require(append(*recorder, third) && recorder->finalize(), "decreasing opaque tokens preserve sparse interval facts");
            require(recorder->statistics().accepted_frames == 3 && recorder->statistics().persisted_frames == 3
                    && recorder->statistics().pose_chunks == 2, "mixed final counts");
            require(!append(*recorder, third) && !recorder->finalize() && !open("mixed_transient"), "terminal publication/no clobber");
        }
        // Each name selects an independent catalog and literal row sequence, including changed widths.
        for (const auto& name : {"without_fluid", "without_winches", "loads_only", "first_positive",
                                 "exact_prior_join", "zero_time_join", "zero_end_time", "prepared", "static",
                                 "prepared_then_positive", "masked", "all_unavailable", "signed_zero",
                                 "multiple_attachments_one_model", "permuted_station_ranges", "large_metadata_domains",
                                 "repeated_consumer_address", "attachment_k9"}) {
            const auto selected_catalog = fixture_catalog(fixture / (std::string(name) + ".spose")); // Variant's own topology.
            const auto& case_info = expected.at("cases").at(std::string(name) + ".spose"); // Manifest entry selecting its oracle.
            const auto& rows = expected.at(case_info.at("frames_key").get<std::string>()); // Borrowed expected source groups.
            auto limits = options; // Isolated two-group budget adjusted for this literal population width.
            // Width is the aggregate raw bytes of all enabled populations for one accepted group.
            const auto width = std::string(name) == "attachment_k9" ? 2164ULL :
                std::string(name) == "without_fluid" ? 1458ULL :
                std::string(name) == "without_winches" ? 1587ULL : std::string(name) == "loads_only" ? 1170ULL : 1875ULL;
            limits.max_batch_bytes = 2 * width;
            auto recorder = pose::Recorder::open(directory / (std::string(name) + ".spose"), selected_catalog.dump(), limits); // Variant recorder.
            require(bool(recorder), "independent positive catalog setup");
            workspace(recorder->statistics(), 2 * width, 2, selected_catalog.contains("winch_geometry") ? physical_bytes : 0,
                      overlay_bytes, 2 * width);
            for (const auto& row : rows) require(bool(append(*recorder, packet(row))), "independent positive row"); // Preserve literal source order.
            require(bool(recorder->finalize()), "independent positive finalize");
        }
        {
            auto recorder = open("empty_failed"); // Declared populations with no accepted source rows.
            require(recorder && recorder->finalize({pose::Outcome::failed, pose::Outcome::stopped}), "empty declared populations");
        }
        {
            auto recorder = open("abandoned"); // Destructor leaves persisted prefix; final buffered row is uncommitted.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "abandoned completed prefix");
        }

        // Every malformed positive packet must fail before any population or counter advances.
        for (int scenario = 0; scenario < 36; ++scenario) { // One isolated packet-admission defect per iteration.
            auto row = second; // Owned fresh positive packet; payload edits remain visible through the views below.
            auto attachments = attachment_views(row); // Mutable copied intervals and borrowed sample spans.
            pose::AppliedLoadsFrameView loads{row.stamp, row.generation, row.phase, attachments}; // Load identity/extent defect target.
            pose::FluidFrameView fluid{row.stamp, row.generation, row.phase, row.fluid}; // Other known population in the same group.
            pose::WinchFrameView winches{row.stamp, row.generation, row.phase, row.winches}; // Other known population in the same group.
            pose::FrameGroupView group{row.stamp, row.physical, &winches, &fluid, &loads}; // Joint append boundary under test.
            // Select exactly one defect. Each value loop changes both attachments together
            // unless the scenario deliberately tests their within-frame prefix disagreement.
            if (scenario == 0) loads.preparation_generation = 3;
            if (scenario == 1) loads.phase = pose::SourcePhase::static_certified;
            if (scenario == 2) loads.stamp.time += .001;
            if (scenario == 3) loads.stamp.state_id = 5;
            if (scenario == 4) loads.stamp.load_factor = -0.0;
            if (scenario == 5) loads.stamp.events = pose::Event::target;
            if (scenario == 6) group.applied_loads = nullptr;
            if (scenario == 7) loads.attachments = loads.attachments.first(1);
            if (scenario == 8) attachments[0].samples = attachments[0].samples.first(4);
            if (scenario == 9) attachments[0].interval.status = static_cast<pose::AppliedIntervalStatus>(2);
            if (scenario == 10) for (auto& value : attachments) value.interval = {};
            if (scenario == 11) for (auto& value : attachments) value.interval.source_accepted_id = 5;
            if (scenario == 12) for (auto& value : attachments) value.interval.source_accepted_id = 4;
            if (scenario == 13) for (auto& value : attachments) value.interval.source.time_s = 1.29;
            if (scenario == 14) for (auto& value : attachments) value.interval.end_accepted_id = 5;
            if (scenario == 15) for (auto& value : attachments) value.interval.end_time_s = 1.291;
            if (scenario == 16) ++attachments[1].interval.prepared_step_sequence;
            if (scenario == 17) ++attachments[1].interval.source.publication;
            if (scenario == 18) row.attachments[0].samples[0].source_position_world_m[0] = std::numeric_limits<double>::infinity();
            if (scenario == 19) row.attachments[1].samples[2].force_density_world_N_per_m[2] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 20) row.attachments[0].samples[0].valid = false;
            if (scenario == 21) { row.attachments[0].samples[0] = {}; row.attachments[0].samples[0].source_position_world_m[0] = -0.0; }
            if (scenario == 22) for (auto& value : attachments) value.interval.source.publication = 0;
            if (scenario == 23) for (auto& value : attachments) value.interval.prepared_step_sequence = 0;
            if (scenario == 24) for (auto& value : attachments) value.interval.packet = 0;
            if (scenario == 25) for (auto& value : attachments) value.interval.packet = std::numeric_limits<std::uint64_t>::max();
            if (scenario == 26) for (auto& value : attachments) { value.interval.source_accepted_id = 0; value.interval.source.time_s = 1.251; }
            if (scenario == 27) for (auto& value : attachments) { value.interval.source_accepted_id = 1; value.interval.source.time_s = 1.25; }
            if (scenario == 28) group.fluid_poses = nullptr;
            if (scenario == 29) group.winches = nullptr;
            if (scenario == 30) row.physical[0] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 31) row.fluid[0].world_pose[3] = 2;
            if (scenario == 32) row.winches[0].world_xyz_m[0] = std::numeric_limits<double>::infinity();
            if (scenario == 33) loads.stamp.analysis = pose::Analysis::static_equilibrium;
            if (scenario == 34) loads.preparation_generation = 0;
            if (scenario == 35) for (auto& value : attachments) value.interval.source.time_s = std::numeric_limits<double>::quiet_NaN();
            auto recorder = open("bad_packet_" + std::to_string(scenario)); // Fresh recorder with one valid buffered predecessor.
            require(recorder && append(*recorder, first), "bad packet prior accepted row");
            require(!recorder->append_frame(group), "bad nested packet rejects atomically");
            require(recorder->statistics().accepted_frames == 1 && recorder->statistics().persisted_frames == 0
                    && recorder->statistics().buffered_frames == 1, "bad packet counters unchanged");
            require(recorder->state() == pose::State::failed && !recorder->finalize(), "bad packet failure latch");
        }
        // Reject unavailable metadata/payload deviations, including noncanonical negative zero.
        for (int scenario = 0; scenario < 6; ++scenario) { // Selected unavailable-field defect.
            auto row = first; // Status0 permits only literal positive-zero unused source metadata and payload.
            for (auto& attachment : row.attachments) { // Apply the same unavailable defect to each complete attachment.
                if (scenario == 0) attachment.interval.source_accepted_id = 1;
                if (scenario == 1) attachment.interval.source.time_s = -0.0;
                if (scenario == 2) attachment.interval.packet = 1;
                if (scenario == 3) attachment.samples[0].valid = true;
                if (scenario == 4) attachment.samples[0].source_position_world_m[0] = -0.0;
                if (scenario == 5) attachment.samples[0].force_density_world_N_per_m[1] = 1;
            }
            auto recorder = open("bad_unavailable_" + std::to_string(scenario)); // No accepted row may precede this rejection.
            require(recorder && !append(*recorder, row) && recorder->statistics().accepted_frames == 0, "status0 canonical admission");
        }
        // File/API rejection is tested here; no claim of numerical solver rollback is made.
        for (int scenario = 0; scenario < 7; ++scenario) { // Selected stale/failed-candidate interval attempt.
            auto row = third; // Malicious completed/candidate attempts following a genuinely persisted group.
            for (auto& attachment : row.attachments) { // Keep common prefix identity except the explicit last mismatch.
                if (scenario == 0) attachment.interval = second.attachments[0].interval;
                if (scenario == 1) { attachment.interval.source_accepted_id = 3; attachment.interval.source.time_s = 1.28; }
                if (scenario == 2) { attachment.interval.source_accepted_id = 4; attachment.interval.source.time_s = 1.291; }
                if (scenario == 3) { attachment.interval.source_accepted_id = 5; attachment.interval.source.time_s = 1.29; }
                if (scenario == 4) { attachment.interval.end_accepted_id = 8; attachment.interval.end_time_s = 1.33; }
                if (scenario == 5) attachment.interval = {};
            }
            if (scenario == 6) row.attachments[1].interval = second.attachments[1].interval;
            auto recorder = open("bad_completed_" + std::to_string(scenario)); // Already-persisted source prefix must remain unchanged.
            require(recorder && append(*recorder, first) && append(*recorder, second), "bad completed prior group");
            require(!append(*recorder, row) && recorder->statistics().accepted_frames == 2
                    && recorder->statistics().persisted_frames == 2, "stale/candidate payload rejected without relabeling");
        }
        {
            // Equal accepted identities and interval endpoints require exact time bits, including zero sign.
            auto initial = packet(expected.at("zero_time_join_frames").at(0)); // Prior accepted negative-zero time.
            auto row = packet(expected.at("zero_time_join_frames").at(1)); // Positive interval whose join sign is corrupted.
            for (auto& attachment : row.attachments) attachment.interval.source.time_s = 0.0; // Both interval prefixes.
            auto recorder = open("bad_zero_join"); // Prior-source signed-zero comparison.
            require(recorder && append(*recorder, initial) && !append(*recorder, row), "equal accepted ID preserves zero time sign");
            row = packet(expected.at("zero_end_frames").at(0));
            for (auto& attachment : row.attachments) attachment.interval.end_time_s = -0.0; // Disagree with structural endpoint sign.
            auto end = open("bad_zero_end"); // Within-group signed-zero comparison.
            require(end && !append(*end, row), "endpoint time bit equality");
        }
        {
            // Legacy overloads cannot silently omit a declared load population; undeclared loads also fail.
            auto old_base = open("old_base_overload"), old_winch = open("old_winch_overload"); // Separate omission paths.
            const pose::WinchFrameView view{first.stamp, first.generation, first.phase, first.winches}; // Borrowed legacy argument.
            require(old_base && !old_base->append_frame(first.stamp, first.physical), "old base overload rejects loads");
            require(old_winch && !old_winch->append_frame(first.stamp, first.physical, view), "old winch overload rejects loads");
            auto plain = catalog; // Setup copy deliberately lacking the load descriptor.
            plain.erase("applied_loads");
            auto extra = pose::Recorder::open(directory / "unexpected_loads.spose", plain.dump(), options); // Undeclared population case.
            require(extra && !append(*extra, first), "unexpected load population rejected");
        }
        // Reuse independently malformed catalog facts without importing the fixture generator.
        for (const auto& [name, info] : expected.at("cases").items()) { // Fixture filename and classification entry.
            const auto reason = info.value("rejection_reason", std::string{}); // Select setup defects, not wire-only negatives.
            if (name == "missing_descriptor.spose" ||
                (reason != "applied catalog contract" && reason != "model attachment/range uniqueness")) continue;
            const auto path = directory / ("bad_catalog_" + name); // Destination whose absence proves pre-file rejection.
            const auto bad = fixture_catalog(fixture / name); // Owned malformed catalog from a literal independent file.
            require(!pose::Recorder::open(path, bad.dump(), options), "independent invalid catalog rejected");
            require(!std::filesystem::exists(path) && !std::filesystem::exists(path.string() + ".partial"), "invalid setup creates no file");
        }
        {
            auto bad = catalog; // Distinct JSON integer coordinates collapse to one binary64 physical address.
            bad["applied_loads"]["stations"][2]["material_coordinate_m"] = 9007199254740992ULL;
            bad["applied_loads"]["stations"][3]["material_coordinate_m"] = 9007199254740993ULL;
            bad["applied_loads"]["stations"][3]["physical_side_token"] = 201;
            const auto path = directory / "bad_catalog_binary64_coordinate.spose"; // Explicit rounded-coordinate admission case.
            require(!pose::Recorder::open(path, bad.dump(), options), "binary64-equivalent logical coordinates reject duplicate token");
            require(!std::filesystem::exists(path) && !std::filesystem::exists(path.string() + ".partial"),
                    "binary64 duplicate rejected before file creation");
        }
        {
            // Compare exact one-group admission against one-byte-short raw/aggregate and scratch-only limits.
            auto limits = options; // Independent budget mutations, reset before changing a different bound.
            limits.max_batch_frames = 100;
            limits.max_workspace_bytes = physical_bytes + overlay_bytes + 2048 + 1875;
            auto exact = pose::Recorder::open(directory / "workspace_one.spose", text, limits); // All charges leave one whole raw group.
            require(bool(exact), "exact all-charges workspace admits one group");
            workspace(exact->statistics(), 1875, 1, physical_bytes, overlay_bytes, 1875);
            require(append(*exact, first) && exact->finalize(), "exact workspace output");
            --limits.max_workspace_bytes;
            require(!pose::Recorder::open(directory / "workspace_too_small.spose", text, limits), "one byte below whole workspace rejects");
            limits = options;
            limits.max_batch_bytes = 1875;
            auto raw = pose::Recorder::open(directory / "raw_one.spose", text, limits); // Raw-only bound admits exactly one group.
            require(bool(raw), "exact one raw group admits");
            workspace(raw->statistics(), 1875, 1, physical_bytes, overlay_bytes, 1875);
            require(append(*raw, first) && raw->finalize(), "one raw group output");
            --limits.max_batch_bytes;
            require(!pose::Recorder::open(directory / "raw_too_small.spose", text, limits), "short raw group rejects");
            limits = options;
            limits.max_workspace_bytes = physical_bytes + overlay_bytes + 2047;
            require(!pose::Recorder::open(directory / "scratch_too_small.spose", text, limits), "both layout charges precede scratch subtraction");
        }

        // All four group chunks and terminal records must retain honest counters on failed I/O.
        // short_write selects outright versus torn writes; occurrence selects one-based finalization writes.
        for (const bool short_write : {false, true}) for (std::uint64_t occurrence = 1; occurrence <= 13; ++occurrence) {
            Fault fault; // Scenario-owned hook context, declared before and outliving its recorder.
            fault.short_write = short_write;
            fault.occurrence = occurrence;
            const auto name = std::string(short_write ? "short_write_" : "write_") + std::to_string(occurrence); // Unique evidence stem.
            auto recorder = open(name, {&fault, fail, limit}); // Initially unarmed hook preserves the prior complete prefix.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "write-fault prefix");
            fault.enabled = true;
            require(!recorder->finalize() && recorder->state() == pose::State::failed, "selected write failure latches");
            require(recorder->statistics().accepted_frames == 3
                    && recorder->statistics().persisted_frames == (occurrence <= 8 ? 2 : 3)
                    && recorder->statistics().pose_chunks == (occurrence <= 8 ? 1 : 2), "write whole-group commit counters");
        }
        // Each flush follows emitted bytes; Python independently measures readable progress beyond committed counters.
        for (std::uint64_t occurrence = 1; occurrence <= 7; ++occurrence) { // One-based finalization flush boundary.
            Fault fault; // Scenario-owned context armed only after accepting the third row.
            fault.operation = pose::IoOperation::flush;
            fault.occurrence = occurrence;
            auto recorder = open("flush_" + std::to_string(occurrence), {&fault, fail, limit}); // Retains failed-flush evidence.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "flush-fault prefix");
            fault.enabled = true;
            require(!recorder->finalize() && recorder->state() == pose::State::failed, "selected flush failure latches");
            require(recorder->statistics().persisted_frames == (occurrence <= 4 ? 2 : 3)
                    && recorder->statistics().pose_chunks == (occurrence <= 4 ? 1 : 2), "flush whole-group commit counters");
        }
        // Terminal failures preserve complete wire facts while withholding successful publication.
        for (const auto operation : {pose::IoOperation::close, pose::IoOperation::publish}) { // Selected terminal boundary.
            Fault fault; // Context retained until recorder destruction completes.
            fault.operation = operation;
            auto recorder = open(operation == pose::IoOperation::close ? "close" : "publish", {&fault, fail, limit}); // Terminal-failure owner.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "late-fault prefix");
            fault.enabled = true;
            require(!recorder->finalize() && recorder->statistics().persisted_frames == 3, "late failure preserves complete wire facts");
        }
        {
            auto recorder = open("collision"); // Destination is initially free, then claimed before publication.
            require(recorder && append(*recorder, first), "collision setup");
            { std::ofstream competing(directory / "collision.spose"); competing << "do not replace"; } // Competing owner's sentinel bytes.
            require(!recorder->finalize(), "racing destination wins unchanged");
        }
        {
            // Reuse fixed packet storage while source identities advance and opaque tokens cycle.
            auto recorder = open("long_bounded"); // Duration-independent retained-workspace scenario.
            require(bool(recorder), "bounded load run setup");
            auto row = first; // Reused owned packet, switching from unavailable to completed interval once.
            double prior_time = 1.25; // Exact last accepted time for the next interval's prior-source join.
            for (std::uint64_t index = 0; index < 1001; ++index) { // Accepted identity and corresponding output row ordinal here.
                if (index == 1) row.attachments = second.attachments;
                row.stamp = {pose::Analysis::transient, index == 0 ? 2U : (index == 1000 ? 8U : 4U),
                             1.25 + .01 * static_cast<double>(index), index, 0};
                row.phase = index ? pose::SourcePhase::transient_accepted : pose::SourcePhase::transient_initial;
                if (index) for (auto& attachment : row.attachments) { // Give both attachments the same accepted interval prefix.
                    attachment.interval.source_accepted_id = index - 1;
                    attachment.interval.source.time_s = prior_time;
                    attachment.interval.end_accepted_id = index;
                    attachment.interval.end_time_s = row.stamp.time;
                    attachment.interval.source.publication = 9 - index % 7;
                    attachment.interval.prepared_step_sequence = 3 + index % 5;
                    attachment.interval.packet = 17 - index % 11;
                }
                require(bool(append(*recorder, row)), "bounded accepted row and opaque identity reuse");
                prior_time = row.stamp.time;
            }
            workspace(recorder->statistics(), 3750, 2, physical_bytes, overlay_bytes, 3750);
            require(recorder->finalize() && recorder->statistics().persisted_frames == 1001
                    && recorder->statistics().pose_chunks == 501, "bounded duration-independent load storage");
        }
        std::cout << "applied recorder checks passed: " << checks << "; output=" << directory.string() << '\n';
        return 0;
    } catch (const std::exception& failure) { // First assertion, input, or setup exception; retain all existing evidence.
        std::cerr << "applied recorder: " << failure.what() << '\n';
        return 1;
    }
}
