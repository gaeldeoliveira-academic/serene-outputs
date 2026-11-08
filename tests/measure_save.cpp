#include "synthetic_case.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
// clang-format off
#include <windows.h>
// psapi.h requires Windows types declared above.
#include <psapi.h>
// clang-format on
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#endif

namespace {
struct Measurement {
    std::atomic<std::uint64_t> allocation_requests{}, reallocation_requests{}, free_requests{},
        allocation_requested_bytes{}, reallocation_requested_bytes{}, peak_private_bytes{},
        samples{};
    std::atomic<bool> active{};
};
Measurement measurement;
std::uint64_t private_bytes() noexcept {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters)))
        return counters.PrivateUsage;
#endif
    return 0;
}
void sample_private() noexcept {
    const auto bytes = private_bytes();
    measurement.samples.fetch_add(1, std::memory_order_relaxed);
    auto peak = measurement.peak_private_bytes.load(std::memory_order_relaxed);
    while (peak < bytes && !measurement.peak_private_bytes.compare_exchange_weak(
                               peak, bytes, std::memory_order_relaxed)) {
    }
}
serene::outputs::Status sample_fault(void *, serene::outputs::IoOperation, std::uint64_t) noexcept {
    sample_private();
    return {};
}
#if defined(_WIN32) && defined(_DEBUG)
int __cdecl allocation_hook(int type, void *, std::size_t size, int, long, const unsigned char *,
                            int) {
    if (measurement.active.load(std::memory_order_relaxed)) {
        if (type == _HOOK_ALLOC) {
            measurement.allocation_requests.fetch_add(1, std::memory_order_relaxed);
            measurement.allocation_requested_bytes.fetch_add(size, std::memory_order_relaxed);
        } else if (type == _HOOK_REALLOC) {
            measurement.reallocation_requests.fetch_add(1, std::memory_order_relaxed);
            measurement.reallocation_requested_bytes.fetch_add(size, std::memory_order_relaxed);
        } else if (type == _HOOK_FREE) {
            measurement.free_requests.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return TRUE;
}
#endif
struct Tracking {
#if defined(_WIN32) && defined(_DEBUG)
    _CRT_ALLOC_HOOK previous{_CrtSetAllocHook(allocation_hook)};
#endif
    Tracking() {
        measurement.active.store(true, std::memory_order_relaxed);
    }
    ~Tracking() {
        measurement.active.store(false, std::memory_order_relaxed);
#if defined(_WIN32) && defined(_DEBUG)
        _CrtSetAllocHook(previous);
#endif
    }
};
} // namespace

int main(int argc, char **argv) {
    using namespace serene::outputs;
    try {
        if (argc != 3)
            throw std::runtime_error("usage: measure_save <record-count> <new-archive.serene>");
        const auto count = std::stoull(argv[1]);
        if (!count || count > 1000000)
            throw std::runtime_error("record count outside measurement range");
        const std::filesystem::path output = argv[2];
        SyntheticCase source(3, static_cast<std::size_t>(count));
        source.limits.frame_values = 4096;
        auto prepared = PreparedExport::prepare(source.description, source.limits);
        auto codec = Codec::create(source.limits.frame_values);
        if (!prepared || !codec)
            throw std::runtime_error("preparation failed");
        const auto view = source.view();
        const auto provenance = source.provenance();
        const auto history_bytes = source.data.capacity() * sizeof(double) +
                                   source.axis.capacity() * sizeof(double) +
                                   source.identities.capacity() * sizeof(std::uint64_t);
        const auto baseline = private_bytes(); // Also warms the process-memory API before tracking.
        measurement.peak_private_bytes.store(baseline, std::memory_order_relaxed);
        SaveStatistics statistics;
        Status status;
        const auto start = std::chrono::steady_clock::now();
        {
            Tracking tracking;
            auto session = ArchiveSession::open(*prepared, *codec, output, {nullptr, sample_fault});
            if (!session)
                status = session.error();
            else {
                status = session->append_run(view, provenance);
                if (status)
                    status = session->finalize();
                statistics = session->statistics();
            }
            sample_private();
        }
        const auto seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (!status)
            throw std::runtime_error(std::string(status_name(status.code)) +
                                     " operation=" + std::to_string(status.operation));
        const auto peak = measurement.peak_private_bytes.load();
        std::cout << "{\"record_count\":" << count << ",\"channel_count\":3,\"frame_values\":4096"
                  << ",\"caller_retained_history_capacity_bytes\":" << history_bytes
                  << ",\"prepared_workspace_plan_bytes\":" << prepared->plan().total_bytes
                  << ",\"prepared_metadata_budget_bytes\":"
                  << prepared->limits().metadata_budget_bytes
                  << ",\"codec_workspace_bytes\":" << codec->workspace_bytes()
                  << ",\"written_frames\":" << statistics.frames
                  << ",\"written_payload_bytes\":" << statistics.payload_bytes
                  << ",\"write_calls\":" << statistics.write_calls
                  << ",\"seek_calls\":" << statistics.seek_calls
                  << ",\"cumulative_written_bytes\":" << statistics.written_bytes
                  << ",\"archive_bytes\":" << std::filesystem::file_size(output)
                  << ",\"save_seconds\":" << seconds
#if defined(_WIN32) && defined(_DEBUG)
                  << ",\"crt_hook_available\":true"
#else
                  << ",\"crt_hook_available\":false"
#endif
#ifdef _WIN32
                  << ",\"private_usage_sampling_available\":true"
#else
                  << ",\"private_usage_sampling_available\":false"
#endif
                  << ",\"crt_allocation_requests\":" << measurement.allocation_requests.load()
                  << ",\"crt_reallocation_requests\":" << measurement.reallocation_requests.load()
                  << ",\"crt_free_requests\":" << measurement.free_requests.load()
                  << ",\"crt_allocation_requested_bytes\":"
                  << measurement.allocation_requested_bytes.load()
                  << ",\"crt_reallocation_requested_bytes\":"
                  << measurement.reallocation_requested_bytes.load()
                  << ",\"private_usage_before_save_bytes\":" << baseline
                  << ",\"private_usage_peak_observed_bytes\":" << peak
                  << ",\"private_usage_peak_observed_delta_bytes\":"
                  << (peak > baseline ? peak - baseline : 0)
                  << ",\"private_usage_samples\":" << measurement.samples.load() << "}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
