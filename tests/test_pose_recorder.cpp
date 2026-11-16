#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <serene/outputs/pose_recorder.hpp>
using namespace serene::outputs;
namespace pose = serene::outputs::pose;
using Json = nlohmann::json;
/* Number of CHECK failures; assertions continue to expose multiple recorder regressions. */ static int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "FAIL " << __LINE__ << ": " #x "\n";                                      \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)
/** Borrowed fault-hook context used by main/fail/limit. Toggle enabled after accepted frames to fail publication; short_write selects physical truncation rather than logical status injection. */ struct Fault {
/** Whether injection is currently active. */     bool enabled{};
/** Logical I/O boundary selected for failure. */     pose::IoOperation op{};
/** Select byte-count reduction instead of the logical failure callback. */     bool short_write{};
};
/** Borrow Fault p and reject enabled matching op unless short-write injection is selected. The unused ordinal is ignored; returns whether the recorder should fail this boundary. */
bool fail(void *p, pose::IoOperation op, std::uint64_t) noexcept {
    // Read the callback configuration and select only the enabled non-short-write operation.

    /* Borrowed synchronous callback configuration. */ auto &f = *static_cast<Fault *>(p);
    return f.enabled && !f.short_write && f.op == op;
}
/** Return allowed physical write bytes for requested n using borrowed Fault p. Enabled short writes over 80 bytes are cut to 40; unused ordinal is ignored. */
std::size_t limit(void *p, std::uint64_t, std::size_t n) noexcept {
    // Apply the short-write limit only to the selected enabled fault mode.

    /* Borrowed synchronous callback configuration. */ auto &f = *static_cast<Fault *>(p);
    return f.enabled && f.short_write && n > 80 ? 40 : n;
}
/** Exercise the raw pose recorder. argv[1] must be a new output directory and argv[2] the catalog JSON; returns two for setup failure, one for failed checks, otherwise zero. */
int main(int argc, char **argv) {
    // Load the catalog and prepare explicit world translations/quaternions plus transient stamps.
    // Exercise batching, static/prepared transitions, abandonment, empty populations and publication faults.
    // Reject invalid poses/stamps/catalogs, then verify fixed batch/index storage across a long stream.

    if (argc != 3)
        return 2;
    /* Fresh output directory supplied by the test harness. */ const std::filesystem::path dir = argv[1];
    if (!std::filesystem::create_directory(dir))
        return 2;
    /* Catalog JSON input stream. */ std::ifstream in(argv[2]);
    /* Owned catalog reused and copied for malformed-input tests. */ Json cat;
    in >> cat;
    /* Canonical catalog JSON text passed into recorder open. */ const auto text = cat.dump();
    /* sqrt(1/2), used for unit-quaternion rotation probes. */ const double q = std::sqrt(0.5);
    /* Four sample poses in catalog order: xyz metres followed by quaternion wxyz. */ std::array<double, 28> base{1, 2, 3, q, 0, 0, q, 1,  2, 8, 1, 0, 0, 0,
                                2, 2, 8, q, q, 0, 0, -4, 6, 1, 1, 0, 0, 0};
    /* Second frame with changed sample translations. */ auto second = base;
    second[7] = 1.1;
    second[8] = 2.2;
    second[9] = 8.3;
    second[14] = 2.1;
    second[15] = 2.2;
    second[16] = 8.3;
    /* Final frame, including an equivalent sign-flipped unit quaternion. */ auto third = base;
    third[7] = 1.25;
    third[8] = 2.5;
    third[9] = 8.75;
    third[14] = 2.25;
    third[15] = 2.5;
    third[16] = 8.75;
    third[17] = -q;
    third[18] = -q;
    /* Initial, scheduled and target transient stamps in seconds with accepted-step identities. */ const pose::Stamp initial{pose::Analysis::transient, pose::Event::initial, 1.25, 0, 0},
        middle{pose::Analysis::transient, pose::Event::scheduled, 1.29, 4, 0},
        target{pose::Analysis::transient, pose::Event::target, 1.32, 7, 0};
    /* Small batch limits forcing full and partial flushes. */ pose::Options opts;
    opts.max_batch_frames = 2;
    opts.max_batch_bytes = 1024;
    /** Open fresh filename n with optional fault hook under the shared catalog/options. Returns the recorder/status result and borrows enclosing values only for the call. */
auto open = [&](const std::string &n, pose::FaultHook hook = {}) {
    // Forward the shared catalog and limits with the caller-selected name and fault hook.

        return pose::Recorder::open(dir / n, text, opts, hook);
    };
    {
        /* Owned recorder for this independent output/failure scenario. */ auto w = open("transient.spose");
        CHECK(w);
        if (!w)
            return 1;
        CHECK(w->append_frame(initial, base));
        CHECK(w->statistics().accepted_frames == 1 && w->statistics().persisted_frames == 0);
        CHECK(w->append_frame(middle, second));
        CHECK(w->statistics().persisted_frames == 2);
        CHECK(w->append_frame(target, third));
        CHECK(w->statistics().buffered_frames == 1);
        CHECK(w->finalize({pose::Outcome::completed, pose::Outcome::completed}));
        /* Statistics snapshot after successful publication. */ auto st = w->statistics();
        CHECK(st.persisted_frames == 3 && st.pose_chunks == 2 && st.batch_capacity_bytes == 576 &&
              st.index_workspace_bytes == 256);
        CHECK(w->state() == pose::State::published &&
              !std::filesystem::exists(w->temporary_path()));
        CHECK(!w->finalize());
        CHECK(!w->append_frame(target, base));
        CHECK(!open("transient.spose"));
    }
    {
        /* Owned recorder for this independent output/failure scenario. */ auto w = open("static.spose");
        CHECK(w);
        CHECK(w->append_frame({pose::Analysis::static_equilibrium, pose::Event::target, 0, 3, 1},
                              base));
        CHECK(w->finalize({pose::Outcome::completed, pose::Outcome::completed}));
    }
    {
        /* Owned recorder for this independent output/failure scenario. */ auto w = open("prepared_then_transient.spose");
        CHECK(w);
        CHECK(w->append_frame({}, base));
        CHECK(w->append_frame({pose::Analysis::transient, 14, 1.25, 0, 0}, base));
        CHECK(w->finalize({pose::Outcome::failed, pose::Outcome::completed}));
    }
    {
        /* Owned recorder for this independent output/failure scenario. */ auto w = open("abandoned.spose");
        CHECK(w);
        CHECK(w->append_frame(initial, base));
        CHECK(w->append_frame(middle, second));
        CHECK(w->append_frame(target, third));
    }
    CHECK(!std::filesystem::exists(dir / "abandoned.spose"));
    CHECK(!open("abandoned.spose"));
    {
        /* Owned recorder for this independent output/failure scenario. */ auto w = open("no_frames.spose");
        CHECK(w);
        CHECK(w->finalize({pose::Outcome::failed, pose::Outcome::stopped}));
    }
    {
        /* Catalog copy with zero visual sample/connectivity population. */ auto empty = cat;
        for (/* Catalog population array being cleared for the empty test. */ auto key : {"samples", "segments", "connections", "port_aliases"})
            empty[key] = Json::array();
        /* Owned recorder for this independent output/failure scenario. */ auto w = pose::Recorder::open(dir / "empty_population.spose", empty.dump());
        CHECK(w);
        CHECK(w->append_frame({}, {}));
        CHECK(w->finalize());
    }
    for (/* Publication boundary selected for fault injection. */ auto op : {pose::IoOperation::write, pose::IoOperation::flush, pose::IoOperation::close,
                    pose::IoOperation::publish}) {
        /* Fault configuration that outlives its recorder callbacks. */ Fault f{false, op, false};
        /* Distinct output name for this injected operation. */ auto name = "fault_" + std::to_string(static_cast<int>(op)) + ".spose";
        /* Owned recorder for this independent output/failure scenario. */ auto w = open(name, {&f, fail, limit});
        CHECK(w);
        CHECK(w->append_frame(initial, base));
        CHECK(w->append_frame(middle, second));
        CHECK(w->append_frame(target, third));
        f.enabled = true;
        CHECK(!w->finalize());
        CHECK(w->state() == pose::State::failed);
        /* First failure status, which subsequent finalize calls must preserve. */ auto first = w->status();
        CHECK(!w->finalize());
        CHECK(w->status().code == first.code);
        CHECK(!std::filesystem::exists(dir / name));
    }
    {
        /* Fault configuration that outlives its recorder callbacks. */ Fault f{false, pose::IoOperation::write, true};
        /* Owned recorder for this independent output/failure scenario. */ auto w = open("short_write.spose", {&f, fail, limit});
        CHECK(w);
        f.enabled = true;
        CHECK(w->append_frame(initial, base));
        CHECK(!w->append_frame(middle, second));
        CHECK(w->statistics().accepted_frames == 2 && w->statistics().persisted_frames == 0);
    }
    {
        /* Owned recorder for this independent output/failure scenario. */ auto w = open("collision.spose");
        CHECK(w);
        CHECK(w->append_frame(initial, base));
        {
            /* Competing final-file writer for no-clobber testing. */ std::ofstream f(dir / "collision.spose");
            f << "do not replace";
        }
        CHECK(!w->finalize());
        /* Readback stream for the competing destination. */ std::ifstream f(dir / "collision.spose");
        /* Readback sentinel text. */ std::string s;
        std::getline(f, s);
        CHECK(s == "do not replace");
    }
    {
        /* Owned recorder for this independent output/failure scenario. */ auto w = open("invalid_pose.spose");
        CHECK(w);
        /* Local invalid pose or catalog copy preserving the valid baseline. */ auto bad = base;
        bad[0] = std::numeric_limits<double>::quiet_NaN();
        CHECK(!w->append_frame(initial, bad));
        CHECK(w->statistics().accepted_frames == 0);
    }
    {
        /* Owned recorder for this independent output/failure scenario. */ auto w = open("invalid_quaternion.spose");
        CHECK(w);
        /* Local invalid pose or catalog copy preserving the valid baseline. */ auto bad = base;
        bad[3] = 3;
        CHECK(!w->append_frame(initial, bad));
    }
    {
        /* Owned recorder for this independent output/failure scenario. */ auto w = open("invalid_stamp.spose");
        CHECK(w);
        CHECK(w->append_frame(initial, base));
        CHECK(!w->append_frame(initial, base));
    }
    {
        /* Owned recorder for this independent output/failure scenario. */ auto w = open("after_target.spose");
        CHECK(w);
        CHECK(w->append_frame(target, base));
        CHECK(!w->append_frame(middle, base));
    }
    {
        /* Options copy used for exact batch-byte admission tests. */ auto o = opts;
        o.max_batch_bytes = 287;
        CHECK(!pose::Recorder::open(dir / "too_small.spose", text, o));
        o.max_batch_bytes = 600;
        o.max_batch_frames = 100;
        /* Owned recorder for this independent output/failure scenario. */ auto w = pose::Recorder::open(dir / "byte_limit.spose", text, o);
        CHECK(w);
        CHECK(w->statistics().effective_batch_frames == 2 &&
              w->statistics().batch_capacity_bytes == 576);
        CHECK(w->finalize());
    }
    CHECK(!pose::Recorder::open(dir / "bom_catalog.spose", std::string("\xef\xbb\xbf") + text));
    for (/* Malformed catalog variant selector. */ int k = 0; k < 5; ++k) {
        /* Local invalid pose or catalog copy preserving the valid baseline. */ auto bad = cat;
        if (k == 0)
            bad["catalog_version"] = 1.0;
        if (k == 1)
            bad.erase("connections");
        if (k == 2)
            bad["occurrences"][0]["parent_id"] = 21;
        if (k == 3)
            bad["samples"][0]["id"] = 1;
        if (k == 4)
            bad["connections"][0]["first_sample_id"] = 0;
        CHECK(!pose::Recorder::open(dir / ("bad_catalog_" + std::to_string(k) + ".spose"),
                                    bad.dump()));
    }
    {
        /* Owned recorder for this independent output/failure scenario. */ auto w = open("long_bounded.spose");
        CHECK(w);
        /* Allocated batch capacity before the long append sequence. */ auto cap = w->statistics().batch_capacity_bytes;
        for (/* Accepted-step identity and scheduled frame index in the long run. */ std::uint64_t i = 0; i < 1001; ++i)
            CHECK(w->append_frame({pose::Analysis::transient, i == 0 ? 2U : (i == 1000 ? 8U : 4U),
                                   1.25 + 0.01 * static_cast<double>(i), i, 0},
                                  base));
        CHECK(w->statistics().batch_capacity_bytes == cap &&
              w->statistics().index_workspace_bytes == 256);
        CHECK(w->finalize({pose::Outcome::completed, pose::Outcome::completed}));
        CHECK(w->statistics().pose_chunks == 501);
    }
    std::cout << "pose recorder checks: " << (failures ? "FAILED" : "passed")
              << "; output=" << dir.string() << "\n";
    return failures ? 1 : 0;
}
