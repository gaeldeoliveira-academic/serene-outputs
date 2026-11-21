#include <algorithm>
#include <array>
#include <cmath>
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
std::uint64_t checks{}; ///< Successful assertions in this invocation only.

/// Count a successful condition, or throw the borrowed diagnostic on failure.
void require(bool condition, const char* diagnostic) {
    // Stop at the first broken contract, retaining this invocation's evidence.
    if (!condition) throw std::runtime_error(diagnostic);
    ++checks;
}

/// Owned independent source packet whose buffers back only synchronous borrowed views.
/// Load literal expected rows, mutate owned copies for negatives, and submit complete groups.
struct Packet {
    pose::Stamp stamp; ///< Actual source state, independent of recorder ordinal.
    std::uint64_t generation{}; ///< Owning preparation identity, never a capture-plan counter.
    pose::SourcePhase phase{}; ///< Initialized, certified, initial or accepted source state.
    std::vector<double> physical; ///< P8 physical xyz/wxyz scalars in unchanged catalog order.
    std::vector<pose::WinchGeometry> winches; ///< Two independently authored A/B/C/D arrangements.
    std::vector<pose::FluidPose> fluid; ///< F rows with caller-owned validity and exact world poses.

    /// Return a temporary view borrowing every winch row until the next mutation or destruction.
    pose::WinchFrameView winch_view() const {
        return {stamp, generation, phase, winches};
    }

    /// Return a temporary view borrowing every F row until the next mutation or destruction.
    pose::FluidFrameView fluid_view() const {
        return {stamp, generation, phase, fluid};
    }
};

/// Translate one independent JSON oracle into owned public API input; malformed fixtures throw.
Packet packet(const Json& row) {
    // Copy actual source facts and three independent value populations without evaluating geometry.
    Packet result; // Owner of every submitted scalar and mask flag.
    const auto& stamp = row.at("stamp"); // Borrowed literal source stamp.
    result.stamp = {static_cast<pose::Analysis>(stamp.at("analysis").get<std::uint32_t>()),
                    stamp.at("events").get<std::uint32_t>(), stamp.at("time").get<double>(),
                    stamp.at("state_id").get<std::uint64_t>(), stamp.at("load_factor").get<double>()};
    const auto& fluid = row.at("fluid_poses"); // Independently authored F identity and poses.
    result.generation = fluid.at("generation").get<std::uint64_t>();
    result.phase = static_cast<pose::SourcePhase>(fluid.at("phase").get<std::uint32_t>());
    for (const auto& sample : row.at("poses"))
        for (const auto& value : sample) result.physical.push_back(value.get<double>());
    if (row.contains("features")) {
        for (const auto& feature : row.at("features")) { // One unchanged J07 arrangement.
            pose::WinchGeometry geometry{}; // Owned flattening of literal A/B/C/D coordinates.
            geometry.geometry = static_cast<pose::WinchGeometryStatus>(feature.at("geometry_status").get<std::uint32_t>());
            std::size_t index{}; // Next xyz scalar in the twelve-value row.
            for (const auto& point : feature.at("world_xyz_m"))
                for (const auto& value : point) geometry.world_xyz_m.at(index++) = value.get<double>();
            require(index == 12, "fixture winch shape");
            result.winches.push_back(geometry);
        }
    }
    for (const auto& sample : fluid.at("samples")) { // One literal F row, independent of physical P8.
        pose::FluidPose value{}; // Canonical default storage is overwritten by every supplied scalar.
        value.valid = sample.at("valid").get<bool>();
        value.world_pose = sample.at("world_pose").get<std::array<double, 7>>();
        result.fluid.push_back(value);
    }
    return result;
}

/// Submit a complete packet; optional flags select only populations admitted by its catalog.
serene::outputs::Status append(pose::Recorder& recorder, const Packet& row,
                               bool with_winches = true, bool with_fluid = true) {
    // Views and their pointer targets remain alive for the entire synchronous public call.
    const auto winch = row.winch_view(); // Local borrowed winch identity/population.
    const auto fluid = row.fluid_view(); // Local borrowed F identity/population.
    return recorder.append_frame({row.stamp, row.physical,
                                  with_winches ? &winch : nullptr, with_fluid ? &fluid : nullptr});
}

