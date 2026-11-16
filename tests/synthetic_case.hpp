#pragma once
#include <cmath>
#include <fstream>
#include <limits>
#include <serene/outputs/archive.hpp>
#include <string>
#include <vector>
/** Owned synthetic run fixture shared by archive, extension, bundle and measurement tests. Strings/vectors back the borrowed result/provenance APIs; mutation invalidates those borrows. Construct, customize, prepare, append, then write independent expected sidecars. */ struct SyntheticCase {
/** Run-kind shorthand used when constructing static versus transient fixtures. */     using Kind = serene::outputs::RunKind;
/** Owned archive/catalog metadata consumed by PreparedExport. */     serene::outputs::ArchiveDescription description;
/** Preparation budgets and frame capacity for the fixture. */     serene::outputs::ExportLimits limits;
    std::vector<double> data, ///< Owned sample-major scalar values backing ValueView.
        axis; ///< Owned time-seconds or static-factor coordinates backing the retained axis.
/** Accepted integration-step or static-level identities aligned with axis. */     std::vector<std::uint64_t> identities;
/** Run outcome, accepted population and axis interpretation copied into views. */     serene::outputs::RunInfoView info;
/** Exact original-input bytes, including formatting and terminal CRLF. */     std::string original{"{ \"fixture\": true, \"source\": \"original bytes\" }\r\n"};
/** Effective-input JSON backing its retained document view. */     std::string effective{"{\"fixture\":true,\"effective\":true}"};
    std::string overrides{"{\"argv\":[]}"}, ///< Owned exact override JSON backing its document view.
        resources{"{\"resources\":[]}"}; ///< Owned exact resource JSON backing its document view.
/** Execution-context JSON backing the context group. */     std::string context{"{\"argv\":[\"synthetic writer\",\"--case\",\"test\"],\"environment\":{}}"};
/** Exact JSONL log bytes; tests may append a partial trailing record. */     std::string log{"{\"event\":\"begin\"}\n{\"event\":\"end\"}\r\n"};
/** JSON association record for the run log. */     std::string log_info{"{\"association\":\"synthetic_run\",\"scope\":\"single_run\"}"};
    bool metadata{true}, ///< Whether provenance documents/logs are retained.
        partial_log{}, ///< Whether the retained log ends with an incomplete fragment.
        unavailable{}; ///< Whether observation catalogs/results are unavailable.
/** Construct deterministic channels-by-records data for archive tests and writers. channels and records size owned storage; kind selects time-series or static axis/layout conventions. No returned view outlives this owner. */
    SyntheticCase(std::size_t channels, std::size_t records, Kind kind = Kind::timeseries) {
    // Build deterministic archive metadata and alternating metre/radian channel descriptions.
    // Fill sample-major values, axis and accepted identities, including signed-zero/subnormal probes.
    // Set a completed run envelope with units and identity conventions matching the chosen run kind.
        /* Convenient reference to this fixture's owned description. */ auto &d = description;
        d.file_id = "00000000-0000-4000-8000-000000000007";
        d.created_utc = "2026-11-08T00:00:00Z";
        d.run.run_id = "synthetic_run";
        d.run.run_name = "Blade response";
        d.run.run_name_present = true;
        d.run.assembly_ref = "blade_test";
        d.run.kind = kind;
        d.run.record_capacity = records;
        limits.frame_values = 4;
        for (/* Request-catalog slot, alternating blade and shaft groups. */ std::size_t q = 0; q < (channels ? 2 : 0); q++)
            d.run.requests.push_back({q ? "shaft" : "blade", "motion", "3.0-wip.4", "1.0",
                                      "{\"fixture\":true,\"request_id\":\"motion\"}"});
        for (/* Logical channel index. */ std::size_t c = 0; c < channels; c++) {
            /* Group identity assigned to this alternating channel. */ auto group = c % 2 ? "shaft" : "blade";
            d.run.channels.push_back({std::string(group) + ".motion.c" + std::to_string(c), group,
                                      "motion", "c" + std::to_string(c), c % 2 ? "rad" : "m",
                                      c % 2});
        }
        data.resize(channels * records);
        axis.resize(records);
        identities.resize(records);
        for (/* Sample or accepted-level index. */ std::size_t j = 0; j < records; j++) {
            axis[j] = kind == Kind::timeseries ? .01 * j : double(j + 1) / records;
            identities[j] = kind == Kind::timeseries ? j : j + 1;
            for (/* Logical channel index. */ std::size_t c = 0; c < channels; c++)
                data[j * channels + c] = c ? c * 4. + j * .25 : 0.;
        }
        if (channels > 1 && records) {
            data[1] = -0.;
            if (records > 1)
                data[channels + 1] = std::numeric_limits<double>::denorm_min();
        }
        info.last_accepted_present = true;
        info.last_accepted_value = kind == Kind::timeseries ? .01 * records : 1.;
        info.requested_end = info.last_accepted_value;
        info.accepted_count = records;
        info.elapsed_seconds = .01;
        info.axis_name = kind == Kind::timeseries ? "time" : "proportional_load";
        info.axis_unit = kind == Kind::timeseries ? "s" : "1";
        info.identity_axis_convention = kind == Kind::timeseries
                                            ? "integration_step_zero_is_initial"
                                            : "accepted_level_one_is_first";
        if (!channels)
            info.observation = serene::outputs::ObservationStatus::disabled;
    }
/** Return borrowed result views over this object, valid until its vectors or metadata change. unavailable returns an empty observation view; static rows and time-series channels use different strides. */
    serene::outputs::RunResultView view() {
    // Copy the current run envelope and stop early for unavailable observations.
    // Bind borrowed axis/identity spans and choose row/column strides for the selected run kind.
        using namespace serene::outputs;
        /* Borrowed result envelope assembled for the current fixture. */ RunResultView v;
        v.info = info;
        if (unavailable)
            return v;
        /* Channel count; K is the retained record count. */ auto C = description.run.channels.size(), K = axis.size();
        v.axis = axis;
        v.accepted_indices = identities;
        v.identity_axis_present = true;
        v.values = *ValueView::strided(data, description.run.kind == Kind::timeseries ? C : K,
                                       description.run.kind == Kind::timeseries ? K : C,
                                       description.run.kind == Kind::timeseries ? 1 : C,
                                       description.run.kind == Kind::timeseries ? C : 1);
        return v;
    }
/** Return borrowed document/log views for archive append calls. metadata=false disables all groups; partial_log retains a raw incomplete log suffix. Strings must remain alive and unchanged through append. */
    serene::outputs::ProvenanceView provenance() {
    // Return disabled groups when provenance capture is off.
    // Otherwise bind the four input documents, execution context and exact log bytes without copying them.
        using namespace serene::outputs;
        /* Borrowed provenance groups assembled for append. */ ProvenanceView p;
        if (!metadata) {
            p.inputs.state = p.context.state = p.logs.state = SectionState::disabled;
            return p;
        }
        /* Small adapter from retained JSON text to an included document view. */ auto doc = [](std::string_view s) {
            // Borrow s as an included JSON document; caller-owned fixture strings outlive append.
            return DocumentView{SectionState::included, ByteSource::text(s), "application/json"};
        };
        p.inputs = {SectionState::included, doc(original), doc(effective), doc(overrides),
                    doc(resources)};
        p.context = {SectionState::included, doc(context)};
        p.logs.state = partial_log ? SectionState::partial : SectionState::included;
        p.logs.bytes = ByteSource::text(log);
        p.logs.schema = "synthetic-jsonl-1";
        p.logs.info_json = doc(log_info);
        return p;
    }
/** Write independent raw value, axis and shape sidecars under filename base for reader comparisons. Uses native binary64 bytes in the fixture environment; does not serialize a production archive. */
    void expected(const std::filesystem::path &base) {
    // Open the values sidecar and emit the logical matrix in archive order.
    // Write the axis and logical dimensions alongside it for independent readers.
        /* Binary expected-value sidecar stream. */ std::ofstream values(base.string() + ".expected.raw", std::ios::binary);
        /* Channel count; K is the retained record count. */ auto C = description.run.channels.size(), K = axis.size();
        if (description.run.kind == Kind::timeseries) {
            for (/* Logical channel index. */ std::size_t c = 0; c < C; c++)
                for (/* Sample or accepted-level index. */ std::size_t j = 0; j < K; j++)
                    values.write(reinterpret_cast<const char *>(&data[j * C + c]), 8);
        } else
            values.write(reinterpret_cast<const char *>(data.data()), data.size() * 8);
        /* Binary axis sidecar stream. */ std::ofstream times(base.string() + ".axis.raw", std::ios::binary);
        times.write(reinterpret_cast<const char *>(axis.data()), axis.size() * 8);
        /* Text logical-dimension sidecar stream. */ std::ofstream shape(base.string() + ".shape.txt");
        shape << (description.run.kind == Kind::timeseries ? C : K) << ' '
              << (description.run.kind == Kind::timeseries ? K : C) << '\n';
    }
};
