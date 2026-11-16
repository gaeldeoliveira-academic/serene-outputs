#include "synthetic_case.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace serene::outputs;
/** Require ok; throw borrowed message when an archive test fails. */
void check(bool ok, const char *message) {
    // Convert failed assertions to the executable's common exception report.

    if (!ok)
        throw std::runtime_error(message);
}
/** Small borrowed fault-hook context used by main/fail. Selects one operation and its first failing ordinal; lives through the associated archive session. */ struct Injection {
/** I/O operation selected for injection. */     IoOperation operation;
/** First matching operation ordinal that fails; zero selects the earliest eligible call. */     std::uint64_t minimum_ordinal{};
};
/** Fault callback borrowing Injection ctx. Fails matching op at or beyond minimum_ordinal, except the initial write; returns a synthetic native/status code and never throws. */
Status fail(void *ctx, IoOperation op, std::uint64_t ordinal) noexcept {
    // Inspect the selected operation and ordinal, allow unmatched activity, then construct the injected failure.

    /* Borrowed selected fault configuration. */ auto &injection = *static_cast<Injection *>(ctx);
    if (op != injection.operation || ordinal < injection.minimum_ordinal ||
        (op == IoOperation::write && ordinal == 0))
        return {};
    /* Failure returned to the writer or the first append/finalize failure. */ auto result = error(op == IoOperation::publish ? StatusCode::publication_failure
                                                   : StatusCode::io_failure);
    result.native_code = op == IoOperation::write ? 28 : 5;
    return result;
}
/** Return the allowed count for one physical write. The unused context is ignored; ordinal five is cut in half to exercise a real short fwrite. */
std::size_t short_write(void *, std::uint64_t ordinal, std::size_t count) noexcept {
    // Limit only the selected physical write ordinal; leave all other writes unchanged.

    return ordinal == 5 ? count / 2 : count;
}
/** Run archive fault, retry and no-clobber checks in a fresh directory. Returns zero on success or one after printing the first exception. */
int main() {
    // Prepare one reusable source, codec and workspace, then inject each I/O boundary failure and retry.
    // Probe oversized JSONL, actual short writes, original JSON bytes and Unicode paths.
    // Verify pre-existing and racing destinations remain untouched and temporary files are cleaned.

    try {
        /* Fresh directory isolating all publication and fault cases. */ auto root = std::filesystem::current_path() /
                    ("archive-tests-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        /* Owned numeric/provenance buffers reused across retries. */ SyntheticCase c(3, 10);
        /* Prepared exporter whose buffers are reused sequentially. */ auto p = PreparedExport::prepare(c.description, c.limits);
        /* Codec reused sequentially across independent sessions. */ auto codec = Codec::create(c.limits.frame_values);
        check(bool(p) && bool(codec), "prepare");
        /* Borrowed result view over c. */ auto v = c.view();
        /* Borrowed exact provenance bytes over c. */ auto provenance = c.provenance();
        for (/* Current fault boundary and minimum call ordinal. */ auto injection : {Injection{IoOperation::source}, Injection{IoOperation::codec},
                               Injection{IoOperation::write}, Injection{IoOperation::flush},
                               Injection{IoOperation::close}, Injection{IoOperation::publish},
                               Injection{IoOperation::seek}, Injection{IoOperation::seek, 2}}) {
            /* Fresh final archive path for this case. */ auto target =
                root / ("fault-" + std::to_string(static_cast<unsigned>(injection.operation)) +
                        "-" + std::to_string(injection.minimum_ordinal) + ".serene");
            /* Owned temporary path captured to verify cleanup after session destruction. */ std::filesystem::path temporary;
            {
                /* Owning archive session under the current fault policy. */ auto session = ArchiveSession::open(*p, *codec, target, {&injection, fail});
                check(bool(session), "fault open");
                temporary = session->temporary_path();
                /* Append result before optional finalization. */ auto a = session->append_run(v, provenance);
                /* Failure returned to the writer or the first append/finalize failure. */ auto result = a ? session->finalize() : a;
                check(!result && session->state() == SessionState::failed, "injected failure");
                check(!std::filesystem::exists(target), "no partial final");
            }
            check(!std::filesystem::exists(temporary), "owned temporary cleanup");
            /* Fresh session proving same-source retry after failed publication. */ auto retry = ArchiveSession::open(*p, *codec, target);
            check(retry && retry->append_run(v, provenance) && retry->finalize(),
                  "retry with same sources");
            check(retry->statistics().frames == 9, "frame count");
        }
        {
            /* Fixture whose complete JSONL record exceeds the accepted bound. */ SyntheticCase oversized_log(3, 10);
            oversized_log.log = "{\"padding\":\"" + std::string(1024 * 1024, 'a') + "\"}\n";
            /* Fresh final archive path for this case. */ auto target = root / "oversized-record.serene";
            /* Owning archive session under the current fault policy. */ auto session = ArchiveSession::open(*p, *codec, target);
            check(bool(session), "oversized record open");
            /* Expected log-record resource-limit result. */ auto rejected = session->append_run(oversized_log.view(), oversized_log.provenance());
            check(!rejected && rejected.code == StatusCode::resource_limit,
                  "oversized complete JSONL record rejected before publication");
            check(!std::filesystem::exists(target), "oversized record not published");
        }
        {
            /* Fresh final archive path for this case. */ auto target = root / "physical-short-write.serene";
            /* Owned temporary path captured to verify cleanup after session destruction. */ std::filesystem::path temporary;
            {
                /* Owning archive session under the current fault policy. */ auto session =
                    ArchiveSession::open(*p, *codec, target, {nullptr, nullptr, short_write});
                check(bool(session), "short-write open");
                temporary = session->temporary_path();
                check(!session->append_run(v, provenance) &&
                          session->state() == SessionState::failed,
                      "actual partial fwrite rejected");
                check(!std::filesystem::exists(target), "short write not published");
            }
            check(!std::filesystem::exists(temporary), "short-write temporary removed");
            /* Fresh session proving same-source retry after failed publication. */ auto retry = ArchiveSession::open(*p, *codec, target);
            check(retry && retry->append_run(v, provenance) && retry->finalize(),
                  "short-write retry");
            check(retry->statistics().seek_calls > 0 && retry->statistics().written_bytes > 128,
                  "physical seek and write metrics");
        }
        {
            /* Fixture retaining an escaped NUL in original JSON evidence. */ SyntheticCase document_nul(3, 10);
            document_nul.original = R"({"authored_value":"\u0000"})";
            /* Fresh final archive path for this case. */ auto target = root / "original-document-nul.serene";
            /* Owning archive session under the current fault policy. */ auto session = ArchiveSession::open(*p, *codec, target);
            check(session && session->append_run(document_nul.view(), document_nul.provenance()) &&
                      session->finalize(),
                  "original document escaped NUL retained");
        }
        {
            /* Fresh final archive path for this case. */ auto target = root / std::filesystem::path(u8"archive-\u03b1.serene");
            /* Owning archive session under the current fault policy. */ auto session = ArchiveSession::open(*p, *codec, target);
            check(session && session->append_run(v, provenance) && session->finalize(),
                  "Unicode native archive pathname");
            check(std::filesystem::exists(target), "Unicode pathname published");
        }
        /* Already-existing destination sentinel path. */ auto collision = root / "existing.serene";
        {
            /* Sentinel writer proving no-clobber admission. */ std::ofstream out(collision);
            out << "sentinel";
        }
        check(!ArchiveSession::open(*p, *codec, collision), "no-clobber preflight");
        /* Existing-file readback stream. */ std::ifstream original(collision);
        /* Readback sentinel contents. */ std::string text;
        original >> text;
        check(text == "sentinel", "existing preserved");
        /* Destination created after open but before finalization. */ auto race = root / "race.serene";
        {
            /* Session whose final destination loses the publication race. */ auto s = ArchiveSession::open(*p, *codec, race);
            check(s && s->append_run(v, provenance), "race setup");
            {
                /* Competing destination writer. */ std::ofstream other(race);
                other << "racer";
            }
            check(!s->finalize(), "no-clobber publication race");
        }
        /* Readback stream for the race winner. */ std::ifstream raced(race);
        raced >> text;
        check(text == "racer", "race file preserved");
        std::cout << "archive failure/retry/collision tests passed at " << root.string() << '\n';
        return 0;
    } catch (/* First test failure printed before returning nonzero. */ const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
