#pragma once
#include <cmath>
#include <fstream>
#include <limits>
#include <serene/outputs/archive.hpp>
#include <string>
#include <vector>
struct SyntheticCase {
    using Kind = serene::outputs::RunKind;
    serene::outputs::ArchiveDescription description;
    serene::outputs::ExportLimits limits;
    std::vector<double> data, axis;
    std::vector<std::uint64_t> identities;
    serene::outputs::RunInfoView info;
    std::string original{"{ \"fixture\": true, \"source\": \"original bytes\" }\r\n"};
    std::string effective{"{\"fixture\":true,\"effective\":true}"};
    std::string overrides{"{\"argv\":[]}"}, resources{"{\"resources\":[]}"};
    std::string context{"{\"argv\":[\"synthetic writer\",\"--case\",\"test\"],\"environment\":{}}"};
    std::string log{"{\"event\":\"begin\"}\n{\"event\":\"end\"}\r\n"};
    std::string log_info{"{\"association\":\"synthetic_run\",\"scope\":\"single_run\"}"};
    bool metadata{true}, partial_log{}, unavailable{};
    SyntheticCase(std::size_t channels, std::size_t records, Kind kind = Kind::timeseries) {
        auto &d = description;
        d.file_id = "00000000-0000-4000-8000-000000000007";
        d.created_utc = "2026-11-08T00:00:00Z";
        d.run.run_id = "synthetic_run";
        d.run.run_name = "Blade response";
        d.run.run_name_present = true;
        d.run.assembly_ref = "blade_test";
        d.run.kind = kind;
        d.run.record_capacity = records;
        limits.frame_values = 4;
        for (std::size_t q = 0; q < (channels ? 2 : 0); q++)
            d.run.requests.push_back({q ? "shaft" : "blade", "motion", "3.0-wip.4", "1.0",
                                      "{\"fixture\":true,\"request_id\":\"motion\"}"});
        for (std::size_t c = 0; c < channels; c++) {
            auto group = c % 2 ? "shaft" : "blade";
            d.run.channels.push_back({std::string(group) + ".motion.c" + std::to_string(c), group,
                                      "motion", "c" + std::to_string(c), c % 2 ? "rad" : "m",
                                      c % 2});
        }
        data.resize(channels * records);
        axis.resize(records);
        identities.resize(records);
        for (std::size_t j = 0; j < records; j++) {
            axis[j] = kind == Kind::timeseries ? .01 * j : double(j + 1) / records;
            identities[j] = kind == Kind::timeseries ? j : j + 1;
            for (std::size_t c = 0; c < channels; c++)
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
    serene::outputs::RunResultView view() {
        using namespace serene::outputs;
        RunResultView v;
        v.info = info;
        if (unavailable)
            return v;
        auto C = description.run.channels.size(), K = axis.size();
        v.axis = axis;
        v.accepted_indices = identities;
        v.identity_axis_present = true;
        v.values = *ValueView::strided(data, description.run.kind == Kind::timeseries ? C : K,
                                       description.run.kind == Kind::timeseries ? K : C,
                                       description.run.kind == Kind::timeseries ? 1 : C,
                                       description.run.kind == Kind::timeseries ? C : 1);
        return v;
    }
    serene::outputs::ProvenanceView provenance() {
        using namespace serene::outputs;
        ProvenanceView p;
        if (!metadata) {
            p.inputs.state = p.context.state = p.logs.state = SectionState::disabled;
            return p;
        }
        auto doc = [](std::string_view s) {
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
    void expected(const std::filesystem::path &base) {
        std::ofstream values(base.string() + ".expected.raw", std::ios::binary);
        auto C = description.run.channels.size(), K = axis.size();
        if (description.run.kind == Kind::timeseries) {
            for (std::size_t c = 0; c < C; c++)
                for (std::size_t j = 0; j < K; j++)
                    values.write(reinterpret_cast<const char *>(&data[j * C + c]), 8);
        } else
            values.write(reinterpret_cast<const char *>(data.data()), data.size() * 8);
        std::ofstream times(base.string() + ".axis.raw", std::ios::binary);
        times.write(reinterpret_cast<const char *>(axis.data()), axis.size() * 8);
        std::ofstream shape(base.string() + ".shape.txt");
        shape << (description.run.kind == Kind::timeseries ? C : K) << ' '
              << (description.run.kind == Kind::timeseries ? K : C) << '\n';
    }
};
