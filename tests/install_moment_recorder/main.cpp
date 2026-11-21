#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <serene/outputs/pose_recorder.hpp>
#if SERENE_MOMENT_CONSUMER_ARCHIVE
#include <serene/outputs/archive.hpp>
#endif

namespace output = serene::outputs;
namespace pose = serene::outputs::pose;

/// Exercise the installed moment-group API and optionally the scientific archive in one executable.
/// argv[1] supplies the independent P8/W2/F6/load-K5-K3/C7 catalog; argv[2] is a NEW output directory.
/// The CMake mode selects combined archive/pose or pose-only imports and public headers.
/// Return zero on success, or distinct setup, capture and publication failure codes.
int main(int argc, char** argv) {
    // Establish fresh output ownership and prepare the literal catalog's fixed raw budget.
    // Emit three independent source groups, then check publication and retained workspace.
    // In combined mode, publish the two positive accepted cut4 Mz values through the archive API.
    if (argc != 3) return 2;
    const std::filesystem::path directory = argv[2]; // New output owner; never replace an earlier run.
    if (!std::filesystem::create_directory(directory)) return 3;
    std::ifstream input(argv[1]); // Read-only independent catalog supplied by the qualification driver.
    if (!input) return 4;
    const std::string catalog((std::istreambuf_iterator<char>(input)), {}); // Owned immutable setup text.
    pose::Options options; // Exactly two complete2124-byte F/load/moment/winch/physical frame groups.
    options.max_batch_frames = 2;
    options.max_batch_bytes = 4248;
    auto recorder = pose::Recorder::open(directory / "installed.spose", catalog, options); // Owned recorder with copied setup state.
    if (!recorder) return 5;
    const std::array<double, 3> times{1.25, 1.29, 1.32}; // Actual initial, scheduled and off-stride target times.
    const std::array<std::uint64_t, 3> identities{0, 4, 7}; // Accepted source steps, independent of row ordinals.
    const std::array<std::uint32_t, 3> events{pose::Event::initial, pose::Event::scheduled, pose::Event::target}; // Capture reasons for each literal time.
    // Build each full population from literals; endpoint F and physical source positions remain independent.
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
        const std::array<pose::ElasticMoment, 7> moments{{
            {{{3, 4, 12}}, true}, {{{-3, -4, -12}}, true},
            {{{-7.25 - static_cast<double>(index), 2.5, 9.75}}, true},
            {{{8.5, -6.25 - static_cast<double>(index), -4.75}}, true},
            {{{0, 0, -12}}, true}, {{{-0.0, 0.0, -0.0}}, true}, {{{0, 0, 0}}, true}
        }}; // Independent full current-N triples: opposed resultant endpoints, torsion, signed/true zero.
        const pose::Stamp stamp{pose::Analysis::transient, events[index], times[index], identities[index], 0}; // Shared end-state source stamp.
        const auto phase = index == 0 ? pose::SourcePhase::transient_initial : pose::SourcePhase::transient_accepted; // Actual initial/positive lifecycle.
        const pose::WinchFrameView winch_view{stamp, 2, phase, winches}; // Borrowed exact winch source identity.
        const pose::FluidFrameView fluid_view{stamp, 2, phase, fluid}; // Borrowed exact F source identity.
        std::array<pose::AppliedLoadSample, 5> left{}; // Default all-zero unavailable initial population.
        std::array<pose::AppliedLoadSample, 3> right{}; // Unequal second attachment, independent of F display samples.
        pose::AppliedInterval interval{}; // Initial force-balance data is not a completed interval.
        if (index != 0) {
            const double source_delta = .25 * static_cast<double>(index); // Actual literal source points differ from end F.
            const std::array<std::array<double, 6>, 5> left_values{{
                {{-20.5 + source_delta, 3.25, 1.75, 12.5, -2.25, 7.75}},
                {{-18.25 + source_delta, 5.5, 2.125, -8.75, 4.5, .625}},
                {{-16.5 + source_delta, 7.75, 3.375, 3.125, -6.5, 9.25}},
                {{-16.5 + source_delta, 7.75, 3.375, -11.25, 8.625, -4.75}},
                {{-12.75 + source_delta, 11.5, 6.25, 5.875, 13.25, -.375}}
            }}; // Independent source XYZ then physical world N/m, retaining both sided values.
            const std::array<std::array<double, 6>, 3> right_values{{
                {{31.5, -7.25 + source_delta, 15.5, -14.75, 3.125, 2.5}},
                {{34.25, -4.5 + source_delta, 18.875, 9.5, -7.75, 6.125}},
                {{38.5, -.75 + source_delta, 23.25, 2.25, 12.875, -15.5}}
            }}; // Complete second model/attachment population, with source geometry independent of target F poses.
            // Split literal XYZ/density rows into the two borrowed complete attachment populations.
            for (std::size_t sample = 0; sample < left.size(); ++sample) { // Left attachment's logical station index.
                left[sample].valid = true;
                for (std::size_t axis = 0; axis < 3; ++axis) { // World x/y/z component shared by position and density.
                    left[sample].source_position_world_m[axis] = left_values[sample][axis];
                    left[sample].force_density_world_N_per_m[axis] = left_values[sample][axis + 3];
                }
            }
            for (std::size_t sample = 0; sample < right.size(); ++sample) { // Right attachment's independent station index.
                right[sample].valid = true;
                for (std::size_t axis = 0; axis < 3; ++axis) { // World x/y/z component shared by position and density.
                    right[sample].source_position_world_m[axis] = right_values[sample][axis];
                    right[sample].force_density_world_N_per_m[axis] = right_values[sample][axis + 3];
                }
            }
            // Identify the actual unsaved source and captured endpoint; tokens are opaque identities.
            interval.status = pose::AppliedIntervalStatus::completed;
            interval.source_accepted_id = index == 1 ? 3 : 6;
            interval.source.time_s = index == 1 ? 1.28 : 1.31;
            interval.end_accepted_id = identities[index];
            interval.end_time_s = times[index];
            interval.source.publication = index == 1 ? 91 : 7;
            interval.source.mechanical_revision = index == 1 ? 83 : 5;
            interval.source.state_revision = index == 1 ? 71 : 3;
            interval.source.geometry_revision = index == 1 ? 61 : 2;
            interval.source.environment_revision = index == 1 ? 53 : 1;
            interval.prepared_step_sequence = index == 1 ? 47 : 13;
            interval.packet = index == 1 ? 39 : 11; // Opaque IDs deliberately decrease without affecting accepted order.
        }
        const std::array<pose::AppliedAttachmentFrameView, 2> attachments{{{interval, left}, {interval, right}}}; // Copied common prefix and borrowed row spans.
        const pose::AppliedLoadsFrameView applied_view{stamp, 2, phase, attachments}; // Same96-byte source prefix for both.
        const pose::ElasticMomentFrameView moment_view{stamp, 2, phase, moments}; // Borrowed signed current-N cut population.
        const pose::FrameGroupView group{stamp, physical, &winch_view, &fluid_view, &applied_view, &moment_view}; // All nested views live through the call.
        if (!recorder->append_frame(group)) return 6;
    }
    // Publish the complete sidecar and check payload charges independently of process memory.
    if (!recorder->finalize({pose::Outcome::completed, pose::Outcome::completed})) return 7;
    const auto stats = recorder->statistics(); // Public payload-workspace and persisted-group accounting.
    if (stats.persisted_frames != 3 || stats.pose_chunks != 2 || stats.batch_capacity_bytes != 4248
        || stats.encoding_workspace_bytes != 2048 || stats.index_workspace_bytes != 256
        || stats.overlay_reference_bytes < 32
        || stats.workspace_capacity_bytes != 4248 + 2048 + stats.physical_reference_bytes + stats.overlay_reference_bytes) return 8;

