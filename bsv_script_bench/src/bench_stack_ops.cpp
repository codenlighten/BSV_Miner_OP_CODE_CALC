#include "bench_harness.h"
#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>

// Simple stack implementation for testing
// TODO: Replace with actual BSV Script stack once integrated
class SimpleStack {
public:
    void push(const std::vector<uint8_t>& data) {
        items_.push_back(data);
    }
    
    std::vector<uint8_t> pop() {
        if (items_.empty()) throw std::runtime_error("Stack underflow");
        auto item = items_.back();
        items_.pop_back();
        return item;
    }
    
    const std::vector<uint8_t>& top() const {
        if (items_.empty()) throw std::runtime_error("Stack empty");
        return items_.back();
    }
    
    void dup() {
        if (items_.empty()) throw std::runtime_error("Stack empty");
        items_.push_back(items_.back());
    }
    
    void swap() {
        if (items_.size() < 2) throw std::runtime_error("Insufficient items");
        std::swap(items_[items_.size() - 1], items_[items_.size() - 2]);
    }
    
    void pick(size_t depth) {
        if (depth >= items_.size()) throw std::runtime_error("Pick out of range");
        items_.push_back(items_[items_.size() - 1 - depth]);
    }
    
    void roll(size_t depth) {
        if (depth >= items_.size()) throw std::runtime_error("Roll out of range");
        auto item = items_[items_.size() - 1 - depth];
        items_.erase(items_.begin() + (items_.size() - 1 - depth));
        items_.push_back(item);
    }
    
    void rot() {
        if (items_.size() < 3) throw std::runtime_error("Insufficient items");
        // Rotate top 3: a b c -> b c a
        auto c = items_.back(); items_.pop_back();
        auto b = items_.back(); items_.pop_back();
        auto a = items_.back(); items_.pop_back();
        items_.push_back(b);
        items_.push_back(c);
        items_.push_back(a);
    }
    
    size_t size() const { return items_.size(); }
    void clear() { items_.clear(); }
    
private:
    std::vector<std::vector<uint8_t>> items_;
};

void benchmark_op_dup(bsv_bench::BenchmarkHarness& harness,
                      std::vector<bsv_bench::BenchResult>& results) {
    std::cout << "Benchmarking OP_DUP...\n";
    
    // Test various stack depths and item sizes
    std::vector<size_t> stack_depths = {1, 10, 100, 1000};
    std::vector<size_t> item_sizes = {1, 100, 10000, 1000000};  // 1B, 100B, 10kB, 1MB
    
    for (auto depth : stack_depths) {
        for (auto item_size : item_sizes) {
            SimpleStack stack;
            std::vector<uint8_t> item(item_size, 0x42);
            
            // Pre-fill stack
            for (size_t i = 0; i < depth; ++i) {
                stack.push(item);
            }
            
            auto result = harness.benchmark(
                "OP_DUP",
                "depth=" + std::to_string(depth) + ",item_size=" + std::to_string(item_size),
                item_size,
                [&stack]() { stack.dup(); stack.pop(); },  // DUP then pop to maintain size
                1000,
                100
            );
            
            results.push_back(result);
            std::cout << "  depth=" << depth << ", size=" << item_size 
                     << " -> " << result.median_cycles << " cycles\n";
        }
    }
}

void benchmark_op_swap(bsv_bench::BenchmarkHarness& harness,
                       std::vector<bsv_bench::BenchResult>& results) {
    std::cout << "Benchmarking OP_SWAP...\n";
    
    std::vector<size_t> stack_depths = {2, 10, 100, 1000};
    std::vector<size_t> item_sizes = {1, 100, 10000, 1000000};
    
    for (auto depth : stack_depths) {
        for (auto item_size : item_sizes) {
            SimpleStack stack;
            std::vector<uint8_t> item(item_size, 0x42);
            
            for (size_t i = 0; i < depth; ++i) {
                stack.push(item);
            }
            
            auto result = harness.benchmark(
                "OP_SWAP",
                "depth=" + std::to_string(depth) + ",item_size=" + std::to_string(item_size),
                item_size * 2,  // Swaps involve two items
                [&stack]() { stack.swap(); },
                1000,
                100
            );
            
            results.push_back(result);
            std::cout << "  depth=" << depth << ", size=" << item_size 
                     << " -> " << result.median_cycles << " cycles\n";
        }
    }
}

