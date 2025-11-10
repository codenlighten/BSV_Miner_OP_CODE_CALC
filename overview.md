Great question. If you want miners to price a transaction by the *work to execute its locking/unlocking scripts* (and not by prunable OP_RETURN bytes), you need two things:

1. a trustworthy way to measure per-opcode cost, and
2. a static estimator that can predict a script's cost before it's mined.

Here's a practical, engineer-friendly plan for building a Bitcoin SV CPU cost calculation engine.

# BSV-Specific Context

Bitcoin SV's unbounded design creates unique requirements:
* **No practical size limits**: Removed 520-byte push limit, raised script size limits to ~4GB
* **Restored opcodes**: `OP_CAT`, `OP_SPLIT`, `OP_NUM2BIN`, `OP_BIN2NUM` enable variable-length data manipulation
* **Large data processing**: Scripts can handle multi-MB inputs in single operations
* **Still deterministic**: No loops means symbolic execution remains tractable despite script complexity

# 1) Build a per-opcode microbenchmark suite

Benchmark the BSV node's actual Script VM (Bitcoin SV implementation), not a toy reimplementation.

## Harness Setup
* Integration: Link directly against the BSV node's script interpreter (libbitcoinconsensus or equivalent)
* Use same compiler flags, optimization level, ECC library (libsecp256k1), and hashing implementations as production nodes
* Benchmark framework: Google Benchmark or custom harness with `rdtsc`/Linux `perf`
* CPU hygiene: pin to isolated core, disable turbo/frequency scaling, warm caches, use `cset shield`, run 1000+ iterations for stable statistics

## Measurement Counters
* **Timing**: CPU cycles (via `rdtsc`) or nanoseconds
* **Instructions**: retired instructions, IPC (instructions per cycle)
* **Memory**: L1d/LLC cache misses, TLB misses, memory bandwidth
* **Branches**: branch mispredictions, pipeline stalls
* **Allocations**: malloc calls, total allocated bytes, realloc counts
* **System**: page faults, context switches (should be zero)

## BSV-Specific Test Matrix

### Stack Operations
* Opcodes: `OP_DUP`, `OP_SWAP`, `OP_PICK`, `OP_ROLL`, `OP_ROT`, etc.
* Stack depths: {1, 10, 100, 1000, 10000}
* Item sizes: {1B, 100B, 10kB, 1MB} (BSV has no 520-byte limit)

### Byte Manipulation (Critical for BSV)
* **`OP_CAT`**: Concatenate buffers of sizes:
  * Small: (10B + 10B), (100B + 100B)
  * Medium: (10kB + 10kB), (100kB + 100kB)
  * Large: (1MB + 1MB), (10MB + 10MB)
  * Asymmetric: (1B + 10MB), (10MB + 1B)
  * Measure memory allocation strategy and reallocation overhead
  
* **`OP_SPLIT`**: Split at various positions in buffers:
  * Sizes: 100B, 10kB, 100kB, 1MB, 10MB
  * Split points: start (1%), middle (50%), end (99%)
  * Measure if implementation uses memcpy or zero-copy slicing
  
* **`OP_NUM2BIN` / `OP_BIN2NUM`**: 
  * Conversions with output sizes: 1B, 8B, 32B, 256B, 10kB, 1MB
  * Test endianness handling and sign extension costs

### Hashing (Linear Cost Model)
* Opcodes: `OP_SHA1`, `OP_SHA256`, `OP_HASH160`, `OP_HASH256`, `OP_RIPEMD160`
* Input sizes: 1B, 64B, 512B, 4kB, 64kB, 1MB, 10MB, 100MB
* Fit: `cost(n) = α + β·n` where n = input bytes
* Measure OpenSSL vs custom implementation differences
* Test cache effects: repeated hashing of same data vs new data

### Control Flow
* `OP_IF/OP_NOTIF/OP_ELSE/OP_ENDIF`: Measure branch prediction costs
* Nested depths: 1, 5, 10, 50 levels
* Push sizes within conditionals: 1B, 1kB, 1MB

