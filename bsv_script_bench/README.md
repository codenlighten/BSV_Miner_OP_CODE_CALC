# BSV Script CPU Cost Benchmark Suite

Accurate CPU cost measurement for Bitcoin SV Script operations.

## Overview

This benchmark suite measures the actual CPU cost of BSV Script opcodes by:
- Direct integration with BSV node's script interpreter
- Hardware performance counter monitoring (cycles, cache misses, IPC)
- Statistical analysis with p90/p99 percentiles
- Support for BSV's unbounded script capabilities (multi-MB operations)

## Build Requirements

- C++17 compatible compiler (GCC 9+, Clang 10+)
- CMake 3.16+
- OpenSSL development libraries
- Linux (for perf_event_open performance counters)
- Root/sudo access recommended (for CPU pinning and frequency scaling)

### Install Dependencies (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install build-essential cmake libssl-dev
```

## Building

```bash
cd bsv_script_bench
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Running Benchmarks

### Individual Benchmarks

```bash
# Pin to CPU core 0 for stable measurements
sudo ./bench_stack_ops
sudo ./bench_byte_ops
sudo ./bench_hash_ops
```

### All Benchmarks

```bash
sudo make run_all_benchmarks
```

## System Preparation (Recommended)

For most accurate measurements:

```bash
# Disable CPU frequency scaling
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Disable turbo boost (Intel)
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

# Disable turbo boost (AMD)
echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost

# Isolate CPU core 0 for benchmarking
# Add to kernel boot parameters: isolcpus=0
```

## Output

Results are saved to `output/` directory:
- `bench_*.csv`: Detailed measurements in CSV format
- `bench_*.json`: JSON format for model fitting

### CSV Schema

```
opcode,param_desc,input_bytes,median_cycles,p90_cycles,p99_cycles,
median_ns,instructions,ipc,l1d_misses,llc_misses,branch_misses,
malloc_count,alloc_bytes
```

## Benchmark Coverage

### Currently Implemented

- **Stack Operations** (`bench_stack_ops`)
  - OP_DUP, OP_SWAP, OP_PICK, OP_ROLL, OP_ROT
  - Tests: Stack depths 1-10k items, item sizes 1B-1MB

- **Byte Operations** (`bench_byte_ops`) - **Critical for BSV**
  - OP_CAT: Tests up to 10MB + 10MB concatenations
  - OP_SPLIT: Various split positions on multi-MB buffers
  - OP_NUM2BIN / OP_BIN2NUM: Conversion operations
  - CAT chains: Measure reallocation overhead

- **Hash Operations** (`bench_hash_ops`)
  - OP_SHA1, OP_SHA256, OP_HASH160, OP_HASH256, OP_RIPEMD160
  - Tests: 1B to 100MB inputs
  - Linear model fitting: cost(n) = c₀ + c₁·n

### TODO - Requires BSV Integration

- **Signature Operations** (`bench_sig_ops`)
  - OP_CHECKSIG with varying transaction sizes
  - OP_CHECKMULTISIG m-of-n combinations
  - SIGHASH preimage cost analysis
  
- **Control Flow** (`bench_control_flow`)
  - OP_IF / OP_ELSE branch prediction
  
- **Arithmetic** (`bench_arithmetic`)
  - OP_ADD, OP_MUL, bitwise operations

## Integration with BSV Node

To benchmark actual BSV Script execution:

1. Clone BSV node source:
```bash
cd deps/
git clone https://github.com/bitcoin-sv/bitcoin-sv.git
cd bitcoin-sv
# Build BSV node libraries
```

2. Update `CMakeLists.txt` to link against BSV:
```cmake
set(BSV_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/bitcoin-sv/src")
include_directories(${BSV_SRC_DIR})
```

3. Replace stub implementations with actual BSV Script interpreter calls

## Performance Notes

- **rdtsc precision**: Cycle-accurate timing using CPU timestamp counter
- **CPU pinning**: Prevents context switches and migration
- **Cache warming**: 100 warmup iterations before measurement
- **Statistics**: Median, p90, p99 for robust outlier handling
- **Large data**: Reduced iteration count for >1MB operations

## Cost Model Fitting

After running benchmarks, use `output/*.csv` files to fit cost models:

```python
# Example: Fit linear model for OP_SHA256
import pandas as pd
import numpy as np

df = pd.read_csv('output/bench_hash_ops.csv')
sha256 = df[df['opcode'] == 'OP_SHA256']

# Linear regression: cycles = c0 + c1 * bytes
X = sha256['input_bytes'].values
y = sha256['median_cycles'].values
c1 = np.cov(X, y)[0, 1] / np.var(X)
c0 = np.mean(y) - c1 * np.mean(X)

print(f"OP_SHA256 model: cost(n) = {c0} + {c1} * n")
```

## Troubleshooting

**Permission denied for perf counters:**
```bash
# Temporarily allow perf for all users
echo -1 | sudo tee /proc/sys/kernel/perf_event_paranoid
```

**Benchmarks show high variance:**
- Disable frequency scaling
- Pin to isolated CPU core
- Close background applications
- Run from terminal (not SSH) to avoid network interrupts

**Missing OpenSSL:**
```bash
sudo apt-get install libssl-dev
```

## Next Steps

1. Integrate with actual BSV Script interpreter
2. Implement signature verification benchmarks
3. Run on multiple hardware profiles (AMD Zen, Intel, ARM)
4. Generate cost_model.json for each hardware class

## License

MIT (adjust as needed for your project)