/// Borrowed deterministic fault state, alive until its Recorder has been destroyed.
struct Fault {
    bool enabled{}; ///< Arm after a valid prior group and one buffered later frame.
    pose::IoOperation operation{pose::IoOperation::write}; ///< Selected failure boundary.
    std::uint64_t seen{}; ///< Matching operations seen since arming.
    std::uint64_t occurrence{1}; ///< One-based selected operation to fail.
    bool short_write{}; ///< Physically truncate the selected write instead of rejecting it outright.
};

/// Count matching armed operations; reject exactly the selected call unless it is a short write.
bool fail(void* context, pose::IoOperation operation, std::uint64_t) noexcept {
    auto& fault = *static_cast<Fault*>(context); // Borrowed invocation-owned fault settings.
    if (!fault.enabled || operation != fault.operation) return false;
    ++fault.seen;
    return !fault.short_write && fault.seen == fault.occurrence;
}

/// Physically retain at most17 bytes of one selected write; preserve every other requested length.
std::size_t limit(void* context, std::uint64_t, std::size_t requested) noexcept {
    const auto& fault = *static_cast<const Fault*>(context); // Counter advanced by fail for this write.
    return fault.enabled && fault.short_write && fault.seen == fault.occurrence
        ? std::min<std::size_t>(17, requested) : requested;
}

/// Assert exact accounting relationships, leaving actual vector reference capacity observable.
void workspace(const pose::Statistics& stats, std::uint64_t raw, std::uint64_t frames,
               std::uint64_t references, std::uint64_t budget) {
    // All charges describe payload workspace, not RSS, initialization JSON or caller buffers.
    require(stats.batch_capacity_bytes == raw && stats.effective_batch_frames == frames, "raw admission");
    require(stats.physical_reference_bytes == references, "physical reference capacity charge");
    require(stats.encoding_workspace_bytes == 2048 && stats.index_workspace_bytes == 256, "fixed scratch charge");
    require(stats.workspace_capacity_bytes == raw + references + 2048, "total workspace charge");
    require(stats.effective_raw_budget_bytes == budget, "effective raw budget");
}
} // namespace

