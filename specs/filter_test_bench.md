# Filter Test Bench Specification

## Overview

The Filter Test Bench is a Unity-based testing framework designed to validate all aspects of the bpipe2 filter public API. It provides a generic compliance test suite that can be applied to any filter implementation, reducing code repetition and ensuring consistent API behavior.

## Design Principles

1. **Unity-First Architecture** - Leverages Unity test framework for assertions and test running
2. **Generic Compliance Testing** - Single test suite applicable to all filter types
3. **Automatic Error Context** - Unity captures file, line, function on assertion failures
4. **Performance Metrics** - Additional layer for KPIs like throughput and latency
5. **Simple Test Writing** - Standard Unity TEST_ASSERT macros with optional enhancements

## Test Framework Architecture

### Unity Integration

The test bench uses Unity as its core assertion and test execution framework. Each compliance test is implemented as a standard Unity test function, with additional infrastructure for filter registration and performance metrics.

### Core Types

```c
// Filter init function type
typedef Bp_EC (*FilterInitFunc)(void* filter, void* config);

// Filter registration for test harness
typedef struct {
    const char* name;           // Filter name for reporting
    size_t filter_size;         // sizeof(MyFilter_t)
    FilterInitFunc init;        // Filter's init function
    void* default_config;       // Default configuration
    size_t config_size;         // sizeof(MyFilterConfig_t)
    // Buffer configuration metadata
    size_t buff_config_offset;  // Offset of BatchBuffer_config in filter's config struct
    bool has_buff_config;       // Whether this filter uses buffer configuration
} FilterRegistration_t;

// Performance metrics (collected separately from Unity)
typedef struct {
    double throughput_samples_per_sec;
    double latency_ns_p50;
    double latency_ns_p99;
    double cpu_usage_percent;
    size_t memory_bytes_peak;
    size_t batches_processed;
} PerfMetrics_t;

// Global state for current test
static FilterRegistration_t* g_filters = NULL;
static size_t g_n_filters = 0;
static size_t g_current_filter = 0;

// Current filter under test
static Filter_t* g_fut = NULL;
static void* g_fut_config = NULL;
static FilterInitFunc g_fut_init = NULL;
static const char* g_filter_name = NULL;

// Performance metrics storage
static PerfMetrics_t g_last_perf_metrics;
static char g_perf_report[8192];

// Test timing
static uint64_t g_test_start_ns = 0;
```

### Unity setUp/tearDown

```c
// Unity setUp - called before each test
void setUp(void) {
    // Create fresh filter instance for each test
    FilterRegistration_t* reg = &g_filters[g_current_filter];
    
    g_fut = (Filter_t*)calloc(1, reg->filter_size);
    g_fut_config = malloc(reg->config_size);
    memcpy(g_fut_config, reg->default_config, reg->config_size);
    g_fut_init = reg->init;
    g_filter_name = reg->name;
    
    // Capture test start time
    g_test_start_ns = get_time_ns();
}

// Unity tearDown - called after each test
void tearDown(void) {
    // Cleanup after each test
    if (g_fut && g_fut->ops.deinit) {
        g_fut->ops.deinit(g_fut);
    }
    free(g_fut);
    free(g_fut_config);
    g_fut = NULL;
    g_fut_config = NULL;
}
```

### Optional Unity Customization

```c
// Optional: Custom Unity failure handler for enhanced debugging
void UnityTestResultsFailBegin(const UNITY_LINE_TYPE line) {
    uint64_t elapsed_ns = get_time_ns() - g_test_start_ns;
    
    // Print enhanced context
    printf("\n[Test: %s, Filter: %s, Elapsed: %.3f ms]\n",
           Unity.CurrentTestName,
           g_filter_name,
           elapsed_ns / 1000000.0);
    
    // Automatic debugger break if attached
    #ifdef DEBUG
    if (is_debugger_attached()) {
        printf("⚠️  Debugger attached - breaking at assertion\n");
        __builtin_trap();
    }
    #endif
}
```

## Mock Filters

Mock filters provide controlled test infrastructure:

### Controllable Producer Filter