### Signature Verification (Dominated by ECC + Preimage)
* **`OP_CHECKSIG` / `OP_CHECKDATASIG`**:
  * Pubkey forms: compressed (33B) vs uncompressed (65B)
  * Signature types: DER-encoded, Schnorr (if enabled)
  * SIGHASH types: ALL, NONE, SINGLE, ANYONECANPAY combinations
  * **Preimage size** (critical): 
    * Transaction sizes: 250B, 1kB, 10kB, 100kB, 1MB, 10MB
    * Input/output counts: 1, 10, 100, 1000, 10000
    * Measure preimage construction cost separately from ECDSA verify
  * Cache effects: same preimage vs different preimage
  
* **`OP_CHECKMULTISIG`**:
  * m-of-n combinations: 1-of-3, 2-of-3, 3-of-5, 7-of-15, 15-of-15, 20-of-20
  * Measure key iteration overhead: cost ≈ m·(verify) + (n-m)·(pubkey_scan)
  * Include off-by-one dummy element pop (Satoshi bug preserved in BSV)
  * Vary preimage size as above

### Arithmetic & Logic
* `OP_ADD`, `OP_SUB`, `OP_MUL`, `OP_DIV`, `OP_MOD`: Test with 1B, 4B, 8B numbers
* `OP_AND`, `OP_OR`, `OP_XOR`: Test with 1B to 1MB bitstrings
* `OP_LSHIFT`, `OP_RSHIFT`: Measure if optimized or byte-by-byte

## Output Format
For each (opcode, parameter_set):
```
opcode, param_desc, size_bytes, median_cycles, p90_cycles, p99_cycles, 
median_ns, instructions, L1_misses, LLC_misses, branch_misses, 
alloc_count, alloc_bytes
```

Generate CSV and JSON outputs for model fitting.

# 2) Fit cost models from measurements

Use regression on benchmark data to derive per-opcode cost formulas:

## Model Categories

### Constant-time operations
Stack manipulations with small items, basic arithmetic on script numbers:
```
cost(op) = c₀
```
Examples: `OP_DUP`, `OP_SWAP`, `OP_ADD` (on script numbers ≤8 bytes)

### Linear in data size
All byte manipulation and hashing operations:
```
cost(op, n) = c₀ + c₁·n
```
Where n = total bytes processed

**Byte operations:**
- `OP_CAT(n₁, n₂)`: `c₀ + c₁·(n₁ + n₂)` plus allocation overhead
- `OP_SPLIT(n)`: `c₀ + c₁·n` (depends on copy vs slice implementation)
- `OP_NUM2BIN(n_out)`: `c₀ + c₁·n_out`
- `OP_BIN2NUM(n_in)`: `c₀ + c₁·n_in`

**Hash operations:**
- All hashing opcodes: `c₀ + c₁·n` where c₁ ≈ cycles-per-byte
- Measure separately for each hash function (SHA1, SHA256, RIPEMD160, etc.)
- Note: c₁ varies by ~2x between fastest (SHA1) and slowest

### Signature verification (complex)

**`OP_CHECKSIG` / `OP_CHECKDATASIG`:**
```
cost = c_ecdsa + c_preimage·tx_bytes_hashed
```
- `c_ecdsa`: ECDSA/Schnorr verification (~50k-100k cycles typical)
- `c_preimage`: cost to build and hash the sighash preimage
- `tx_bytes_hashed` depends on SIGHASH type:
  - `SIGHASH_ALL`: entire transaction (all inputs + all outputs)
  - `SIGHASH_SINGLE`: current input + corresponding output
  - `SIGHASH_NONE`: current input only
  - `ANYONECANPAY` flag: excludes other inputs

**`OP_CHECKMULTISIG(m, n)`:**
```
cost = m·(c_ecdsa + c_preimage·tx_bytes_hashed) + c_keyscan·(n - m) + c_setup
```
- Verify m signatures
- Scan through remaining (n-m) pubkeys for matches
- `c_setup`: overhead for dummy element pop and setup

### Memory allocation overhead (BSV-specific)

