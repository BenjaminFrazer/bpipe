# Tee Filter Batch Size Options: Performance Analysis

## Option A: All Outputs Same Batch Size (Can Differ from Input)

### Performance Characteristics

**Memory Usage:**
- Requires accumulation buffer: `max(output_batch_size) * sizeof(sample) * n_outputs`
- Additional state tracking: ~32 bytes
- Memory allocation per output batch operation

**CPU Usage:**
- Extra memcpy operations for accumulation/distribution
- Batch boundary calculations on every operation
- Timestamp interpolation for split batches

**Latency:**
- Increased latency when accumulating (output > input size)
- Must wait for multiple input batches before producing output
- Example: 64-sample input, 256-sample output = 4x input latency

**Throughput Impact:**
```
Relative throughput = 1 / (1 + overhead_ratio)
where overhead_ratio = (accumulation_copies + distribution_copies) / base_copies

For 3 outputs with size conversion:
- Base: 3 copies (one per output)
- With conversion: 3 + 2 (accumulate + redistribute) = 5 copies
- Throughput = 1 / (1 + 2/3) = 60% of base
```

### Code Complexity
- ~200 additional lines for batch management
- Edge cases: partial batches, timestamp handling, buffer wraparound
- Harder to verify correctness

## Option B: All I/O Same Batch Size

### Performance Characteristics

**Memory Usage:**
- No additional buffers needed
- Minimal state: just the base filter structure
- Zero allocation overhead

**CPU Usage:**
- Single memcpy per output
- No calculations beyond basic address arithmetic
- Direct timestamp passthrough

**Latency:**
- Zero additional latency
- Output available immediately after input processed
- Predictable timing behavior

**Throughput Impact:**
```
Throughput = base throughput (100%)
No overhead beyond the fundamental copying
```

### Code Complexity
- Current implementation (~120 lines total)
- Easy to verify and test
- No edge cases

## Option C: Fully Flexible (Each Output Can Have Different Size)

### Performance Characteristics

**Memory Usage:**
- Accumulation buffer per output: `n_outputs * max(output_batch_size) * sizeof(sample)`
- Complex state tracking per output
- Significant memory overhead

**CPU Usage:**
- Independent conversion logic per output
- Multiple accumulation paths
- Complex timestamp management

**Latency:**
- Variable per output
- Difficult to predict
- Can cause priority inversion (fast outputs wait for slow ones)

**Throughput Impact:**
```
Worst case with 3 outputs of different sizes:
- 3 independent accumulation operations
- 3 independent distribution operations  
- Throughput < 40% of base
```

## Benchmarks (Estimated for 1M samples/sec float32 stream)

| Metric | Option B (Same Size) | Option A (Same Output) | Option C (Flexible) |
|--------|---------------------|----------------------|-------------------|
| Memory Overhead | 0 KB | ~256 KB | ~768 KB |
| CPU Overhead | 0% | 40-60% | 100-150% |
| Added Latency | 0 ms | 0-50 ms | 0-100 ms |
| Cache Misses | Baseline | +20% | +50% |
| Code Size | ~1 KB | ~3 KB | ~5 KB |

## Recommendation

**Option B (Matched Batch Sizes)** is strongly recommended because:

1. **Zero Performance Penalty** - No overhead beyond fundamental copying
2. **Predictable Behavior** - Latency and throughput are deterministic
3. **Cache Friendly** - Sequential memory access patterns
4. **Composable** - Use separate Reframe filter for size conversion

### Example Pipeline for Size Conversion
```
Input(128) -> Tee -> Output1(128)
                  -> Reframe(128->256) -> Output2(256)
                  -> Reframe(128->64) -> Output3(64)
```

This approach:
- Keeps each component simple and fast
- Makes performance costs visible and controllable
- Allows optimization of each component independently
- Enables parallel processing of different paths