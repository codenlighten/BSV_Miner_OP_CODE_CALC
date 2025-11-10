#include "bench_harness.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <cstring>
#include <iostream>

namespace bsv_bench {

// Perf event setup helper
static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                           int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

BenchmarkHarness::BenchmarkHarness() 
    : perf_fd_cycles_(-1)
    , perf_fd_instructions_(-1)
    , perf_fd_l1d_misses_(-1)
    , perf_fd_llc_misses_(-1)
    , perf_fd_branch_misses_(-1)
    , perf_counters_enabled_(false)
    , pinned_cpu_(-1) {
}

BenchmarkHarness::~BenchmarkHarness() {
    if (perf_fd_cycles_ >= 0) close(perf_fd_cycles_);
    if (perf_fd_instructions_ >= 0) close(perf_fd_instructions_);
    if (perf_fd_l1d_misses_ >= 0) close(perf_fd_l1d_misses_);
    if (perf_fd_llc_misses_ >= 0) close(perf_fd_llc_misses_);
    if (perf_fd_branch_misses_ >= 0) close(perf_fd_branch_misses_);
}

void BenchmarkHarness::initialize(int cpu_core) {
    if (cpu_core >= 0) {
        pin_to_cpu(cpu_core);
        pinned_cpu_ = cpu_core;
    }
    
    // Set up performance counters
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(pe);
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    
    // CPU cycles counter
    pe.config = PERF_COUNT_HW_CPU_CYCLES;
    perf_fd_cycles_ = perf_event_open(&pe, 0, -1, -1, 0);
    
    // Instructions counter
    pe.config = PERF_COUNT_HW_INSTRUCTIONS;
    perf_fd_instructions_ = perf_event_open(&pe, 0, -1, -1, 0);
    
    // L1 data cache misses
    pe.config = PERF_COUNT_HW_CACHE_MISSES;
    perf_fd_l1d_misses_ = perf_event_open(&pe, 0, -1, -1, 0);
    
    // LLC (Last Level Cache) misses
    pe.type = PERF_TYPE_HW_CACHE;
    pe.config = (PERF_COUNT_HW_CACHE_LL) |
                (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    perf_fd_llc_misses_ = perf_event_open(&pe, 0, -1, -1, 0);
    
    // Branch misses
    pe.type = PERF_TYPE_HARDWARE;
    pe.config = PERF_COUNT_HW_BRANCH_MISSES;
    perf_fd_branch_misses_ = perf_event_open(&pe, 0, -1, -1, 0);
    
    perf_counters_enabled_ = (perf_fd_cycles_ >= 0);
    
    if (!perf_counters_enabled_) {
        std::cerr << "Warning: Performance counters not available. Running with rdtsc only.\n";
    }
}

Statistics BenchmarkHarness::calculate_stats(std::vector<uint64_t>& samples) {
    std::sort(samples.begin(), samples.end());
    
    Statistics stats;
    stats.mean = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    stats.median = samples[samples.size() / 2];
    stats.p90 = samples[static_cast<size_t>(samples.size() * 0.90)];
    stats.p95 = samples[static_cast<size_t>(samples.size() * 0.95)];
    stats.p99 = samples[static_cast<size_t>(samples.size() * 0.99)];
    
    double variance = 0.0;
    for (auto sample : samples) {
        variance += (sample - stats.mean) * (sample - stats.mean);
    }
    stats.stddev = std::sqrt(variance / samples.size());
    
    return stats;
}

void BenchmarkHarness::export_csv(const std::vector<BenchResult>& results, 
                                  const std::string& filename) {
    std::ofstream out(filename);
    
    // Header
    out << "opcode,param_desc,input_bytes,median_cycles,p90_cycles,p99_cycles,"
        << "median_ns,instructions,ipc,l1d_misses,llc_misses,branch_misses,"
        << "malloc_count,alloc_bytes\n";
    
    // Data rows
    for (const auto& r : results) {
        out << r.opcode << ","
            << r.param_desc << ","
            << r.input_bytes << ","
            << r.median_cycles << ","
            << r.p90_cycles << ","
            << r.p99_cycles << ","
            << r.median_ns << ","
            << r.instructions << ","
            << r.ipc << ","
            << r.l1d_misses << ","
            << r.llc_misses << ","
            << r.branch_misses << ","
            << r.malloc_count << ","
            << r.alloc_bytes << "\n";
    }
}

void BenchmarkHarness::export_json(const std::vector<BenchResult>& results,
                                   const std::string& filename) {
    std::ofstream out(filename);
    out << "{\n  \"benchmarks\": [\n";
    
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        out << "    {\n"
            << "      \"opcode\": \"" << r.opcode << "\",\n"
            << "      \"param_desc\": \"" << r.param_desc << "\",\n"
            << "      \"input_bytes\": " << r.input_bytes << ",\n"
            << "      \"median_cycles\": " << r.median_cycles << ",\n"
            << "      \"p90_cycles\": " << r.p90_cycles << ",\n"
            << "      \"p99_cycles\": " << r.p99_cycles << ",\n"
            << "      \"median_ns\": " << r.median_ns << ",\n"
            << "      \"instructions\": " << r.instructions << ",\n"
            << "      \"ipc\": " << r.ipc << ",\n"
            << "      \"l1d_misses\": " << r.l1d_misses << ",\n"
            << "      \"llc_misses\": " << r.llc_misses << ",\n"
            << "      \"branch_misses\": " << r.branch_misses << "\n"
            << "    }" << (i < results.size() - 1 ? "," : "") << "\n";
    }
    
    out << "  ]\n}\n";
}

void pin_to_cpu(int cpu_core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_core, &cpuset);
    
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        std::cerr << "Warning: Failed to pin to CPU " << cpu_core << "\n";
    }
}

} // namespace bsv_bench
