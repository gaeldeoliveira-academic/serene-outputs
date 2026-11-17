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
std::uint64_t checks{}; ///< Completed assertions, reported only for this invocation.

/// Require condition; throw the borrowed diagnostic on failure and count successful checks.
void require(bool condition, const char* diagnostic) {
    // Stop at the first failed contract so later evidence cannot mask it.
    if (!condition) throw std::runtime_error(diagnostic);
    ++checks;
}

/// Owned fixture packet used by the tests; vectors back views only during synchronous calls.
/// Read an independent expected row, optionally mutate a copy, then submit the complete packet.
struct Packet {
    pose::Stamp stamp; ///< Actual accepted/source state and capture events.
    std::uint64_t generation{}; ///< Core context generation, distinct from capture-plan identity.
    pose::WinchSourcePhase phase{}; ///< Initialized, certified or transient source phase.
    std::vector<double> poses; ///< Dense physical xyz/wxyz values in catalog sample order.
    std::vector<pose::WinchGeometry> features; ///< All arrangement rows in fixed catalog order.

    /// Return a view borrowing this packet's feature storage; valid until mutation/destruction.
    pose::WinchFrameView view() const {
        // Bind the independently asserted source identity and complete feature population.
        return {stamp, generation, phase, features};
    }
};

/// Read one independent expected JSON row into owned buffers; throw on malformed test input.
Packet packet(const Json& row) {
    // Translate literal source facts, flatten physical poses, then flatten each arrangement.
    const auto& stamp = row.at("stamp"); // Borrowed independently authored state dictionary.
    Packet result; // Owner of every value submitted by this test.
    result.stamp = {static_cast<pose::Analysis>(stamp.at("analysis").get<std::uint32_t>()),
                    stamp.at("events").get<std::uint32_t>(), stamp.at("time").get<double>(),
                    stamp.at("state_id").get<std::uint64_t>(), stamp.at("load_factor").get<double>()};
    result.generation = row.at("source_generation").get<std::uint64_t>();
    result.phase = static_cast<pose::WinchSourcePhase>(row.at("source_phase").get<std::uint32_t>());
    for (const auto& pose : row.at("poses")) { // One physical sample's exact seven scalars.
        for (const auto& value : pose) result.poses.push_back(value.get<double>());
    }
    for (const auto& feature : row.at("features")) { // One arrangement's literal XYZ/status row.
        pose::WinchGeometry geometry{}; // Owned flattened geometry without normalization.
        geometry.geometry = static_cast<pose::WinchGeometryStatus>(feature.at("geometry_status").get<std::uint32_t>());
        std::size_t at{}; // Next Axyz/Bxyz/Cxyz/Dxyz scalar position.
        for (const auto& point : feature.at("world_xyz_m"))
            for (const auto& value : point) geometry.world_xyz_m.at(at++) = value.get<double>();
        require(at == 12, "fixture geometry shape");
        result.features.push_back(geometry);
    }
    return result;
}

/// Borrowed I/O fault control; Recorder never owns this object or its callback functions.
struct Fault {
    bool enabled{}; ///< Enabled after setup/valid prefix, never during catalog admission.
    pose::IoOperation operation{pose::IoOperation::write}; ///< Boundary selected for failure.
    std::uint64_t writes{}; ///< Matching active writes counted since enablement.
    std::uint64_t fail_write{1}; ///< One-based active write to fail logically, or zero for none.
    std::uint64_t short_write{}; ///< One-based active write to truncate physically, or zero.
};

/// Count enabled writes and fail the selected operation; unused global ordinal is not reinterpreted.
bool fail(void* context, pose::IoOperation operation, std::uint64_t) noexcept {
    // Count only the active region, then choose the requested write or other I/O boundary.
    auto& fault = *static_cast<Fault*>(context); // Borrowed test-owned lifetime-stable settings.
    if (!fault.enabled) return false;
    if (operation == pose::IoOperation::write) ++fault.writes;
    if (operation != fault.operation || fault.short_write) return false;
    return operation != pose::IoOperation::write || fault.writes == fault.fail_write;
}

