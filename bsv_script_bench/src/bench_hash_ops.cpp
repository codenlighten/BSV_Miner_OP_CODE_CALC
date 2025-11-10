#include "bench_harness.h"
#include <vector>
#include <cstdint>
#include <iostream>
#include <openssl/sha.h>
#include <openssl/ripemd.h>

// Hash operation wrappers using OpenSSL
std::vector<uint8_t> op_sha1(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash(SHA_DIGEST_LENGTH);
    SHA1(data.data(), data.size(), hash.data());
    return hash;
}

std::vector<uint8_t> op_sha256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), hash.data());
    return hash;
}

std::vector<uint8_t> op_hash160(const std::vector<uint8_t>& data) {
    // Hash160 = RIPEMD160(SHA256(data))
    std::vector<uint8_t> sha256_hash(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), sha256_hash.data());
    
    std::vector<uint8_t> hash(RIPEMD160_DIGEST_LENGTH);
    RIPEMD160(sha256_hash.data(), sha256_hash.size(), hash.data());
    return hash;
}

std::vector<uint8_t> op_hash256(const std::vector<uint8_t>& data) {
    // Hash256 = SHA256(SHA256(data))
    std::vector<uint8_t> first_hash(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), first_hash.data());
    
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(first_hash.data(), first_hash.size(), hash.data());
    return hash;
}

std::vector<uint8_t> op_ripemd160(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash(RIPEMD160_DIGEST_LENGTH);
    RIPEMD160(data.data(), data.size(), hash.data());
    return hash;
}

void benchmark_hash_op(bsv_bench::BenchmarkHarness& harness,
                       std::vector<bsv_bench::BenchResult>& results,
                       const std::string& opcode_name,
                       std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)> hash_fn) {
    
    std::cout << "Benchmarking " << opcode_name << "...\n";
    
    // Test sizes from 1 byte to 100MB (BSV can handle large data)
    std::vector<size_t> sizes = {
        1,
        64,         // Single SHA256 block
        512,        // Multiple blocks
        4096,       // 4kB
        65536,      // 64kB
        1000000,    // 1MB
        10000000,   // 10MB
        100000000   // 100MB
    };
    
    for (auto size : sizes) {
        std::vector<uint8_t> data(size, 0x42);
        
        auto result = harness.benchmark(
            opcode_name,
            std::to_string(size) + "B",
            size,
            [&data, &hash_fn]() {
                auto hash = hash_fn(data);
                volatile size_t s = hash.size();
                (void)s;
            },
            (size > 10000000) ? 50 : (size > 1000000) ? 100 : 1000,
            (size > 10000000) ? 5 : (size > 1000000) ? 10 : 100
        );
        
        results.push_back(result);
        
        std::cout << "  " << size << "B -> " << result.median_cycles << " cycles";
        
        if (size > 0) {
            double cycles_per_byte = (double)result.median_cycles / size;
            std::cout << " (" << cycles_per_byte << " cycles/byte)";
        }
        std::cout << "\n";
    }
}

void analyze_hash_linearity(const std::vector<bsv_bench::BenchResult>& results,
                            const std::string& opcode_name) {
    std::cout << "\n=== Linear Model Analysis for " << opcode_name << " ===\n";
    
    // Filter results for this opcode
    std::vector<std::pair<uint64_t, uint64_t>> points;  // (bytes, cycles)
    for (const auto& r : results) {
        if (r.opcode == opcode_name) {
            points.push_back({r.input_bytes, r.median_cycles});
        }
    }
    
    if (points.size() < 2) return;
    
    // Simple linear regression: cycles = c0 + c1 * bytes
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
    int n = points.size();
    
    for (const auto& [x, y] : points) {
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_xx += x * x;
    }
    
    double c1 = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
    double c0 = (sum_y - c1 * sum_x) / n;
    
    std::cout << "  Model: cost(n) = " << c0 << " + " << c1 << " * n\n";
    std::cout << "  c0 (base cost): " << c0 << " cycles\n";
    std::cout << "  c1 (per-byte cost): " << c1 << " cycles/byte\n";
    
    // Calculate R²
    double mean_y = sum_y / n;
    double ss_tot = 0, ss_res = 0;
    for (const auto& [x, y] : points) {
        double y_pred = c0 + c1 * x;
        ss_tot += (y - mean_y) * (y - mean_y);
        ss_res += (y - y_pred) * (y - y_pred);
    }
    double r_squared = 1.0 - (ss_res / ss_tot);
    std::cout << "  R² (fit quality): " << r_squared << "\n";
}

int main(int argc, char** argv) {
    std::cout << "=== BSV Script Benchmark: Hash Operations ===\n";
    std::cout << "Testing linear cost model: cost(n) = c0 + c1*n\n\n";
    
    bsv_bench::BenchmarkHarness harness;
    harness.initialize(0);  // Pin to CPU 0
    
    std::vector<bsv_bench::BenchResult> results;
    
    benchmark_hash_op(harness, results, "OP_SHA1", op_sha1);
    benchmark_hash_op(harness, results, "OP_SHA256", op_sha256);
    benchmark_hash_op(harness, results, "OP_HASH160", op_hash160);
    benchmark_hash_op(harness, results, "OP_HASH256", op_hash256);
    benchmark_hash_op(harness, results, "OP_RIPEMD160", op_ripemd160);
    
    // Analyze linear models
    analyze_hash_linearity(results, "OP_SHA1");
    analyze_hash_linearity(results, "OP_SHA256");
    analyze_hash_linearity(results, "OP_HASH160");
    analyze_hash_linearity(results, "OP_HASH256");
    analyze_hash_linearity(results, "OP_RIPEMD160");
    
    // Export results
    std::string csv_file = "output/bench_hash_ops.csv";
    std::string json_file = "output/bench_hash_ops.json";
    
    harness.export_csv(results, csv_file);
    harness.export_json(results, json_file);
    
    std::cout << "\n=== Results exported to:\n";
    std::cout << "  " << csv_file << "\n";
    std::cout << "  " << json_file << "\n";
    
    return 0;
}