/// Produce focused F-enabled recordings from independent catalog/expected files.
/// argv supplies NEW_OUTPUT_DIR, catalog.json, expected.json. No build or solver is invoked.
/// Return zero only after public API, lifecycle, workspace and deterministic I/O assertions pass.
int main(int argc, char** argv) {
    // Load independent source facts and establish bounded fresh output ownership.
    // Exercise complete packets, exact representations, setup rejection and latching failures.
    // Retain files for a separate Python decoder and byte-level structural-link review.
    try {
        if (argc != 4) return 2;
        const std::filesystem::path directory = argv[1]; // Invocation-owned destination, never reused.
        require(std::filesystem::create_directory(directory), "output directory must be new");
        std::ifstream catalog_input(argv[2]), expected_input(argv[3]); // Read-only fixture streams.
        Json catalog, expected; // Owned immutable setup description and independent literal oracles.
        catalog_input >> catalog;
        expected_input >> expected;
        const auto text = catalog.dump(); // Stable borrowed JSON for synchronous setup calls.
        const auto first = packet(expected.at("transient_frames").at(0)); // Initial accepted step0.
        const auto second = packet(expected.at("transient_frames").at(1)); // Sparse scheduled step4.
        const auto third = packet(expected.at("transient_frames").at(2)); // Off-stride terminal step7.
        require(first.physical.size() == 56 && first.winches.size() == 2 && first.fluid.size() == 6, "P8 W2 K6 fixture");
        pose::Options options; // Two complete1217-byte F/winch/physical frame groups.
        options.max_batch_frames = 2;
        options.max_batch_bytes = 2434;
        require(options.max_workspace_bytes == 128ULL * 1024 * 1024, "default workspace ceiling");
        const auto open = [&](const std::string& name, pose::FaultHook hook = {}) {
            return pose::Recorder::open(directory / (name + ".spose"), text, options, hook);
        }; // Open one fresh scenario with the same immutable literal catalog.
        std::uint64_t reference_bytes{}; // Actual admitted reference-table capacity, reused for exact caps.
        {
            auto recorder = open("transient"); // Real producer of three independent source states.
            require(bool(recorder), "transient setup");
            reference_bytes = recorder->statistics().physical_reference_bytes;
            require(reference_bytes >= 48 && reference_bytes % 24 == 0, "two winch reference triples");
            workspace(recorder->statistics(), 2434, 2, reference_bytes, 2434);
            auto borrowed = first; // Invalidated while its first accepted row is still buffered.
            require(bool(append(*recorder, borrowed)), "initial group");
            std::fill(borrowed.physical.begin(), borrowed.physical.end(), std::numeric_limits<double>::quiet_NaN());
            for (auto& row : borrowed.winches) row.world_xyz_m.fill(std::numeric_limits<double>::quiet_NaN());
            for (auto& row : borrowed.fluid) { row.valid = false; row.world_pose.fill(std::numeric_limits<double>::quiet_NaN()); }
            require(recorder->statistics().accepted_frames == 1 && recorder->statistics().persisted_frames == 0,
                    "all borrowed rows copied before return");
            require(append(*recorder, second) && recorder->statistics().persisted_frames == 2, "whole group persisted");
            require(append(*recorder, third) && recorder->finalize({pose::Outcome::completed, pose::Outcome::completed}), "target final group");
            require(recorder->statistics().accepted_frames == 3 && recorder->statistics().persisted_frames == 3
                    && recorder->statistics().pose_chunks == 2, "final structural-only counters");
            require(recorder->state() == pose::State::published && !append(*recorder, third)
                    && !recorder->finalize(), "published lifecycle terminal");
            require(!open("transient"), "published destination preserved");
        }
        for (const auto& name : {"static", "prepared", "masked", "all_unavailable"}) {
            auto recorder = open(name); // One independent phase or validity scenario.
            require(bool(recorder), "phase/mask scenario setup");
            for (const auto& row : expected.at(std::string(name) + "_frames"))
                require(bool(append(*recorder, packet(row))), "phase/mask row");
            require(bool(recorder->finalize()), "phase/mask finalize");
        }
        {
            auto recorder = open("prepared_then_transient"); // Diagnostic clock100 may precede initial clock1.25.
            require(recorder && append(*recorder, packet(expected.at("prepared_frames").at(0)))
                    && append(*recorder, first) && append(*recorder, third) && recorder->finalize(), "prepared transition");
        }
        {
            auto recorder = open("initial_disabled"); // First capture is a positive accepted state.
            require(recorder && append(*recorder, second) && append(*recorder, third) && recorder->finalize(), "initial omitted");
        }
        {
            auto no_winch = catalog; // F-enabled catalog with no declared winch population.
            no_winch.erase("winch_geometry");
            auto recorder = pose::Recorder::open(directory / "without_winches.spose", no_winch.dump(), options);
            require(bool(recorder), "F-only setup");
            workspace(recorder->statistics(), 1858, 2, 0, 2434);
            for (const auto& row : expected.at("no_winch_frames"))
                require(bool(append(*recorder, packet(row), false)), "F-only row");
            require(bool(recorder->finalize()), "F-only finalize");
        }
        {
            auto row = first; // Valid F values must preserve signs and non-normalized accepted quaternion bits.
            row.fluid[0].world_pose = {-0.0, 0.0, -0.0, -1.0, -0.0, 0.0, -0.0};
            row.fluid[1].world_pose[3] = std::nextafter(.5, 1.0);
            auto recorder = open("signed_zero");
            require(recorder && append(*recorder, row) && recorder->finalize(), "valid F representation fidelity");
        }
        for (std::size_t count : {7U, 8U, 9U}) { // Packed mask transitions and unaligned binary64 payload starts.
            auto larger = catalog; // Extra declared stations extend only the second F line.
            for (std::size_t id = 6; id < count; ++id) {
                larger["fluid_poses"]["samples"].push_back({{"id", id}, {"line_id", 1},
                    {"material_coordinate_m", static_cast<double>(id - 2)}, {"side", "left"}, {"native_element", 0}});
                larger["fluid_poses"]["segments"][2]["sample_ids"].push_back(id);
            }
            auto limits = options; // Admit exactly two larger complete groups.
            const auto group_width = 800 + 80 + (count + 7) / 8 + 56 * count;
            limits.max_batch_bytes = 2 * group_width;
            auto recorder = pose::Recorder::open(directory / ("k" + std::to_string(count) + ".spose"), larger.dump(), limits);
            require(bool(recorder), "mask-boundary population setup");
            workspace(recorder->statistics(), 2 * group_width, 2, reference_bytes, 2 * group_width);
            for (const auto& row : expected.at("k" + std::to_string(count) + "_frames"))
                require(bool(append(*recorder, packet(row))), "mask-boundary exact row");
            require(bool(recorder->finalize()), "mask-boundary finalize");
        }
        {
            auto recorder = open("empty_failed"); // A declared population may have no captured states.
            require(recorder && recorder->finalize({pose::Outcome::failed, pose::Outcome::stopped}), "empty failed file");
        }
        {
            auto recorder = open("abandoned"); // Destructor cannot publish or persist a buffered final group.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "abandoned prefix");
        }

        // Reject every malformed complete group before admitting any of its populations.
        for (int scenario = 0; scenario < 29; ++scenario) {
            auto row = second; // Good source packet, locally corrupted after an earlier accepted row.
            auto winch = row.winch_view(); // Borrowed winch stamp can differ independently from F and physical.
            auto fluid = row.fluid_view(); // Borrowed F stamp and extent can differ independently.
            pose::FrameGroupView group{row.stamp, row.physical, &winch, &fluid}; // Complete candidate group.
            if (scenario == 0) fluid.preparation_generation = 0;
            if (scenario == 1) fluid.preparation_generation = 3;
            if (scenario == 2) fluid.phase = pose::SourcePhase::static_certified;
            if (scenario == 3) fluid.phase = static_cast<pose::SourcePhase>(4);
            if (scenario == 4) fluid.stamp.time = std::nextafter(row.stamp.time, 2.0);
            if (scenario == 5) fluid.stamp.state_id = 5;
            if (scenario == 6) fluid.stamp.events = pose::Event::target;
            if (scenario == 7) fluid.stamp.analysis = pose::Analysis::static_equilibrium;
            if (scenario == 8) fluid.stamp.load_factor = -0.0;
            if (scenario == 9) fluid.poses = fluid.poses.first(5);
            if (scenario == 10) group.fluid_poses = nullptr;
            if (scenario == 11) group.winches = nullptr;
            if (scenario == 12) row.fluid[5].world_pose[2] = std::numeric_limits<double>::infinity();
            if (scenario == 13) row.fluid[5].world_pose[6] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 14) row.fluid[0].world_pose[3] = 2;
            if (scenario == 15) row.fluid[0].valid = false;
            if (scenario == 16) { row.fluid[0] = {}; row.fluid[0].world_pose[0] = -0.0; }
            if (scenario == 17) { row.fluid[0] = {}; row.fluid[0].world_pose[3] = -1.0; }
            if (scenario == 18) { row.fluid[0] = {}; row.fluid[0].world_pose[6] = -0.0; }
            if (scenario == 19) row.physical[0] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 20) row.winches[1].world_xyz_m[6] = std::numeric_limits<double>::infinity();
            if (scenario == 21) winch.preparation_generation = 3;
            if (scenario == 22) group.structural_poses = group.structural_poses.first(55);
            if (scenario == 23) { group.stamp.time = first.stamp.time; fluid.stamp = winch.stamp = group.stamp; }
            if (scenario == 24) { group.stamp.state_id = 0; fluid.stamp = winch.stamp = group.stamp; }
            if (scenario == 25) { group.stamp.events = pose::Event::initial; fluid.stamp = winch.stamp = group.stamp; }
            if (scenario == 26) { group.stamp.load_factor = 1; fluid.stamp = winch.stamp = group.stamp; }
            if (scenario == 27) winch.stamp.time = std::nextafter(row.stamp.time, 2.0);
            if (scenario == 28) fluid.preparation_generation = std::numeric_limits<std::uint64_t>::max();
            auto recorder = open("bad_packet_" + std::to_string(scenario)); // Separate failure latch per invalid packet.
            require(recorder && append(*recorder, first), "bad group prefix");
            require(!recorder->append_frame(group), "bad group rejected");
            require(recorder->statistics().accepted_frames == 1 && recorder->statistics().buffered_frames == 1
                    && recorder->statistics().persisted_frames == 0 && recorder->statistics().pose_chunks == 0,
                    "group rejection leaves all counters unchanged");
            require(recorder->state() == pose::State::failed && !append(*recorder, second)
                    && !recorder->finalize(), "group failure latches");
        }
        {
            auto old_base = open("old_base_overload"); // Legacy overloads cannot silently discard enabled F.
            auto old_winch = open("old_winch_overload");
            require(old_base && !old_base->append_frame(first.stamp, first.physical), "old base rejects F");
            require(old_winch && !old_winch->append_frame(first.stamp, first.physical, first.winch_view()), "old winch rejects F");
            auto plain = catalog; // Extra population pointers are rejected when descriptors are absent.
            plain.erase("fluid_poses");
            auto extra = pose::Recorder::open(directory / "unexpected_fluid.spose", plain.dump(), options);
            require(extra && !append(*extra, first), "unexpected F rejected");
            auto no_winch = catalog;
            no_winch.erase("winch_geometry");
            auto extra_winch = pose::Recorder::open(directory / "unexpected_winch.spose", no_winch.dump(), options);
            require(extra_winch && !append(*extra_winch, first), "unexpected winch rejected");
        }
        for (int scenario = 0; scenario < 28; ++scenario) { // One isolated immutable catalog error per setup.
            auto bad = catalog; // Retain unrelated P8 topology and valid J07 arrangements.
            auto& f = bad["fluid_poses"]; // Mutable F descriptor for this isolated negative.
            if (scenario == 0) f["kind"] = 100;
            if (scenario == 1) f["stream_id"] = 42;
            if (scenario == 2) f["layout_version"] = 2;
            if (scenario == 3) f["encoding"] = "other";
            if (scenario == 4) f["source_preparation_generation"] = 3;
            if (scenario == 5) f["source_preparation_generation"] = 0;
            if (scenario == 6) f["source_preparation_generation"] = 2.0;
            if (scenario == 7) f["lines"][0]["occurrence_id"] = 20;
            if (scenario == 8) f["lines"][0]["native_element_count"] = 0;
            if (scenario == 9) f["lines"][1]["occurrence_id"] = 21;
            if (scenario == 10) f["lines"][0]["spanline_id"] = "";
            if (scenario == 11) f["lines"][0]["source_geometry_fingerprint"] = "";
            if (scenario == 12) f["samples"][0]["line_id"] = false;
            if (scenario == 13) f["samples"][0]["id"] = 0.0;
            if (scenario == 14) f["samples"][0]["native_element"] = 2;
            if (scenario == 15) f["samples"][0]["material_coordinate_m"] = -1;
            if (scenario == 16) f["samples"][0]["side"] = "none";
            if (scenario == 17) f["samples"][2]["side"] = "left";
            if (scenario == 18) f["segments"][0]["sample_ids"] = Json::array({0, 99});
            if (scenario == 19) f["segments"][0]["sample_ids"] = Json::array({0, 4});
            if (scenario == 20) f["segments"][0]["sample_ids"] = Json::array({1, 0});
            if (scenario == 21) f["segments"].erase(2);
            if (scenario == 22) f["samples"] = Json::array();
            if (scenario == 23) f["lines"][0]["effective_geometry_fingerprint"] = std::string("bad\0text", 8);
            if (scenario == 24) f["source_preparation_generation"] = std::numeric_limits<std::uint64_t>::max();
            if (scenario == 25) bad["applied_loads"] = Json::object();
            if (scenario == 26) bad["elastic_moments"] = Json::object();
            if (scenario == 27) bad["inflow_planes"] = Json::object();
            const auto path = directory / ("bad_catalog_" + std::to_string(scenario) + ".spose");
            require(!pose::Recorder::open(path, bad.dump(), options), "invalid F catalog rejected");
            require(!std::filesystem::exists(path) && !std::filesystem::exists(path.string() + ".partial"),
                    "catalog failure before file creation");
        }
        {
            auto limits = options; // Exact raw and whole-workspace admission, with finite arithmetic.
            limits.max_batch_frames = 100;
            limits.max_workspace_bytes = reference_bytes + 2048 + 1217;
            auto recorder = pose::Recorder::open(directory / "workspace_one.spose", text, limits);
            require(bool(recorder), "exact one-group workspace admitted");
            workspace(recorder->statistics(), 1217, 1, reference_bytes, 1217);
            require(append(*recorder, first) && recorder->finalize(), "one-group workspace output");
            --limits.max_workspace_bytes;
            require(!pose::Recorder::open(directory / "workspace_too_small.spose", text, limits), "workspace cannot fit group");
            limits = options;
            limits.max_batch_bytes = 1217;
            auto raw = pose::Recorder::open(directory / "raw_one.spose", text, limits);
            require(bool(raw), "exact one-group raw budget admitted");
            workspace(raw->statistics(), 1217, 1, reference_bytes, 1217);
            require(append(*raw, first) && raw->finalize(), "one-group raw output");
            limits.max_batch_bytes = 1216;
            require(!pose::Recorder::open(directory / "raw_too_small.spose", text, limits), "raw cannot fit group");
            limits = options;
            limits.max_workspace_bytes = 128ULL * 1024 * 1024 + 1;
            require(!pose::Recorder::open(directory / "workspace_overflow.spose", text, limits), "workspace hard ceiling");
            limits.max_workspace_bytes = 2047;
            require(!pose::Recorder::open(directory / "scratch_too_small.spose", text, limits), "scratch budget subtraction");
            limits = options;
            limits.max_batch_frames = std::numeric_limits<std::uint64_t>::max();
            require(!pose::Recorder::open(directory / "count_overflow.spose", text, limits), "batch count overflow");
        }

        // Fail every F/winch/pose and terminal-record write after one complete prior group.
        for (const bool short_write : {false, true}) {
            for (std::uint64_t occurrence = 1; occurrence <= 11; ++occurrence) {
                Fault fault; // Borrowed callback state outlives the recorder in this scope.
                fault.occurrence = occurrence;
                fault.short_write = short_write;
                const auto name = std::string(short_write ? "short_write_" : "write_") + std::to_string(occurrence);
                auto recorder = open(name, {&fault, fail, limit});
                require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "write-fault prefix");
                fault.enabled = true;
                require(!recorder->finalize() && recorder->state() == pose::State::failed, "selected write failure latches");
                const auto stats = recorder->statistics(); // Counters advance only after all three chunks flush.
                require(stats.accepted_frames == 3 && stats.persisted_frames == (occurrence <= 6 ? 2 : 3)
                        && stats.pose_chunks == (occurrence <= 6 ? 1 : 2), "write failure whole-group counters");
                require(!std::filesystem::exists(directory / (name + ".spose")), "write failure unpublished");
            }
        }
        for (std::uint64_t occurrence = 1; occurrence <= 6; ++occurrence) {
            Fault fault; // F, winch, pose, index, completion, and footer flush failures.
            fault.operation = pose::IoOperation::flush;
            fault.occurrence = occurrence;
            auto recorder = open("flush_" + std::to_string(occurrence), {&fault, fail, limit});
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "flush-fault prefix");
            fault.enabled = true;
            require(!recorder->finalize() && recorder->state() == pose::State::failed, "selected flush failure latches");
            require(recorder->statistics().persisted_frames == (occurrence <= 3 ? 2 : 3)
                    && recorder->statistics().pose_chunks == (occurrence <= 3 ? 1 : 2), "flush counter commit boundary");
        }
        for (const auto operation : {pose::IoOperation::close, pose::IoOperation::publish}) {
            Fault fault; // Late failure leaves a complete wire file that remains unpublished.
            fault.operation = operation;
            const auto name = operation == pose::IoOperation::close ? "close" : "publish";
            auto recorder = open(name, {&fault, fail, limit});
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "late-fault prefix");
            fault.enabled = true;
            require(!recorder->finalize() && recorder->state() == pose::State::failed
                    && recorder->statistics().persisted_frames == 3, "late failure preserves persisted facts");
        }
        {
            auto recorder = open("collision"); // Another owner wins the final destination after setup.
            require(recorder && append(*recorder, first), "collision setup");
            { std::ofstream competing(directory / "collision.spose"); competing << "do not replace"; }
            require(!recorder->finalize(), "publication remains no-clobber");
        }
        {
            auto recorder = open("long_bounded"); // Fixed two-group storage over many accepted source states.
            require(bool(recorder), "bounded run setup");
            auto row = first; // Reused caller storage, no duration-sized producer-side accumulation.
            for (std::uint64_t index = 0; index < 1001; ++index) {
                row.stamp = {pose::Analysis::transient, index == 0 ? 2U : (index == 1000 ? 8U : 4U),
                             1.25 + .01 * static_cast<double>(index), index, 0};
                row.phase = index == 0 ? pose::SourcePhase::transient_initial : pose::SourcePhase::transient_accepted;
                require(bool(append(*recorder, row)), "bounded accepted row");
            }
            workspace(recorder->statistics(), 2434, 2, reference_bytes, 2434);
            require(recorder->finalize() && recorder->statistics().pose_chunks == 501
                    && recorder->statistics().persisted_frames == 1001, "bounded final counters");
        }
        std::cout << "fluid recorder checks passed: " << checks << "; output=" << directory.string() << '\n';
        return 0;
    } catch (const std::exception& failure) { // Report only the first malformed fixture or contract failure.
        std::cerr << "fluid recorder: " << failure.what() << '\n';
        return 1;
    }
}
