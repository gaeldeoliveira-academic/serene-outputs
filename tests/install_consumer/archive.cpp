#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <serene/outputs/archive.hpp>

/** Write and inspect a small archive using only installed public APIs. Returns zero on success; each nonzero stage code identifies preparation, view, write, publication, header or cleanup failure. */
int main() {
    // Prepare a single-channel run and borrowed numeric arrays.
    // Publish to a unique filename, verify magic bytes, and remove only that newly created artifact.

    using namespace serene::outputs;
    /* Owned single-run catalog and stable file metadata. */ ArchiveDescription description;
    description.file_id = "00000000-0000-4000-8000-000000000004";
    description.created_utc = "2026-11-08T00:00:00Z";
    description.run.run_id = "installed_writer";
    description.run.assembly_ref = "blade";
    description.run.record_capacity = 3;
    description.run.requests.push_back({"tip", "position", "3.0-wip.3", "1.0", "{}"});
    description.run.channels.push_back({"tip.position.x", "tip", "position", "x", "m", 0});
    /* Small frame capacity for the consumer probe. */ ExportLimits limits;
    limits.frame_values = 4;
    /* Owned prepared export state. */ auto prepared = PreparedExport::prepare(std::move(description), limits);
    /* Codec with matching frame capacity. */ auto codec = Codec::create(limits.frame_values);
    if (!prepared || !codec)
        return 1;
    /* Three retained times in seconds. */ std::array<double, 3> axis{0.0, 0.04, 0.08};
    /* One channel of metre-valued positions. */ std::array<double, 3> values{0.0, 0.125, 0.25};
    /* Borrowed one-row logical matrix. */ auto view = ValueView::strided(values, 1, 3, 3, 1);
    if (!view)
        return 2;
    /* Borrowed axes/values with completed accepted-state metadata. */ RunResultView run;
    run.axis = axis;
    run.values = *view;
    run.info.accepted_count = 2;
    run.info.last_accepted_present = true;
    run.info.last_accepted_value = 0.08;
    run.info.requested_end = 0.08;
    /* Process-local unique filename suffix from a steady clock. */ const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    /* Fresh archive destination. */ const auto output = std::filesystem::current_path() /
                        ("installed-consumer-" + std::to_string(stamp) + ".serene");
    /* Owning writer borrowing the prepared exporter and codec. */ auto session = ArchiveSession::open(*prepared, *codec, output);
    if (!session)
        return 3;
    if (!session->append_run(run))
        return 4;
    if (!session->finalize())
        return 5;
    if (session->state() != SessionState::published || !std::filesystem::is_regular_file(output))
        return 6;
    /* Header-magic readback buffer. */ std::array<char, 8> magic{};
    {
        /* Published archive readback stream. */ std::ifstream input(output, std::ios::binary);
        input.read(magic.data(), magic.size());
        if (!input || magic != std::array<char, 8>{'S', 'E', 'R', 'E', 'N', 'E', 0, 0})
            return 7;
    }
    std::cout << "Installed native archive written: " << output.string() << '\n';
    // Remove only this test's freshly created artifact after confirming publication.
    if (!std::filesystem::remove(output))
        return 8;
    return 0;
}
