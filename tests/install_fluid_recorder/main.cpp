#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <serene/outputs/pose_recorder.hpp>
#if SERENE_FLUID_CONSUMER_ARCHIVE
#include <serene/outputs/archive.hpp>
#endif

namespace output = serene::outputs;
namespace pose = serene::outputs::pose;

/// Exercise the installed F-group API and optionally the scientific archive in one executable.
/// argv[1] supplies the independent P8/W2/K6 catalog; argv[2] is a NEW output directory.
/// The CMake mode selects combined archive/pose or pose-only imports and public headers.
/// Return zero on success, or distinct setup, capture and publication failure codes.
int main(int argc, char** argv) {
    // Establish fresh ownership, borrow exact catalog bytes, and emit independent literal groups.
    if (argc != 3) return 2;
    const std::filesystem::path directory = argv[2]; // New output owner; never replace an earlier run.
    if (!std::filesystem::create_directory(directory)) return 3;
    std::ifstream input(argv[1]); // Read-only independent catalog supplied by the qualification driver.
    if (!input) return 4;
    const std::string catalog((std::istreambuf_iterator<char>(input)), {}); // Owned immutable setup text.
    pose::Options options; // Exactly two complete1217-byte F/winch/physical frame groups.
    options.max_batch_frames = 2;
    options.max_batch_bytes = 2434;
    auto recorder = pose::Recorder::open(directory / "installed.spose", catalog, options);
    if (!recorder) return 5;
    const std::array<double, 3> times{1.25, 1.29, 1.32}; // Actual initial, scheduled and off-stride target times.
    const std::array<std::uint64_t, 3> identities{0, 4, 7}; // Accepted source steps, independent of row ordinals.
    const std::array<std::uint32_t, 3> events{pose::Event::initial, pose::Event::scheduled, pose::Event::target};
    for (std::size_t index = 0; index < times.size(); ++index) { // Current independent accepted source group.
        const double delta = .125 * static_cast<double>(index); // Exact dyadic displacement in literal oracles.
        const std::array<pose::WinchGeometry, 2> winches{{
            {{{-1.25 + delta, 2.5, 3.75, .75 + delta, 2.5, 3.75,
                -.25 + delta, 5, 4.25, 8.5, 14.25, -2}}, pose::WinchGeometryStatus::valid_positive_tension},
            {{{10.25, -3.5 + delta, 6.75, 10.25, -1.5 + delta, 6.75,
                12.5, -2.5 + delta, 7.125, -7.5, 11.25, 9}}, pose::WinchGeometryStatus::zero_tension_limit}
        }}; // Complete A/B/C/D arrangements with unchanged algebraic C and zero-limit provenance.
        const std::array<double, 56> physical{
            0, 0, 0, 1, 0, 0, 0, 0, 0, 5, .5, .5, .5, .5,
            -1.25 + delta, 2.5, 3.75, 1, 0, 0, 0, 8.5, 14.25, -2, 1, 0, 0, 0,
            .75 + delta, 2.5, 3.75, 1, 0, 0, 0, 10.25, -3.5 + delta, 6.75, 1, 0, 0, 0,
            10.25, -1.5 + delta, 6.75, 1, 0, 0, 0, -7.5, 11.25, 9, 1, 0, 0, 0
        }; // Physical P8 is unchanged by F sampling, including its asymmetric rotated quaternion.
        const std::array<pose::FluidPose, 6> fluid{{
            {{{-2.5 + delta, .75, 1.125, 1, 0, 0, 0}}, true},
            {{{.25 + delta, 2.75, 3.125, .5, .5, .5, .5}}, true},
            {{{.25 + delta, 2.75, 3.125, .5, -.5, .5, -.5}}, true},
            {{{4.5 + delta, -1.75, 6.125, 0, 0, 1, 0}}, true},
            {{{10.25, -4.5 + delta, 7.75, 0, 1, 0, 0}}, true},
            {{{12.5, -2.25 + delta, 11.875, -.5, -.5, .5, .5}}, true}
        }}; // Two F owners; coincident left/right positions retain distinct orientations.
        const pose::Stamp stamp{pose::Analysis::transient, events[index], times[index], identities[index], 0};
        const auto phase = index == 0 ? pose::SourcePhase::transient_initial : pose::SourcePhase::transient_accepted;
        const pose::WinchFrameView winch_view{stamp, 2, phase, winches}; // Borrowed exact winch source identity.
        const pose::FluidFrameView fluid_view{stamp, 2, phase, fluid}; // Borrowed exact F source identity.
        const pose::FrameGroupView group{stamp, physical, &winch_view, &fluid_view}; // Views live through the call.
        if (!recorder->append_frame(group)) return 6;
    }
    if (!recorder->finalize({pose::Outcome::completed, pose::Outcome::completed})) return 7;
    const auto stats = recorder->statistics(); // Public payload-workspace and persisted-group accounting.
    if (stats.persisted_frames != 3 || stats.pose_chunks != 2 || stats.batch_capacity_bytes != 2434
        || stats.encoding_workspace_bytes != 2048 || stats.index_workspace_bytes != 256
        || stats.workspace_capacity_bytes != 2434 + 2048 + stats.physical_reference_bytes) return 8;

#if SERENE_FLUID_CONSUMER_ARCHIVE
    // Exercise unchanged scientific archive APIs from the same exact installed package.
    output::ArchiveDescription description; // Owned single-channel catalog with accepted-state provenance.
    description.file_id = "00000000-0000-4000-8000-000000000008";
    description.created_utc = "2026-11-20T00:00:00Z";
    description.run.run_id = "installed_fluid";
    description.run.assembly_ref = "bench";
    description.run.record_capacity = 3;
    description.run.requests.push_back({"fluid", "position", "3.0-wip.13", "1.0", "{}"});
    description.run.channels.push_back({"fluid.position.first.x", "fluid", "position", "x", "m", 0});
    output::ExportLimits limits; // Small independently bounded scientific codec workspace.
    limits.frame_values = 4;
    auto prepared = output::PreparedExport::prepare(std::move(description), limits);
    auto codec = output::Codec::create(limits.frame_values);
    if (!prepared || !codec) return 9;
    const std::array<double, 3> values{-2.5, -2.375, -2.25}; // First F sample's literal accepted x coordinates.
    auto matrix = output::ValueView::strided(values, 1, 3, 3, 1); // Borrowed channel-major scalar series.
    if (!matrix) return 10;
    output::RunResultView run; // Borrowed actual axis and accepted source identities, never inferred cadence.
    run.axis = times;
    run.accepted_indices = identities;
    run.identity_axis_present = true;
    run.values = *matrix;
    run.info.accepted_count = 7;
    run.info.last_accepted_present = true;
    run.info.last_accepted_value = times.back();
    run.info.requested_begin = times.front();
    run.info.requested_end = times.back();
    run.info.elapsed_seconds_present = false;
    run.info.elapsed_seconds_scope = {}; // Do not invent runtime measurements in an API consumer test.
    run.info.identity_axis_convention = "zero_based_accepted_step_including_initial_state";
    auto archive = output::ArchiveSession::open(*prepared, *codec, directory / "installed.serene");
    if (!archive) return 11;
    if (!archive->append_run(run)) return 12;
    if (!archive->finalize()) return 13;
    if (archive->state() != output::SessionState::published) return 14;
#endif
    if (recorder->state() != pose::State::published) return 15;
    std::cout << "Installed F consumer passed (archive=" << SERENE_FLUID_CONSUMER_ARCHIVE
              << "): " << directory.string() << '\n';
    return 0;
}