For operations that allocate large buffers (`OP_CAT` on MB-sized data):
```
cost_alloc = c_malloc + c_memcpy·n
```
Track separately if allocator behavior changes at size thresholds (e.g., small vs large allocations)

## Fitting Process

1. **Collect benchmark CSV data** from Section 1
2. **Fit linear models**: Use least-squares regression (or quantile regression for p95/p99)
3. **Validate**: Test predicted vs measured on held-out parameter combinations
4. **Per-hardware profiles**: Fit separately for different CPU architectures:
   - AMD Zen 3/4
   - Intel Ice Lake / Sapphire Rapids
   - ARM Neoverse
   
5. **Conservative estimates**: Use p95 or p99 measurements for coefficients to avoid underpricing worst cases

## Output: cost_model.json

```json
{
  "profile": "AMD_Zen4_4.0GHz_libsecp256k1_v0.4.0_OpenSSL_3.0",
  "timestamp": "2025-11-10",
  "constants": {
    "c_dispatch": 5.2,
    "c_parse_per_byte": 0.8
  },
  "opcodes": {
    "OP_DUP": {"model": "constant", "c0": 12},
    "OP_CAT": {"model": "linear", "c0": 150, "c1": 1.2, "c_alloc": 80},
    "OP_SPLIT": {"model": "linear", "c0": 120, "c1": 1.1},
    "OP_SHA256": {"model": "linear", "c0": 200, "c1": 3.8},
    "OP_CHECKSIG": {
      "model": "signature",
      "c_ecdsa": 85000,
      "c_preimage_per_byte": 2.5
    },
    "OP_CHECKMULTISIG": {
      "model": "multisig",
      "c_ecdsa": 85000,
      "c_preimage_per_byte": 2.5,
      "c_keyscan": 150,
      "c_setup": 300
    }
  }
}
```

Miners publish their active profile. Wallets use it to estimate fees.

# 3) Deterministic static cost estimator (pre-mining)

Bitcoin Script has no loops, so you can *symbolically execute* the script to compute cost upper bounds without running the VM.

## Core Algorithm: Symbolic Stack Simulator

```
Input: unlocking_script, locking_script, transaction, input_index, cost_model
Output: estimated_cost, peak_stack_bytes, signature_count

1. Parse combined_script = unlocking_script || locking_script
2. Initialize symbolic_stack = []
3. total_cost = c_parse · len(combined_script)
4. For each opcode in combined_script:
   a. total_cost += c_dispatch
   b. Execute opcode symbolically:
      - Track stack item sizes (bytes) without computing values
      - For OP_CAT(item1, item2): push (size1 + size2)
      - For OP_SPLIT(item, n): push (n, size-n)
      - For OP_DUP: push duplicate of top item size
      - For OP_HASH256(item): push 32 bytes
   c. Look up cost_model(opcode, parameters)
      - Parameters = item sizes from symbolic stack
      - For OP_CHECKSIG: compute tx_bytes_hashed from SIGHASH type
   d. total_cost += cost_model(opcode, params)
   e. Track peak_stack_bytes = max over execution
5. Return total_cost
```

## Handling BSV-Specific Challenges

### Large Data Tracking
- Track exact byte sizes through the execution
- For `OP_CAT` chains, accumulate sizes: `size(a||b||c) = size(a) + size(b) + size(c)`
- Detect potential memory explosions (e.g., recursive doubling via CAT)

### Branching (`OP_IF` / `OP_ELSE`)
Conservative worst-case approach:
```
For OP_IF ... OP_ELSE ... OP_ENDIF:
  - Execute both branches symbolically
  - Take max(cost_if_branch, cost_else_branch)
  - Stack state after: must be compatible from both branches
```

Alternative: Assume worst case branch (usually the longer one)

### Data-Dependent Operations
Some operations depend on runtime values not known statically:
- `OP_SPLIT` at unknown position: assume worst-case (full copy)
- `OP_PICK` / `OP_ROLL` with unknown depth: assume max stack depth

For **unbounded worst cases**, set conservative limits or reject from mempool.

### SIGHASH Preimage Calculation

