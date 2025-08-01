# Filter Test Bench Specification

## Overview

The Filter Test Bench is a comprehensive testing framework designed to validate all aspects of the bpipe2 filter public API. It provides a generic compliance test suite that can be applied to any filter implementation, reducing code repetition and ensuring consistent API behavior.

## Design Principles

1. **Generic Compliance Testing** - Single test suite applicable to all filter types
2. **Clean Separation** - Test context (input) separate from test results (output)
3. **Rich Error Context** - Capture file, line, function for debugging
4. **Performance Metrics** - Extract KPIs like throughput and latency
5. **Flexible Results** - Support pass/fail/skip/performance outcomes

## Test Framework Architecture

### Core Types

```c
// Test result status
typedef enum {
    TEST_RESULT_PASS,
    TEST_RESULT_FAIL,
    TEST_RESULT_SKIP,      // Test not applicable to this filter
    TEST_RESULT_TIMEOUT,   // Test exceeded time limit
    TEST_RESULT_PERF,      // Performance test completed (check metrics)
    TEST_RESULT_BASELINE   // Baseline measurement captured
} TestResultStatus_t;

// Performance/KPI metrics
typedef struct {
    double throughput_samples_per_sec;
    double latency_ns_p50;
    double latency_ns_p99;
    double cpu_usage_percent;
    size_t memory_bytes_peak;
    size_t batches_processed;
} TestMetrics_t;

// Test error details
typedef struct {
    Bp_EC ec;
    const char* file;
    const char* func;
    int line;
    const char* expr;
    const char* message;
} TestError_t;

// Error callback for real-time error reporting
typedef void (*TestErrorCallback)(const TestError_t* error, void* user_data);

// Test harness context (input to test)
typedef struct {
    Filter_t* fut;              // Filter under test
    TestErrorCallback on_error; // Optional error callback
    void* user_data;           // Callback user data
    double timeout_sec;        // Test timeout
    bool capture_metrics;      // Enable metric collection
    void* default_config;      // Default config for filter init
    FilterInitFunc init_func;  // Filter's init function
} TestContext_t;

// Test result (output from test)
typedef struct {
    TestResultStatus_t status;
    TestMetrics_t metrics;
    char message[256];         // Error or skip reason
    
    // Error details (if failed)
    Bp_EC error_code;
    const char* error_file;
    const char* error_func;
    int error_line;
} TestResult_t;

// Test case function signature
typedef TestResult_t (*FilterTestCase)(TestContext_t* ctx);
```

### Test Assertion Macro

```c
// TH_ASSERT macro that captures context and returns on failure
#define TH_ASSERT(ctx, condition, error_code, message) \
    do { \
        if (!(condition)) { \
            TestResult_t result = { \
                .status = TEST_RESULT_FAIL, \
                .error_code = (error_code), \
                .error_file = __FILE__, \
                .error_func = __func__, \
                .error_line = __LINE__ \
            }; \
            snprintf(result.message, sizeof(result.message), \
                     "%s: %s", #condition, (message)); \
            if ((ctx)->on_error) { \
                TestError_t error = { \
                    .ec = (error_code), \
                    .file = __FILE__, \
                    .func = __func__, \
                    .line = __LINE__, \
                    .expr = #condition, \
                    .message = (message) \
                }; \
                (ctx)->on_error(&error, (ctx)->user_data); \
            } \
            return result; \
        } \
    } while(0)
```

### Example Test Implementation