/// Return a deliberately short payload count only at the chosen active write; preserve others.
std::size_t limit(void* context, std::uint64_t, std::size_t requested) noexcept {
    // Use the counter advanced by fail, truncating an actual payload rather than a status alone.
    const auto& fault = *static_cast<const Fault*>(context); // Borrowed fault state for this write.
    return fault.enabled && fault.short_write == fault.writes ? std::min<std::size_t>(17, requested) : requested;
}

/// Submit all borrowed physical/feature values under their independently carried source stamp.
serene::outputs::Status append(pose::Recorder& recorder, const Packet& row) {
    // Construct the short-lived view and let the public recorder copy it synchronously.
    return recorder.append_frame(row.stamp, row.poses, row.view());
}

/// Set one physical and feature point for representation-bound tests; modifies only caller buffers.
void point(Packet& row, std::size_t sample, std::size_t arrangement, std::size_t role,
           const std::array<double, 3>& physical, const std::array<double, 3>& feature) {
    // Keep the physical and feature bytes separate, including deliberate signed-zero differences.
    for (std::size_t axis = 0; axis < 3; ++axis) { // World Cartesian coordinate index.
        row.poses.at(sample * 7 + axis) = physical[axis];
        row.features.at(arrangement).world_xyz_m.at(role * 3 + axis) = feature[axis];
    }
}
} // namespace

