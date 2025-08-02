/**
 * @file test_filter_bench.c
 * @brief API Compliance Test Bench for bpipe2 filters
 * 
 * Unity-based testing framework that validates all aspects of the bpipe2 
 * filter public API. Provides generic compliance tests applicable to any 
 * filter implementation.
 */

#define _DEFAULT_SOURCE
#include "unity.h"
#include "core.h"
#include "utils.h"
#include "mock_filters.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

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

// Helper macros for skipping inapplicable tests
#define SKIP_IF_NO_INPUTS() \
    if (g_fut->n_input_buffers == 0) { \
        TEST_IGNORE_MESSAGE("Filter has no inputs"); \
        return; \
    }

#define SKIP_IF_NO_OUTPUTS() \
    if (g_fut->n_sinks == 0) { \
        TEST_IGNORE_MESSAGE("Filter has no outputs"); \
        return; \
    }

#define SKIP_IF_NO_WORKER() \
    if (g_fut->worker == NULL) { \
        TEST_IGNORE_MESSAGE("Filter has no worker thread"); \
        return; \
    }

// Helper to get current time in nanoseconds
static uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Unity setUp - called before each test
void setUp(void)
{
    // Create fresh filter instance for each test
    FilterRegistration_t* reg = &g_filters[g_current_filter];
    
    g_fut = (Filter_t*)calloc(1, reg->filter_size);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_fut, "Failed to allocate filter");
    
    g_fut_config = malloc(reg->config_size);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_fut_config, "Failed to allocate config");
    
    memcpy(g_fut_config, reg->default_config, reg->config_size);
    g_fut_init = reg->init;
    g_filter_name = reg->name;
    
    // Capture test start time
    g_test_start_ns = get_time_ns();
}

// Unity tearDown - called after each test
void tearDown(void)
{
    // Cleanup after each test
    if (g_fut) {
        // Stop filter if running
        if (atomic_load(&g_fut->running)) {
            filt_stop(g_fut);
        }
        
        // Deinit if initialized
        if (g_fut->name[0] != '\0') {
            filt_deinit(g_fut);
        }
        
        free(g_fut);
        g_fut = NULL;
    }
    
    if (g_fut_config) {
        free(g_fut_config);
        g_fut_config = NULL;
    }
}

//=============================================================================
// Lifecycle Compliance Tests
//=============================================================================

void test_lifecycle_basic(void) {
    TEST_ASSERT_NOT_NULL(g_fut);
    
    // Test init
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Filter init failed");
    TEST_ASSERT_TRUE_MESSAGE(g_fut->name[0] != '\0', "Filter name not set after init");
    
    // Test that we can't double-init
    err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(Bp_EC_OK, err, "Filter should reject double init");
    
    // Test deinit
    err = filt_deinit(g_fut);
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Filter deinit failed");
}

