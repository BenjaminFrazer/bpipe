# Testing Guidelines

This guide describes best practices for testing bpipe2 filters and components.

**Note**: For comprehensive automated filter compliance testing, see the [Filter Compliance Framework Reference](../reference/filter_compliance_framework_reference.md).

## Test Framework

Bpipe2 uses the Unity test framework for C unit tests. Test files are located in the `tests/` directory.

## Test Structure

### Basic Test File Structure

```c
#include "unity.h"
#include "bpipe/core.h"
#include "bpipe/utils.h"
#include "bpipe/my_filter.h"

// Test fixtures
void setUp(void) {
    // Run before each test
}

void tearDown(void) {
    // Run after each test
}

// Test functions
void test_my_filter_init(void) {
    MyFilter_t filter;
    MyFilterCfg_t cfg = { /* ... */ };
    
    CHECK_ERR(my_filter_init(&filter, cfg));
    
    TEST_ASSERT_EQUAL_STRING("my_filter", filter.base.name);
    TEST_ASSERT_EQUAL(1, filter.base.n_sources);
    
    my_filter_deinit(&filter);
}

// Main
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_my_filter_init);
    RUN_TEST(test_my_filter_basic);
    // ... more tests
    return UNITY_END();
}
```

## Error Handling in Tests

### Using CHECK_ERR Macro

The `CHECK_ERR` macro from utils.h provides clean error checking:

```c
#define CHECK_ERR(ERR)                                          \
  do {                                                          \
    Bp_EC _ec = ERR;                                            \
    TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, _ec, err_lut[_ec]); \
  } while (false);

// Usage
CHECK_ERR(filter_init(&filter, "test", FILT_T_MAP, 1, 1));
CHECK_ERR(filter_start(&filter));
```

### Checking Worker Thread Errors

Always verify worker thread errors after joining:

```c
void test_filter_with_worker(void) {
    MyFilter_t filter;
    CHECK_ERR(my_filter_init(&filter, cfg));
    CHECK_ERR(filter_start(&filter.base));
    
    // Do work...
    
    CHECK_ERR(filter_stop(&filter.base));
    
    // IMPORTANT: Check worker thread error
    CHECK_ERR(filter.base.worker_err_info.ec);
    
    my_filter_deinit(&filter);
}
```

## Test Utilities

### Test Sink

For capturing filter output:

```c
typedef struct {
    Filter_t base;
    float* captured_data;      // Dynamically allocated
    size_t captured_samples;   // Number of samples captured
    size_t max_samples;        // Maximum samples to capture
    Batch_t* current_batch;    // Current batch being processed
} TestSink_t;

// Initialize with maximum samples to capture
Bp_EC test_sink_init(TestSink_t* sink, size_t max_samples);

// Usage
void test_filter_output(void) {
    MyFilter_t filter;
    TestSink_t sink;
    
    CHECK_ERR(my_filter_init(&filter, cfg));
    CHECK_ERR(test_sink_init(&sink, 1024));
    
    CHECK_ERR(bp_connect(&filter.base, 0, &sink.base, 0));
    CHECK_ERR(filter_start(&filter.base));
    CHECK_ERR(filter_start(&sink.base));
    
    // Wait for data
    while (sink.captured_samples < expected_samples) {
        usleep(1000);
    }
    
    // Verify output
    TEST_ASSERT_EQUAL_FLOAT(expected_value, sink.captured_data[0]);
    
    // Cleanup
    CHECK_ERR(filter_stop(&filter.base));
    CHECK_ERR(filter_stop(&sink.base));
    CHECK_ERR(filter.base.worker_err_info.ec);
    CHECK_ERR(sink.base.worker_err_info.ec);
    
    test_sink_deinit(&sink);
    my_filter_deinit(&filter);
}
```

### Test Source

For providing test input:

```c
typedef struct {
    Filter_t base;
    float* test_data;
    size_t data_size;
    size_t samples_sent;
    uint64_t period_ns;
} TestSource_t;

Bp_EC test_source_init(TestSource_t* src, float* data, size_t size, uint64_t period_ns);
```

## Testing Patterns

### 1. Initialization Tests

Test all configuration validation:

```c
void test_invalid_configuration(void) {
    MyFilter_t filter;
    MyFilterCfg_t cfg;
    
    // Test null pointer
    TEST_ASSERT_EQUAL(Bp_EC_NULL_PTR, my_filter_init(NULL, cfg));
    
    // Test invalid parameters
    cfg.param = -1;  // Invalid
    TEST_ASSERT_EQUAL(Bp_EC_INVALID_ARG, my_filter_init(&filter, cfg));
    
    // Test boundary conditions
    cfg.param = MAX_VALUE + 1;
    TEST_ASSERT_EQUAL(Bp_EC_INVALID_ARG, my_filter_init(&filter, cfg));
}
```

### 2. Data Processing Tests

Test actual data transformation:

