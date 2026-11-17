#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <serene/outputs/archive.hpp>
#include <serene/outputs/pose_recorder.hpp>

namespace output = serene::outputs;
namespace pose = serene::outputs::pose;

/// Exercise archive and feature recorder APIs imported from one installed package.
/// argv[1] names the independent P8/F2 catalog; argv[2] is a NEW output directory.
/// Preserve published files for independent Python readback. Return zero on success
/// or a distinct setup, recording, archive-preparation or publication failure code.
int main(int argc, char** argv) {
    // Establish fresh ownership, read the immutable catalog and record literal paired packets.
    // Prepare a tiny scientific Cx series, publish it with accepted identities and retain both files.
    if (argc != 3) return 2;
    const std::filesystem::path directory = argv[2]; // New invocation-owned output destination.
    if (!std::filesystem::create_directory(directory)) return 3;
    std::ifstream input(argv[1]); // Read-only independent catalog source.
    if (!input) return 4;
    const std::string catalog((std::istreambuf_iterator<char>(input)), {}); // Exact owned setup JSON.
    pose::Options options; // Combined800-byte frame times two, using only public limit fields.
    options.max_batch_frames = 2;
    options.max_batch_bytes = 1600;
    auto recorder = pose::Recorder::open(directory / "installed.spose", catalog, options); // Sole pose session owner.
    if (!recorder) return 5;
    const std::array<double, 3> times{1.25, 1.29, 1.32}; // Actual initial, sparse scheduled and off-stride target times.
    const std::array<std::uint64_t, 3> identities{0, 4, 7}; // Accepted source steps; never inferred from row numbers.
    const std::array<std::uint32_t, 3> events{pose::Event::initial, pose::Event::scheduled, pose::Event::target}; // Captured reasons.
    for (std::size_t index = 0; index < times.size(); ++index) { // Current accepted literal packet.
        const double delta = .125 * static_cast<double>(index); // Exact binary movement of each occurrence.
        const std::array<pose::WinchGeometry, 2> features{{
            {{{-1.25 + delta, 2.5, 3.75, .75 + delta, 2.5, 3.75,
                -.25 + delta, 5, 4.25, 8.5, 14.25, -2}}, pose::WinchGeometryStatus::valid_positive_tension},
            {{{10.25, -3.5 + delta, 6.75, 10.25, -1.5 + delta, 6.75,
                12.5, -2.5 + delta, 7.125, -7.5, 11.25, 9}}, pose::WinchGeometryStatus::zero_tension_limit}
        }}; // Owned A/B/C/D packets; C remains algebraic and zero-limit geometry remains populated.
        const std::array<double, 56> poses{
            0, 0, 0, 1, 0, 0, 0, 0, 0, 5, .5, .5, .5, .5,
            -1.25 + delta, 2.5, 3.75, 1, 0, 0, 0, 8.5, 14.25, -2, 1, 0, 0, 0,
            .75 + delta, 2.5, 3.75, 1, 0, 0, 0, 10.25, -3.5 + delta, 6.75, 1, 0, 0, 0,
            10.25, -1.5 + delta, 6.75, 1, 0, 0, 0, -7.5, 11.25, 9, 1, 0, 0, 0
        }; // Unchanged physical P8 xyz/wxyz, including the asymmetric rotated structural sample.
        const pose::Stamp stamp{pose::Analysis::transient, events[index], times[index], identities[index], 0}; // Exact wire identity.
        const pose::WinchFrameView view{stamp, 2, index == 0 ? pose::WinchSourcePhase::transient_initial
                                                           : pose::WinchSourcePhase::transient_accepted,
                                         features}; // Synchronous borrowed complete source population.
        if (!recorder->append_frame(stamp, poses, view)) return 6;
    }
    if (!recorder->finalize({pose::Outcome::completed, pose::Outcome::completed})) return 7;
    if (recorder->statistics().persisted_frames != 3 || recorder->statistics().pose_chunks != 2) return 8;

    // Exercise the unchanged scientific archive component in this same linked executable.
    output::ArchiveDescription description; // Owned one-run scalar catalog and stable archive identity.
    description.file_id = "00000000-0000-4000-8000-000000000007";
    description.created_utc = "2026-11-17T00:00:00Z";
    description.run.run_id = "installed_winch";
    description.run.assembly_ref = "bench";
    description.run.record_capacity = 3;
    description.run.requests.push_back({"winch", "position", "3.0-wip.13", "1.0", "{}"});
    description.run.channels.push_back({"winch.position.c.x", "winch", "position", "x", "m", 0});
    output::ExportLimits limits; // Small finite independent scientific codec frame budget.
    limits.frame_values = 4;
    auto prepared = output::PreparedExport::prepare(std::move(description), limits); // Owned prepared archive description.
    auto codec = output::Codec::create(limits.frame_values); // Owned scientific codec working storage.
    if (!prepared || !codec) return 9;
    const std::array<double, 3> values{-.25, -.125, 0}; // First arrangement's exact Cx at the same accepted states.
    auto matrix = output::ValueView::strided(values, 1, 3, 3, 1); // Borrowed logical channel-major matrix.
    if (!matrix) return 10;
    output::RunResultView run; // Borrowed completed source facts and committed scalar prefix.
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
    run.info.elapsed_seconds_scope = {}; // No invented measurement scope when no duration was measured.
    run.info.identity_axis_convention = "zero_based_accepted_step_including_initial_state";
    auto archive = output::ArchiveSession::open(*prepared, *codec, directory / "installed.serene"); // Sole archive publication owner.
    if (!archive) return 11;
    if (!archive->append_run(run)) return 12;
    if (!archive->finalize()) return 13;
    if (archive->state() != output::SessionState::published || recorder->state() != pose::State::published) return 14;
    std::cout << "Installed combined archive/feature consumer passed: " << directory.string() << '\n';
    return 0;
}