### Controllable Consumer Filter

### Passthrough Filter with Metrics

## Test Utilities

### Data Integrity Verification
```c
// Verify sequential data pattern
bool verify_sequential_data(const Batch_t* batch, size_t* expected_value);

// Compute and verify checksum
bool verify_checksum(const Batch_t* batch);

// Validate timing accuracy
bool verify_timing(const Batch_t* batch, uint64_t expected_t_ns, 
                  uint64_t tolerance_ns);
```

### Performance Measurement
```c
// Measure end-to-end latency
double measure_latency_ns(Filter_t* filter);

// Measure throughput
double measure_throughput_samples_per_sec(Filter_t* filter, double duration_sec);

// Get memory usage
size_t get_current_memory_bytes(void);
size_t get_peak_memory_bytes(void);

// Get CPU usage
double get_cpu_usage_percent(void);
```

### Test Pipeline Helpers
```c
// Create standard test pipelines
Bp_EC create_linear_pipeline(Filter_t* fut, MockProducer_t* prod, 
                           MockConsumer_t* cons);
Bp_EC create_fan_out_pipeline(Filter_t* fut, MockProducer_t* prod,
                            MockConsumer_t* cons[], size_t n_consumers);
Bp_EC create_fan_in_pipeline(Filter_t* fut, MockProducer_t* prod[],
                           size_t n_producers, MockConsumer_t* cons);
```

### Buffer Configuration Testing

The test framework supports testing filters with different buffer configurations to validate edge cases, performance characteristics, and backpressure behavior.

```c
// Predefined buffer profiles for different test scenarios
typedef enum {
    BUFF_PROFILE_DEFAULT,      // Standard config (6/8)
    BUFF_PROFILE_TINY,         // Minimum sizes (2/2) - edge case testing
    BUFF_PROFILE_SMALL,        // Small buffers (4/3) - backpressure testing
    BUFF_PROFILE_LARGE,        // Large buffers (10/10) - performance testing
    BUFF_PROFILE_BACKPRESSURE, // Normal batch, tiny ring (6/2)
    BUFF_PROFILE_PERF,         // Optimized for throughput (10/8)
} BufferProfile_t;

// Apply buffer profile to filter configuration
void apply_buffer_profile(void* filter_config, size_t buff_config_offset, 
                         BufferProfile_t profile);
```

Tests can apply specific buffer profiles based on their testing needs:
- **Backpressure tests**: Use `BUFF_PROFILE_BACKPRESSURE` to force buffer full conditions
- **Performance tests**: Use `BUFF_PROFILE_PERF` for maximum throughput testing
- **Edge case tests**: Use `BUFF_PROFILE_TINY` to test minimum buffer handling
- **Overflow tests**: Use small buffers with different overflow modes

## Test Categories

### 1. Lifecycle Compliance Tests

Tests that validate filter lifecycle management:
- `test_lifecycle_basic` - Init → Start → Stop → Deinit
- `test_lifecycle_restart` - Multiple start/stop cycles
- `test_lifecycle_errors` - Invalid state transitions
- `test_lifecycle_cleanup` - Resource cleanup verification

### 2. Connection Compliance Tests

Tests that validate filter connection patterns:
- `test_connection_single_sink` - Basic connection
- `test_connection_multi_sink` - Fan-out connections
- `test_connection_multi_input` - Fan-in connections
- `test_connection_type_safety` - Type mismatch detection
- `test_connection_limits` - Max connections validation

### 3. Data Flow Compliance Tests

Tests that validate data flow patterns:
- `test_dataflow_passthrough` - Data integrity
- `test_dataflow_backpressure` - Buffer full handling
- `test_dataflow_starvation` - Empty buffer handling
- `test_dataflow_timing` - Timestamp preservation
- `test_dataflow_ordering` - Sample order preservation

### 4. Error Handling Compliance Tests

Tests that validate error handling:
- `test_error_worker_assert` - Worker thread assertions
- `test_error_propagation` - Error flow downstream
- `test_error_timeout` - Timeout handling
- `test_error_invalid_config` - Configuration validation
- `test_error_recovery` - Error recovery behavior

