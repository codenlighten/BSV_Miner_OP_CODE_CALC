# BSV Opcode CPU Cost Engine

Complete system for pricing Bitcoin SV transactions by actual CPU execution cost.

## Project Structure

```
.
├── overview.md                       # Technical design document
│
├── bsv_script_bench/                # Phase 1-2: Benchmark Suite ✅
│   ├── src/                         # Microbenchmark infrastructure
│   ├── build/output/                # Measured data (CSV/JSON)
│   ├── RESULTS.md                   # Performance analysis
│   └── README.md
│
├── libbsv_cost_estimator/           # Phase 3: Static Estimator ✅
│   ├── include/bsv/                 # Public API
│   ├── src/                         # Symbolic executor + cost calculator
│   ├── examples/                    # Usage examples
│   ├── tests/                       # Unit tests
│   └── README.md
│
└── cost_models/                     # Phase 2: Fitted Models ✅
    └── benchmark_2025_11_10.json    # Real measurements
```

## What This Is

Two complementary tools:

### 1. Benchmark Suite (For Calibration)
Measures actual CPU costs by executing operations many times:
- Stack ops: OP_DUP (130 cycles), OP_SWAP (86 cycles)
- Byte ops: OP_CAT (0.15 cycles/byte), OP_SPLIT (0.35 cycles/byte)
- Hashing: OP_SHA256 (1.35 cycles/byte, R²=0.9999)

**Use case**: Hardware calibration, research, model validation

### 2. Cost Estimator (For Miners)
Predicts script costs without execution using symbolic analysis:
- Input: Transaction + scripts
- Output: Estimated CPU cycles
- Speed: <1ms per transaction

**Use case**: Production mempool admission, fee calculation

## Quick Start

### Run Benchmarks

```bash
cd bsv_script_bench
mkdir build && cd build
cmake .. && make -j$(nproc)

./bench_stack_ops   # Stack operations
./bench_byte_ops    # OP_CAT, OP_SPLIT (BSV-critical)
./bench_hash_ops    # Hash operations with linear fitting

# Results in output/*.csv
```

### Use Cost Estimator

```bash
cd libbsv_cost_estimator
mkdir build && cd build
cmake .. && make -j$(nproc)

./estimate_tx       # Example: estimate P2PKH, OP_CAT, hash chains
./test_estimator    # Run unit tests
```

**API Example**:
```cpp
#include "bsv/cost_estimator.h"

CostEstimator estimator("cost_models/benchmark_2025_11_10.json");
auto estimate = estimator.estimate(unlocking, locking, tx, 0);

std::cout << "CPU Cost: " << estimate.total_cycles << " cycles\n";
std::cout << "Fee: " << estimate.to_fee() << " compute units\n";
```

## Current Status

| Component | Status | Description |
|-----------|--------|-------------|
| Benchmark Infrastructure | ✅ Complete | rdtsc, perf counters, CPU pinning |
| Stack Ops Benchmarks | ✅ Complete | 48 measurements |
| Byte Ops Benchmarks | ✅ Complete | 45 measurements (up to 20MB) |
| Hash Ops Benchmarks | ✅ Complete | 40 measurements + linear models |
| Cost Model (measured) | ✅ Complete | Real data from benchmarks |
| Symbolic Executor | ✅ Complete | Stack-based cost prediction |
| SIGHASH Analyzer | ✅ Complete | Preimage size calculation |
| API & Examples | ✅ Complete | C++ library + CLI tool |
| BSV Integration | ⏳ Pending | Need actual Script VM (using stubs) |
| Signature Benchmarks | ⏳ Pending | OP_CHECKSIG with real ECDSA |
| Multi-Hardware | ⏳ Pending | AMD Zen, Intel, ARM profiles |

## Key Findings

### BSV Scales Linearly

From real benchmarks:
- **20MB OP_CAT**: ~4M cycles (~1ms on 4GHz CPU)
- **100MB OP_SHA256**: ~135M cycles (~34ms)
- **No quadratic blowup**: Memory allocation remains linear

### Hash Operations Are Predictable

Linear models fit with R² > 0.9999:
```
OP_SHA256:   cost = 1000 + 1.35·n cycles
OP_HASH256:  cost = 1800 + 1.38·n cycles  
OP_RIPEMD:   cost = 1000 + 7.60·n cycles
```

### Stack Ops Are Cheap

Even with 10k stack items:
- OP_DUP: 130 cycles (constant)
- OP_SWAP: 86 cycles (constant)
- OP_PICK: 136 cycles (constant even at depth 10k!)

## For Miners

### Integration Steps

1. **Calibrate your hardware**:
```bash
cd bsv_script_bench/build
./bench_stack_ops && ./bench_byte_ops && ./bench_hash_ops
# Generates cost_model.json for your CPU
```

2. **Load estimator in node**:
```cpp
CostEstimator estimator("my_hardware.json");
```

3. **Price transactions**:
```cpp
for (auto& input : tx.inputs) {
    auto cost = estimator.estimate(input.script_sig, prevout_script, tx, i);
    required_fee += cost.to_fee(CYCLES_PER_UNIT);
}
```

### Fee Calculation

```cpp
// Example pricing
const uint64_t CYCLES_PER_UNIT = 100000;    // 100k cycles = 1 unit
const double SATOSHIS_PER_UNIT = 1.0;        // 1 sat per unit

double cpu_fee = estimate.total_cycles / CYCLES_PER_UNIT * SATOSHIS_PER_UNIT;
```

## Documentation

- **overview.md** - Complete technical specification (algorithms, models, implementation)
- **bsv_script_bench/README.md** - Benchmark suite usage and system preparation
- **bsv_script_bench/RESULTS.md** - Performance analysis and findings
- **libbsv_cost_estimator/README.md** - API documentation and integration guide

## Next Steps

### Immediate (Production-Ready)

1. Integrate with actual BSV Script interpreter (replace stubs)
2. Benchmark ECDSA verification on target hardware
3. Add remaining BSV opcodes (currently ~15 implemented)

### Phase 4 (Multi-Hardware)

4. Run benchmarks on AMD Zen 3/4, Intel Sapphire Rapids, ARM
5. Generate hardware-specific cost models
6. Publish miner profiles

### Phase 5 (Advanced)

7. Cache optimization analysis
8. Fuzzing and edge case testing
9. Performance tuning (estimator currently <1ms per tx)

## License

MIT
