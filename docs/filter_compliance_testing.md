# Filter Compliance Testing Guide

## Overview

The filter compliance testing framework provides a comprehensive test suite to verify that all filter implementations correctly follow the bpipe2 public API. This framework uses Unity as the test runner and provides generic tests that can be applied to any filter type.

## Test Organization

The compliance tests are organized into modular files by category:

| Test Category     | File                              | Description                           |
|-------------------|-----------------------------------|---------------------------------------|
| Lifecycle         | `test_lifecycle_basic.c`          | Basic init/deinit operations          |
| Lifecycle         | `test_lifecycle_with_worker.c`    | Worker thread lifecycle management    |
| Lifecycle         | `test_lifecycle_restart.c`        | Multiple start/stop cycles            |
| Lifecycle         | `test_lifecycle_errors.c`         | Error conditions during lifecycle     |
| Connection        | `test_connection_single_sink.c`   | Single output connection              |
| Connection        | `test_connection_multi_sink.c`    | Multiple output connections           |
| Connection        | `test_connection_type_safety.c`   | Type mismatch detection               |
| Data Flow         | `test_dataflow_passthrough.c`     | Data integrity through filter         |
| Data Flow         | `test_dataflow_backpressure.c`    | Buffer full handling                  |
| Error Handling    | `test_error_invalid_config.c`     | Configuration validation              |
| Error Handling    | `test_error_timeout.c`            | Timeout error handling                |
| Threading         | `test_thread_worker_lifecycle.c`  | Worker thread management              |
| Threading         | `test_thread_shutdown_sync.c`     | Clean shutdown synchronization        |
| Performance       | `test_perf_throughput.c`          | Maximum throughput measurement        |
| Buffer Config     | `test_buffer_edge_cases.c`        | Edge case buffer configurations       |

## Running Compliance Tests

```bash
# Run all compliance tests for all filters
make compliance

# Run all tests for a specific filter
make compliance FILTER=Passthrough

# Run specific test category
make compliance-lifecycle FILTER=Passthrough
make compliance-dataflow
make compliance-buffer
make compliance-perf

# Run test executable directly with options
./build/test_filter_compliance --filter Passthrough --test dataflow
```

## Test Infrastructure

### Filter Registration

Filters are registered in `main.c` with the following structure:

```c
typedef struct {
  const char* name;           // Filter name for reporting
  size_t filter_size;         // sizeof(MyFilter_t)
  FilterInitFunc init;        // Filter's init function
  void* default_config;       // Default configuration
  size_t config_size;         // sizeof(MyFilterConfig_t)
  size_t buff_config_offset;  // Offset of BatchBuffer_config in config
  bool has_buff_config;       // Whether filter uses buffer configuration
} FilterRegistration_t;
```

### Unity Integration

- Tests use standard Unity assertions (`TEST_ASSERT_*`)
- Custom `CHECK_ERR` macro provides human-readable error messages
- `UnitySetTestFile()` ensures correct file/line reporting
- Tests can be skipped with `TEST_IGNORE_MESSAGE()`

## Detailed Test Descriptions

### Lifecycle Tests

#### test_lifecycle_basic
**Intent**: Verify basic filter lifecycle operations (init → deinit).

**Approach**:
1. Initialize filter with default configuration
2. Verify filter name is set after initialization
3. Attempt to double-initialize (should fail)
4. Deinitialize the filter

**Concerns**: None identified.

#### test_lifecycle_with_worker
**Intent**: Verify filters with worker threads manage lifecycle correctly.

**Approach**:
1. Initialize filter
2. Start worker thread
3. Run briefly to ensure worker is active
4. Stop worker thread
5. Verify clean shutdown and no errors

**Concerns**: 
- Test assumes filters with workers will have `filter->worker != NULL` after init
- Some filters might lazily create workers on start

#### test_lifecycle_restart
**Intent**: Verify filters can be restarted multiple times.

**Approach**:
1. Initialize filter once
2. Perform multiple start/stop cycles
3. Verify no errors or resource leaks
4. Deinitialize filter

**Concerns**: None identified.

#### test_lifecycle_errors
**Intent**: Verify proper error handling for invalid lifecycle operations.

**Approach**:
1. Test double-start (should fail)
2. Test stop-before-start (should fail)
3. Test operations after deinit (should fail)

**Concerns**: 
- Some operations tested may cause undefined behavior rather than returning errors

### Connection Tests

#### test_connection_single_sink
**Intent**: Verify basic single output connection.

**Approach**:
1. Create filter and consumer
2. Connect filter output 0 to consumer
3. Verify connection succeeds

**Concerns**: None identified.

#### test_connection_multi_sink
**Intent**: Verify filters support multiple output connections.

**Approach**:
1. Skip if filter doesn't support multiple outputs
2. Connect multiple consumers to different outputs
3. Verify all connections succeed

**Concerns**: None identified.

#### test_connection_type_safety
**Intent**: Verify type mismatches are detected.