### 5. Threading Compliance Tests

Tests that validate thread safety:
- `test_thread_worker_lifecycle` - Worker thread management
- `test_thread_concurrent_ops` - Concurrent operations
- `test_thread_shutdown_sync` - Clean shutdown
- `test_thread_force_return` - Force return mechanism

### 6. Performance Compliance Tests

Tests that measure performance:
- `test_perf_throughput` - Maximum throughput
- `test_perf_latency` - End-to-end latency
- `test_perf_cpu_usage` - CPU efficiency
- `test_perf_memory_usage` - Memory efficiency
- `test_perf_scaling` - Performance vs load

### 7. Buffer Configuration Tests

Tests that validate filter behavior with different buffer configurations:
- `test_buffer_minimum_size` - Verify filters work with tiny buffers (2/2)
- `test_buffer_overflow_modes` - Test DROP_HEAD/DROP_TAIL with small buffers
- `test_buffer_large_batches` - Verify handling of maximum batch sizes
- `test_buffer_backpressure_scenarios` - Systematic backpressure testing
- `test_buffer_performance_impact` - Measure throughput vs buffer size

## Test Registration and Execution

### Unity Test Runner

## Benefits of Unity-First Architecture

1. **Simplicity**: Developers use familiar Unity TEST_ASSERT macros
2. **Proven Framework**: Unity is mature, well-documented, and widely used
3. **Zero Learning Curve**: Team likely already knows Unity
4. **Built-in Features**: Test discovery, skipping, timing, and reporting
5. **CI/CD Ready**: Standard output format parseable by most CI tools
6. **Extensible**: Can add custom handlers and metrics as needed
7. **Lightweight**: Unity has minimal overhead and dependencies

## Success Criteria

1. **Functional Correctness**
   - All API functions work as documented
   - Data integrity maintained
   - Proper error handling

2. **Performance Targets**
   - < 1μs overhead per filter
   - > 10M samples/second throughput
   - < 100μs worst-case latency

3. **Reliability**
   - No memory leaks
   - No crashes under any test
   - Clean shutdown in all scenarios

## Implementation Plan

1. **Phase 1**: Unity Integration ✓
   - Set up Unity test framework
   - Create basic test harness with setUp/tearDown
   - Implement filter registration structure
   - Test with one simple filter

2. **Phase 2**: Core Compliance Tests ✓
   - Port existing tests to Unity format
   - Implement lifecycle tests
   - Implement connection tests
   - Implement basic data flow tests

3. **Phase 3**: Mock Filters ✓
   - MockProducer with configurable patterns
   - MockConsumer with validation
   - MockPassthrough with metrics
   - Test utilities for data verification

4. **Phase 4**: Advanced Tests ✓
   - Error handling tests
   - Threading tests  
   - Performance tests with metrics collection
   - Skip logic for inapplicable tests

5. **Phase 5**: Test Modularization ✓
   - Break monolithic test file into focused modules
   - One file per test category
   - Common infrastructure in shared files
   - Improved maintainability and reduced token usage

6. **Phase 6**: Buffer Configuration Testing (Current)
   - Add buffer configuration metadata to FilterRegistration_t
   - Implement buffer profile system
   - Update existing tests to use appropriate profiles
   - Add dedicated buffer configuration tests

7. **Phase 7**: Full Integration
   - Apply to all existing filters
   - CI/CD integration with Unity output parsing
   - Documentation and examples
   - Performance baselines

## Outstanding Issues

### Critical Issues

1. **Memory Corruption in test_dataflow_backpressure** ✓
   - **Symptom**: Segmentation fault when stopping filter (g_fut pointer corrupted to `0x4228000042280000`)
   - **Details**: The filter pointer gets corrupted, possibly with floating point data (42.0 in double precision)
   - **Status**: RESOLVED - Was caused by buffer size mismatch for passthrough filter
   - **Fix**: Test now detects passthrough filter type and matches consumer buffer size to input buffer size
   - **Root Cause**: Passthrough filter expects matching buffer sizes between input and output

