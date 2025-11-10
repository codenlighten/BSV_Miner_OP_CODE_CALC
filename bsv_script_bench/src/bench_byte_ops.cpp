#include "bench_harness.h"
#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <algorithm>

// Simulate OP_CAT (concatenate two byte arrays)
std::vector<uint8_t> op_cat(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::vector<uint8_t> result;
    result.reserve(a.size() + b.size());
    result.insert(result.end(), a.begin(), a.end());
    result.insert(result.end(), b.begin(), b.end());
    return result;
}

// Simulate OP_SPLIT (split byte array at position)
std::pair<std::vector<uint8_t>, std::vector<uint8_t>> 
op_split(const std::vector<uint8_t>& data, size_t position) {
    if (position > data.size()) {
        throw std::runtime_error("Split position out of range");
    }
    std::vector<uint8_t> left(data.begin(), data.begin() + position);
    std::vector<uint8_t> right(data.begin() + position, data.end());
    return {left, right};
}

// Simulate OP_NUM2BIN (convert number to binary of specific size)
std::vector<uint8_t> op_num2bin(int64_t num, size_t size) {
    std::vector<uint8_t> result(size, 0);
    // Simplified: just fill with pattern
    for (size_t i = 0; i < size && i < 8; ++i) {
        result[i] = (num >> (i * 8)) & 0xFF;
    }
    return result;
}

// Simulate OP_BIN2NUM (convert binary to number)
int64_t op_bin2num(const std::vector<uint8_t>& data) {
    int64_t result = 0;
    for (size_t i = 0; i < std::min(data.size(), size_t(8)); ++i) {
        result |= (int64_t)data[i] << (i * 8);
    }
    return result;
}

void benchmark_op_cat(bsv_bench::BenchmarkHarness& harness,
                      std::vector<bsv_bench::BenchResult>& results) {
    std::cout << "Benchmarking OP_CAT (CRITICAL for BSV)...\n";
    
    // Test sizes: Small to very large (BSV allows multi-MB)
    std::vector<std::pair<size_t, size_t>> size_pairs = {
        {10, 10},           // 10B + 10B
        {100, 100},         // 100B + 100B
        {1000, 1000},       // 1kB + 1kB
        {10000, 10000},     // 10kB + 10kB
        {100000, 100000},   // 100kB + 100kB
        {1000000, 1000000}, // 1MB + 1MB
        {10000000, 10000000}, // 10MB + 10MB
        {1, 10000000},      // Asymmetric: 1B + 10MB
        {10000000, 1},      // Asymmetric: 10MB + 1B
    };
    
    for (const auto& [size_a, size_b] : size_pairs) {
        std::vector<uint8_t> a(size_a, 0x42);
        std::vector<uint8_t> b(size_b, 0x43);
        
        auto result = harness.benchmark(
            "OP_CAT",
            std::to_string(size_a) + "B + " + std::to_string(size_b) + "B",
            size_a + size_b,
            [&a, &b]() {
                auto cat_result = op_cat(a, b);
                // Force use to prevent optimization
                volatile size_t s = cat_result.size();
                (void)s;
            },
            (size_a + size_b > 1000000) ? 100 : 1000,  // Fewer iterations for large sizes
            (size_a + size_b > 1000000) ? 10 : 100
        );
        
        results.push_back(result);
        std::cout << "  " << size_a << "B + " << size_b << "B -> " 
                 << result.median_cycles << " cycles";
        
        // Calculate cycles per byte
        if (size_a + size_b > 0) {
            double cycles_per_byte = (double)result.median_cycles / (size_a + size_b);
            std::cout << " (" << cycles_per_byte << " cycles/byte)";
        }
        std::cout << "\n";
    }
}

void benchmark_op_split(bsv_bench::BenchmarkHarness& harness,
                        std::vector<bsv_bench::BenchResult>& results) {
    std::cout << "Benchmarking OP_SPLIT...\n";
    
    std::vector<size_t> sizes = {100, 1000, 10000, 100000, 1000000, 10000000};
    std::vector<double> split_positions = {0.01, 0.5, 0.99};  // Start, middle, end
    
    for (auto size : sizes) {
        for (auto split_ratio : split_positions) {
            size_t position = static_cast<size_t>(size * split_ratio);
            std::vector<uint8_t> data(size, 0x42);
            
            auto result = harness.benchmark(
                "OP_SPLIT",
                std::to_string(size) + "B @ " + std::to_string(int(split_ratio * 100)) + "%",
                size,
                [&data, position]() {
                    auto [left, right] = op_split(data, position);
                    volatile size_t s = left.size() + right.size();
                    (void)s;
                },
                (size > 1000000) ? 100 : 1000,
                (size > 1000000) ? 10 : 100
            );
            
            results.push_back(result);
            std::cout << "  " << size << "B @ " << int(split_ratio * 100) << "% -> "
                     << result.median_cycles << " cycles";
            
            if (size > 0) {
                double cycles_per_byte = (double)result.median_cycles / size;
                std::cout << " (" << cycles_per_byte << " cycles/byte)";
            }
            std::cout << "\n";
        }
    }
}

