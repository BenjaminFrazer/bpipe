# Map Filter Test Suite Documentation

## Overview

The map filter test suite (`tests/test_map.c`) provides comprehensive coverage of the map filter implementation, testing initialization, data transformation, multi-threading, and error handling capabilities. The tests use the Unity test framework and are designed to verify both functional correctness and edge case handling.

## Test Strategy

### Buffer Configurations

The test suite uses two primary buffer configurations:

1. **Standard Configuration** (256 samples/batch, 63 batch ring)
   - Used for functional and performance testing
   - Provides realistic data volumes for stress testing

2. **Small Configuration** (16 samples/batch, 7 batch ring)
   - Used for edge case testing
   - Forces buffer wraparound and backpressure scenarios

### Timeout Handling

A critical implementation detail: timeout value of 0 means infinite wait in this API. Tests use:
- 1μs for non-blocking checks
- 10ms for standard operations
- Explicit timeout values to avoid deadlocks

## Test Categories and Intent

### 1. Configuration Tests

**test_map_init_valid_config**
- Validates successful initialization with valid parameters
- Verifies filter name, type, and function pointer storage
- Ensures proper cleanup with `filt_deinit()`

**test_map_init_null_filter**
- Tests error handling when NULL filter pointer is provided
- Expects `Bp_EC_INVALID_CONFIG` return

**test_map_init_null_function**
- Tests error handling when NULL map function is provided
- Expects `Bp_EC_INVALID_CONFIG` return

### 2. Functional Transform Tests

**test_single_threaded_linear_ramp**
- Tests identity passthrough with incrementing values (0, 1, 2, ...)
- Processes 2× ring capacity to test sustained operation
- Verifies:
  - Data integrity through the pipeline
  - Timestamp and period preservation
  - Proper buffer consumption to avoid overflow

**test_scale_transform**
- Tests mathematical transformation (×2.0)
- Single batch verification of transform correctness
- Simple focused test for transform logic

**test_chained_transforms**
- Tests cascade: scale (×2) → offset (+100)
- Verifies data flows correctly through multiple filters
- Tests filter interconnection and compound transformations

**test_buffer_wraparound**
- Uses small buffers to force ring buffer wraparound
- Submits (ring_size + 2) batches to test edge cases
- Verifies data order preservation across wraparound
- Tests backpressure handling

### 3. Multi-threaded Tests

**test_multi_stage_single_threaded**
- Two-stage cascade with identity filters
- Single thread produces and consumes data
- Tests filter chaining without threading complexity
- Processes 2× ring capacity for thorough testing

**test_multi_threaded_slow_consumer**
- True multi-threaded test with separate producer thread
- Producer generates data at 2ms/batch
- Consumer processes at 5ms/batch (testing backpressure)
- Uses cascaded transforms (scale → offset) with small buffers
- Verifies:
  - Thread synchronization
  - Data integrity through transforms
  - Proper cleanup and error propagation

### 4. Error Handling

**test_map_error_handling**
- Tests filter behavior when map function returns error
- Verifies:
  - Worker thread stops on error
  - `atomic_load(&filter.base.running)` becomes false
  - Error code is properly stored in `worker_err_info.ec`

## Test Implementation Patterns

### Producer-Consumer Pattern
```c
// Separate producer thread generates data
producer_thread() {
  for each batch:
    - Get buffer head
    - Fill with incrementing data
    - Submit batch
    - Sleep 2ms (simulate data source)
}

// Main thread consumes and verifies
while (batches_consumed < expected):
  - Get output batch
  - Verify transformed data
  - Delete batch
  - Sleep 5ms (simulate slow processing)
```

### Buffer Management
- Tests actively consume output to prevent deadlock
- Non-blocking checks (1μs timeout) used in loops
- Explicit nanosleep() calls to allow worker threads to process

### Data Verification
- Incrementing patterns for easy verification
- Known transformations (scale, offset) for predictable results
- Batch-specific patterns for wraparound testing

## Coverage Analysis

### Strengths
1. **Comprehensive initialization testing** - All error cases covered
2. **Multiple transform types** - Identity, scale, offset, error
3. **Edge case coverage** - Buffer wraparound, backpressure
4. **True multi-threading** - Separate producer/consumer threads
5. **Error propagation** - Worker thread error handling

### Potential Gaps
1. **Performance metrics** - No explicit throughput/latency measurements
2. **Resource limits** - No testing of memory constraints
3. **Dynamic reconfiguration** - No runtime parameter changes
4. **Multiple data types** - Only DTYPE_FLOAT tested
5. **Concurrent producers** - Only single producer tested

### Non-Orthogonality Issues
1. **test_single_threaded_linear_ramp** and **test_multi_stage_single_threaded** both test identity passthrough with similar patterns
   - Difference: single vs cascaded filters
   - Could be merged with a parameter

2. **Buffer consumption pattern** repeated in multiple tests
   - Could be extracted to a helper function

3. **Setup/teardown code** duplicated across tests
   - Unity's setUp/tearDown underutilized

## Recommendations

1. **Add performance benchmarks** - Measure throughput under various conditions
2. **Test other data types** - Extend beyond DTYPE_FLOAT
3. **Add stress tests** - Multiple concurrent producers/consumers
4. **Extract common patterns** - Reduce code duplication
5. **Add negative buffer size tests** - Test system limits
6. **Test filter lifecycle** - Start/stop/restart sequences

## Usage

Run all tests:
```bash
make test-map
```

Run with timeout protection:
```bash
timeout 30 ./build/test_map
```

The test suite provides a solid foundation for validating map filter functionality while demonstrating proper patterns for filter testing in the bpipe2 framework.