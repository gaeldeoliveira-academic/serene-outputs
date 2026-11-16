#include "synthetic_case.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace serene::outputs;
/** Write owned fixture c under fresh basename dir/name; name is borrowed during this call. Emits expected sidecars unless observations are unavailable; throws on any writer failure. */
void save(SyntheticCase c, const std::filesystem::path &dir, const char *name) {
    // Prepare exporter and codec, derive paths, and open a no-clobber session.
    // Append views while c is alive, finalize, then emit expected sidecars and frame counts.

    /* Owned prepared exporter. */ auto p = PreparedExport::prepare(c.description, c.limits);
    /* Codec sized to the fixture frame capacity. */ auto codec = Codec::create(c.limits.frame_values);
    if (!p || !codec)
        throw std::runtime_error("preparation");
    /* Basename shared by archive and expected sidecars. */ auto base = dir / name;
    /* Final archive destination. */ auto target = std::filesystem::path(base.string() + ".serene");
    /* Owning session borrowing the prepared exporter and codec. */ auto s = ArchiveSession::open(*p, *codec, target);
    if (!s)
        throw std::runtime_error(std::string(status_name(s.error().code)));
    /* Append/finalization outcome. */ auto status = s->append_run(c.view(), c.provenance());
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
/** Write synthetic archives into argv[1]; optional --large-only selects one large-document case. Returns zero or one after reporting the first error. */
int main(int argc, char **argv) {
    // Validate the requested generation mode and handle the large-document-only branch.
    // Write normal, Unicode, static, empty and failed-prefix numeric cases.
    // Write unavailable, disabled and partial-log metadata variants with explicit outcomes.

    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("output directory required");
        /* Requested output directory. */ std::filesystem::path out = argv[1];
        std::filesystem::create_directories(out);
        if (argc == 3) {
            if (std::string_view(argv[2]) != "--large-only")
                throw std::runtime_error("unknown generation option");
            /* Fixture with a large original-input document. */ SyntheticCase large(3, 10);
            large.original = "{\"padding\":\"" + std::string(2 * 1024 * 1024, 'a') + "\"}";
            save(std::move(large), out, "synthetic_large_documents");
            return 0;
        }
        save(SyntheticCase(3, 10), out, "synthetic_timeseries");
        /* Unicode identity and escaped-NUL evidence fixture. */ SyntheticCase unicode(3, 10);
        unicode.description.run.run_id = "run/"
                                         "\xce\xb1";
        unicode.description.run.run_name = "Blade "
                                           "\xce\xb2";
        unicode.description.run.requests[0].group_id = "blade_"
                                                       "\xce\xb1";
        for (/* Channel identity kept consistent with its Unicode request. */ auto &channel : unicode.description.run.channels)
            if (channel.request_index == 0) {
                channel.group_id = unicode.description.run.requests[0].group_id;
                channel.scalar_id = channel.group_id + ".motion." + channel.component_path;
            }
        unicode.original = R"({"authored_value":"\u0000"})";
        save(std::move(unicode), out, "synthetic_unicode_document_nul");
        save(SyntheticCase(5, 2, RunKind::steady_state), out, "synthetic_static");
        save(SyntheticCase(5, 1, RunKind::steady_state), out, "synthetic_final_static");
        save(SyntheticCase(0, 0), out, "synthetic_empty");
        /* Numerical-failure prefix with sparse selected steps. */ SyntheticCase failed(3, 2);
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
        /* Observation failure with no retained sample. */ SyntheticCase none(3, 0);
        none.info.outcome = RunOutcome::observation_failure;
        none.info.numerical = NumericalStatus::stopped_before_target;
        none.info.observation = ObservationStatus::failed;
        none.info.requested_end = .1;
        none.info.failure_stage = "observation";
        none.info.failure_code = "test_failure";
        save(std::move(none), out, "synthetic_no_samples");
        /* Static numerical failure without output levels. */ SyntheticCase fs(5, 0, RunKind::steady_state);
        fs.info.outcome = RunOutcome::numerical_failure;
        fs.info.numerical = NumericalStatus::failed;
        fs.info.failure_stage = "equilibrium";
        fs.info.failure_code = "test_failure";
        fs.info.last_accepted_value = 0;
        save(std::move(fs), out, "synthetic_failed_static");
        /* Preparation failure with unavailable catalog/provenance. */ SyntheticCase unavailable(0, 0);
        unavailable.unavailable = true;
        unavailable.metadata = false;
        /* Unavailable-case run-info envelope. */ auto &i = unavailable.info;
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
        /* Successful numeric fixture with metadata capture off. */ SyntheticCase disabled(3, 10);
        disabled.metadata = false;
        disabled.description.run.effective_requests = SectionState::disabled;
        save(std::move(disabled), out, "synthetic_optional_off");
        /* Log failure retaining an incomplete trailing record. */ SyntheticCase partial(3, 10);
        partial.partial_log = true;
        partial.log += "{\"partial\":";
        partial.info.outcome = RunOutcome::logging_failure;
        partial.info.logging = LoggingStatus::partial;
        partial.info.failure_stage = "logging";
        partial.info.failure_code = "partial_capture";
        save(std::move(partial), out, "synthetic_partial_log");
        return 0;
    } catch (/* First preparation or writer error. */ const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
