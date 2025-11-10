#include "bsv/cost_estimator.h"
#include <iostream>
#include <cassert>

using namespace bsv::cost;

void test_basic_estimation() {
    std::cout << "Test: Basic cost estimation..." << std::endl;
    
    CostEstimator estimator("../../cost_models/example_model.json");
    
    Transaction tx;
    tx.version = 1;
    tx.locktime = 0;
    
    TxInput input;
    input.prevout_hash = std::vector<uint8_t>(32, 0);
    input.prevout_index = 0;
    input.sequence = 0xffffffff;
    input.script_sig = {};
    tx.inputs.push_back(input);
    
    TxOutput output;
    output.value = 100000;
    output.script_pubkey = {0x76, 0xa9, 0x14};  // OP_DUP OP_HASH160 ...
    output.script_pubkey.resize(25);
    tx.outputs.push_back(output);
    
    // Simple script: OP_DUP
    Script unlocking = {};
    Script locking = {static_cast<uint8_t>(OpCode::OP_DUP)};
    
    auto result = estimator.estimate(unlocking, locking, tx, 0);
    
    assert(result.total_cycles > 0);
    assert(result.opcode_count > 0);
    std::cout << "  ✓ Estimated " << result.total_cycles << " cycles" << std::endl;
}

void test_cat_operation() {
    std::cout << "Test: OP_CAT cost scaling..." << std::endl;
    
    CostEstimator estimator("../../cost_models/example_model.json");
    
    Transaction tx;
    tx.version = 1;
    tx.locktime = 0;
    
    TxInput input;
    input.prevout_hash = std::vector<uint8_t>(32, 0);
    input.prevout_index = 0;
    input.sequence = 0xffffffff;
    input.script_sig = {};
    tx.inputs.push_back(input);
    
    TxOutput output;
    output.value = 100000;
    output.script_pubkey = {};
    tx.outputs.push_back(output);
    
    // Push two items and concatenate
    Script unlocking = {
        0x0a,  // Push 10 bytes
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0a,  // Push 10 bytes
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    
    Script locking = {static_cast<uint8_t>(OpCode::OP_CAT)};
    
    auto result = estimator.estimate(unlocking, locking, tx, 0);
    
    assert(result.total_cycles > 0);
    assert(result.breakdown.byte_ops > 0);
    std::cout << "  ✓ OP_CAT (20 bytes): " << result.breakdown.byte_ops 
              << " cycles" << std::endl;
}

void test_hash_operations() {
    std::cout << "Test: Hash operations..." << std::endl;
    
    CostEstimator estimator("../../cost_models/example_model.json");
    
    Transaction tx;
    tx.version = 1;
    tx.locktime = 0;
    
    TxInput input;
    input.prevout_hash = std::vector<uint8_t>(32, 0);
    input.prevout_index = 0;
    input.sequence = 0xffffffff;
    input.script_sig = {};
    tx.inputs.push_back(input);
    
    TxOutput output;
    output.value = 100000;
    output.script_pubkey = {};
    tx.outputs.push_back(output);
    
    // Push data and hash it
    Script unlocking = {0x20};  // 32 bytes
    unlocking.resize(33);  // 1 + 32 bytes
    
    Script locking = {static_cast<uint8_t>(OpCode::OP_SHA256)};
    
    auto result = estimator.estimate(unlocking, locking, tx, 0);
    
    assert(result.total_cycles > 0);
    assert(result.breakdown.hashing > 0);
    std::cout << "  ✓ OP_SHA256 (32 bytes): " << result.breakdown.hashing 
              << " cycles" << std::endl;
}

void test_limits() {
    std::cout << "Test: Safety limits..." << std::endl;
    
    CostEstimator estimator("../../cost_models/example_model.json");
    
    Transaction tx;
    tx.version = 1;
    tx.locktime = 0;
    
    TxInput input;
    input.prevout_hash = std::vector<uint8_t>(32, 0);
    input.prevout_index = 0;
    input.sequence = 0xffffffff;
    input.script_sig = {};
    tx.inputs.push_back(input);
    
    TxOutput output;
    output.value = 100000;
    output.script_pubkey = {};
    tx.outputs.push_back(output);
    
    // Very large script
    Script unlocking = std::vector<uint8_t>(1000, 0x01);
    Script locking = {};
    
    EstimatorLimits limits;
    limits.max_script_size = 500;  // Set low limit
    
    auto result = estimator.estimate_with_limits(unlocking, locking, tx, 0, limits);
    
    assert(!result.warnings.empty());
    std::cout << "  ✓ Detected limit violation: " << result.warnings[0] << std::endl;
}

int main() {
    std::cout << "=== Running Cost Estimator Tests ===" << std::endl;
    std::cout << std::endl;
    
    try {
        test_basic_estimation();
        test_cat_operation();
        test_hash_operations();
        test_limits();
        
        std::cout << std::endl;
        std::cout << "All tests passed! ✓" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