void benchmark_op_pick(bsv_bench::BenchmarkHarness& harness,
                       std::vector<bsv_bench::BenchResult>& results) {
    std::cout << "Benchmarking OP_PICK...\n";
    
    std::vector<size_t> stack_depths = {10, 100, 1000, 10000};
    std::vector<size_t> pick_depths = {0, 5, 50, 500};  // Relative to stack depth
    size_t item_size = 100;
    
    for (auto depth : stack_depths) {
        for (auto pick_depth : pick_depths) {
            if (pick_depth >= depth) continue;
            
            SimpleStack stack;
            std::vector<uint8_t> item(item_size, 0x42);
            
            for (size_t i = 0; i < depth; ++i) {
                stack.push(item);
            }
            
            auto result = harness.benchmark(
                "OP_PICK",
                "stack_depth=" + std::to_string(depth) + ",pick_depth=" + std::to_string(pick_depth),
                item_size,
                [&stack, pick_depth]() { stack.pick(pick_depth); stack.pop(); },
                1000,
                100
            );
            
            results.push_back(result);
            std::cout << "  stack=" << depth << ", pick=" << pick_depth 
                     << " -> " << result.median_cycles << " cycles\n";
        }
    }
}

void benchmark_op_roll(bsv_bench::BenchmarkHarness& harness,
                       std::vector<bsv_bench::BenchResult>& results) {
    std::cout << "Benchmarking OP_ROLL...\n";
    
    std::vector<size_t> stack_depths = {10, 100, 1000};
    std::vector<size_t> roll_depths = {1, 5, 50};
    size_t item_size = 100;
    
    for (auto depth : stack_depths) {
        for (auto roll_depth : roll_depths) {
            if (roll_depth >= depth) continue;
            
            auto result = harness.benchmark(
                "OP_ROLL",
                "stack_depth=" + std::to_string(depth) + ",roll_depth=" + std::to_string(roll_depth),
                item_size,
                [depth, roll_depth, item_size]() {
                    SimpleStack stack;
                    std::vector<uint8_t> item(item_size, 0x42);
                    for (size_t i = 0; i < depth; ++i) stack.push(item);
                    stack.roll(roll_depth);
                },
                1000,
                100
            );
            
            results.push_back(result);
            std::cout << "  stack=" << depth << ", roll=" << roll_depth 
                     << " -> " << result.median_cycles << " cycles\n";
        }
    }
}

void benchmark_op_rot(bsv_bench::BenchmarkHarness& harness,
                      std::vector<bsv_bench::BenchResult>& results) {
    std::cout << "Benchmarking OP_ROT...\n";
    
    std::vector<size_t> stack_depths = {3, 10, 100, 1000};
    std::vector<size_t> item_sizes = {1, 100, 10000, 1000000};
    
    for (auto depth : stack_depths) {
        for (auto item_size : item_sizes) {
            SimpleStack stack;
            std::vector<uint8_t> item(item_size, 0x42);
            
            for (size_t i = 0; i < depth; ++i) {
                stack.push(item);
            }
            
            auto result = harness.benchmark(
                "OP_ROT",
                "depth=" + std::to_string(depth) + ",item_size=" + std::to_string(item_size),
                item_size * 3,  // Rotates 3 items
                [&stack]() { stack.rot(); },
                1000,
                100
            );
            
            results.push_back(result);
            std::cout << "  depth=" << depth << ", size=" << item_size 
                     << " -> " << result.median_cycles << " cycles\n";
        }
    }
}

int main(int argc, char** argv) {
    std::cout << "=== BSV Script Benchmark: Stack Operations ===\n\n";
    
    bsv_bench::BenchmarkHarness harness;
    harness.initialize(0);  // Pin to CPU 0
    
    std::vector<bsv_bench::BenchResult> results;
    
    benchmark_op_dup(harness, results);
    benchmark_op_swap(harness, results);
    benchmark_op_pick(harness, results);
    benchmark_op_roll(harness, results);
    benchmark_op_rot(harness, results);
    
    // Export results
    std::string csv_file = "output/bench_stack_ops.csv";
    std::string json_file = "output/bench_stack_ops.json";
    
    harness.export_csv(results, csv_file);
    harness.export_json(results, json_file);
    
    std::cout << "\n=== Results exported to:\n";
    std::cout << "  " << csv_file << "\n";
    std::cout << "  " << json_file << "\n";
    
    return 0;
}
