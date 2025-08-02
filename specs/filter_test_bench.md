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

### Example Test Implementations

```c
// Lifecycle compliance test using Unity assertions
void test_lifecycle_basic(void) {
    TEST_ASSERT_NOT_NULL(g_fut);
    
    // Test init
    TEST_ASSERT_EQUAL(Bp_EC_OK, g_fut_init(g_fut, g_fut_config));
    TEST_ASSERT_NOT_NULL(g_fut->name);
    
    // Test start
    TEST_ASSERT_EQUAL(Bp_EC_OK, filt_start(g_fut));
    TEST_ASSERT_TRUE(atomic_load(&g_fut->running));
    
    // Test stop
    TEST_ASSERT_EQUAL(Bp_EC_OK, filt_stop(g_fut));
    TEST_ASSERT_FALSE(atomic_load(&g_fut->running));
    
    // Test deinit
    TEST_ASSERT_EQUAL(Bp_EC_OK, filt_deinit(g_fut));
}

// Connection test with skip support
void test_connection_single_sink(void) {
    TEST_ASSERT_EQUAL(Bp_EC_OK, g_fut_init(g_fut, g_fut_config));
    
    // Skip if filter has no outputs
    if (g_fut->max_sinks == 0) {
        TEST_IGNORE_MESSAGE("Filter has no outputs");
        return;
    }
    
    MockConsumer_t consumer;
    TEST_ASSERT_EQUAL(Bp_EC_OK, mock_consumer_init(&consumer));
    
    TEST_ASSERT_EQUAL(Bp_EC_OK, filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]));
    TEST_ASSERT_NOT_NULL(g_fut->sinks[0]);
    TEST_ASSERT_EQUAL_PTR(consumer.base.input_buffers[0], g_fut->sinks[0]);
}

// Performance test with metrics collection
void test_perf_throughput(void) {
    // Skip for zero-input filters
    if (g_fut->n_input_buffers == 0) {
        TEST_IGNORE_MESSAGE("Filter has no inputs");
        return;
    }
    
    TEST_ASSERT_EQUAL(Bp_EC_OK, g_fut_init(g_fut, g_fut_config));
    
    // Setup high-rate pipeline
    MockProducer_t producer;
    TEST_ASSERT_EQUAL(Bp_EC_OK, mock_producer_init(&producer, &(MockProducerConfig_t){
        .samples_per_sec = 10000000.0,  // 10M samples/sec
        .batch_size = 1024
    }));
    
    MockConsumer_t consumer;
    TEST_ASSERT_EQUAL(Bp_EC_OK, mock_consumer_init(&consumer));
    
    // Connect and run
    TEST_ASSERT_EQUAL(Bp_EC_OK, bp_connect(&producer.base, 0, g_fut, 0));
    TEST_ASSERT_EQUAL(Bp_EC_OK, bp_connect(g_fut, 0, &consumer.base, 0));
    
    uint64_t start_ns = get_time_ns();
    
    TEST_ASSERT_EQUAL(Bp_EC_OK, filt_start(&producer.base));
    TEST_ASSERT_EQUAL(Bp_EC_OK, filt_start(g_fut));
    TEST_ASSERT_EQUAL(Bp_EC_OK, filt_start(&consumer.base));
    
    sleep(1);  // Run for 1 second
    
    TEST_ASSERT_EQUAL(Bp_EC_OK, filt_stop(&producer.base));
    TEST_ASSERT_EQUAL(Bp_EC_OK, filt_stop(g_fut));
    TEST_ASSERT_EQUAL(Bp_EC_OK, filt_stop(&consumer.base));
    
    uint64_t elapsed_ns = get_time_ns() - start_ns;
    
    // Calculate and store metrics
    double throughput = consumer.samples_consumed * 1e9 / elapsed_ns;
    g_last_perf_metrics.throughput_samples_per_sec = throughput;
    
    // Record performance metric
    char buf[256];
    snprintf(buf, sizeof(buf), "Throughput: %.2f Msamples/sec\n", throughput / 1e6);
    strcat(g_perf_report, buf);
    
    // Assert minimum performance
    TEST_ASSERT_GREATER_THAN(1000000, throughput);  // > 1M samples/sec
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

### Unity Test Runner

```c
// All compliance tests as Unity test functions
static void (*compliance_tests[])(void) = {
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

// Helper macros for skipping inapplicable tests
#define SKIP_IF_NO_INPUTS() \
    if (g_fut->n_input_buffers == 0) { \
        TEST_IGNORE_MESSAGE("Filter has no inputs"); \
        return; \
    }

#define SKIP_IF_NO_OUTPUTS() \
    if (g_fut->max_sinks == 0) { \
        TEST_IGNORE_MESSAGE("Filter has no outputs"); \
        return; \
    }

// Main test program
int main(int argc, char* argv[]) {
    // Command line options
    const char* filter_pattern = NULL;
    const char* test_pattern = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter_pattern = argv[++i];
        } else if (strcmp(argv[i], "--test") == 0 && i + 1 < argc) {
            test_pattern = argv[++i];
        }
    }
    
    // Register filters to test
    FilterRegistration_t filters[] = {
        {
            .name = "SignalGenerator",
            .filter_size = sizeof(SignalGenerator_t),
            .init = (FilterInitFunc)signal_generator_init,
            .default_config = &default_sg_config,
            .config_size = sizeof(SignalGeneratorConfig_t)
        },
        {
            .name = "MapFilter",
            .filter_size = sizeof(MapFilter_t),
            .init = (FilterInitFunc)map_filter_init,
            .default_config = &default_map_config,
            .config_size = sizeof(MapConfig_t)
        },
        {
            .name = "CSVSource",
            .filter_size = sizeof(CSVSource_t),
            .init = (FilterInitFunc)csv_source_init,
            .default_config = &default_csv_config,
            .config_size = sizeof(CSVSourceConfig_t)
        },
        // ... add more filters
    };
    
    g_filters = filters;
    g_n_filters = sizeof(filters) / sizeof(filters[0]);
    
    // Run all tests for each filter
    for (g_current_filter = 0; g_current_filter < g_n_filters; g_current_filter++) {
        // Skip if filter doesn't match pattern
        if (filter_pattern && !strstr(filters[g_current_filter].name, filter_pattern)) {
            continue;
        }
        
        printf("\n========== Testing %s ==========\n", 
               filters[g_current_filter].name);
        
        // Clear performance report
        g_perf_report[0] = '\0';
        
        UNITY_BEGIN();
        
        for (size_t i = 0; i < sizeof(compliance_tests)/sizeof(compliance_tests[0]); i++) {
            // Skip if test doesn't match pattern
            if (test_pattern) {
                const char* test_name = Unity.TestFile; // Unity tracks current test name
                if (!strstr(test_name, test_pattern)) {
                    continue;
                }
            }
            
            RUN_TEST(compliance_tests[i]);
        }
        
        UNITY_END();
        
        // Print performance metrics if collected
        if (strlen(g_perf_report) > 0) {
            printf("\n=== %s Performance Metrics ===\n%s\n", 
                   filters[g_current_filter].name, g_perf_report);
        }
    }
    
    // Summary
    printf("\n========== SUMMARY ==========\n");
    printf("Tested %zu filters with %zu compliance tests each\n", 
           g_n_filters, sizeof(compliance_tests)/sizeof(compliance_tests[0]));
    
    return 0;
}
```

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

1. **Phase 1**: Unity Integration
   - Set up Unity test framework
   - Create basic test harness with setUp/tearDown
   - Implement filter registration structure
   - Test with one simple filter

2. **Phase 2**: Core Compliance Tests  
   - Port existing tests to Unity format
   - Implement lifecycle tests
   - Implement connection tests
   - Implement basic data flow tests

3. **Phase 3**: Mock Filters
   - MockProducer with configurable patterns
   - MockConsumer with validation
   - MockPassthrough with metrics
   - Test utilities for data verification

4. **Phase 4**: Advanced Tests
   - Error handling tests
   - Threading tests  
   - Performance tests with metrics collection
   - Skip logic for inapplicable tests

5. **Phase 5**: Enhancements
   - Custom Unity error handler with timing
   - Performance metric reporting
   - Command-line test filtering
   - Debugger integration

6. **Phase 6**: Full Integration
   - Apply to all existing filters
   - CI/CD integration with Unity output parsing
   - Documentation and examples
   - Performance baselines