#if SERENE_MOMENT_CONSUMER_ARCHIVE
    // Exercise unchanged scientific archive APIs from the same exact installed package.
    output::ArchiveDescription description; // Owned single-channel catalog with accepted-state provenance.
    description.file_id = "00000000-0000-4000-8000-000000000010";
    description.created_utc = "2026-11-20T00:00:00Z";
    description.run.run_id = "installed_moment";
    description.run.assembly_ref = "bench";
    description.run.record_capacity = 2;
    description.run.requests.push_back({"moment", "elastic_moment", "3.0-wip.13", "1.0", "{}"});
    description.run.channels.push_back({"moment.cut4.Mz", "moment", "elastic_moment", "Mz", "N.m", 0});
    output::ExportLimits limits; // Small independently bounded scientific codec workspace.
    limits.frame_values = 4;
    auto prepared = output::PreparedExport::prepare(std::move(description), limits); // Owned validated scientific export plan.
    auto codec = output::Codec::create(limits.frame_values); // Owned bounded codec workspace used by the archive session.
    if (!prepared || !codec) return 9;
    const std::array<double, 2> values{-12.0, -12.0}; // Literal nonzero cut4 Mz at the two positive accepted states; initial capture remains in the sidecar.
    const std::array<double, 2> completed_times{1.29, 1.32}; // Actual accepted ends of the two completed intervals.
    const std::array<std::uint64_t, 2> completed_ids{4, 7}; // Sparse captured accepted endpoints, no inferred step count.
    auto matrix = output::ValueView::strided(values, 1, 2, 2, 1); // Borrowed channel-major scalar series.
    if (!matrix) return 10;
    output::RunResultView run; // Borrowed actual axis and accepted source identities, never inferred cadence.
    run.axis = completed_times;
    run.accepted_indices = completed_ids;
    run.identity_axis_present = true;
    run.values = *matrix;
    // Capture provenance describes accepted source endpoints, separately from the two retained rows.
    run.info.accepted_count = 7;
    run.info.last_accepted_present = true;
    run.info.last_accepted_value = times.back();
    run.info.requested_begin = times.front();
    run.info.requested_end = times.back();
    run.info.elapsed_seconds_present = false;
    run.info.elapsed_seconds_scope = {}; // Do not invent runtime measurements in an API consumer test.
    run.info.identity_axis_convention = "zero_based_accepted_step_including_initial_state";
    // Publish using the same installed package; borrowed plan, codec and run buffers remain alive.
    auto archive = output::ArchiveSession::open(*prepared, *codec, directory / "installed.serene"); // Scientific archive session owner.
    if (!archive) return 11;
    if (!archive->append_run(run)) return 12;
    if (!archive->finalize()) return 13;
    if (archive->state() != output::SessionState::published) return 14;
#endif
    if (recorder->state() != pose::State::published) return 15;
    std::cout << "Installed moment consumer passed (archive=" << SERENE_MOMENT_CONSUMER_ARCHIVE
              << "): " << directory.string() << '\n';
    return 0;
}