2. **Race Condition in tearDown()** ✓
   - **Issue**: Filter could be deinitialized while worker thread still active
   - **Status**: RESOLVED - Fixed by adding pthread_join() after filt_stop()
   - **Impact**: Could cause segfaults or memory corruption during test cleanup

### Minor Issues

3. **Filter Initialization Order Bug**
   - **Issue**: SKIP_IF_NO_* macros were called before filter initialization
   - **Status**: Fixed by moving g_fut_init() before all skip checks
   - **Impact**: Tests incorrectly skipped for filters with outputs/inputs/workers
   - **Root Cause**: Filter properties only populated during init

4. **Sequence Validation Failures**
   - **Symptom**: test_dataflow_passthrough reports 640 sequence errors for Passthrough filter
   - **Impact**: Test failures but filter appears to work correctly
   - **Possible Cause**: The controllable consumer's sequence validation logic may not account for multiple producers with different start sequences
   - **Next Steps**: Debug sequence number propagation through passthrough filter

5. **Type Safety Test Failing**
   - **Issue**: test_connection_type_safety expects connection to fail with type mismatch, but it succeeds
   - **Impact**: Type checking may not be working as expected
   - **Next Steps**: Verify type checking logic in filt_sink_connect

6. **Worker Thread Errors During Shutdown**
   - **Issue**: NO_SINK errors reported when filter stopped without connected sink
   - **Status**: Expected behavior but causes test failures
   - **Recommendation**: Tests should connect sinks before starting filters with outputs

### Architectural Improvements Needed

7. **Enhanced Error Context**
   - **Status**: Implemented custom assert macros in test_filter_bench_asserts.h
   - **Features**: Automatic file/line context, human-readable error codes, descriptive messages
   - **Next Steps**: Extend to all test assertions for consistency

8. **Redundant Passthrough Implementation**
   - **Issue**: Both core.c (matched_passthrough) and passthrough.c implement the same functionality
   - **Impact**: Maintenance burden and potential confusion
   - **Recommendation**: Remove duplicate or consolidate implementations

9. **Resource Management**
   - **Issue**: Complex cleanup paths with multiple allocation points
   - **Status**: Added tracking of allocated vs initialized resources
   - **Recommendation**: Implement RAII-style resource management helpers

### Test Infrastructure Improvements

10. **Mock Filter Enhancements**
    - Need better cleanup in error paths
    - Add bounds checking in controllable_producer
    - Implement debug logging for troubleshooting
    - Add memory corruption detection

11. **Performance Test Infrastructure**
    - PassthroughMetrics filter not yet implemented (test_perf_latency disabled)
    - Need consistent timing measurement across all tests
    - Add CPU and memory usage tracking
    - Implement configurable performance thresholds

### Lessons Learned

1. **Initialization Order Matters**: Always initialize filters before checking their properties
2. **Thread Synchronization**: Must ensure worker threads are fully stopped before cleanup
3. **Error Context is Critical**: Rich error messages dramatically improve debugging efficiency
4. **Resource Tracking**: Distinguish between allocated and initialized resources for proper cleanup
5. **Test Isolation**: Each test must fully clean up to prevent affecting subsequent tests
6. **Buffer Size Compatibility**: Some filters (like passthrough) require matching buffer sizes between components
7. **Modular Test Structure**: Breaking tests into separate files improves maintainability and reduces token usage

## Recent Improvements

### Test Modularization (Completed)
The original monolithic `test_filter_bench.c` (1,799 lines) has been refactored into focused test modules:
- Each test category now has its own file (e.g., `test_lifecycle_basic.c`, `test_dataflow_backpressure.c`)
- Common infrastructure moved to `common.h` and `common.c`
- Reduces token usage from ~45k to 1-5k per test file
- Improves git history clarity and reduces merge conflicts
- Makes it easier to work on specific test categories

### Buffer Configuration Testing (In Progress)
Adding support for testing filters with different buffer configurations:
- Filters can be tested with tiny, small, large, and performance-optimized buffers
- Each test can apply the appropriate buffer profile for its scenario
- Helps identify buffer-size-dependent bugs and performance characteristics