/// Run focused feature recorder checks. argv names NEW_OUTPUT_DIR, catalog.json and expected.json.
/// Return zero only after all assertions pass; failures preserve this invocation's partial evidence.
int main(int argc, char** argv) {
    // Load literal independent rows and establish fresh destinations and finite batch budgets.
    // Exercise coherent packets, representations, whole-packet rejection and immutable catalog rules.
    // Inject paired-write/publication failures, then check duration-independent storage counters.
    try {
        if (argc != 4) return 2;
        const std::filesystem::path directory = argv[1]; // New, caller-selected output owner.
        require(std::filesystem::create_directory(directory), "output directory must be new");
        std::ifstream catalog_input(argv[2]), expected_input(argv[3]); // Read-only fixture streams.
        Json catalog, expected; // Owned catalog and independently authored reference packets.
        catalog_input >> catalog;
        expected_input >> expected;
        const auto text = catalog.dump(); // Exact setup text retained by each emitted file.
        const auto first = packet(expected.at("transient_frames").at(0)); // Initial accepted state.
        const auto second = packet(expected.at("transient_frames").at(1)); // Sparse scheduled step4.
        const auto third = packet(expected.at("transient_frames").at(2)); // Off-stride target step7.
        require(first.poses.size() == 56 && first.features.size() == 2, "focused fixture P8/F2");
        pose::Options options; // Two whole800-byte paired frames force full and final-partial batches.
        options.max_batch_frames = 2;
        options.max_batch_bytes = 1600;
        /// Open one fresh scenario under the shared immutable feature catalog and limits.
        const auto open = [&](const std::string& name, pose::FaultHook hook = {}) {
            // Synchronously consume setup text and retain only recorder-owned bounded storage.
            return pose::Recorder::open(directory / (name + ".spose"), text, options, hook);
        };
        {
            auto recorder = open("transient"); // Actual producer for exact independent byte readback.
            require(bool(recorder), "transient open");
            auto borrowed = first; // Deliberately invalidated after append to prove synchronous copying.
            require(bool(append(*recorder, borrowed)), "initial append");
            std::fill(borrowed.poses.begin(), borrowed.poses.end(), std::numeric_limits<double>::quiet_NaN());
            for (auto& feature : borrowed.features) feature.world_xyz_m.fill(std::numeric_limits<double>::quiet_NaN());
            require(recorder->statistics().accepted_frames == 1 && recorder->statistics().persisted_frames == 0,
                    "copied initial packet remains buffered");
            require(bool(append(*recorder, second)), "scheduled append");
            require(recorder->statistics().persisted_frames == 2, "complete paired batch persisted");
            require(bool(append(*recorder, third)), "off-stride target append");
            require(bool(recorder->finalize()), "transient finalize");
            const auto stats = recorder->statistics(); // Published paired accounting, not OS memory.
            require(stats.accepted_frames == 3 && stats.persisted_frames == 3 && stats.pose_chunks == 2,
                    "paired final counts");
            require(stats.batch_capacity_bytes == 1600 && stats.effective_batch_frames == 2 && stats.index_workspace_bytes == 256,
                    "combined bounded workspace");
            require(recorder->state() == pose::State::published, "published lifecycle");
            require(!open("transient"), "existing final not overwritten");
        }
        {
            auto recorder = open("static"); // One certified equilibrium with exact static factor.
            const auto row = packet(expected.at("static_frames").at(0)); // Independent certified fixture.
            require(recorder && append(*recorder, row) && recorder->finalize(), "static packet");
        }
        {
            auto recorder = open("prepared_then_transient"); // Initialized diagnostics may precede startup.
            const auto row = packet(expected.at("prepared_frames").at(0)); // Prepared time100, independent of accepted clock.
            require(recorder && append(*recorder, row) && append(*recorder, first) && append(*recorder, third)
                    && recorder->finalize(), "prepared prefix and transient packet");
        }
        {
            auto recorder = open("initial_disabled"); // A positive accepted step can be first captured.
            require(recorder && append(*recorder, second) && append(*recorder, third) && recorder->finalize(),
                    "initial-disabled transient capture");
        }
        {
            auto recorder = open("empty_failed"); // Topology remains available when no state was captured.
            require(recorder && recorder->finalize({pose::Outcome::failed, pose::Outcome::stopped}), "empty failed recording");
        }
        {
            auto recorder = open("abandoned"); // Only two persisted pairs survive destruction.
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third),
                    "abandoned prefix setup");
        }
        require(!std::filesystem::exists(directory / "abandoned.spose"), "destructor does not publish");

        // Representation checks preserve both inputs instead of rewriting a physical sample.
        const double tolerance = std::ldexp(1.0, -46); // Exactly64*2^-52 at metre scale S=1.
        {
            auto row = first; // Independent caller data at the inclusive consistency boundary.
            point(row, 2, 0, 0, {0, 0, 0}, {tolerance, 0, 0});
            auto recorder = open("consistency_boundary"); // Preserves distinct finite physical/feature bytes.
            require(recorder && append(*recorder, row) && recorder->finalize(), "inclusive consistency bound");
        }
        {
            auto row = first; // One large coordinate scales the bound for the entire XYZ point.
            point(row, 2, 0, 0, {128, 0, 0}, {128, std::ldexp(1.0, -39), 0});
            auto recorder = open("scaled_consistency_boundary"); // Infinity-norm scale applies to each component.
            require(recorder && append(*recorder, row) && recorder->finalize(), "scaled consistency bound");
        }
        {
            auto row = first; // Zero signs differ intentionally between canonical sample and feature.
            point(row, 2, 0, 0, {0.0, 0.0, 0.0}, {-0.0, -0.0, -0.0});
            auto recorder = open("signed_zero"); // Numerical equality must not canonicalize either payload.
            require(recorder && append(*recorder, row) && recorder->finalize(), "signed zeros compare equal");
        }
        {
            auto shared = catalog; // Two selected forces sharing the same physical A/B/D identities.
            shared["winch_geometry"]["arrangements"][1] = shared["winch_geometry"]["arrangements"][0];
            shared["winch_geometry"]["arrangements"][1]["index"] = 1;
            shared["winch_geometry"]["arrangements"][1]["force_id"] = "other";
            shared["winch_geometry"]["arrangements"][1]["key"] = "winch_with_saddle.v1.o11_/bench/left.f5_other";
            auto row = first; // Each feature is compared to the canonical pose, not to the other feature.
            row.features[1] = row.features[0];
            row.features[1].geometry = pose::WinchGeometryStatus::zero_tension_limit;
            point(row, 2, 0, 0, {0, 0, 0}, {tolerance, 0, 0});
            point(row, 2, 1, 0, {0, 0, 0}, {-tolerance, 0, 0});
            auto recorder = pose::Recorder::open(directory / "shared_boundary.spose", shared.dump(), options);
            require(recorder && append(*recorder, row) && recorder->finalize(), "shared canonical representation bound");
            row.features[1].world_xyz_m[0] = std::nextafter(-tolerance, -std::numeric_limits<double>::infinity());
            auto rejected = pose::Recorder::open(directory / "shared_excess.spose", shared.dump(), options);
            require(rejected && !append(*rejected, row) && rejected->statistics().accepted_frames == 0,
                    "shared sample excess rejected before admission");
        }

        // Every malformed complete packet must fail before admitting either population.
        for (int scenario = 0; scenario < 11; ++scenario) { // Isolated full-packet violation selector.
            auto row = second; // The first row remains valid; the second submission is damaged.
            auto source = row.view(); // Borrowed view whose source stamp can be mismatched independently.
            if (scenario == 0) row.features[1].world_xyz_m[6] = std::numeric_limits<double>::quiet_NaN();
            if (scenario == 1) source.preparation_generation = 17;
            if (scenario == 2) source.phase = pose::WinchSourcePhase::static_certified;
            if (scenario == 3) source.stamp.time = std::nextafter(row.stamp.time, 2.0);
            if (scenario == 4) source.geometries = source.geometries.first(1);
            if (scenario == 5) row.poses[14] = std::numeric_limits<double>::infinity();
            if (scenario == 6) row.features[0].geometry = static_cast<pose::WinchGeometryStatus>(7);
            if (scenario == 7) source.stamp.state_id = 5;
            if (scenario == 8) source.stamp.load_factor = 1;
            if (scenario == 9) {
                point(row, 2, 0, 0, {0, 0, 0}, {std::nextafter(tolerance, 1.0), 0, 0});
            }
            if (scenario == 10) source.stamp.events = pose::Event::target;
            auto recorder = open("bad_packet_" + std::to_string(scenario)); // Fresh latching session per violation.
            require(recorder && append(*recorder, first), "bad packet valid prefix");
            require(!recorder->append_frame(row.stamp, row.poses, source), "malformed packet rejected");
            require(recorder->statistics().accepted_frames == 1 && recorder->statistics().buffered_frames == 1
                    && recorder->statistics().persisted_frames == 0, "whole-packet counters unchanged");
            require(recorder->state() == pose::State::failed && !recorder->finalize(), "packet failure latches");
        }
        {
            auto recorder = open("missing_feature_overload"); // Old overload cannot discard an enabled feature population.
            require(recorder && !recorder->append_frame(first.stamp, first.poses), "enabled catalog requires complete packet");
            auto plain = catalog; // Unchanged physical catalog with feature descriptor omitted.
            plain.erase("winch_geometry");
            auto wrong = pose::Recorder::open(directory / "unexpected_feature_overload.spose", plain.dump(), options);
            require(wrong && !append(*wrong, first), "feature-free catalog rejects feature overload");
        }
        for (int scenario = 0; scenario < 12; ++scenario) { // One setup contract violation per catalog copy.
            auto bad = catalog; // Keep all unrelated physical topology/metadata unchanged.
            auto& winch = bad["winch_geometry"]; // Mutable selected feature descriptor.
            if (scenario == 0) winch["arrangements"][0]["points"][1]["role"] = "A";
            if (scenario == 1) winch["arrangements"][0]["points"][0]["sample_id"] = 99999;
            if (scenario == 2) winch["arrangements"][0]["points"][2]["sample_id"] = 0;
            if (scenario == 3) { winch["arrangements"][1] = winch["arrangements"][0]; winch["arrangements"][1]["index"] = 1; }
            if (scenario == 4) winch["layout_version"] = 2;
            if (scenario == 5) winch["kind"] = 1;
            if (scenario == 6) winch["stream_id"] = 1;
            if (scenario == 7) winch["source_preparation_generation"] = 2.0;
            if (scenario == 8) winch["arrangements"] = Json::array();
            if (scenario == 9) winch["source_preparation_generation"] = std::numeric_limits<std::uint64_t>::max();
            if (scenario == 10) winch["arrangements"][0]["points"][0]["sample_id"] = 0;
            if (scenario == 11) winch["arrangements"][0]["edges"][2]["points"] = Json::array({3, 2});
            require(!pose::Recorder::open(directory / ("bad_catalog_" + std::to_string(scenario) + ".spose"), bad.dump(), options),
                    "invalid feature catalog rejected");
        }
        {
            auto limits = options; // Exact combined-pair admission and count-overflow probes.
            limits.max_batch_bytes = 799;
            require(!pose::Recorder::open(directory / "too_small.spose", text, limits), "one whole pair must fit");
            limits.max_batch_bytes = 1599;
            limits.max_batch_frames = 100;
            auto recorder = pose::Recorder::open(directory / "byte_limit.spose", text, limits);
            require(recorder && recorder->statistics().batch_capacity_bytes == 800
                    && recorder->statistics().effective_batch_frames == 1 && recorder->finalize(), "combined byte cap");
            limits.max_batch_frames = std::numeric_limits<std::uint64_t>::max();
            require(!pose::Recorder::open(directory / "count_overflow.spose", text, limits), "frame count overflow rejected");
        }

        // Faults after a valid two-pair prefix never fabricate successful publication.
        for (const auto operation : {pose::IoOperation::write, pose::IoOperation::flush, pose::IoOperation::close,
                                     pose::IoOperation::publish}) { // Separate deterministic I/O boundary.
            Fault fault; // Stack-owned synchronous injection state.
            fault.operation = operation;
            const auto name = "fault_" + std::to_string(static_cast<int>(operation)); // Unique scenario path.
            auto recorder = open(name, {&fault, fail, limit});
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "fault prefix");
            fault.enabled = true;
            require(!recorder->finalize() && recorder->state() == pose::State::failed, "I/O fault latches");
            require(!std::filesystem::exists(directory / (name + ".spose")), "failed session never publishes");
        }
        for (int scenario = 0; scenario < 3; ++scenario) { // Missing P header, partial F payload, partial P payload.
            Fault fault; // Starts disabled so the first two pairs persist normally.
            fault.fail_write = scenario == 0 ? 3 : 0;
            fault.short_write = scenario == 1 ? 2 : (scenario == 2 ? 4 : 0);
            const auto name = scenario == 0 ? "between_pair" : (scenario == 1 ? "short_feature" : "short_pose");
            auto recorder = open(name, {&fault, fail, limit});
            require(recorder && append(*recorder, first) && append(*recorder, second) && append(*recorder, third), "paired fault prefix");
            fault.enabled = true;
            require(!recorder->finalize() && recorder->statistics().accepted_frames == 3
                    && recorder->statistics().persisted_frames == 2, "paired fault retains prior complete count");
        }
        {
            auto recorder = open("collision"); // Destination created after open must win unchanged.
            require(recorder && append(*recorder, first), "collision setup");
            { std::ofstream other(directory / "collision.spose"); other << "do not replace"; }
            require(!recorder->finalize(), "no-clobber publication");
            std::ifstream input(directory / "collision.spose"); // Read the competing file, not the partial.
            std::string preserved; // Exact sentinel text belonging to the other writer.
            std::getline(input, preserved);
            require(preserved == "do not replace", "racing destination preserved");
        }
        {
            auto recorder = open("long_bounded"); // Many accepted frames, one fixed two-frame storage budget.
            require(bool(recorder), "long recorder setup");
            const auto capacity = recorder->statistics().batch_capacity_bytes; // Setup-admitted capacity.
            auto row = first; // Reused caller-owned packet; no duration-sized producer input.
            for (std::uint64_t index = 0; index < 1001; ++index) { // Accepted identity and local test step.
                row.stamp = {pose::Analysis::transient, index == 0 ? 2U : (index == 1000 ? 8U : 4U),
                             1.25 + .01 * static_cast<double>(index), index, 0};
                row.phase = index == 0 ? pose::WinchSourcePhase::transient_initial : pose::WinchSourcePhase::transient_accepted;
                require(bool(append(*recorder, row)), "long append");
            }
            require(recorder->statistics().batch_capacity_bytes == capacity && capacity == 1600
                    && recorder->statistics().index_workspace_bytes == 256, "long storage remains bounded");
            require(recorder->finalize() && recorder->statistics().pose_chunks == 501, "long final paired counts");
        }
        std::cout << "winch recorder checks passed: " << checks << "; output=" << directory.string() << '\n';
        return 0;
    } catch (const std::exception& failure) { // First violated contract or malformed fixture diagnostic.
        std::cerr << "winch recorder: " << failure.what() << '\n';
        return 1;
    }
}
