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
/** Process-local atomic counters used by callbacks and main. Tracking gates CRT requests; explicit sampling records process-private usage independently. */ struct Measurement {
    std::atomic<std::uint64_t> allocation_requests{}, ///< CRT allocation request count in the active window.
        reallocation_requests{}, ///< CRT reallocation request count in the active window.
        free_requests{}, ///< CRT free request count in the active window.
        allocation_requested_bytes{}, ///< Sum of requested CRT allocation bytes.
        reallocation_requested_bytes{}, ///< Sum of requested CRT reallocation bytes.
        peak_private_bytes{}, ///< Largest explicit process-private-usage sample, in bytes.
        samples{}; ///< Number of explicit process-private-usage samples.
/** Whether CRT requests belong to the measured save interval. */     std::atomic<bool> active{};
};
/* Single process-local measurement state. */ Measurement measurement;
/** Return current Windows process private usage in bytes, or zero when unsupported/query fails. This is a sample, not an OS high-water mark. */
std::uint64_t private_bytes() noexcept {
    // Query the Windows process-memory API when available; otherwise report an unavailable zero sample.

#ifdef _WIN32
    /* OS process-memory snapshot buffer. */ PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters)))
        return counters.PrivateUsage;
#endif
    return 0;
}
/** Record one private-usage sample and raise the observed peak atomically. Does not allocate or measure every transient OS peak. */
void sample_private() noexcept {
    // Query private bytes, count the sample, and compare/exchange the observed high-water mark.

    /* Current sampled private usage in bytes. */ const auto bytes = private_bytes();
    measurement.samples.fetch_add(1, std::memory_order_relaxed);
    /* Observed private-usage high-water mark. */ auto peak = measurement.peak_private_bytes.load(std::memory_order_relaxed);
    while (peak < bytes && !measurement.peak_private_bytes.compare_exchange_weak(
                               peak, bytes, std::memory_order_relaxed)) {
    }
}
/** Memory-sampling archive hook. Context, operation and ordinal are unused; always returns successful status and injects no fault. */
serene::outputs::Status sample_fault(void *, serene::outputs::IoOperation, std::uint64_t) noexcept {
    // Sample private usage at this archive boundary, then return success.

    sample_private();
    return {};
}
#if defined(_WIN32) && defined(_DEBUG)
/** Debug CRT hook counting active-window requests by type and requested size. Other callback arguments are unused; returns TRUE to permit allocation. Totals measure churn, not live bytes. */
int __cdecl allocation_hook(int type, void *, std::size_t size, int, long, const unsigned char *,
                            int) {
    // While tracking is active, count allocation/reallocation/free requests and requested-byte totals.
    // Always permit the CRT operation.

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
/** RAII window used by main to gate accounting and restore the prior Debug CRT hook. Prepared histories and codec buffers exist before this object. */ struct Tracking {
#if defined(_WIN32) && defined(_DEBUG)
    /** Prior CRT allocation hook restored at scope exit. */ _CRT_ALLOC_HOOK previous{_CrtSetAllocHook(allocation_hook)};
#endif
/** Start the scoped measurement interval after capturing the previous Debug CRT hook. Intended for one window per process. */
    Tracking() {
    // Enable accounting after hook installation.

        measurement.active.store(true, std::memory_order_relaxed);
    }
/** End the measurement interval and restore the prior Windows Debug CRT hook; no measured allocations are owned here. */
    ~Tracking() {
    // Disable accounting, then restore the saved hook when supported.

        measurement.active.store(false, std::memory_order_relaxed);
#if defined(_WIN32) && defined(_DEBUG)
        _CrtSetAllocHook(previous);
#endif
    }
};
} // namespace

/** Measure a save with record count argv[1] and fresh archive path argv[2]. Prints distinct retained-history, workspace, churn and sampled-private-usage quantities; returns zero or one on failure. */
int main(int argc, char **argv) {
    // Prepare source histories, exporter and codec before tracking.
    // Measure open/append/finalize with boundary samples, restoring the hook on scope exit.
    // Report elapsed time and each distinct memory quantity without claiming an exact OS peak.

    using namespace serene::outputs;
    try {
        if (argc != 3)
            throw std::runtime_error("usage: measure_save <record-count> <new-archive.serene>");
        /* Requested record count within this harness's bounded range. */ const auto count = std::stoull(argv[1]);
        if (!count || count > 1000000)
            throw std::runtime_error("record count outside measurement range");
        /* Fresh destination supplied by the caller. */ const std::filesystem::path output = argv[2];
        /* Owned three-channel histories and provenance. */ SyntheticCase source(3, static_cast<std::size_t>(count));
        source.limits.frame_values = 4096;
        /* Exporter prepared before measurement. */ auto prepared = PreparedExport::prepare(source.description, source.limits);
        /* Codec workspace allocated before measurement. */ auto codec = Codec::create(source.limits.frame_values);
        if (!prepared || !codec)
            throw std::runtime_error("preparation failed");
        /* Borrowed numerical history view. */ const auto view = source.view();
        /* Borrowed provenance document/log views. */ const auto provenance = source.provenance();
        /* Retained numeric/axis/identity capacities, excluding object metadata. */ const auto history_bytes = source.data.capacity() * sizeof(double) +
                                   source.axis.capacity() * sizeof(double) +
                                   source.identities.capacity() * sizeof(std::uint64_t);
        /* Private usage before tracking; also warms the OS query. */ const auto baseline = private_bytes(); // Also warms the process-memory API before tracking.
        measurement.peak_private_bytes.store(baseline, std::memory_order_relaxed);
        /* Writer counters copied before session destruction. */ SaveStatistics statistics;
        /* Append/finalize outcome used after leaving the measured scope. */ Status status;
        /* Wall-clock start of the instrumented save. */ const auto start = std::chrono::steady_clock::now();
        {
            /* Scoped gate that restores the allocation hook on every exit. */ Tracking tracking;
            /* Archive session with memory-sampling callbacks. */ auto session = ArchiveSession::open(*prepared, *codec, output, {nullptr, sample_fault});
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
        /* Instrumented elapsed save time in seconds. */ const auto seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (!status)
            throw std::runtime_error(std::string(status_name(status.code)) +
                                     " operation=" + std::to_string(status.operation));
        const /* Observed private-usage high-water mark. */ auto peak = measurement.peak_private_bytes.load();
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
    } catch (/* First preparation or save error. */ const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