void test_lifecycle_with_worker(void) {
    SKIP_IF_NO_WORKER();
    
    // Initialize filter
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // For filters with outputs, connect a consumer
    ControllableConsumer_t* consumer = NULL;
    if (g_fut->n_sinks > 0) {
        consumer = (ControllableConsumer_t*)malloc(sizeof(ControllableConsumer_t));
        TEST_ASSERT_NOT_NULL(consumer);
        
        ControllableConsumerConfig_t consumer_config = {
            .name = "test_consumer",
            .buff_config = {
                .dtype = DTYPE_FLOAT,
                .batch_capacity_expo = 6,
                .ring_capacity_expo = 8,
                .overflow_behaviour = OVERFLOW_BLOCK
            },
            .timeout_us = 1000000,
            .process_delay_us = 0,
            .validate_sequence = false,
            .validate_timing = false
        };
        
        err = controllable_consumer_init(consumer, consumer_config);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        err = filt_sink_connect(g_fut, 0, consumer->base.input_buffers[0]);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    }
    
    // Start filter
    err = filt_start(g_fut);
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Filter start failed");
    TEST_ASSERT_TRUE_MESSAGE(atomic_load(&g_fut->running), "Filter not running after start");
    
    // Start consumer if connected
    if (consumer) {
        err = filt_start(&consumer->base);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    }
    
    // Let it run briefly
    usleep(10000); // 10ms
    
    // Stop consumer first
    if (consumer) {
        err = filt_stop(&consumer->base);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    }
    
    // Stop filter
    err = filt_stop(g_fut);
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Filter stop failed");
    TEST_ASSERT_FALSE_MESSAGE(atomic_load(&g_fut->running), "Filter still running after stop");
    
    // Check for worker errors
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, g_fut->worker_err_info.ec, 
                              "Worker thread reported error");
    
    // Cleanup consumer
    if (consumer) {
        filt_deinit(&consumer->base);
        free(consumer);
    }
    
    // Deinit filter
    err = filt_deinit(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_lifecycle_restart(void) {
    SKIP_IF_NO_WORKER();
    
    // Initialize
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Multiple start/stop cycles
    for (int i = 0; i < 3; i++) {
        err = filt_start(g_fut);
        TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Filter start failed");
        TEST_ASSERT_TRUE(atomic_load(&g_fut->running));
        
        usleep(5000); // 5ms
        
        err = filt_stop(g_fut);
        TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Filter stop failed");
        TEST_ASSERT_FALSE(atomic_load(&g_fut->running));
        TEST_ASSERT_EQUAL(Bp_EC_OK, g_fut->worker_err_info.ec);
    }
    
    err = filt_deinit(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_lifecycle_errors(void) {
    // Test operations on uninitialized filter
    Bp_EC err = filt_start(g_fut);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(Bp_EC_OK, err, 
                                  "Should not start uninitialized filter");
    
    err = filt_stop(g_fut);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(Bp_EC_OK, err, 
                                  "Should not stop uninitialized filter");
    
    err = filt_deinit(g_fut);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(Bp_EC_OK, err, 
                                  "Should not deinit uninitialized filter");
}

//=============================================================================
// Connection Compliance Tests
//=============================================================================

void test_connection_single_sink(void) {
    SKIP_IF_NO_OUTPUTS();
    
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Create consumer
    ControllableConsumer_t consumer;
    ControllableConsumerConfig_t consumer_config = {
        .name = "test_consumer",
        .buff_config = {
            .dtype = (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) ? g_fut->input_buffers[0]->dtype : DTYPE_FLOAT,
            .batch_capacity_expo = 6,
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000
    };
    
    err = controllable_consumer_init(&consumer, consumer_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Connect
    err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Connection failed");
    TEST_ASSERT_NOT_NULL_MESSAGE(g_fut->sinks[0], "Sink not set after connection");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(consumer.base.input_buffers[0], g_fut->sinks[0],
                                  "Sink pointer mismatch");
    
    // Disconnect
    err = filt_sink_disconnect(g_fut, 0);
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Disconnection failed");
    TEST_ASSERT_NULL_MESSAGE(g_fut->sinks[0], "Sink not cleared after disconnect");
    
    // Cleanup
    filt_deinit(&consumer.base);
    filt_deinit(g_fut);
}

void test_connection_multi_sink(void) {
    SKIP_IF_NO_OUTPUTS();
    
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Skip if filter doesn't support multiple outputs
    if (g_fut->n_sinks < 2) {
        TEST_IGNORE_MESSAGE("Filter doesn't support multiple outputs");
        return;
    }
    
    // Create multiple consumers
    ControllableConsumer_t consumers[2];
    for (int i = 0; i < 2; i++) {
        ControllableConsumerConfig_t config = {
            .name = "test_consumer",
            .buff_config = {
                .dtype = (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) ? g_fut->input_buffers[0]->dtype : DTYPE_FLOAT,
                .batch_capacity_expo = 6,
                .ring_capacity_expo = 8,
                .overflow_behaviour = OVERFLOW_BLOCK
            },
            .timeout_us = 1000000
        };
        
        err = controllable_consumer_init(&consumers[i], config);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        // Connect
        err = filt_sink_connect(g_fut, i, consumers[i].base.input_buffers[0]);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        TEST_ASSERT_NOT_NULL(g_fut->sinks[i]);
    }
    
    // Verify all connections
    for (int i = 0; i < 2; i++) {
        TEST_ASSERT_EQUAL_PTR(consumers[i].base.input_buffers[0], g_fut->sinks[i]);
    }
    
    // Cleanup
    for (int i = 0; i < 2; i++) {
        filt_deinit(&consumers[i].base);
    }
    filt_deinit(g_fut);
}

void test_connection_type_safety(void) {
    SKIP_IF_NO_OUTPUTS();
    SKIP_IF_NO_INPUTS();
    
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // For filters with inputs, check type safety
    if (g_fut->n_input_buffers == 0) {
        TEST_IGNORE_MESSAGE("Filter has no inputs to test type safety");
        return;
    }
    
    // Get the expected data type from the filter's input buffer
    SampleDtype_t expected_dtype = g_fut->input_buffers[0]->dtype;
    
    // Create consumer with wrong data type
    SampleDtype_t wrong_dtype = (expected_dtype == DTYPE_FLOAT) ? DTYPE_I32 : DTYPE_FLOAT;
    
    ControllableConsumer_t consumer;
    ControllableConsumerConfig_t consumer_config = {
        .name = "test_consumer",
        .buff_config = {
            .dtype = wrong_dtype,  // Intentionally wrong type
            .batch_capacity_expo = 6,
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000
    };
    
    err = controllable_consumer_init(&consumer, consumer_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Connection should fail due to type mismatch
    err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(Bp_EC_OK, err, 
                                  "Connection should fail with type mismatch");
    
    // Cleanup
    filt_deinit(&consumer.base);
    filt_deinit(g_fut);
}

//=============================================================================
// Data Flow Compliance Tests  
//=============================================================================

void test_dataflow_passthrough(void) {
    SKIP_IF_NO_INPUTS();
    SKIP_IF_NO_OUTPUTS();
    SKIP_IF_NO_WORKER();
    
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Create producer and consumer
    ControllableProducer_t producer;
    ControllableProducerConfig_t prod_config = {
        .name = "test_producer",
        .timeout_us = 1000000,
        .samples_per_second = 10000,
        .batch_size = 64,
        .pattern = PATTERN_SEQUENTIAL,
        .max_batches = 10,
        .start_sequence = 1000
    };
    
    err = controllable_producer_init(&producer, prod_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    ControllableConsumer_t consumer;
    ControllableConsumerConfig_t cons_config = {
        .name = "test_consumer",
        .buff_config = {
            .dtype = (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) ? g_fut->input_buffers[0]->dtype : DTYPE_FLOAT,
            .batch_capacity_expo = 6,
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000,
        .validate_sequence = true,
        .validate_timing = true
    };
    
    err = controllable_consumer_init(&consumer, cons_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Connect pipeline: producer -> filter -> consumer
    err = filt_sink_connect(&producer.base, 0, g_fut->input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Start all filters
    err = filt_start(&producer.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(&consumer.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Wait for completion
    while (atomic_load(&producer.batches_produced) < prod_config.max_batches) {
        usleep(1000);
    }
    usleep(10000); // Extra time for data to flow through
    
    // Stop pipeline
    filt_stop(&producer.base);
    filt_stop(g_fut);
    filt_stop(&consumer.base);
    
    // Check for errors
    TEST_ASSERT_EQUAL(Bp_EC_OK, producer.base.worker_err_info.ec);
    TEST_ASSERT_EQUAL(Bp_EC_OK, g_fut->worker_err_info.ec);
    TEST_ASSERT_EQUAL(Bp_EC_OK, consumer.base.worker_err_info.ec);
    
    // Verify data integrity
    size_t seq_errors = atomic_load(&consumer.sequence_errors);
    TEST_ASSERT_EQUAL_MESSAGE(0, seq_errors, "Sequence errors detected");
    
    size_t timing_errors = atomic_load(&consumer.timing_errors);
    TEST_ASSERT_EQUAL_MESSAGE(0, timing_errors, "Timing errors detected");
    
    // Cleanup
    filt_deinit(&producer.base);
    filt_deinit(&consumer.base);
    filt_deinit(g_fut);
}

void test_dataflow_backpressure(void) {
    SKIP_IF_NO_INPUTS();
    SKIP_IF_NO_OUTPUTS();
    SKIP_IF_NO_WORKER();
    
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Create fast producer and slow consumer to test backpressure
    ControllableProducer_t producer;
    ControllableProducerConfig_t prod_config = {
        .name = "fast_producer",
        .timeout_us = 1000000,
        .samples_per_second = 1000000,  // 1M samples/sec
        .batch_size = 1024,
        .pattern = PATTERN_CONSTANT,
        .constant_value = 42.0,
        .max_batches = 50
    };
    
    err = controllable_producer_init(&producer, prod_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    ControllableConsumer_t consumer;
    ControllableConsumerConfig_t cons_config = {
        .name = "slow_consumer",
        .buff_config = {
            .dtype = (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) ? g_fut->input_buffers[0]->dtype : DTYPE_FLOAT,
            .batch_capacity_expo = 4,  // Small buffer to trigger backpressure
            .ring_capacity_expo = 4,   // Small ring
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000,
        .process_delay_us = 10000  // 10ms per batch - very slow
    };
    
    err = controllable_consumer_init(&consumer, cons_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Connect pipeline
    err = filt_sink_connect(&producer.base, 0, g_fut->input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Start pipeline
    err = filt_start(&producer.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(&consumer.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Let it run for a bit
    usleep(100000); // 100ms
    
    // Stop pipeline
    filt_stop(&producer.base);
    filt_stop(g_fut);
    filt_stop(&consumer.base);
    
    // Verify no data loss - all produced batches should be consumed
    size_t produced = atomic_load(&producer.batches_produced);
    size_t consumed = atomic_load(&consumer.batches_consumed);
    
    // Allow for some batches in flight
    TEST_ASSERT_INT_WITHIN_MESSAGE(5, produced, consumed, 
                                   "Backpressure caused data loss");
    
    // Cleanup
    filt_deinit(&producer.base);
    filt_deinit(&consumer.base);
    filt_deinit(g_fut);
}

//=============================================================================
// Error Handling Compliance Tests
//=============================================================================

void test_error_invalid_config(void) {
    // Test with NULL config if filter requires config
    Bp_EC err = g_fut_init(g_fut, NULL);
    if (err != Bp_EC_OK) {
        // Filter properly rejected NULL config
        TEST_PASS_MESSAGE("Filter correctly rejected NULL config");
    } else {
        // Filter accepted NULL config - that's fine too
        TEST_PASS_MESSAGE("Filter accepts NULL config");
        filt_deinit(g_fut);
    }
}

void test_error_timeout(void) {
    SKIP_IF_NO_INPUTS();
    SKIP_IF_NO_WORKER();
    
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Start filter with no producer - should handle timeouts gracefully
    err = filt_start(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Let it run and timeout a few times
    usleep(50000); // 50ms
    
    // Stop should work cleanly even with timeouts
    err = filt_stop(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // No worker errors expected from timeouts
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, g_fut->worker_err_info.ec,
                              "Timeout caused worker error");
    
    filt_deinit(g_fut);
}

//=============================================================================
// Threading Compliance Tests
//=============================================================================

void test_thread_worker_lifecycle(void) {
    SKIP_IF_NO_WORKER();
    
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Verify worker thread not running
    TEST_ASSERT_FALSE(atomic_load(&g_fut->running));
    
    // Start should create worker thread
    err = filt_start(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    TEST_ASSERT_TRUE(atomic_load(&g_fut->running));
    
    // Give thread time to actually start
    usleep(1000);
    
    // Stop should terminate worker thread
    err = filt_stop(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    TEST_ASSERT_FALSE(atomic_load(&g_fut->running));
    
    // Verify clean termination
    TEST_ASSERT_EQUAL(Bp_EC_OK, g_fut->worker_err_info.ec);
    
    filt_deinit(g_fut);
}

void test_thread_shutdown_sync(void) {
    SKIP_IF_NO_WORKER();
    SKIP_IF_NO_OUTPUTS();
    
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Create a consumer that will block
    ControllableConsumer_t consumer;
    ControllableConsumerConfig_t cons_config = {
        .name = "blocking_consumer",
        .buff_config = {
            .dtype = (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) ? g_fut->input_buffers[0]->dtype : DTYPE_FLOAT,
            .batch_capacity_expo = 2,  // Very small buffer
            .ring_capacity_expo = 2,   // Very small ring
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 10000000,  // 10 second timeout
        .process_delay_us = 1000000  // 1 second per batch - extremely slow
    };
    
    err = controllable_consumer_init(&consumer, cons_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Connect
    err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Start both
    err = filt_start(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(&consumer.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Let them run briefly
    usleep(10000); // 10ms
    
    // Now stop - this tests force_return mechanism
    err = filt_stop(&consumer.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_stop(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Both should have stopped cleanly
    TEST_ASSERT_FALSE(atomic_load(&g_fut->running));
    TEST_ASSERT_FALSE(atomic_load(&consumer.base.running));
    
    // Cleanup
    filt_deinit(&consumer.base);
    filt_deinit(g_fut);
}

//=============================================================================
// Performance Compliance Tests
//=============================================================================

void test_perf_throughput(void) {
    SKIP_IF_NO_INPUTS();
    SKIP_IF_NO_OUTPUTS();
    SKIP_IF_NO_WORKER();
    
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // High-rate producer
    ControllableProducer_t producer;
    ControllableProducerConfig_t prod_config = {
        .name = "perf_producer",
        .timeout_us = 1000000,
        .samples_per_second = 10000000,  // 10M samples/sec target
        .batch_size = 1024,
        .pattern = PATTERN_CONSTANT,
        .constant_value = 1.0,
        .max_batches = 1000  // ~1M samples
    };
    
    err = controllable_producer_init(&producer, prod_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Fast consumer
    ControllableConsumer_t consumer;
    ControllableConsumerConfig_t cons_config = {
        .name = "perf_consumer",
        .buff_config = {
            .dtype = (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) ? g_fut->input_buffers[0]->dtype : DTYPE_FLOAT,
            .batch_capacity_expo = 10,  // Large batches
            .ring_capacity_expo = 8,    // Large ring
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000,
        .process_delay_us = 0  // No artificial delay
    };
    
    err = controllable_consumer_init(&consumer, cons_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Connect pipeline
    err = filt_sink_connect(&producer.base, 0, g_fut->input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Measure time
    uint64_t start_ns = get_time_ns();
    
    // Start pipeline
    err = filt_start(&producer.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(&consumer.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Wait for completion
    while (atomic_load(&consumer.batches_consumed) < prod_config.max_batches) {
        usleep(1000);
    }
    
    uint64_t elapsed_ns = get_time_ns() - start_ns;
    
    // Stop pipeline
    filt_stop(&producer.base);
    filt_stop(g_fut);
    filt_stop(&consumer.base);
    
    // Calculate throughput
    size_t total_samples = atomic_load(&consumer.samples_consumed);
    double throughput = (double)total_samples * 1e9 / elapsed_ns;
    
    g_last_perf_metrics.throughput_samples_per_sec = throughput;
    g_last_perf_metrics.batches_processed = atomic_load(&consumer.batches_consumed);
    
    // Record in performance report
    char buf[256];
    snprintf(buf, sizeof(buf), "  Throughput: %.2f Msamples/sec\n", throughput / 1e6);
    strcat(g_perf_report, buf);
    snprintf(buf, sizeof(buf), "  Batches: %zu\n", g_last_perf_metrics.batches_processed);
    strcat(g_perf_report, buf);
    snprintf(buf, sizeof(buf), "  Time: %.2f ms\n", elapsed_ns / 1e6);
    strcat(g_perf_report, buf);
    
    // Performance assertion - expect at least 100K samples/sec for any filter
    TEST_ASSERT_GREATER_THAN_MESSAGE(100000, throughput,
                                     "Filter throughput below minimum threshold");
    
    // Cleanup
    filt_deinit(&producer.base);
    filt_deinit(&consumer.base);
    filt_deinit(g_fut);
}

// TODO: Implement passthrough_metrics filter
#if 0
void test_perf_latency(void) {
    SKIP_IF_NO_INPUTS();
    SKIP_IF_NO_OUTPUTS();
    SKIP_IF_NO_WORKER();
    
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Create passthrough metrics filter to measure latency
    PassthroughMetrics_t passthrough;
    PassthroughMetricsConfig_t pass_config = {
        .name = "latency_measure",
        .buff_config = {
            .dtype = (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) ? g_fut->input_buffers[0]->dtype : DTYPE_FLOAT,
            .batch_capacity_expo = 6,
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000,
        .measure_latency = true,
        .measure_queue_depth = false
    };
    
    err = passthrough_metrics_init(&passthrough, pass_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Low-rate producer for accurate latency measurement
    ControllableProducer_t producer;
    ControllableProducerConfig_t prod_config = {
        .name = "latency_producer",
        .timeout_us = 1000000,
        .samples_per_second = 1000,  // 1K samples/sec
        .batch_size = 64,
        .pattern = PATTERN_SEQUENTIAL,
        .max_batches = 100
    };
    
    err = controllable_producer_init(&producer, prod_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Connect: producer -> filter -> passthrough
    err = filt_sink_connect(&producer.base, 0, g_fut->input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_sink_connect(g_fut, 0, passthrough.base.input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Start pipeline
    err = filt_start(&producer.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(&passthrough.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Wait for completion
    while (atomic_load(&passthrough.batches_processed) < prod_config.max_batches) {
        usleep(10000);
    }
    
    // Stop pipeline
    filt_stop(&producer.base);
    filt_stop(g_fut);
    filt_stop(&passthrough.base);
    
    // Get latency metrics
    size_t batches = atomic_load(&passthrough.batches_processed);
    uint64_t total_latency = atomic_load(&passthrough.total_latency_ns);
    uint64_t max_latency = atomic_load(&passthrough.max_latency_ns);
    uint64_t min_latency = atomic_load(&passthrough.min_latency_ns);
    
    double avg_latency_ns = batches > 0 ? (double)total_latency / batches : 0;
    
    g_last_perf_metrics.latency_ns_p50 = avg_latency_ns;
    g_last_perf_metrics.latency_ns_p99 = (double)max_latency;
    
    // Record in performance report
    char buf[256];
    snprintf(buf, sizeof(buf), "  Avg Latency: %.2f μs\n", avg_latency_ns / 1000.0);
    strcat(g_perf_report, buf);
    snprintf(buf, sizeof(buf), "  Min Latency: %.2f μs\n", min_latency / 1000.0);
    strcat(g_perf_report, buf);
    snprintf(buf, sizeof(buf), "  Max Latency: %.2f μs\n", max_latency / 1000.0);
    strcat(g_perf_report, buf);
    
    // Performance assertion - expect less than 1ms average latency
    TEST_ASSERT_LESS_THAN_MESSAGE(1000000, avg_latency_ns,
                                  "Filter latency above threshold");
    
    // Cleanup
    filt_deinit(&producer.base);
    filt_deinit(&passthrough.base);
    filt_deinit(g_fut);
}
#endif

//=============================================================================
// Test Registration and Main
//=============================================================================

// Example default configurations for testing
static ControllableProducerConfig_t default_producer_config = {
    .name = "default_producer",
    .timeout_us = 1000000,
    .samples_per_second = 1000,
    .batch_size = 64,
    .pattern = PATTERN_SEQUENTIAL,
    .max_batches = 0
};

static ControllableConsumerConfig_t default_consumer_config = {
    .name = "default_consumer",
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 6,
        .ring_capacity_expo = 8,
        .overflow_behaviour = OVERFLOW_BLOCK
    },
    .timeout_us = 1000000,
    .process_delay_us = 0,
    .validate_sequence = false,
    .validate_timing = false
};

// All compliance tests as Unity test functions
static void (*compliance_tests[])(void) = {
    // Lifecycle tests
    test_lifecycle_basic,
    test_lifecycle_with_worker,
    test_lifecycle_restart,
    test_lifecycle_errors,
    
    // Connection tests
    test_connection_single_sink,
    test_connection_multi_sink,
    test_connection_type_safety,
    
    // Data flow tests
    test_dataflow_passthrough,
    test_dataflow_backpressure,
    
    // Error handling tests
    test_error_invalid_config,
    test_error_timeout,
    
    // Threading tests
    test_thread_worker_lifecycle,
    test_thread_shutdown_sync,
    
    // Performance tests
    test_perf_throughput,
    // test_perf_latency,  // TODO: Implement passthrough_metrics filter
};

int main(int argc, char* argv[]) {
    // Command line options
    const char* filter_pattern = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter_pattern = argv[++i];
        }
    }
    
    // Register filters to test
    FilterRegistration_t filters[] = {
        // Example: Test the mock filters themselves
        {
            .name = "ControllableProducer",
            .filter_size = sizeof(ControllableProducer_t),
            .init = (FilterInitFunc)controllable_producer_init,
            .default_config = &default_producer_config,
            .config_size = sizeof(ControllableProducerConfig_t)
        },
        {
            .name = "ControllableConsumer",
            .filter_size = sizeof(ControllableConsumer_t),
            .init = (FilterInitFunc)controllable_consumer_init,
            .default_config = &default_consumer_config,
            .config_size = sizeof(ControllableConsumerConfig_t)
        },
        // Add more filters here as needed
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
        
        for (size_t i = 0; i < sizeof(compliance_tests) / sizeof(compliance_tests[0]); i++) {
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
           g_n_filters, sizeof(compliance_tests) / sizeof(compliance_tests[0]));
    
    return 0;
}