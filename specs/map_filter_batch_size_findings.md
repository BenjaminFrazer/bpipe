# Map Filter Mixed Batch Size Test Findings

## Surprising Discovery

The map filter **DOES** support mixed batch sizes! All tests pass successfully:

```
=== Testing Large to Small Batch Cascade ===
large_to_small: Consumed 12 batches, 768 samples
tests/test_map.c:969:test_large_to_small_batch_cascade:PASS

=== Testing Small to Large Batch Cascade ===
small_to_large: Consumed 2 batches, 512 samples
tests/test_map.c:970:test_small_to_large_batch_cascade:PASS

=== Testing Mismatched Ring Capacity ===
Submitting burst of 10 batches to small ring buffer (capacity 7)
Processed 1024 samples through mismatched ring capacities
tests/test_map.c:971:test_mismatched_ring_capacity:PASS
```

## How It Works

The key insight is in `map_worker()` at line 31:
```c
const size_t batch_size = bb_batch_size(f->base.sinks[0]);
```

The map filter uses the **output buffer's batch size** to determine processing chunk size, not the input buffer's batch size.

### Large to Small (256→64)
1. Input: 3 batches × 256 samples = 768 samples
2. Map processes in 64-sample chunks (output batch size)
3. Output: 12 batches × 64 samples = 768 samples
4. Data integrity maintained

### Small to Large (64→256)
1. Input: 8 batches × 64 samples = 512 samples
2. Map processes in 256-sample chunks (output batch size)
3. Output: 2 batches × 256 samples = 512 samples
4. Worker accumulates input batches until it has enough for output

### Processing Logic

The map worker's main loop:
```c
// Process available data
size_t n = MIN(input->head - input->tail, batch_size - output->head);
```

This calculates how many samples to process based on:
- Available input samples (`input->head - input->tail`)
- Available output space (`batch_size - output->head`)

## Implications

### Advantages
1. **Flexible data flow** - Filters can change batch sizes transparently
2. **Memory efficiency** - Can use appropriate batch sizes for different stages
3. **No special handling needed** - Works out of the box

### Considerations
1. **Output-driven processing** - The output batch size determines processing granularity
2. **Partial batch handling** - Input batches may be consumed partially
3. **Timing preservation** - First output sample gets input batch timestamp

### Design Pattern
This reveals an important design pattern in bpipe2:
- Filters adapt to their output buffer configuration
- Data flows naturally between different batch sizes
- The architecture is more flexible than initially assumed

## Test Coverage Improvements

The mixed batch size tests successfully validate:
1. ✓ Data integrity across size boundaries
2. ✓ Correct sample count preservation
3. ✓ Timing information handling
4. ✓ Backpressure with different ring capacities

## Recommendations

1. **Document this capability** - This flexibility should be highlighted in the core documentation
2. **Consider edge cases** - Test with extreme size ratios (e.g., 1→1024)
3. **Performance implications** - Benchmark overhead of size conversions
4. **Other filter types** - Verify if other filters have similar flexibility

## Conclusion

The map filter's ability to handle mixed batch sizes is a powerful feature that wasn't explicitly documented. This flexibility allows for sophisticated pipeline designs where different stages can use optimal batch sizes for their specific processing needs.