```c
void test_data_processing(void) {
    // Description: Verify filter correctly processes input data
    
    MyFilter_t filter;
    TestSource_t source;
    TestSink_t sink;
    
    // Setup test data
    float input_data[] = {1.0, 2.0, 3.0, 4.0};
    float expected_output[] = {2.0, 4.0, 6.0, 8.0};  // Doubled
    
    // Initialize components
    CHECK_ERR(my_filter_init(&filter, cfg));
    CHECK_ERR(test_source_init(&source, input_data, 4, 1000000));
    CHECK_ERR(test_sink_init(&sink, 4));
    
    // Connect pipeline: source -> filter -> sink
    CHECK_ERR(bp_connect(&source.base, 0, &filter.base, 0));
    CHECK_ERR(bp_connect(&filter.base, 0, &sink.base, 0));
    
    // Start processing
    CHECK_ERR(filter_start(&source.base));
    CHECK_ERR(filter_start(&filter.base));
    CHECK_ERR(filter_start(&sink.base));
    
    // Wait for completion
    while (sink.captured_samples < 4) {
        usleep(1000);
    }
    
    // Verify output
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_FLOAT(expected_output[i], sink.captured_data[i]);
    }
    
    // Cleanup...
}
```

### 3. Threading Tests

Test concurrent behavior:

```c
void test_concurrent_processing(void) {
    // Description: Verify filter handles concurrent batches correctly
    
    MyFilter_t filter;
    TestSink_t sinks[4];
    
    CHECK_ERR(my_filter_init(&filter, cfg));
    
    // Create multiple output connections
    for (int i = 0; i < 4; i++) {
        CHECK_ERR(test_sink_init(&sinks[i], 256));
        CHECK_ERR(bp_connect(&filter.base, 0, &sinks[i].base, i));
        CHECK_ERR(filter_start(&sinks[i].base));
    }
    
    CHECK_ERR(filter_start(&filter.base));
    
    // Wait and verify all sinks receive data
    // ...
}
```

### 4. Error Recovery Tests

Test error conditions:

```c
void test_error_recovery(void) {
    // Description: Verify filter recovers from transient errors
    
    MyFilter_t filter;
    
    // Simulate various error conditions
    // Test recovery mechanisms
    // Verify filter returns to normal operation
}
```

## Using Timeout Scripts

For tests that might hang, use the timeout wrapper:

```bash
# Run single test with 30 second timeout
./scripts/run_with_timeout.sh 30 ./build/test_my_filter

# In Makefile
test-my-filter: build/test_my_filter
	./scripts/run_with_timeout.sh 30 ./build/test_my_filter
```

The timeout script:
- Exits with code 124 on timeout
- Logs timeouts to `timeout.log`
- Kills hung processes cleanly

## Test Documentation

### Test Function Comments

Each test should have a clear description:

```c
void test_phase_continuity(void) {
    // Description: Verify waveform phase continuity across batch boundaries
    // Uses sawtooth waveform for predictable linear progression
    // Checks that the value difference at batch boundaries matches
    // the expected per-sample increment
    
    // Test implementation...
}
```

### Complex Test Explanation

For complex tests, add detailed comments:

```c
void test_time_alignment(void) {
    // Description: Verify multi-input time synchronization
    //
    // This test creates two sources with different sample rates:
    // - Source 1: 1000 Hz (1ms period)
    // - Source 2: 2000 Hz (0.5ms period)
    //
    // The sync filter should align batches by timestamp and
    // interpolate as needed to produce synchronized output
    
    // Test implementation...
}
```

## Debugging Hanging Tests

If tests hang:

1. **Use strace**: `strace -f ./build/test_my_filter`
2. **Check for deadlocks**: Look for mutex/condition variable waits
3. **Add printf debugging**: Strategic prints can reveal where execution stops
4. **Use timeout script**: Prevents CI/CD hanging indefinitely
5. **Check worker threads**: Ensure `running` flag is properly managed

## Best Practices

1. **Test One Thing**: Each test function should verify one specific behavior
2. **Use Descriptive Names**: `test_filter_handles_null_input` not `test_1`
3. **Clean Up Resources**: Always deinit filters and free memory
4. **Test Edge Cases**: Empty batches, maximum sizes, boundary conditions
5. **Verify Metadata**: Check batch timestamps, periods, and error codes
6. **Test Error Paths**: Verify error handling and recovery
7. **Document Intent**: Comment what each test is verifying and why

## Example: Complete Test Suite

```c
#include "unity.h"
#include "bpipe/utils.h"
#include "bpipe/my_filter.h"
#include "test_utils.h"

void setUp(void) {}
void tearDown(void) {}

void test_initialization(void) {
    // Description: Verify filter initializes with valid configuration
    MyFilter_t filter;
    MyFilterCfg_t cfg = {.param = 42};
    
    CHECK_ERR(my_filter_init(&filter, cfg));
    TEST_ASSERT_EQUAL(42, filter.param);
    
    my_filter_deinit(&filter);
}

void test_invalid_config(void) {
    // Description: Verify filter rejects invalid configuration
    MyFilter_t filter;
    MyFilterCfg_t cfg = {.param = -1};
    
    TEST_ASSERT_EQUAL(Bp_EC_INVALID_ARG, my_filter_init(&filter, cfg));
}

void test_basic_processing(void) {
    // Description: Verify filter processes data correctly
    // ... full test implementation
}

void test_thread_safety(void) {
    // Description: Verify filter is thread-safe under concurrent access
    // ... full test implementation
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_initialization);
    RUN_TEST(test_invalid_config);
    RUN_TEST(test_basic_processing);
    RUN_TEST(test_thread_safety);
    return UNITY_END();
}
```