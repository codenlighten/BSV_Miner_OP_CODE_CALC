# BSV Script CPU Cost Benchmark Results

Generated: 2025-11-10

## System Information
- CPU: (detected from benchmarks)
- Compiler: GCC 11.4.0
- Build Flags: -O3 -march=native
- Note: Performance counters unavailable (running in container/VM), using rdtsc only

## Key Findings

### Stack Operations (OP_DUP, OP_SWAP, etc.)
- **OP_DUP**: ~130 cycles baseline, increases with item size
- **OP_SWAP**: Constant ~86 cycles regardless of stack depth or item size
- **OP_PICK**: Constant ~136 cycles (efficient even at 10k stack depth)
- **OP_ROLL**: Linear in stack size (O(n)), expensive for deep stacks

### Byte Operations (Critical for BSV)

#### OP_CAT Performance
- Small (20B total): ~122 cycles (~6 cycles/byte)
- Medium (20kB total): ~456 cycles (~0.02 cycles/byte)
- Large (2MB total): ~238k cycles (~0.12 cycles/byte)
- Very Large (20MB total): ~3.9M cycles (~0.20 cycles/byte)
- **Linear model**: cost ≈ c₀ + c₁·n where c₁ ≈ 0.12-0.20 cycles/byte

#### OP_SPLIT Performance
- Consistent across split positions (start/middle/end)
- Linear in buffer size: ~0.20-0.40 cycles/byte for MB-sized buffers
- 10MB split: ~4M cycles

### Hash Operations (Excellent Linear Fit)

All hash operations show excellent linear models (R² > 0.9999):

| Operation | c₀ (base) | c₁ (cycles/byte) | Use Case |
|-----------|-----------|------------------|----------|
| OP_SHA1 | -31k | 1.24 | Legacy hash |
| OP_SHA256 | -12k | 1.35 | Bitcoin addresses |
| OP_HASH160 | -19k | 1.37 | Pay-to-pubkey-hash |
| OP_HASH256 | -4.5k | 1.38 | Double SHA256 |
| OP_RIPEMD160 | 84k | 7.60 | Slowest hash |

**Note**: Negative c₀ values are artifacts of linear regression on small samples. Use measured median cycles for small inputs.

## Cost Model Recommendations

### For Estimator Implementation

```json
{
  "OP_DUP": {"model": "constant", "c0": 130},
  "OP_SWAP": {"model": "constant", "c0": 86},
  "OP_CAT": {"model": "linear", "c0": 150, "c1": 0.15},
  "OP_SPLIT": {"model": "linear", "c0": 200, "c1": 0.35},
  "OP_SHA256": {"model": "linear", "c0": 1000, "c1": 1.35},
  "OP_HASH256": {"model": "linear", "c0": 1800, "c1": 1.38},
  "OP_RIPEMD160": {"model": "linear", "c0": 1000, "c1": 7.60}
}
```

Use p95 values from CSV for conservative estimates.

## BSV-Specific Insights

1. **OP_CAT scales well**: 20MB concatenation completes in ~4M cycles (~1ms on 4GHz CPU)
2. **Memory allocation is linear**: No quadratic blowup observed
3. **Hash operations dominate**: A 100MB OP_SHA256 costs ~135M cycles (~34ms)
4. **Stack operations are cheap**: Even with 10k items, operations remain constant time

## Next Steps

1. ✅ Benchmark suite operational
2. ⏳ Integrate with actual BSV Script interpreter
3. ⏳ Add signature verification benchmarks (OP_CHECKSIG)
4. ⏳ Test on multiple hardware profiles (AMD Zen, Intel, ARM)
5. ⏳ Build static cost estimator using these models

## Data Files

All raw measurements available in:
- `output/bench_stack_ops.csv` - 48 measurements
- `output/bench_byte_ops.csv` - 45 measurements  
- `output/bench_hash_ops.csv` - 40 measurements