```python
def compute_tx_bytes_hashed(tx, input_idx, sighash_type):
    base_type = sighash_type & 0x1f
    anyone_can_pay = bool(sighash_type & 0x80)
    
    bytes_hashed = 4  # version
    
    if anyone_can_pay:
        bytes_hashed += 1 + len(tx.inputs[input_idx].serialize())
    else:
        bytes_hashed += varint_size(len(tx.inputs))
        bytes_hashed += sum(len(inp.serialize()) for inp in tx.inputs)
    
    if base_type == SIGHASH_SINGLE:
        bytes_hashed += 1 + len(tx.outputs[input_idx].serialize())
    elif base_type == SIGHASH_NONE:
        bytes_hashed += 1  # empty outputs
    else:  # SIGHASH_ALL
        bytes_hashed += varint_size(len(tx.outputs))
        bytes_hashed += sum(len(out.serialize()) for out in tx.outputs)
    
    bytes_hashed += 4  # locktime
    bytes_hashed += 4  # sighash type itself
    
    return bytes_hashed
```

## Edge Cases & Limits

1. **Script size limits**: BSV allows ~4GB scripts, but set practical estimator limits (e.g., 100MB)
2. **Stack depth**: Track current and peak stack depth, reject if >10,000 items
3. **Item size explosion**: Reject if any single stack item exceeds reasonable limit (e.g., 100MB)
4. **Execution steps**: Count total opcodes, reject if >1,000,000 operations
5. **Time limits**: Estimator must complete in <100ms; abort and reject if too complex

## Estimator Output Structure

```json
{
  "total_cost_cycles": 15234567,
  "breakdown": {
    "parsing": 1200,
    "dispatch": 5000,
    "stack_ops": 8500,
    "hashing": 3456789,
    "signatures": 11763078
  },
  "peak_stack_bytes": 2048576,
  "peak_stack_items": 42,
  "signature_count": 3,
  "opcode_count": 1247,
  "warnings": ["Large OP_CAT detected: 5MB allocation"]
}
```

## Implementation Notes

- **Language**: C++ or Rust for performance (must handle MB-sized scripts quickly)
- **Library structure**: 
  - `script_parser`: Parse BSV script into opcode sequence
  - `symbolic_executor`: Simulate stack without actual computation
  - `cost_calculator`: Apply cost models from JSON profile
- **Testing**: Validate against actual execution on test vectors
- **Caching**: Cache preimage calculations for same tx structure

# 4) Fee calculation (for miner reference)

Once you have EST_COST from the estimator, miners can price transactions:

```
FEE_CPU = rate_cpu · EST_COST / CYCLES_PER_UNIT
```

Where `CYCLES_PER_UNIT` normalizes to convenient units (e.g., 100k cycles = 1 compute unit).

This can be combined with storage/bandwidth fees as needed, but the core engine delivers the CPU cost component.

# 5) Implementation considerations

## Critical BSV-Specific Issues

### SIGHASH Preimage Dominates Cost
- In transactions with many inputs/outputs, preimage construction and hashing can cost 10x more than ECDSA verification
- BSV's large blocks mean transactions can have thousands of inputs/outputs
- **Cache strategy**: Node should cache preimages when validating multiple signatures in same tx
- **Estimator decision**: Use conservative (no-cache) costs for safety, or credit cache hits with proof

### OP_CAT Memory Allocation
- Repeated `OP_CAT` can cause reallocation cascades
- Example: `(a || b) || c` may allocate, copy, then reallocate
- Measure actual BSV node allocator behavior (tcmalloc? jemalloc? glibc malloc?)
- Model allocation overhead separately for sizes: <4kB, 4kB-1MB, >1MB