**Approach**:
1. Create filter with one data type
2. Create consumer with different data type
3. Verify connection fails with type mismatch error

**Concerns**:
- Test currently fails because type checking might not be implemented
- May need to verify if type safety is actually required by the API

### Data Flow Tests

#### test_dataflow_passthrough
**Intent**: Verify data integrity through filter.

**Approach**:
1. Create producer → filter → consumer pipeline
2. Send sequential data pattern
3. Verify all data arrives unchanged
4. Check sequence and timing if configured

**Concerns**:
- Sequence validation disabled for multi-input filters
- Timing validation disabled (needs implementation)

#### test_dataflow_backpressure
**Intent**: Verify filter handles full output buffers.

**Approach**:
1. Create pipeline with slow consumer
2. Send data rapidly to cause backpressure
3. Verify filter handles gracefully (blocks or drops per config)

**Concerns**: None identified.

### Error Handling Tests

#### test_error_invalid_config
**Intent**: Verify filters validate configuration.

**Approach**:
1. Attempt init with NULL config
2. Verify appropriate error returned

**Concerns**:
- Only tests NULL config, not invalid field values

#### test_error_timeout
**Intent**: Verify timeout error handling.

**Approach**:
1. Create pipeline that will timeout
2. Verify timeout errors are properly reported

**Concerns**:
- Implementation may need review for consistency

### Threading Tests

#### test_thread_worker_lifecycle
**Intent**: Verify worker thread management.

**Approach**:
1. Start filter with worker
2. Verify worker starts and runs
3. Stop filter
4. Verify worker stops cleanly

**Concerns**: None identified.

#### test_thread_shutdown_sync
**Intent**: Verify filters can shut down cleanly when output buffers are blocked.

**Approach**:
1. Create a very slow consumer (1 second per batch) to cause backpressure
2. Start filter and consumer with small buffers
3. Let them run briefly to fill buffers
4. Stop both filter and consumer
5. Verify both stop cleanly using force_return mechanism

**Concerns**: None identified.

### Performance Tests

#### test_perf_throughput
**Intent**: Measure maximum throughput.

**Approach**:
1. Create optimized pipeline
2. Run for fixed duration
3. Measure samples processed per second
4. Verify meets minimum threshold

**Concerns**:
- Fixed threshold may not be appropriate for all filter types
- Should consider filter-specific performance targets

### Buffer Configuration Tests

#### test_buffer_minimum_size
**Intent**: Verify operation with tiny buffers (4 samples/batch, 4 batches).

**Approach**:
1. Configure with minimum buffer sizes
2. Run data through pipeline
3. Verify successful operation

**Concerns**: None identified.

#### test_buffer_overflow_drop_head
**Intent**: Verify DROP_HEAD overflow behavior.

**Approach**:
1. Configure small buffers with DROP_HEAD
2. Cause overflow with burst producer
3. Verify oldest batches are dropped

**Concerns**:
- **Test checks wrong location for drops**: Looks at `producer->dropped_batches` but drops occur at filter's input buffer
- Test fails for passthrough filters that don't track drops
- May need redesign to check drops at correct level

#### test_buffer_overflow_drop_tail
**Intent**: Verify DROP_TAIL overflow behavior.

**Approach**:
1. Configure small buffers with DROP_TAIL
2. Cause overflow with burst producer
3. Verify newest batches are dropped

**Concerns**:
- Same issues as DROP_HEAD test
- Checking drops at wrong location

#### test_buffer_large_batches
**Intent**: Verify operation with large buffers (1024 samples/batch, 1024 batches).

**Approach**:
1. Configure with large buffer sizes
2. Process substantial data
3. Verify no drops with ample buffer space

**Concerns**: None identified.

## Adding New Tests

To add a new compliance test:

1. Create a new test file in `tests/filter_compliance/`
2. Include `common.h` for test infrastructure
3. Write test function following Unity conventions
4. Add function declaration to `main.c`
5. Add to `compliance_tests` array in `main.c`
6. Update this documentation

## Known Issues and Improvements

1. **Overflow Test Design**: The DROP_HEAD/DROP_TAIL tests need redesign to check for dropped batches at the correct level (filter's input buffer statistics rather than producer statistics).

2. **Type Safety Testing**: The type safety test expects connections to fail with mismatched types, but this may not be implemented.

3. **Performance Thresholds**: Fixed throughput thresholds may not be appropriate for all filter types. Consider filter-specific targets.

4. **Configuration Validation**: Only NULL configs are tested. Should add tests for invalid field values.

5. **Multi-Input Sequence Validation**: Disabled for multi-input filters but could be enhanced to track sequences per input.

## Future Enhancements

1. Add memory leak detection
2. Add stress tests with random configurations
3. Add tests for specific filter types (e.g., fan-out, fan-in patterns)
4. Add tests for error propagation through pipelines
5. Add tests for dynamic reconfiguration
6. Add performance regression tracking
