#include "bsv/cost_estimator.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cmath>

// Simple JSON parsing (for production, use nlohmann/json or similar)
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace bsv {
namespace cost {

// Cost model for a single opcode
struct OpcodeCostModel {
    enum class Type {
        CONSTANT,
        LINEAR,
        SIGNATURE,
        MULTISIG
    } type;
    
    double c0 = 0;              // Base cost
    double c1 = 0;              // Per-byte cost (linear model)
    double c_ecdsa = 0;         // ECDSA verification cost
    double c_preimage_per_byte = 0;  // Preimage hashing cost
    double c_keyscan = 0;       // Per-key scan (multisig)
    double c_setup = 0;         // Setup overhead
    double c_alloc = 0;         // Allocation overhead
};

// Internal implementation
class CostEstimator::Impl {
public:
    explicit Impl(const std::string& model_path) {
        load_model(model_path);
    }
    
    CostEstimate estimate(
        const Script& unlocking_script,
        const Script& locking_script,
        const Transaction& tx,
        uint32_t input_index,
        const EstimatorLimits& limits
    ) const;
    
    std::string profile_id;
    std::string hardware_info;
    
private:
    void load_model(const std::string& path);
    uint64_t calculate_opcode_cost(OpCode op, const std::vector<uint64_t>& params) const;
    
    double c_dispatch = 5.0;      // Per-opcode dispatch overhead
    double c_parse_per_byte = 0.8; // Script parsing cost
    
    std::map<OpCode, OpcodeCostModel> opcode_costs;
};

void CostEstimator::Impl::load_model(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open cost model: " + path);
    }
    
    json model;
    file >> model;
    
    profile_id = model.value("profile_id", "unknown");
    
    // Load constants
    if (model.contains("constants")) {
        c_dispatch = model["constants"].value("c_dispatch", 5.0);
        c_parse_per_byte = model["constants"].value("c_parse_per_byte", 0.8);
    }
    
    // Load opcode models
    if (model.contains("opcodes")) {
        for (auto& [opcode_name, opcode_data] : model["opcodes"].items()) {
            OpcodeCostModel cost_model;
            
            std::string model_type = opcode_data.value("model", "constant");
            if (model_type == "constant") {
                cost_model.type = OpcodeCostModel::Type::CONSTANT;
                cost_model.c0 = opcode_data.value("c0", 0.0);
            } else if (model_type == "linear") {
                cost_model.type = OpcodeCostModel::Type::LINEAR;
                cost_model.c0 = opcode_data.value("c0", 0.0);
                cost_model.c1 = opcode_data.value("c1", 0.0);
                cost_model.c_alloc = opcode_data.value("c_alloc", 0.0);
            } else if (model_type == "signature") {
                cost_model.type = OpcodeCostModel::Type::SIGNATURE;
                cost_model.c_ecdsa = opcode_data.value("c_ecdsa", 85000.0);
                cost_model.c_preimage_per_byte = opcode_data.value("c_preimage_per_byte", 2.5);
            } else if (model_type == "multisig") {
                cost_model.type = OpcodeCostModel::Type::MULTISIG;
                cost_model.c_ecdsa = opcode_data.value("c_ecdsa", 85000.0);
                cost_model.c_preimage_per_byte = opcode_data.value("c_preimage_per_byte", 2.5);
                cost_model.c_keyscan = opcode_data.value("c_keyscan", 150.0);
                cost_model.c_setup = opcode_data.value("c_setup", 300.0);
            }
            
            // Map opcode name to enum (simplified - expand as needed)
            if (opcode_name == "OP_DUP") opcode_costs[OpCode::OP_DUP] = cost_model;
            else if (opcode_name == "OP_SWAP") opcode_costs[OpCode::OP_SWAP] = cost_model;
            else if (opcode_name == "OP_CAT") opcode_costs[OpCode::OP_CAT] = cost_model;
            else if (opcode_name == "OP_SPLIT") opcode_costs[OpCode::OP_SPLIT] = cost_model;
            else if (opcode_name == "OP_SHA256") opcode_costs[OpCode::OP_SHA256] = cost_model;
            else if (opcode_name == "OP_HASH256") opcode_costs[OpCode::OP_HASH256] = cost_model;
            else if (opcode_name == "OP_CHECKSIG") opcode_costs[OpCode::OP_CHECKSIG] = cost_model;
            else if (opcode_name == "OP_CHECKMULTISIG") opcode_costs[OpCode::OP_CHECKMULTISIG] = cost_model;
        }
    }
}

uint64_t CostEstimator::Impl::calculate_opcode_cost(
    OpCode op,
    const std::vector<uint64_t>& params
) const {
    auto it = opcode_costs.find(op);
    if (it == opcode_costs.end()) {
        // Unknown opcode - use default
        return 100;
    }
    
    const auto& model = it->second;
    
    switch (model.type) {
        case OpcodeCostModel::Type::CONSTANT:
            return static_cast<uint64_t>(model.c0);
            
        case OpcodeCostModel::Type::LINEAR: {
            uint64_t n = params.empty() ? 0 : params[0];
            return static_cast<uint64_t>(model.c0 + model.c1 * n + model.c_alloc);
        }
            
        case OpcodeCostModel::Type::SIGNATURE: {
            uint64_t preimage_size = params.empty() ? 1000 : params[0];
            return static_cast<uint64_t>(
                model.c_ecdsa + model.c_preimage_per_byte * preimage_size
            );
        }
            
        case OpcodeCostModel::Type::MULTISIG: {
            uint64_t m = params.size() > 0 ? params[0] : 1;  // signatures to verify
            uint64_t n = params.size() > 1 ? params[1] : 3;  // total pubkeys
            uint64_t preimage_size = params.size() > 2 ? params[2] : 1000;
            
            return static_cast<uint64_t>(
                m * (model.c_ecdsa + model.c_preimage_per_byte * preimage_size) +
                (n - m) * model.c_keyscan +
                model.c_setup
            );
        }
    }
    
    return 100; // Fallback
}

CostEstimate CostEstimator::Impl::estimate(
    const Script& unlocking_script,
    const Script& locking_script,
    const Transaction& tx,
    uint32_t input_index,
    const EstimatorLimits& limits
) const {
    CostEstimate result;
    result.total_cycles = 0;
    result.breakdown = {};
    result.peak_stack_bytes = 0;
    result.peak_stack_items = 0;
    result.signature_count = 0;
    result.opcode_count = 0;
    
    // Combine scripts (unlocking || locking)
    Script combined;
    combined.insert(combined.end(), unlocking_script.begin(), unlocking_script.end());
    combined.insert(combined.end(), locking_script.begin(), locking_script.end());
    
    // Check size limits
    if (combined.size() > limits.max_script_size) {
        result.warnings.push_back("Script exceeds size limit");
        return result;
    }
    
    // Parsing cost
    result.breakdown.parsing = static_cast<uint64_t>(c_parse_per_byte * combined.size());
    result.total_cycles += result.breakdown.parsing;
    
    // Symbolic execution
    std::vector<uint64_t> stack_sizes;  // Track size of each stack item
    uint64_t current_stack_bytes = 0;
    
    size_t pc = 0;  // Program counter
    while (pc < combined.size()) {
        if (result.opcode_count >= limits.max_opcode_count) {
            result.warnings.push_back("Opcode count limit exceeded");
            break;
        }
        
        uint8_t op_byte = combined[pc++];
        result.opcode_count++;
        
        // Dispatch overhead
        result.breakdown.dispatch += static_cast<uint64_t>(c_dispatch);
        result.total_cycles += static_cast<uint64_t>(c_dispatch);
        
        // Handle push operations
        if (op_byte > 0 && op_byte < 0x4c) {
            // Direct push of N bytes
            uint64_t push_size = op_byte;
            pc += push_size;
            stack_sizes.push_back(push_size);
            current_stack_bytes += push_size;
        } else if (op_byte == static_cast<uint8_t>(OpCode::OP_PUSHDATA1) && pc < combined.size()) {
            uint64_t push_size = combined[pc++];
            pc += push_size;
            stack_sizes.push_back(push_size);
            current_stack_bytes += push_size;
        } else {
            // Execute opcode symbolically
            OpCode op = static_cast<OpCode>(op_byte);
            std::vector<uint64_t> params;
            
            switch (op) {
                case OpCode::OP_DUP:
                    if (!stack_sizes.empty()) {
                        uint64_t top_size = stack_sizes.back();
                        stack_sizes.push_back(top_size);
                        current_stack_bytes += top_size;
                        params = {top_size};
                    }
                    result.breakdown.stack_ops += calculate_opcode_cost(op, params);
                    break;
                    
                case OpCode::OP_SWAP:
                    // Just swap, no size change
                    if (stack_sizes.size() >= 2) {
                        std::swap(stack_sizes[stack_sizes.size()-1], stack_sizes[stack_sizes.size()-2]);
                    }
                    result.breakdown.stack_ops += calculate_opcode_cost(op, {});
                    break;
                    
                case OpCode::OP_CAT:
                    if (stack_sizes.size() >= 2) {
                        uint64_t size_b = stack_sizes.back(); stack_sizes.pop_back();
                        uint64_t size_a = stack_sizes.back(); stack_sizes.pop_back();
                        uint64_t result_size = size_a + size_b;
                        stack_sizes.push_back(result_size);
                        current_stack_bytes = current_stack_bytes - size_a - size_b + result_size;
                        params = {result_size};
                    }
                    result.breakdown.byte_ops += calculate_opcode_cost(op, params);
                    break;
                    
                case OpCode::OP_SHA256:
                case OpCode::OP_HASH256:
                    if (!stack_sizes.empty()) {
                        uint64_t input_size = stack_sizes.back();
                        stack_sizes.pop_back();
                        current_stack_bytes -= input_size;
                        stack_sizes.push_back(32);  // SHA256 output
                        current_stack_bytes += 32;
                        params = {input_size};
                    }
                    result.breakdown.hashing += calculate_opcode_cost(op, params);
                    break;
                    
                case OpCode::OP_CHECKSIG: {
                    // Calculate preimage size
                    uint64_t preimage_size = calculate_sighash_size(tx, input_index, SIGHASH_ALL);
                    result.breakdown.signatures += calculate_opcode_cost(op, {preimage_size});
                    result.signature_count++;
                    // Pop sig and pubkey from stack
                    if (stack_sizes.size() >= 2) {
                        stack_sizes.pop_back();
                        stack_sizes.pop_back();
                        stack_sizes.push_back(1);  // Push result (true/false)
                    }
                    break;
                }
                    
                default:
                    // Unknown opcode - estimate conservatively
                    result.total_cycles += 100;
                    break;
            }
            
            result.total_cycles += calculate_opcode_cost(op, params);
        }
        
        // Track peak stack usage
        result.peak_stack_bytes = std::max(result.peak_stack_bytes, current_stack_bytes);
        result.peak_stack_items = std::max(result.peak_stack_items, static_cast<uint32_t>(stack_sizes.size()));
        
        // Check limits
        if (current_stack_bytes > limits.max_stack_item_size) {
            result.warnings.push_back("Stack byte limit exceeded");
            break;
        }
        if (stack_sizes.size() > limits.max_stack_items) {
            result.warnings.push_back("Stack item count limit exceeded");
            break;
        }
    }
    
    return result;
}

// Public API implementation

CostEstimator::CostEstimator(const std::string& model_path)
    : pimpl_(std::make_unique<Impl>(model_path)) {
}

CostEstimator::~CostEstimator() = default;

CostEstimate CostEstimator::estimate(
    const Script& unlocking_script,
    const Script& locking_script,
    const Transaction& tx,
    uint32_t input_index
) const {
    EstimatorLimits default_limits;
    return pimpl_->estimate(unlocking_script, locking_script, tx, input_index, default_limits);
}

CostEstimate CostEstimator::estimate_with_limits(
    const Script& unlocking_script,
    const Script& locking_script,
    const Transaction& tx,
    uint32_t input_index,
    const EstimatorLimits& limits
) const {
    return pimpl_->estimate(unlocking_script, locking_script, tx, input_index, limits);
}

std::string CostEstimator::get_profile_id() const {
    return pimpl_->profile_id;
}

std::string CostEstimator::get_hardware_info() const {
    return pimpl_->hardware_info;
}

// Helper implementations

size_t Transaction::serialize_size() const {
    size_t size = 4; // version
    size += 1; // input count (assuming compact)
    for (const auto& input : inputs) {
        size += 36; // outpoint (32 + 4)
        size += 1 + input.script_sig.size(); // script length + script
        size += 4; // sequence
    }
    size += 1; // output count
    for (const auto& output : outputs) {
        size += 8; // value
        size += 1 + output.script_pubkey.size(); // script
    }
    size += 4; // locktime
    return size;
}

uint64_t calculate_sighash_size(
    const Transaction& tx,
    uint32_t input_index,
    SigHashType sighash_type
) {
    uint32_t base_type = sighash_type & 0x1f;
    bool anyone_can_pay = (sighash_type & SIGHASH_ANYONECANPAY) != 0;
    
    uint64_t size = 4; // version
    
    if (anyone_can_pay) {
        // Only current input
        size += 1 + 36 + 1 + tx.inputs[input_index].script_sig.size() + 4;
    } else {
        // All inputs
        size += 1; // input count
        for (const auto& input : tx.inputs) {
            size += 36 + 1 + input.script_sig.size() + 4;
        }
    }
    
    if (base_type == SIGHASH_SINGLE) {
        // Only corresponding output
        if (input_index < tx.outputs.size()) {
            size += 1 + 8 + 1 + tx.outputs[input_index].script_pubkey.size();
        }
    } else if (base_type == SIGHASH_NONE) {
        size += 1; // empty outputs
    } else {  // SIGHASH_ALL
        size += 1; // output count
        for (const auto& output : tx.outputs) {
            size += 8 + 1 + output.script_pubkey.size();
        }
    }
    
    size += 4; // locktime
    size += 4; // sighash type
    
    return size;
}

} // namespace cost
} // namespace bsv