### OP_CHECKMULTISIG Scaling
- m-of-n scales as O(m·n) in worst case (if keys don't match in order)
- BSV allows up to 20 signatures (old consensus rules)
- A 15-of-15 multisig on a 10MB transaction is a worst-case scenario
- Estimate conservatively: assume keys checked in worst order

### Very Large Scripts
- BSV allows scripts up to ~4GB (consensus-limited by transaction size)
- Estimator must handle efficiently without loading entire script into memory
- **Streaming parser**: Process script in chunks if >100MB
- **Reject limits**: Set practical limits for mempool (e.g., 1GB script max)

### Determinism & Precision
- Round cost coefficients to p95/p99 to avoid underpricing edge cases
- Use integer arithmetic in estimator to avoid floating-point non-determinism
- All miners using same cost_model.json must get identical EST_COST

## Validation & Testing

1. **Ground truth dataset**: Run 10,000 real transactions through actual BSV node with cycle counting
2. **Correlation test**: Compare estimated vs actual costs, target R² > 0.95
3. **Worst-case vectors**: Generate adversarial scripts (nested CAT, large multisig, etc.)
4. **Cross-validation**: Test estimator on different CPU architectures
5. **Regression suite**: Ensure estimator never underprices by >10%

## Performance Requirements

- **Estimator speed**: Must analyze typical transaction in <1ms, complex scripts in <100ms
- **Throughput**: Node should estimate 10,000 tx/sec on moderate hardware
- **Memory**: Estimator should use <100MB RAM for any single script analysis

# 6) Deliverables for the CPU cost engine

## 1. Benchmark Suite (`bsv_script_bench/`)
**Purpose**: Measure actual BSV node opcode costs

**Structure**:
```
bsv_script_bench/
├── CMakeLists.txt or Makefile
├── src/
│   ├── bench_harness.cpp       # Core benchmarking infrastructure
│   ├── bench_stack_ops.cpp     # OP_DUP, OP_SWAP, etc.
│   ├── bench_byte_ops.cpp      # OP_CAT, OP_SPLIT (critical for BSV)
│   ├── bench_hash_ops.cpp      # OP_SHA256, etc.
│   ├── bench_sig_ops.cpp       # OP_CHECKSIG, OP_CHECKMULTISIG
│   ├── bench_control_flow.cpp  # OP_IF, OP_ELSE
│   └── bench_arithmetic.cpp    # OP_ADD, OP_MUL, etc.
├── deps/
│   └── bitcoin-sv/             # Link against actual BSV node libs
└── output/
    ├── results_zen4.csv        # Benchmark results per hardware
    └── results_zen4.json
```

**Output Schema** (CSV):
```
opcode,param_desc,input_bytes,median_cycles,p90_cycles,p99_cycles,median_ns,
instructions,IPC,L1d_misses,LLC_misses,branch_misses,malloc_count,alloc_bytes
```

**Key Features**:
- Direct integration with BSV node script interpreter
- CPU isolation and stable measurement environment
- Parameterized tests covering 1 byte to 100MB inputs
- Separate measurement of allocation overhead

## 2. Cost Model Definitions (`cost_models/`)
**Purpose**: Store fitted cost coefficients per hardware profile

**Structure**:
```
cost_models/
├── amd_zen4_4.0ghz.json
├── intel_sapphire_rapids_3.5ghz.json
├── arm_neoverse_n2_3.0ghz.json
└── model_schema.json
```

**Schema** (`model_schema.json`):
```json
{
  "profile_id": "string",
  "hardware": {
    "cpu": "string",
    "frequency_ghz": "number",
    "cache_sizes": "string"
  },
  "software": {
    "bsv_version": "string",
    "libsecp256k1_version": "string",
    "openssl_version": "string"
  },
  "calibration_date": "ISO-8601",
  "constants": {
    "c_dispatch": "number (cycles per opcode)",
    "c_parse_per_byte": "number (cycles per script byte)"
  },
  "opcodes": {
    "OP_XXX": {
      "model": "constant|linear|signature|multisig",
      "c0": "number (base cost)",
      "c1": "number (per-byte cost, if linear)",
      "c_ecdsa": "number (signature verify cost)",
      "c_preimage_per_byte": "number",
      "c_keyscan": "number (per-key in multisig)",
      "c_alloc": "number (allocation overhead)"
    }
  }
}
```

## 3. Static Cost Estimator Library (`libbsv_cost_estimator`)
**Purpose**: Symbolically execute scripts and compute estimated CPU cost

**API** (C++):
```cpp
namespace bsv::cost {

struct CostEstimate {
    uint64_t total_cycles;
    uint64_t peak_stack_bytes;
    uint32_t peak_stack_items;
    uint32_t signature_count;
    uint32_t opcode_count;
    std::map<std::string, uint64_t> breakdown;  // cost by category
    std::vector<std::string> warnings;
};

class CostEstimator {
public:
    // Load cost model from JSON file
    explicit CostEstimator(const std::string& model_path);
    
    // Estimate cost of a transaction input script execution
    CostEstimate estimate(
        const CScript& unlocking_script,
        const CScript& locking_script,
        const CTransaction& tx,
        uint32_t input_index,
        uint32_t flags = SCRIPT_VERIFY_P2SH
    );
    
    // Estimate cost with limits (reject if exceeded)
    struct Limits {
        uint64_t max_script_size = 100'000'000;  // 100MB
        uint32_t max_stack_items = 10'000;
        uint64_t max_stack_item_size = 100'000'000;  // 100MB
        uint32_t max_opcode_count = 1'000'000;
    };
    
    CostEstimate estimate_with_limits(
        const CScript& unlocking_script,
        const CScript& locking_script,
        const CTransaction& tx,
        uint32_t input_index,
        const Limits& limits,
        uint32_t flags = SCRIPT_VERIFY_P2SH
    );
};

} // namespace bsv::cost
```

**Implementation Files**:
```
libbsv_cost_estimator/
├── include/
│   └── bsv/cost_estimator.h
├── src/
│   ├── cost_estimator.cpp
│   ├── symbolic_executor.cpp    # Core symbolic stack simulation
│   ├── cost_calculator.cpp      # Apply cost models
│   ├── sighash_analyzer.cpp     # Compute preimage sizes
│   └── model_loader.cpp         # Parse cost_model.json
├── tests/
│   ├── test_symbolic_executor.cpp
│   ├── test_cost_accuracy.cpp   # Compare vs actual execution
│   └── test_edge_cases.cpp      # Large scripts, nested ops
└── CMakeLists.txt
```

**Key Algorithms**:
- Symbolic stack simulator with exact byte-size tracking
- Conservative branch analysis (worst-case of IF/ELSE)
- SIGHASH preimage size calculation
- Safety limits and timeout protection

## 4. Command-Line Tool (`bsv-estimate-cost`)
**Purpose**: Standalone tool for testing and integration

```bash
# Estimate cost of a transaction
./bsv-estimate-cost \
  --tx-hex <hex> \
  --input-index 0 \
  --prevout-script <hex> \
  --cost-model cost_models/amd_zen4_4.0ghz.json \
  --output json

# Batch processing
./bsv-estimate-cost \
  --batch transactions.jsonl \
  --cost-model cost_models/amd_zen4_4.0ghz.json \
  --output-dir estimates/
```

## 5. Validation & Testing Suite
**Purpose**: Ensure estimator accuracy

**Components**:
1. **Ground truth dataset**: 10,000 real BSV transactions with measured actual costs
2. **Correlation tests**: R² between estimated and actual costs
3. **Adversarial scripts**: Worst-case scenarios (nested CAT, large multisig, etc.)
4. **Regression tests**: Ensure estimates never fall below actual costs by >10%
5. **Performance benchmarks**: Estimator must process 10k tx/sec

## Development Roadmap

**Phase 1** (Weeks 1-4): Benchmark suite
- Integrate with BSV node codebase
- Implement measurement harness
- Run benchmark matrix on 3 hardware profiles

**Phase 2** (Weeks 5-8): Cost model fitting
- Regression analysis on benchmark data
- Generate cost_model.json files
- Validation against held-out test data

**Phase 3** (Weeks 9-12): Static estimator
- Implement symbolic executor
- SIGHASH preimage analysis
- Safety limits and edge case handling

**Phase 4** (Weeks 13-14): Validation
- Ground truth dataset collection
- Accuracy testing
- Performance optimization

**Phase 5** (Week 15-16): Integration
- CLI tool
- Documentation
- Miner integration guide