```c
// Example compliance test - lifecycle validation
TestResult_t test_lifecycle_basic(TestContext_t* ctx) {
    TestResult_t result = {.status = TEST_RESULT_PASS};
    Filter_t* fut = ctx->fut;
    
    TH_ASSERT(ctx, fut != NULL, Bp_EC_NULL_PTR, "Filter under test is NULL");
    
    // Test init using provided init function
    Bp_EC err = ctx->init_func(fut, ctx->default_config);
    TH_ASSERT(ctx, err == Bp_EC_OK, err, "Failed to initialize filter");
    
    // Test start
    err = filt_start(fut);
    TH_ASSERT(ctx, err == Bp_EC_OK, err, "Failed to start filter");
    
    // Test stop
    err = filt_stop(fut);
    TH_ASSERT(ctx, err == Bp_EC_OK, err, "Failed to stop filter");
    
    // Test deinit
    err = filt_deinit(fut);
    TH_ASSERT(ctx, err == Bp_EC_OK, err, "Failed to deinitialize filter");
    
    return result;
}

// Example performance test
TestResult_t test_throughput_max(TestContext_t* ctx) {
    TestResult_t result = {.status = TEST_RESULT_PERF};
    
    // Skip if filter has no inputs
    if (ctx->fut->n_input_buffers == 0) {
        result.status = TEST_RESULT_SKIP;
        snprintf(result.message, sizeof(result.message), 
                 "Filter has no inputs - skipping throughput test");
        return result;
    }
    
    // Setup test pipeline...
    MockProducer_t producer;
    MockConsumer_t consumer;
    
    // Run throughput test...
    double start_time = get_time_sec();
    size_t samples_processed = run_throughput_test(ctx->fut, &producer, &consumer);
    double elapsed_time = get_time_sec() - start_time;
    
    // Capture metrics
    result.metrics.throughput_samples_per_sec = samples_processed / elapsed_time;
    result.metrics.memory_bytes_peak = get_peak_memory();
    result.metrics.batches_processed = consumer.batches_received;
    
    return result;
}
```

## Mock Filters

Mock filters provide controlled test infrastructure:

### Controllable Producer Filter
```c
typedef struct {
    Filter_t base;
    
    // Configuration
    double samples_per_sec;     // Generation rate
    size_t batch_size;          // Samples per batch
    DataPattern_t pattern;      // SEQUENTIAL, RANDOM, SINE, etc.
    bool burst_mode;            // Enable burst generation
    
    // Error injection
    bool inject_error;          // Enable error injection
    Bp_EC error_code;          // Error to inject
    size_t error_after_samples; // When to inject error
    
    // Metrics
    size_t batches_produced;
    size_t samples_generated;
    double timing_error_ns;     // Actual vs expected timing
} MockProducer_t;
```

### Controllable Consumer Filter
```c
typedef struct {
    Filter_t base;
    
    // Configuration
    long processing_delay_us;   // Delay per batch
    bool validate_sequence;     // Check sequential data
    bool validate_checksum;     // Verify data integrity
    ConsumptionPattern_t pattern; // STEADY, BURSTY, etc.
    
    // Metrics
    size_t batches_consumed;
    double avg_latency_ns;
    bool data_valid;           // Data integrity check result
} MockConsumer_t;
```

### Passthrough Filter with Metrics
```c
typedef struct {
    Filter_t base;
    
    // Metrics
    size_t batches_passed;
    double min_latency_ns;
    double max_latency_ns;
    double avg_latency_ns;
    size_t max_queue_depth;
} MockPassthrough_t;
```

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

## Test Registration and Execution

### Filter Registration

```c
// Filter init function type (matches existing filter init signatures)
typedef Bp_EC (*FilterInitFunc)(void* filter, void* config);

// Filter registration entry
typedef struct {
    size_t filter_size;         // sizeof(MyFilter_t)
    FilterInitFunc init;        // Filter's init function
    void* default_config;       // Default configuration
    size_t config_size;         // sizeof(MyFilterConfig_t)
} FilterRegistration_t;

// Global compliance test suite
static const FilterTestCase compliance_tests[] = {
    // Lifecycle tests
    test_lifecycle_basic,
    test_lifecycle_restart,
    test_lifecycle_errors,
    test_lifecycle_cleanup,
    
    // Connection tests
    test_connection_single_sink,
    test_connection_multi_sink,
    test_connection_multi_input,
    test_connection_type_safety,
    test_connection_limits,
    
    // Data flow tests
    test_dataflow_passthrough,
    test_dataflow_backpressure,
    test_dataflow_starvation,
    test_dataflow_timing,
    test_dataflow_ordering,
    
    // Error handling tests
    test_error_worker_assert,
    test_error_propagation,
    test_error_timeout,
    test_error_invalid_config,
    test_error_recovery,
    
    // Threading tests
    test_thread_worker_lifecycle,
    test_thread_concurrent_ops,
    test_thread_shutdown_sync,
    test_thread_force_return,
    
    // Performance tests
    test_perf_throughput,
    test_perf_latency,
    test_perf_cpu_usage,
    test_perf_memory_usage,
    test_perf_scaling,
};
```

