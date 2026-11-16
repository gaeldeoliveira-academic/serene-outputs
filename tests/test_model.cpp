#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <serene/outputs/model.hpp>
#include <stdexcept>
#include <utility>
using namespace serene::outputs;
/** Require predicate p; throw runtime_error containing borrowed diagnostic m on failure. Used by this executable instead of a test framework. */
void check(bool p, const char *m) {
    // Turn a failed predicate into the executable's common exception failure path.
    if (!p)
        throw std::runtime_error(m);
}
/** Return a minimal owned two-channel, three-record description used by model admission checks. Each call returns a fresh mutable fixture. */
ArchiveDescription desc() {
    // Fill stable file/run identities and a single effective request.
    // Attach two metre-valued channels that refer to that request.
    /* Fresh owned metadata fixture. */ ArchiveDescription d;
    d.file_id = "00000000-0000-4000-8000-000000000001";
    d.created_utc = "2026-11-08T00:00:00Z";
    /* Run metadata within the owned archive description. */ auto &r = d.run;
    r.run_id = "test";
    r.assembly_ref = "blade";
    r.record_capacity = 3;
    r.requests.push_back({"group", "position", "3.0-wip.4", "1.0", "{\"quantity\":\"position\"}"});
    r.channels.push_back({"group.position.x", "group", "position", "x", "m", 0});
    r.channels.push_back({"group.position.y", "group", "position", "y", "m", 0});
    return d;
}
/** Run model/view, ownership, budget and JSON admission checks. Returns zero on success or one after reporting the first exception. */
int main() {
    // Exercise strided gathering and malformed dimensions, then prepare and move an exporter.
    // Probe retained metadata capacities and exact workspace/metadata limits.
    // Validate duplicate keys, UTF-8, numeric/depth/node bounds and prepared buffer accounting.
    try {
        /* Sample-major source values for gather checks. */ std::array<double, 6> data{1, 2, 3, 4, 5, 6};
        /* Validated strided borrowed matrix view. */ auto v = ValueView::strided(data, 2, 3, 1, 2);
        check(bool(v), "strided create");
        /* Gather destination for two selected values. */ std::array<double, 2> dst{};
        check(bool(v->fill(1, 1, dst)) && dst[0] == 4 && dst[1] == 6, "sample-major gather");
        check(!v->fill(1, 2, dst), "short range");
        check(!ValueView::strided(data, 2, 4, 1, 2), "source extent");
        check(!ValueView::strided(data, UINT64_MAX, 3, 1, 2), "dimension overflow");
        check(!ValueView::strided(data, 2, 3, 0, 2), "zero stride");
        /* Copy mutated to probe UUID/date admission. */ auto bad_identity = desc();
        bad_identity.file_id = "not-a-uuid-but-exactly-thirtysix-bytes";
        check(!PreparedExport::prepare(bad_identity), "UUID rejected");
        bad_identity = desc();
        bad_identity.created_utc = "2026-02-31T00:00:00Z";
        check(!PreparedExport::prepare(bad_identity), "invalid UTC date rejected");
        /* Prepared exporter under test. */ auto p = PreparedExport::prepare(desc());
        check(bool(p), "prepare");
        /* Borrowed result envelope used for repeated validation. */ RunResultView rv;
        /* Three retained times in seconds. */ std::array<double, 3> t{0, .04, .08};
        rv.axis = t;
        rv.values = *v;
        rv.info.requested_end = .1;
        check(bool(p->validate(rv)), "view validation");
        for (/* Repetition or generated-node index for the bounded check. */ int k = 0; k < 1000; k++)
            check(bool(v->fill(0, 0, std::span(p->gather_buffer()).first(3))), "repeat fill");
        /* Plan snapshot used to verify move preservation. */ auto old_plan = p->plan();
        /* New owner after moving the prepared exporter. */ auto moved = std::move(*p);
        check(moved.valid() && !p->valid() && moved.plan().total_bytes == old_plan.total_bytes,
              "move contract");
        check(!p->validate(rv), "moved invalid");
        /* Limits deliberately reduced below admission requirements. */ ExportLimits small;
        small.workspace_budget_bytes = 1;
        check(!PreparedExport::prepare(desc(), small), "workspace budget");
        small = {};
        small.metadata_budget_bytes = 1;
        check(!PreparedExport::prepare(desc(), small), "metadata budget");
        /* Description with excessive retained container capacities. */ auto over_reserved = desc();
        over_reserved.run.channels.reserve(1024);
        over_reserved.writer_name.reserve(65536);
        /* Workspace ceiling copied from a valid prepared plan. */ auto exact_budget = ExportLimits{};
        exact_budget.workspace_budget_bytes = moved.plan().total_bytes;
        /* Expected resource-limit result for excess retained capacity. */ auto retained_capacity = PreparedExport::prepare(std::move(over_reserved), exact_budget);
        check(!retained_capacity && retained_capacity.error().code == StatusCode::resource_limit,
              "retained moved metadata capacities counted in aggregate budget");
        /* Fresh description mutated for one invalid-contract case at a time. */ auto bad = desc();
        bad.run.channels[1].scalar_id = bad.run.channels[0].scalar_id;
        check(!PreparedExport::prepare(std::move(bad)), "duplicate scalar");
        bad = desc();
        bad.run.requests[0].effective_json = "{\"x\":1,\"x\":2}";
        check(!PreparedExport::prepare(std::move(bad)), "duplicate JSON key");
        check(!validate_json_object("{\"x\":NaN}"), "nonfinite JSON");
        check(!valid_utf8(std::string_view("\xc0\x80", 2)), "overlong UTF8");
        check(bool(validate_json_object("{\"x\":{\"y\":1},\"z\":[1,2]}")), "nested JSON");
        check(!validate_json_object(R"({"x":"\u0000"})"), "request escaped NUL rejected");
        check(!validate_json_object(R"({"x":"\ud800"})"), "unpaired surrogate rejected");
        check(!validate_json_object(R"({"x":1,"\u0078":2})"), "decoded duplicate key rejected");
        check(bool(validate_json_object(R"({"x":"\ud83d\ude00"})")), "valid surrogate pair");
        /* JSON nesting probe beyond the metadata parser depth ceiling. */ auto deep =
            std::string("{\"x\":") + std::string(16, '[') + "0" + std::string(16, ']') + "}";
        check(validate_json_object(deep).code == StatusCode::resource_limit,
              "request depth ceiling");
        /* Overlong numeric token probe. */ auto number = "{\"x\":" + std::string(129, '1') + "}";
        check(validate_json_object(number).code == StatusCode::resource_limit,
              "numeric token bound");
        /* Generated JSON array used to exceed the value-complexity ceiling. */ std::string nodes = "{\"x\":[";
        for (/* Repetition or generated-node index for the bounded check. */ int k = 0; k < 16384; k++)
            nodes += k ? ",0" : "0";
        nodes += "]}";
        check(validate_json_object(nodes).code == StatusCode::resource_limit,
              "value complexity ceiling");
        check(moved.directory_buffer().size() == moved.plan().directory_bytes &&
                  moved.serialization_buffer().size() == moved.plan().serialization_bytes,
              "custom container buffers prepared and budgeted");
        std::cout << "model checks passed; workspace=" << moved.plan().total_bytes << "\n";
        return 0;
    } catch (/* First reported test failure. */ const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
