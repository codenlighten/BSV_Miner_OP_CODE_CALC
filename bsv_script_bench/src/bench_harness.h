#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <fstream>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/perf_event.h>

namespace bsv_bench {

// Benchmark result for a single measurement
struct BenchResult {
    std::string opcode;
    std::string param_desc;
    uint64_t input_bytes;
    
    // Timing measurements
    uint64_t median_cycles;
    uint64_t p90_cycles;
    uint64_t p99_cycles;
    double median_ns;
    
    // Performance counters
    uint64_t instructions;
    double ipc;  // Instructions per cycle
    uint64_t l1d_misses;
    uint64_t llc_misses;
    uint64_t branch_misses;
    
    // Memory allocation tracking
    uint64_t malloc_count;
    uint64_t alloc_bytes;
};

// Statistics calculator
struct Statistics {
    double mean;
    double median;
    double p90;
    double p95;
    double p99;
    double stddev;
};

// Helper: Pin current thread to specific CPU core
void pin_to_cpu(int cpu_core);

// Helper: Disable CPU frequency scaling (requires root)
void disable_cpu_scaling();

// Helper: Read current cycle count (rdtsc)
inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// Helper: Serialize instruction execution (prevents reordering)
inline void serialize() {
    __asm__ __volatile__ ("cpuid" : : : "rax", "rbx", "rcx", "rdx", "memory");
}

// Helper: Memory fence
inline void mfence() {
    __asm__ __volatile__ ("mfence" : : : "memory");
}

class BenchmarkHarness {
public:
    BenchmarkHarness();
    ~BenchmarkHarness();
    
    // Initialize performance counters and CPU pinning
    void initialize(int cpu_core = -1);
    
    // Run a benchmark function multiple times and collect statistics
    template<typename Func>
    BenchResult benchmark(
        const std::string& opcode_name,
        const std::string& param_description,
        uint64_t input_size_bytes,
        Func&& operation,
        int iterations = 1000,
        int warmup_iterations = 100
    ) {
        std::vector<uint64_t> cycle_samples;
        cycle_samples.reserve(iterations);
        
        uint64_t total_instructions = 0;
        uint64_t total_l1d_misses = 0;
        uint64_t total_llc_misses = 0;
        uint64_t total_branch_misses = 0;
        
        // Warmup
        for (int i = 0; i < warmup_iterations; ++i) {
            operation();
        }
        
        // Actual measurements
        for (int i = 0; i < iterations; ++i) {
            // Reset and enable counters
            if (perf_counters_enabled_) {
                ioctl(perf_fd_cycles_, PERF_EVENT_IOC_RESET, 0);
                ioctl(perf_fd_instructions_, PERF_EVENT_IOC_RESET, 0);
                ioctl(perf_fd_l1d_misses_, PERF_EVENT_IOC_RESET, 0);
                ioctl(perf_fd_llc_misses_, PERF_EVENT_IOC_RESET, 0);
                ioctl(perf_fd_branch_misses_, PERF_EVENT_IOC_RESET, 0);
                
                ioctl(perf_fd_cycles_, PERF_EVENT_IOC_ENABLE, 0);
                ioctl(perf_fd_instructions_, PERF_EVENT_IOC_ENABLE, 0);
                ioctl(perf_fd_l1d_misses_, PERF_EVENT_IOC_ENABLE, 0);
                ioctl(perf_fd_llc_misses_, PERF_EVENT_IOC_ENABLE, 0);
                ioctl(perf_fd_branch_misses_, PERF_EVENT_IOC_ENABLE, 0);
            }
            
            // Measure with rdtsc
            serialize();
            uint64_t start = rdtsc();
            mfence();
            
            operation();
            
            mfence();
            uint64_t end = rdtsc();
            serialize();
            
            // Disable counters
            if (perf_counters_enabled_) {
                ioctl(perf_fd_cycles_, PERF_EVENT_IOC_DISABLE, 0);
                ioctl(perf_fd_instructions_, PERF_EVENT_IOC_DISABLE, 0);
                ioctl(perf_fd_l1d_misses_, PERF_EVENT_IOC_DISABLE, 0);
                ioctl(perf_fd_llc_misses_, PERF_EVENT_IOC_DISABLE, 0);
                ioctl(perf_fd_branch_misses_, PERF_EVENT_IOC_DISABLE, 0);
                
                uint64_t count;
                if (read(perf_fd_instructions_, &count, sizeof(count)) == sizeof(count))
                    total_instructions += count;
                if (read(perf_fd_l1d_misses_, &count, sizeof(count)) == sizeof(count))
                    total_l1d_misses += count;
                if (read(perf_fd_llc_misses_, &count, sizeof(count)) == sizeof(count))
                    total_llc_misses += count;
                if (read(perf_fd_branch_misses_, &count, sizeof(count)) == sizeof(count))
                    total_branch_misses += count;
            }
            
            cycle_samples.push_back(end - start);
        }
        
        Statistics stats = calculate_stats(cycle_samples);
        
        // Assume 3.5 GHz for ns conversion (adjust based on actual CPU)
        const double CPU_GHZ = 3.5;
        
        BenchResult result;
        result.opcode = opcode_name;
        result.param_desc = param_description;
        result.input_bytes = input_size_bytes;
        result.median_cycles = static_cast<uint64_t>(stats.median);
        result.p90_cycles = static_cast<uint64_t>(stats.p90);
        result.p99_cycles = static_cast<uint64_t>(stats.p99);
        result.median_ns = stats.median / CPU_GHZ;
        result.instructions = total_instructions / iterations;
        result.ipc = perf_counters_enabled_ ? 
                     (double)result.instructions / result.median_cycles : 0.0;
        result.l1d_misses = total_l1d_misses / iterations;
        result.llc_misses = total_llc_misses / iterations;
        result.branch_misses = total_branch_misses / iterations;
        result.malloc_count = 0;  // TODO: Hook malloc
        result.alloc_bytes = 0;   // TODO: Track allocations
        
        return result;
    }
    
    // Export results to CSV
    void export_csv(const std::vector<BenchResult>& results, const std::string& filename);
    
    // Export results to JSON
    void export_json(const std::vector<BenchResult>& results, const std::string& filename);
    
private:
    // Cycle counter using rdtsc
    static inline uint64_t read_cycles();
    
    // Calculate statistics from samples
    static Statistics calculate_stats(std::vector<uint64_t>& samples);
    
    // Performance counter file descriptors (Linux perf_event_open)
    int perf_fd_cycles_;
    int perf_fd_instructions_;
    int perf_fd_l1d_misses_;
    int perf_fd_llc_misses_;
    int perf_fd_branch_misses_;
    
    bool perf_counters_enabled_;
    int pinned_cpu_;
};

} // namespace bsv_bench