### Running Tests

```c
// Test runner that loops through all filters and all tests
TestSummary_t run_filter_compliance_tests(FilterRegistration_t filters[], 
                                         size_t n_filters) {
    TestSummary_t summary = {0};
    
    // Loop through each filter
    for (size_t f = 0; f < n_filters; f++) {
        // Allocate filter and config
        void* filter = calloc(1, filters[f].filter_size);
        void* config = malloc(filters[f].config_size);
        memcpy(config, filters[f].default_config, filters[f].config_size);
        
        // Initialize to get filter name
        Bp_EC err = filters[f].init(filter, config);
        if (err != Bp_EC_OK) {
            printf("Failed to initialize filter for testing\n");
            free(filter);
            free(config);
            continue;
        }
        
        Filter_t* base = (Filter_t*)filter;
        printf("Testing %s...\n", base->name);
        
        // Cleanup this instance
        if (base->ops.deinit) base->ops.deinit(base);
        free(filter);
        free(config);
        
        // Loop through each compliance test
        for (size_t t = 0; t < sizeof(compliance_tests)/sizeof(compliance_tests[0]); t++) {
            // Create fresh filter instance for each test
            filter = calloc(1, filters[f].filter_size);
            config = malloc(filters[f].config_size);
            memcpy(config, filters[f].default_config, filters[f].config_size);
            
            // Setup test context (uninitialized filter)
            TestContext_t ctx = {
                .fut = (Filter_t*)filter,
                .on_error = default_error_handler,
                .timeout_sec = 30.0,
                .capture_metrics = true,
                .default_config = config,
                .init_func = filters[f].init
            };
            
            // Run test
            TestResult_t result = compliance_tests[t](&ctx);
            
            // Record result
            record_test_result(&summary, base->name, 
                             get_test_name(compliance_tests[t]), &result);
            
            // Cleanup
            base = (Filter_t*)filter;
            if (base->ops.deinit) base->ops.deinit(base);
            free(filter);
            free(config);
        }
    }
    
    return summary;
}

// Example usage in main test program
int main() {
    // Register all filters to test
    FilterRegistration_t filters[] = {
        {
            .filter_size = sizeof(SignalGenerator_t),
            .init = (FilterInitFunc)signal_generator_init,
            .default_config = &default_sg_config,
            .config_size = sizeof(SignalGeneratorConfig_t)
        },
        {
            .filter_size = sizeof(MapFilter_t),
            .init = (FilterInitFunc)map_filter_init,
            .default_config = &default_map_config,
            .config_size = sizeof(MapConfig_t)
        },
        {
            .filter_size = sizeof(CSVSource_t),
            .init = (FilterInitFunc)csv_source_init,
            .default_config = &default_csv_config,
            .config_size = sizeof(CSVSourceConfig_t)
        },
        // ... add more filters
    };
    
    // Run all compliance tests on all filters
    TestSummary_t summary = run_filter_compliance_tests(filters, 
                                                       sizeof(filters)/sizeof(filters[0]));
    
    // Print results
    print_test_summary(&summary);
    
    return summary.failed_count > 0 ? 1 : 0;
}
```

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

1. **Phase 1**: Core framework
   - Test types and macros
   - Mock filters
   - Basic test registration

2. **Phase 2**: Compliance tests
   - Lifecycle tests
   - Connection tests
   - Data flow tests

3. **Phase 3**: Advanced tests
   - Error handling tests
   - Threading tests
   - Performance tests

4. **Phase 4**: Integration
   - Apply to existing filters
   - CI/CD integration
   - Documentation