void benchmark_op_num2bin(bsv_bench::BenchmarkHarness& harness,
                          std::vector<bsv_bench::BenchResult>& results) {
    std::cout << "Benchmarking OP_NUM2BIN...\n";
    
    std::vector<size_t> output_sizes = {1, 8, 32, 256, 1000, 10000, 1000000};
    
    for (auto size : output_sizes) {
        int64_t num = 0x123456789ABCDEF0;
        
        auto result = harness.benchmark(
            "OP_NUM2BIN",
            "output_size=" + std::to_string(size) + "B",
            size,
            [num, size]() {
                auto bin = op_num2bin(num, size);
                volatile size_t s = bin.size();
                (void)s;
            },
            (size > 100000) ? 100 : 1000,
            (size > 100000) ? 10 : 100
        );
        
        results.push_back(result);
        std::cout << "  " << size << "B -> " << result.median_cycles << " cycles\n";
    }
}

void benchmark_op_bin2num(bsv_bench::BenchmarkHarness& harness,
                          std::vector<bsv_bench::BenchResult>& results) {
    std::cout << "Benchmarking OP_BIN2NUM...\n";
    
    std::vector<size_t> input_sizes = {1, 8, 32, 256, 1000, 10000, 1000000};
    
    for (auto size : input_sizes) {
        std::vector<uint8_t> data(size, 0x42);
        
        auto result = harness.benchmark(
            "OP_BIN2NUM",
            "input_size=" + std::to_string(size) + "B",
            size,
            [&data]() {
                auto num = op_bin2num(data);
                volatile int64_t n = num;
                (void)n;
            },
            (size > 100000) ? 100 : 1000,
            (size > 100000) ? 10 : 100
        );
        
        results.push_back(result);
        std::cout << "  " << size << "B -> " << result.median_cycles << " cycles\n";
    }
}

void benchmark_cat_chain(bsv_bench::BenchmarkHarness& harness,
                         std::vector<bsv_bench::BenchResult>& results) {
    std::cout << "Benchmarking OP_CAT chains (reallocation test)...\n";
    
    // Test repeated CAT operations to measure reallocation overhead
    std::vector<int> chain_lengths = {2, 4, 8, 16};
    std::vector<size_t> chunk_sizes = {100, 1000, 10000, 100000};
    
    for (auto chain_len : chain_lengths) {
        for (auto chunk_size : chunk_sizes) {
            std::vector<uint8_t> chunk(chunk_size, 0x42);
            
            auto result = harness.benchmark(
                "OP_CAT_CHAIN",
                std::to_string(chain_len) + " x " + std::to_string(chunk_size) + "B",
                chain_len * chunk_size,
                [&chunk, chain_len]() {
                    std::vector<uint8_t> result = chunk;
                    for (int i = 1; i < chain_len; ++i) {
                        result = op_cat(result, chunk);
                    }
                    volatile size_t s = result.size();
                    (void)s;
                },
                (chunk_size > 10000) ? 100 : 500,
                (chunk_size > 10000) ? 10 : 50
            );
            
            results.push_back(result);
            std::cout << "  " << chain_len << " x " << chunk_size << "B -> "
                     << result.median_cycles << " cycles\n";
        }
    }
}

int main(int argc, char** argv) {
    std::cout << "=== BSV Script Benchmark: Byte Operations ===\n";
    std::cout << "Testing OP_CAT, OP_SPLIT (critical for BSV unbounded scripts)\n\n";
    
    bsv_bench::BenchmarkHarness harness;
    harness.initialize(0);  // Pin to CPU 0
    
    std::vector<bsv_bench::BenchResult> results;
    
    benchmark_op_cat(harness, results);
    benchmark_op_split(harness, results);
    benchmark_op_num2bin(harness, results);
    benchmark_op_bin2num(harness, results);
    benchmark_cat_chain(harness, results);
    
    // Export results
    std::string csv_file = "output/bench_byte_ops.csv";
    std::string json_file = "output/bench_byte_ops.json";
    
    harness.export_csv(results, csv_file);
    harness.export_json(results, json_file);
    
    std::cout << "\n=== Results exported to:\n";
    std::cout << "  " << csv_file << "\n";
    std::cout << "  " << json_file << "\n";
    
    return 0;
}
