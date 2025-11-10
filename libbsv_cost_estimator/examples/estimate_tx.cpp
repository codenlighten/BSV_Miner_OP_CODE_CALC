#include "bsv/cost_estimator.h"
#include <iostream>
#include <iomanip>
#include <fstream>

using namespace bsv::cost;

// Helper to create a simple P2PKH transaction
Transaction create_sample_transaction() {
    Transaction tx;
    tx.version = 1;
    tx.locktime = 0;
    
    // One input
    TxInput input;
    input.prevout_hash = std::vector<uint8_t>(32, 0x00);
    input.prevout_index = 0;
    input.sequence = 0xffffffff;
    
    // Simple unlocking script (signature + pubkey placeholders)
    input.script_sig = {
        0x47,  // 71-byte signature
        // ... signature bytes ...
        0x21,  // 33-byte compressed pubkey
        // ... pubkey bytes ...
    };
    input.script_sig.resize(71 + 33 + 2);  // Sig + pubkey + push opcodes
    
    tx.inputs.push_back(input);
    
    // One output
    TxOutput output;
    output.value = 100000;  // 100k satoshis
    
    // P2PKH locking script: OP_DUP OP_HASH160 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG
    output.script_pubkey = {
        static_cast<uint8_t>(OpCode::OP_DUP),
        static_cast<uint8_t>(OpCode::OP_HASH160),
        0x14,  // 20 bytes
    };
    output.script_pubkey.resize(25);  // Standard P2PKH
    
    tx.outputs.push_back(output);
    
    return tx;
}

void print_estimate(const CostEstimate& est) {
    std::cout << "\n=== Cost Estimate ===" << std::endl;
    std::cout << "Total Cycles: " << est.total_cycles << std::endl;
    std::cout << "Estimated Fee: " << std::fixed << std::setprecision(2) 
              << est.to_fee() << " compute units" << std::endl;
    
    std::cout << "\nBreakdown:" << std::endl;
    std::cout << "  Parsing:      " << est.breakdown.parsing << " cycles" << std::endl;
    std::cout << "  Dispatch:     " << est.breakdown.dispatch << " cycles" << std::endl;
    std::cout << "  Stack Ops:    " << est.breakdown.stack_ops << " cycles" << std::endl;
    std::cout << "  Byte Ops:     " << est.breakdown.byte_ops << " cycles" << std::endl;
    std::cout << "  Hashing:      " << est.breakdown.hashing << " cycles" << std::endl;
    std::cout << "  Signatures:   " << est.breakdown.signatures << " cycles" << std::endl;
    
    std::cout << "\nResource Usage:" << std::endl;
    std::cout << "  Peak Stack:   " << est.peak_stack_bytes << " bytes ("
              << est.peak_stack_items << " items)" << std::endl;
    std::cout << "  Signatures:   " << est.signature_count << std::endl;
    std::cout << "  Opcodes:      " << est.opcode_count << std::endl;
    
    if (!est.warnings.empty()) {
        std::cout << "\nWarnings:" << std::endl;
        for (const auto& warning : est.warnings) {
            std::cout << "  - " << warning << std::endl;
        }
    }
}

int main(int argc, char** argv) {
    std::cout << "=== BSV Cost Estimator Example ===" << std::endl;
    
    // Load cost model
    std::string model_path = argc > 1 ? argv[1] : "../../cost_models/example_model.json";
    
    try {
        CostEstimator estimator(model_path);
        std::cout << "Loaded cost model: " << estimator.get_profile_id() << "\n" << std::endl;
        
        // Create sample transaction
        Transaction tx = create_sample_transaction();
        
        // Example 1: Simple P2PKH
        std::cout << "\n--- Example 1: Standard P2PKH ---" << std::endl;
        Script unlocking = {0x47, 0x21};  // Placeholder sig + pubkey sizes
        unlocking.resize(71 + 33 + 2);
        
        Script locking = {
            static_cast<uint8_t>(OpCode::OP_DUP),
            static_cast<uint8_t>(OpCode::OP_HASH160),
            0x14,  // 20 bytes pubkey hash
        };
        locking.resize(25);
        
        auto est1 = estimator.estimate(unlocking, locking, tx, 0);
        print_estimate(est1);
        
        // Example 2: Script with OP_CAT
        std::cout << "\n--- Example 2: Large OP_CAT ---" << std::endl;
        Script cat_unlocking = {
            0x4c, 0xff, 0x03,  // PUSHDATA1 1023 bytes
        };
        cat_unlocking.resize(1026);  // Push 1MB data
        
        Script cat_locking = {
            static_cast<uint8_t>(OpCode::OP_DUP),
            static_cast<uint8_t>(OpCode::OP_CAT),
            static_cast<uint8_t>(OpCode::OP_SHA256),
        };
        
        auto est2 = estimator.estimate(cat_unlocking, cat_locking, tx, 0);
        print_estimate(est2);
        
        // Example 3: Multiple hashes
        std::cout << "\n--- Example 3: Hash Chain ---" << std::endl;
        Script hash_unlocking = {0x20};  // 32 bytes
        hash_unlocking.resize(33);
        
        Script hash_locking = {
            static_cast<uint8_t>(OpCode::OP_SHA256),
            static_cast<uint8_t>(OpCode::OP_SHA256),
            static_cast<uint8_t>(OpCode::OP_SHA256),
            static_cast<uint8_t>(OpCode::OP_HASH256),
        };
        
        auto est3 = estimator.estimate(hash_unlocking, hash_locking, tx, 0);
        print_estimate(est3);
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
