#include "synthetic_case.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace serene::outputs;
void save(SyntheticCase c, const std::filesystem::path &dir, const char *name) {
    auto p = PreparedExport::prepare(c.description, c.limits);
    auto codec = Codec::create(c.limits.frame_values);
    if (!p || !codec)
        throw std::runtime_error("preparation");
    auto base = dir / name;
    auto target = std::filesystem::path(base.string() + ".serene");
    auto s = ArchiveSession::open(*p, *codec, target);
    if (!s)
        throw std::runtime_error(std::string(status_name(s.error().code)));
    auto status = s->append_run(c.view(), c.provenance());
    if (status)
        status = s->finalize();
    if (!status)
        throw std::runtime_error(std::string(name) + ": " + std::string(status_name(status.code)) +
                                 " op=" + std::to_string(status.operation));
    if (!c.unavailable)
        c.expected(base);
    std::cout << name << " frames=" << s->statistics().frames
              << " payload=" << s->statistics().payload_bytes << '\n';
}
int main(int argc, char **argv) {
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("output directory required");
        std::filesystem::path out = argv[1];
        std::filesystem::create_directories(out);
        if (argc == 3) {
            if (std::string_view(argv[2]) != "--large-only")
                throw std::runtime_error("unknown generation option");
            SyntheticCase large(3, 10);
            large.original = "{\"padding\":\"" + std::string(2 * 1024 * 1024, 'a') + "\"}";
            save(std::move(large), out, "synthetic_large_documents");
            return 0;
        }
        save(SyntheticCase(3, 10), out, "synthetic_timeseries");
        SyntheticCase unicode(3, 10);
        unicode.description.run.run_id = "run/"
                                         "\xce\xb1";
        unicode.description.run.run_name = "Blade "
                                           "\xce\xb2";
        unicode.description.run.requests[0].group_id = "blade_"
                                                       "\xce\xb1";
        for (auto &channel : unicode.description.run.channels)
            if (channel.request_index == 0) {
                channel.group_id = unicode.description.run.requests[0].group_id;
                channel.scalar_id = channel.group_id + ".motion." + channel.component_path;
            }
        unicode.original = R"({"authored_value":"\u0000"})";
        save(std::move(unicode), out, "synthetic_unicode_document_nul");
        save(SyntheticCase(5, 2, RunKind::steady_state), out, "synthetic_static");
        save(SyntheticCase(5, 1, RunKind::steady_state), out, "synthetic_final_static");
        save(SyntheticCase(0, 0), out, "synthetic_empty");
        SyntheticCase failed(3, 2);
        failed.axis = {0, .04};
        failed.identities = {0, 4};
        failed.info.outcome = RunOutcome::numerical_failure;
        failed.info.numerical = NumericalStatus::failed;
        failed.info.failure_stage = "integration";
        failed.info.failure_code = "test_failure";
        failed.info.termination_reason = "solver_failure";
        failed.info.accepted_count = 6;
        failed.info.last_accepted_value = .06;
        failed.info.requested_end = .1;
        save(std::move(failed), out, "synthetic_failed_prefix");
        SyntheticCase none(3, 0);
        none.info.outcome = RunOutcome::observation_failure;
        none.info.numerical = NumericalStatus::stopped_before_target;
        none.info.observation = ObservationStatus::failed;
        none.info.requested_end = .1;
        none.info.failure_stage = "observation";
        none.info.failure_code = "test_failure";
        save(std::move(none), out, "synthetic_no_samples");
        SyntheticCase fs(5, 0, RunKind::steady_state);
        fs.info.outcome = RunOutcome::numerical_failure;
        fs.info.numerical = NumericalStatus::failed;
        fs.info.failure_stage = "equilibrium";
        fs.info.failure_code = "test_failure";
        fs.info.last_accepted_value = 0;
        save(std::move(fs), out, "synthetic_failed_static");
        SyntheticCase unavailable(0, 0);
        unavailable.unavailable = true;
        unavailable.metadata = false;
        auto &i = unavailable.info;
        i.outcome = RunOutcome::preparation_failure;
        i.numerical = NumericalStatus::not_started;
        i.observation = ObservationStatus::not_started;
        i.logging = LoggingStatus::unavailable;
        i.catalog_state = SectionState::unavailable;
        i.last_accepted_present = false;
        i.requested_bounds_present = false;
        i.elapsed_seconds_present = false;
        i.elapsed_seconds = 0;
        i.elapsed_seconds_scope = "";
        i.identity_axis_convention = "";
        i.failure_stage = "preparation";
        i.failure_code = "test_failure";
        save(std::move(unavailable), out, "synthetic_unavailable");
        SyntheticCase disabled(3, 10);
        disabled.metadata = false;
        disabled.description.run.effective_requests = SectionState::disabled;
        save(std::move(disabled), out, "synthetic_optional_off");
        SyntheticCase partial(3, 10);
        partial.partial_log = true;
        partial.log += "{\"partial\":";
        partial.info.outcome = RunOutcome::logging_failure;
        partial.info.logging = LoggingStatus::partial;
        partial.info.failure_stage = "logging";
        partial.info.failure_code = "partial_capture";
        save(std::move(partial), out, "synthetic_partial_log");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
