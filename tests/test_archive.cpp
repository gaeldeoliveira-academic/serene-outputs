#include "synthetic_case.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace serene::outputs;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Injection {
    IoOperation operation;
    std::uint64_t minimum_ordinal{};
};
Status fail(void *ctx, IoOperation op, std::uint64_t ordinal) noexcept {
    auto &injection = *static_cast<Injection *>(ctx);
    if (op != injection.operation || ordinal < injection.minimum_ordinal ||
        (op == IoOperation::write && ordinal == 0))
        return {};
    auto result = error(op == IoOperation::publish ? StatusCode::publication_failure
                                                   : StatusCode::io_failure);
    result.native_code = op == IoOperation::write ? 28 : 5;
    return result;
}
std::size_t short_write(void *, std::uint64_t ordinal, std::size_t count) noexcept {
    return ordinal == 5 ? count / 2 : count;
}
int main() {
    try {
        auto root = std::filesystem::current_path() /
                    ("archive-tests-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        SyntheticCase c(3, 10);
        auto p = PreparedExport::prepare(c.description, c.limits);
        auto codec = Codec::create(c.limits.frame_values);
        check(bool(p) && bool(codec), "prepare");
        auto v = c.view();
        auto provenance = c.provenance();
        for (auto injection : {Injection{IoOperation::source}, Injection{IoOperation::codec},
                               Injection{IoOperation::write}, Injection{IoOperation::flush},
                               Injection{IoOperation::close}, Injection{IoOperation::publish},
                               Injection{IoOperation::seek}, Injection{IoOperation::seek, 2}}) {
            auto target =
                root / ("fault-" + std::to_string(static_cast<unsigned>(injection.operation)) +
                        "-" + std::to_string(injection.minimum_ordinal) + ".serene");
            std::filesystem::path temporary;
            {
                auto session = ArchiveSession::open(*p, *codec, target, {&injection, fail});
                check(bool(session), "fault open");
                temporary = session->temporary_path();
                auto a = session->append_run(v, provenance);
                auto result = a ? session->finalize() : a;
                check(!result && session->state() == SessionState::failed, "injected failure");
                check(!std::filesystem::exists(target), "no partial final");
            }
            check(!std::filesystem::exists(temporary), "owned temporary cleanup");
            auto retry = ArchiveSession::open(*p, *codec, target);
            check(retry && retry->append_run(v, provenance) && retry->finalize(),
                  "retry with same sources");
            check(retry->statistics().frames == 9, "frame count");
        }
        {
            SyntheticCase oversized_log(3, 10);
            oversized_log.log = "{\"padding\":\"" + std::string(1024 * 1024, 'a') + "\"}\n";
            auto target = root / "oversized-record.serene";
            auto session = ArchiveSession::open(*p, *codec, target);
            check(bool(session), "oversized record open");
            auto rejected = session->append_run(oversized_log.view(), oversized_log.provenance());
            check(!rejected && rejected.code == StatusCode::resource_limit,
                  "oversized complete JSONL record rejected before publication");
            check(!std::filesystem::exists(target), "oversized record not published");
        }
        {
            auto target = root / "physical-short-write.serene";
            std::filesystem::path temporary;
            {
                auto session =
                    ArchiveSession::open(*p, *codec, target, {nullptr, nullptr, short_write});
                check(bool(session), "short-write open");
                temporary = session->temporary_path();
                check(!session->append_run(v, provenance) &&
                          session->state() == SessionState::failed,
                      "actual partial fwrite rejected");
                check(!std::filesystem::exists(target), "short write not published");
            }
            check(!std::filesystem::exists(temporary), "short-write temporary removed");
            auto retry = ArchiveSession::open(*p, *codec, target);
            check(retry && retry->append_run(v, provenance) && retry->finalize(),
                  "short-write retry");
            check(retry->statistics().seek_calls > 0 && retry->statistics().written_bytes > 128,
                  "physical seek and write metrics");
        }
        {
            SyntheticCase document_nul(3, 10);
            document_nul.original = R"({"authored_value":"\u0000"})";
            auto target = root / "original-document-nul.serene";
            auto session = ArchiveSession::open(*p, *codec, target);
            check(session && session->append_run(document_nul.view(), document_nul.provenance()) &&
                      session->finalize(),
                  "original document escaped NUL retained");
        }
        {
            auto target = root / std::filesystem::path(u8"archive-\u03b1.serene");
            auto session = ArchiveSession::open(*p, *codec, target);
            check(session && session->append_run(v, provenance) && session->finalize(),
                  "Unicode native archive pathname");
            check(std::filesystem::exists(target), "Unicode pathname published");
        }
        auto collision = root / "existing.serene";
        {
            std::ofstream out(collision);
            out << "sentinel";
        }
        check(!ArchiveSession::open(*p, *codec, collision), "no-clobber preflight");
        std::ifstream original(collision);
        std::string text;
        original >> text;
        check(text == "sentinel", "existing preserved");
        auto race = root / "race.serene";
        {
            auto s = ArchiveSession::open(*p, *codec, race);
            check(s && s->append_run(v, provenance), "race setup");
            {
                std::ofstream other(race);
                other << "racer";
            }
            check(!s->finalize(), "no-clobber publication race");
        }
        std::ifstream raced(race);
        raced >> text;
        check(text == "racer", "race file preserved");
        std::cout << "archive failure/retry/collision tests passed at " << root.string() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
