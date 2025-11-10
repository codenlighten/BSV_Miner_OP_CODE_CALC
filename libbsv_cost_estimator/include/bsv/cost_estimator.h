#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace bsv {
namespace cost {

// Script opcode identifiers (subset - expand as needed)
enum class OpCode : uint8_t {
    // Stack operations
    OP_DUP = 0x76,
    OP_SWAP = 0x7c,
    OP_PICK = 0x79,
    OP_ROLL = 0x7a,
    OP_ROT = 0x7b,
    
    // Byte operations
    OP_CAT = 0x7e,
    OP_SPLIT = 0x7f,
    OP_NUM2BIN = 0x80,
    OP_BIN2NUM = 0x81,
    
    // Hashing
    OP_SHA1 = 0xa7,
    OP_SHA256 = 0xa8,
    OP_HASH160 = 0xa9,
    OP_HASH256 = 0xaa,
    OP_RIPEMD160 = 0xa6,
    
    // Signatures
    OP_CHECKSIG = 0xac,
    OP_CHECKSIGVERIFY = 0xad,
    OP_CHECKMULTISIG = 0xae,
    
    // Control
    OP_IF = 0x63,
    OP_ELSE = 0x67,
    OP_ENDIF = 0x68,
    
    // Constants
    OP_0 = 0x00,
    OP_1 = 0x51,
    OP_PUSHDATA1 = 0x4c,
    OP_PUSHDATA2 = 0x4d,
    OP_PUSHDATA4 = 0x4e,
};

// SIGHASH type flags
enum SigHashType : uint32_t {
    SIGHASH_ALL = 0x01,
    SIGHASH_NONE = 0x02,
    SIGHASH_SINGLE = 0x03,
    SIGHASH_ANYONECANPAY = 0x80,
};

// Simple script representation (will integrate with actual BSV types later)
using Script = std::vector<uint8_t>;

// Simplified transaction structure
struct TxInput {
    std::vector<uint8_t> prevout_hash;
    uint32_t prevout_index;
    Script script_sig;  // Unlocking script
    uint32_t sequence;
};

struct TxOutput {
    uint64_t value;
    Script script_pubkey;  // Locking script
};

struct Transaction {
    uint32_t version;
    std::vector<TxInput> inputs;
    std::vector<TxOutput> outputs;
    uint32_t locktime;
    
    size_t serialize_size() const;
};

// Cost estimation result
struct CostEstimate {
    uint64_t total_cycles;
    
    // Breakdown by category
    struct Breakdown {
        uint64_t parsing;
        uint64_t dispatch;
        uint64_t stack_ops;
        uint64_t byte_ops;
        uint64_t hashing;
        uint64_t signatures;
        uint64_t control_flow;
    } breakdown;
    
    // Resource usage
    uint64_t peak_stack_bytes;
    uint32_t peak_stack_items;
    uint32_t signature_count;
    uint32_t opcode_count;
    
    // Warnings
    std::vector<std::string> warnings;
    
    // Convert to fee (cycles / cycles_per_unit)
    double to_fee(uint64_t cycles_per_unit = 100000) const {
        return static_cast<double>(total_cycles) / cycles_per_unit;
    }
};

// Safety limits for script execution
struct EstimatorLimits {
    uint64_t max_script_size = 100'000'000;      // 100MB
    uint32_t max_stack_items = 10'000;
    uint64_t max_stack_item_size = 100'000'000;  // 100MB
    uint32_t max_opcode_count = 1'000'000;
    uint64_t max_total_cycles = 10'000'000'000;  // 10B cycles (safety)
};

// Main cost estimator class
class CostEstimator {
public:
    // Load cost model from JSON file
    explicit CostEstimator(const std::string& model_path);
    ~CostEstimator();
    
    // Non-copyable
    CostEstimator(const CostEstimator&) = delete;
    CostEstimator& operator=(const CostEstimator&) = delete;
    
    // Estimate cost of a transaction input script execution
    CostEstimate estimate(
        const Script& unlocking_script,
        const Script& locking_script,
        const Transaction& tx,
        uint32_t input_index
    ) const;
    
    // Estimate with custom limits
    CostEstimate estimate_with_limits(
        const Script& unlocking_script,
        const Script& locking_script,
        const Transaction& tx,
        uint32_t input_index,
        const EstimatorLimits& limits
    ) const;
    
    // Get model metadata
    std::string get_profile_id() const;
    std::string get_hardware_info() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

// Helper: Parse script bytes into opcode sequence
std::vector<OpCode> parse_script(const Script& script);

// Helper: Calculate SIGHASH preimage size
uint64_t calculate_sighash_size(
    const Transaction& tx,
    uint32_t input_index,
    SigHashType sighash_type
);

} // namespace cost
} // namespace bsv
