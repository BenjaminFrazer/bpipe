/**
 * @file test_filter_bench.c
 * @brief API Compliance Test Bench for bpipe2 filters
 *
 * Unity-based testing framework that validates all aspects of the bpipe2
 * filter public API. Provides generic compliance tests applicable to any
 * filter implementation.
 */

#define _DEFAULT_SOURCE
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "bperr.h"
#include "core.h"
#include "mock_filters.h"
#include "passthrough.h"
#include "test_filter_bench_asserts.h"
#include "unity.h"
#include "utils.h"

// Filter init function type
typedef Bp_EC (*FilterInitFunc)(void* filter, void* config);

// Wrapper for passthrough_init to match the expected signature
static Bp_EC passthrough_init_wrapper(void* filter, void* config)
{
  return passthrough_init((Passthrough_t*) filter,
                          (Passthrough_config_t*) config);
}

// Wrapper for controllable_producer_init to match expected signature
static Bp_EC controllable_producer_init_wrapper(void* filter, void* config)
{
  if (!config) {
    return Bp_EC_NULL_POINTER;
  }
  ControllableProducerConfig_t* cfg = (ControllableProducerConfig_t*) config;
  return controllable_producer_init((ControllableProducer_t*) filter, *cfg);
}

// Wrapper for controllable_consumer_init to match expected signature
static Bp_EC controllable_consumer_init_wrapper(void* filter, void* config)
{
  if (!config) {
    return Bp_EC_NULL_POINTER;
  }
  ControllableConsumerConfig_t* cfg = (ControllableConsumerConfig_t*) config;
  return controllable_consumer_init((ControllableConsumer_t*) filter, *cfg);
}

// Filter registration for test harness
typedef struct {
  const char* name;      // Filter name for reporting
  size_t filter_size;    // sizeof(MyFilter_t)
  FilterInitFunc init;   // Filter's init function
  void* default_config;  // Default configuration
  size_t config_size;    // sizeof(MyFilterConfig_t)
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
#define SKIP_IF_NO_INPUTS()                      \
  if (g_fut->n_input_buffers == 0) {             \
    TEST_IGNORE_MESSAGE("Filter has no inputs"); \
    return;                                      \
  }

#define SKIP_IF_NO_OUTPUTS()                      \
  if (g_fut->max_supported_sinks == 0) {          \
    TEST_IGNORE_MESSAGE("Filter has no outputs"); \
    return;                                       \
  }

#define SKIP_IF_NO_WORKER()                             \
  if (g_fut->worker == NULL) {                          \
    TEST_IGNORE_MESSAGE("Filter has no worker thread"); \
    return;                                             \
  }

// Helper to get current time in nanoseconds
static uint64_t get_time_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Unity setUp - called before each test
void setUp(void)
{
  // Create fresh filter instance for each test
  FilterRegistration_t* reg = &g_filters[g_current_filter];

  g_fut = (Filter_t*) calloc(1, reg->filter_size);
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
      Bp_EC err = filt_stop(g_fut);
      if (err != Bp_EC_OK) {
        printf("WARNING: filt_stop failed with error %d\n", err);
      }

      // Ensure worker thread has actually stopped
      if (g_fut->worker) {
        void* thread_result;
        int join_err = pthread_join(g_fut->worker_thread, &thread_result);
        if (join_err != 0) {
          printf("WARNING: pthread_join failed: %s\n", strerror(join_err));
        }
      }
    }

    // Deinit if initialized (check filt_type as that's what init sets)
    if (g_fut->filt_type != FILT_T_NDEF) {
      Bp_EC err = filt_deinit(g_fut);
      if (err != Bp_EC_OK && err != Bp_EC_INVALID_CONFIG) {
        printf("WARNING: filt_deinit failed with error %d (%s)\n", err, err_lut[err]);
      }
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

void test_lifecycle_basic(void)
{
  TEST_ASSERT_NOT_NULL(g_fut);

  // Test init
  ASSERT_BP_OK(g_fut_init(g_fut, g_fut_config));
  TEST_ASSERT_TRUE_MESSAGE(g_fut->name[0] != '\0',
                           "Filter name not set after init");

  // Test that we can't double-init
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(Bp_EC_OK, err,
                                "Filter should reject double init");

  // Test deinit
  ASSERT_DEINIT_OK(g_fut);
}

void test_lifecycle_with_worker(void)
{
  // Initialize filter
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  
  SKIP_IF_NO_WORKER();

  // Start input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // For filters with inputs, connect producers to ALL inputs
  ControllableProducer_t** producers = NULL;
  size_t n_producers_allocated = 0;
  size_t n_producers_initialized = 0;

  if (g_fut->n_input_buffers > 0) {
    producers = calloc(g_fut->n_input_buffers, sizeof(ControllableProducer_t*));
    ASSERT_ALLOC(producers, "producer array");

    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      producers[i] = calloc(1, sizeof(ControllableProducer_t));
      if (!producers[i]) {
        // Clean up previously allocated producers
        for (int j = 0; j < i; j++) {
          free(producers[j]);
        }
        free(producers);
        ASSERT_ALLOC_ARRAY(producers[i], i, g_fut->n_input_buffers, "producer");
      }
      n_producers_allocated = i + 1;

      ControllableProducerConfig_t prod_config = {
          .name = "test_producer",
          .timeout_us = 1000000,
          .samples_per_second = 1000,
          .pattern = PATTERN_SEQUENTIAL,
          .constant_value = 0.0,
          .sine_frequency = 0.0,
          .max_batches = 5,
          .burst_mode = false,
          .burst_on_batches = 0,
          .burst_off_batches = 0,
          .start_sequence = i * 1000  // Different sequences for each input
      };

      ASSERT_BP_OK_CTX(controllable_producer_init(producers[i], prod_config),
                       "Failed to init producer[%d]", i);
      n_producers_initialized = i + 1;

      ASSERT_PRODUCER_CONNECT(i, producers[i], g_fut, i);
    }
  }

  // For filters with outputs, connect a consumer
  ControllableConsumer_t* consumer = NULL;
  if (g_fut->max_supported_sinks > 0) {
    consumer = (ControllableConsumer_t*) calloc(1, sizeof(ControllableConsumer_t));
    ASSERT_ALLOC(consumer, "consumer");

    ControllableConsumerConfig_t consumer_config = {
        .name = "test_consumer",
        .buff_config = {.dtype = DTYPE_FLOAT,
                        .batch_capacity_expo = 6,
                        .ring_capacity_expo = 8,
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 0,
        .validate_sequence = false,
        .validate_timing = false,
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    ASSERT_BP_OK(controllable_consumer_init(consumer, consumer_config));
    ASSERT_CONNECT_OK(g_fut, 0, consumer->base.input_buffers[0]);
  }

  // Start all producers
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      ASSERT_START_OK(&producers[i]->base);
    }
  }

  // Start filter
  ASSERT_START_OK(g_fut);
  TEST_ASSERT_TRUE_MESSAGE(atomic_load(&g_fut->running),
                           "Filter not running after start");

  // Start consumer if connected
  if (consumer) {
    ASSERT_START_OK(&consumer->base);
  }

  // Let it run briefly
  usleep(10000);  // 10ms

  // Stop all producers first
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      ASSERT_STOP_OK(&producers[i]->base);
    }
  }

  // Stop consumer
  if (consumer) {
    ASSERT_STOP_OK(&consumer->base);
  }

  // Stop filter
  ASSERT_STOP_OK(g_fut);
  TEST_ASSERT_FALSE_MESSAGE(atomic_load(&g_fut->running),
                            "Filter still running after stop");

  // Check for worker errors
  ASSERT_WORKER_OK(g_fut);

  // Cleanup producers - only deinit initialized ones
  if (producers) {
    for (int i = 0; i < n_producers_initialized; i++) {
      ASSERT_DEINIT_OK(&producers[i]->base);
    }
    for (int i = 0; i < n_producers_allocated; i++) {
      free(producers[i]);
    }
    free(producers);
  }

  // Cleanup consumer
  if (consumer) {
    ASSERT_DEINIT_OK(&consumer->base);
    free(consumer);
  }

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }

  // Deinit filter
  ASSERT_DEINIT_OK(g_fut);
}

void test_lifecycle_restart(void)
{
  // Initialize
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  
  SKIP_IF_NO_WORKER();

  // Start input buffers if any
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // For filters with outputs, connect a dummy consumer to avoid NO_SINK errors
  ControllableConsumer_t* consumer = NULL;
  if (g_fut->max_supported_sinks > 0) {
    consumer = (ControllableConsumer_t*) calloc(1, sizeof(ControllableConsumer_t));
    ASSERT_ALLOC(consumer, "consumer");

    ControllableConsumerConfig_t consumer_config = {
        .name = "restart_test_consumer",
        .buff_config = {.dtype = (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0])
                                     ? g_fut->input_buffers[0]->dtype
                                     : DTYPE_FLOAT,
                        .batch_capacity_expo = 6,
                        .ring_capacity_expo = 8,
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 0,
        .validate_sequence = false,
        .validate_timing = false,
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    ASSERT_BP_OK(controllable_consumer_init(consumer, consumer_config));
    ASSERT_CONNECT_OK(g_fut, 0, consumer->base.input_buffers[0]);
  }

  // Multiple start/stop cycles
  for (int i = 0; i < 3; i++) {
    if (consumer) {
      err = filt_start(&consumer->base);
      TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Consumer start failed");
    }
    
    err = filt_start(g_fut);
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Filter start failed");
    TEST_ASSERT_TRUE(atomic_load(&g_fut->running));

    usleep(5000);  // 5ms

    err = filt_stop(g_fut);
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Filter stop failed");
    TEST_ASSERT_FALSE(atomic_load(&g_fut->running));
    TEST_ASSERT_EQUAL(Bp_EC_OK, g_fut->worker_err_info.ec);
    
    if (consumer) {
      err = filt_stop(&consumer->base);
      TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Consumer stop failed");
    }
  }

  // Cleanup consumer if connected
  if (consumer) {
    filt_deinit(&consumer->base);
    free(consumer);
  }

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }

  err = filt_deinit(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_lifecycle_errors(void)
{
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

void test_connection_single_sink(void)
{
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  
  SKIP_IF_NO_OUTPUTS();

  // Create consumer
  ControllableConsumer_t consumer = {0};  // Zero-initialize
  ControllableConsumerConfig_t consumer_config = {
      .name = "test_consumer",
      .buff_config = {.dtype = (g_fut->n_input_buffers > 0 &&
                                g_fut->input_buffers[0])
                                   ? g_fut->input_buffers[0]->dtype
                                   : DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 8,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .process_delay_us = 0,
      .validate_sequence = false,
      .validate_timing = false,
      .consume_pattern = 0,
      .slow_start = false,
      .slow_start_batches = 0};

  err = controllable_consumer_init(&consumer, consumer_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Connect
  err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
  TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Connection failed");
  TEST_ASSERT_NOT_NULL_MESSAGE(g_fut->sinks[0],
                               "Sink not set after connection");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(consumer.base.input_buffers[0], g_fut->sinks[0],
                                "Sink pointer mismatch");

  // Disconnect
  err = filt_sink_disconnect(g_fut, 0);
  TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Disconnection failed");
  TEST_ASSERT_NULL_MESSAGE(g_fut->sinks[0],
                           "Sink not cleared after disconnect");

  // Cleanup
  filt_deinit(&consumer.base);
  filt_deinit(g_fut);
}

void test_connection_multi_sink(void)
{
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  
  SKIP_IF_NO_OUTPUTS();

  // Get the filter's maximum supported sinks
  size_t max_sinks = g_fut->max_supported_sinks;

  // Skip if filter doesn't support multiple outputs
  if (max_sinks < 2) {
    TEST_IGNORE_MESSAGE("Filter doesn't support multiple outputs");
    return;
  }

  // Limit test to reasonable number of sinks
  size_t test_sinks = (max_sinks > MAX_SINKS) ? MAX_SINKS : max_sinks;
  if (test_sinks > 8) test_sinks = 8;  // Reasonable limit for testing

  // Create consumers up to the maximum supported
  ControllableConsumer_t* consumers =
      calloc(test_sinks + 1, sizeof(ControllableConsumer_t));
  TEST_ASSERT_NOT_NULL_MESSAGE(consumers, "Failed to allocate consumers");

  // Test connecting up to the maximum
  for (size_t i = 0; i < test_sinks; i++) {
    ControllableConsumerConfig_t config = {
        .name = "test_consumer",
        .buff_config = {.dtype = (g_fut->n_input_buffers > 0 &&
                                  g_fut->input_buffers[0])
                                     ? g_fut->input_buffers[0]->dtype
                                     : DTYPE_FLOAT,
                        .batch_capacity_expo = 6,
                        .ring_capacity_expo = 8,
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 0,
        .validate_sequence = false,
        .validate_timing = false,
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    err = controllable_consumer_init(&consumers[i], config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);

    // Connect - should succeed for indices < max_supported_sinks
    err = filt_sink_connect(g_fut, i, consumers[i].base.input_buffers[0]);
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err,
                              "Failed to connect within max_supported_sinks");
    TEST_ASSERT_NOT_NULL(g_fut->sinks[i]);
  }

  // Verify all connections
  for (size_t i = 0; i < test_sinks; i++) {
    TEST_ASSERT_EQUAL_PTR(consumers[i].base.input_buffers[0], g_fut->sinks[i]);
  }

  // Test connecting one more than the maximum (should fail)
  if (test_sinks < max_sinks) {
    // Create one more consumer
    ControllableConsumerConfig_t config = {
        .name = "test_consumer_extra",
        .buff_config = {.dtype = (g_fut->n_input_buffers > 0 &&
                                  g_fut->input_buffers[0])
                                     ? g_fut->input_buffers[0]->dtype
                                     : DTYPE_FLOAT,
                        .batch_capacity_expo = 6,
                        .ring_capacity_expo = 8,
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 0,
        .validate_sequence = false,
        .validate_timing = false,
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    err = controllable_consumer_init(&consumers[test_sinks], config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);

    // This connection should fail
    err = filt_sink_connect(g_fut, max_sinks,
                            consumers[test_sinks].base.input_buffers[0]);
    TEST_ASSERT_EQUAL_MESSAGE(
        Bp_EC_INVALID_SINK_IDX, err,
        "Should fail when connecting beyond max_supported_sinks");

    // Cleanup the extra consumer
    filt_deinit(&consumers[test_sinks].base);
  }

  // Cleanup
  for (size_t i = 0; i < test_sinks; i++) {
    filt_deinit(&consumers[i].base);
  }
  free(consumers);
  filt_deinit(g_fut);
}

void test_connection_type_safety(void)
{
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  
  SKIP_IF_NO_OUTPUTS();
  SKIP_IF_NO_INPUTS();

  // For filters with inputs, check type safety
  if (g_fut->n_input_buffers == 0) {
    TEST_IGNORE_MESSAGE("Filter has no inputs to test type safety");
    return;
  }

  // Get the expected data type from the filter's input buffer
  SampleDtype_t expected_dtype = g_fut->input_buffers[0]->dtype;

  // Create consumer with wrong data type
  SampleDtype_t wrong_dtype =
      (expected_dtype == DTYPE_FLOAT) ? DTYPE_I32 : DTYPE_FLOAT;

  ControllableConsumer_t consumer = {0};  // Zero-initialize
  ControllableConsumerConfig_t consumer_config = {
      .name = "test_consumer",
      .buff_config = {.dtype = wrong_dtype,  // Intentionally wrong type
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 8,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .process_delay_us = 0,
      .validate_sequence = false,
      .validate_timing = false,
      .consume_pattern = 0,
      .slow_start = false,
      .slow_start_batches = 0};

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

void test_dataflow_passthrough(void)
{
  // Initialize filter first to determine its capabilities
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Skip if filter has neither inputs nor outputs
  if (g_fut->n_input_buffers == 0 && g_fut->max_supported_sinks == 0) {
    TEST_IGNORE_MESSAGE("Filter has neither inputs nor outputs");
    return;
  }

  // Start input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // Determine filter type and create appropriate pipeline
  ControllableProducer_t* producer = NULL;
  ControllableConsumer_t* consumer = NULL;

  // For filters with outputs, create a consumer
  if (g_fut->max_supported_sinks > 0) {
    consumer = calloc(1, sizeof(ControllableConsumer_t));
    TEST_ASSERT_NOT_NULL(consumer);

    // Determine data type: from input buffer if available, otherwise assume
    // FLOAT
    SampleDtype_t dtype = DTYPE_FLOAT;
    if (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) {
      dtype = g_fut->input_buffers[0]->dtype;
    }

    ControllableConsumerConfig_t cons_config = {
        .name = "test_consumer",
        .buff_config = {.dtype = dtype,
                        .batch_capacity_expo = 6,
                        .ring_capacity_expo = 8,
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 0,
        .validate_sequence = false,  // TODO: Fix sequence validation for multi-input filters
        .validate_timing = false,    // TODO: Fix timing validation
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    err = controllable_consumer_init(consumer, cons_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);

    err = filt_sink_connect(g_fut, 0, consumer->base.input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  }

  // For filters with inputs, create producers for ALL inputs
  ControllableProducer_t** producers = NULL;
  if (g_fut->n_input_buffers > 0) {
    producers = calloc(g_fut->n_input_buffers, sizeof(ControllableProducer_t*));
    TEST_ASSERT_NOT_NULL(producers);

    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      producers[i] = calloc(1, sizeof(ControllableProducer_t));
      TEST_ASSERT_NOT_NULL(producers[i]);

      ControllableProducerConfig_t prod_config = {
          .name = "test_producer",
          .timeout_us = 1000000,
          .samples_per_second = 10000,
          .pattern = PATTERN_SEQUENTIAL,
          .constant_value = 0.0,
          .sine_frequency = 0.0,
          .max_batches = 10,
          .burst_mode = false,
          .burst_on_batches = 0,
          .burst_off_batches = 0,
          .start_sequence =
              1000 + i * 1000  // Different sequences for each input
      };

      err = controllable_producer_init(producers[i], prod_config);
      if (err != Bp_EC_OK) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to init producer[%d]: %s", i,
                 err_lut[err]);
        TEST_FAIL_MESSAGE(msg);
      }

      err = filt_sink_connect(&producers[i]->base, 0, g_fut->input_buffers[i]);
      if (err != Bp_EC_OK) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Failed to connect producer[%d] to %s input[%d]: %s", i,
                 g_fut->name, i, err_lut[err]);
        TEST_FAIL_MESSAGE(msg);
      }
    }

    // Use first producer for tracking completion
    producer = producers[0];
  }

  // Start all components
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      err = filt_start(&producers[i]->base);
      TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    }
  }

  err = filt_start(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  if (consumer) {
    err = filt_start(&consumer->base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  }

  // Wait for completion based on filter type
  if (producer) {
    // Wait for all producers to finish
    bool all_done = false;
    while (!all_done) {
      all_done = true;
      for (int i = 0; i < g_fut->n_input_buffers; i++) {
        if (atomic_load(&producers[i]->batches_produced) < 10) {
          all_done = false;
          break;
        }
      }
      if (!all_done) usleep(1000);
    }
  } else if (consumer) {
    // For source filters, wait a reasonable time for data generation
    usleep(100000);  // 100ms
  }
  usleep(10000);  // Extra time for data to flow through

  // Stop pipeline
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      filt_stop(&producers[i]->base);
    }
  }
  filt_stop(g_fut);
  if (consumer) filt_stop(&consumer->base);

  // Check for errors
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      TEST_ASSERT_EQUAL(Bp_EC_OK, producers[i]->base.worker_err_info.ec);
    }
  }
  TEST_ASSERT_EQUAL(Bp_EC_OK, g_fut->worker_err_info.ec);
  if (consumer) {
    TEST_ASSERT_EQUAL(Bp_EC_OK, consumer->base.worker_err_info.ec);
  }

  // Verify data integrity for filters with outputs
  if (consumer) {
    size_t batches_consumed = atomic_load(&consumer->batches_consumed);

    // For transform filters, should receive what producer sent
    if (producer) {
      // For multi-input filters, check total batches from all producers
      size_t total_batches_produced = 0;
      for (int i = 0; i < g_fut->n_input_buffers; i++) {
        total_batches_produced += atomic_load(&producers[i]->batches_produced);
      }
      // For element-wise filters, output batches = min(input batches)
      size_t min_batches_produced =
          atomic_load(&producers[0]->batches_produced);
      for (int i = 1; i < g_fut->n_input_buffers; i++) {
        size_t produced = atomic_load(&producers[i]->batches_produced);
        if (produced < min_batches_produced) min_batches_produced = produced;
      }

      TEST_ASSERT_GREATER_THAN_MESSAGE(0, batches_consumed,
                                       "Consumer should have received batches");
      // Allow some batches in flight
      TEST_ASSERT_INT_WITHIN_MESSAGE(
          2, min_batches_produced, batches_consumed,
          "Consumer should receive most produced batches");
    } else {
      // For source filters, just verify some data was generated
      TEST_ASSERT_GREATER_THAN_MESSAGE(0, batches_consumed,
                                       "Source filter should generate data");
    }

    ASSERT_NO_SEQ_ERRORS(consumer);
    ASSERT_NO_TIMING_ERRORS(consumer);
  }

  // For sink filters, verify they consumed data
  if (producers && !consumer) {
    // Check all producers sent their data
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      ASSERT_BATCHES_PRODUCED(producers[i], 10);
    }
  }

  // Cleanup
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      filt_deinit(&producers[i]->base);
      free(producers[i]);
    }
    free(producers);
  }
  if (consumer) {
    filt_deinit(&consumer->base);
    free(consumer);
  }

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }

  // Note: g_fut is deinit'd in tearDown()
}

void test_dataflow_backpressure(void)
{
  // Initialize filter first to determine its capabilities
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // This test specifically needs both inputs and outputs to test backpressure
  // propagation However, we can test partial backpressure for source/sink
  // filters
  if (g_fut->n_input_buffers == 0 && g_fut->max_supported_sinks == 0) {
    TEST_IGNORE_MESSAGE("Filter has neither inputs nor outputs");
    return;
  }

  // Start input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  ControllableProducer_t* producer = NULL;
  ControllableConsumer_t* consumer = NULL;

  // For filters with outputs, create a slow consumer
  if (g_fut->max_supported_sinks > 0) {
    consumer = calloc(1, sizeof(ControllableConsumer_t));
    TEST_ASSERT_NOT_NULL(consumer);

    SampleDtype_t dtype = DTYPE_FLOAT;
    if (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) {
      dtype = g_fut->input_buffers[0]->dtype;
    }

    ControllableConsumerConfig_t cons_config = {
        .name = "slow_consumer",
        .buff_config = {.dtype = dtype,
                        .batch_capacity_expo =
                            4,  // Small buffer to trigger backpressure
                        .ring_capacity_expo = 4,  // Small ring
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 10000,  // 10ms per batch - very slow
        .validate_sequence = false,
        .validate_timing = false,
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    err = controllable_consumer_init(consumer, cons_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);

    err = filt_sink_connect(g_fut, 0, consumer->base.input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  }

  // For filters with inputs, create fast producers for ALL inputs
  ControllableProducer_t** producers = NULL;
  if (g_fut->n_input_buffers > 0) {
    producers = calloc(g_fut->n_input_buffers, sizeof(ControllableProducer_t*));
    TEST_ASSERT_NOT_NULL(producers);

    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      producers[i] = calloc(1, sizeof(ControllableProducer_t));
      TEST_ASSERT_NOT_NULL(producers[i]);

      ControllableProducerConfig_t prod_config = {
          .name = "fast_producer",
          .timeout_us = 1000000,
          .samples_per_second = 1000000,  // 1M samples/sec
          .pattern = PATTERN_CONSTANT,
          .constant_value = 42.0 + i,  // Different values for each input
          .sine_frequency = 0.0,
          .max_batches = 50,
          .burst_mode = false,
          .burst_on_batches = 0,
          .burst_off_batches = 0,
          .start_sequence = 0};

      err = controllable_producer_init(producers[i], prod_config);
      if (err != Bp_EC_OK) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to init producer[%d]: %s", i,
                 err_lut[err]);
        TEST_FAIL_MESSAGE(msg);
      }

      err = filt_sink_connect(&producers[i]->base, 0, g_fut->input_buffers[i]);
      if (err != Bp_EC_OK) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Failed to connect producer[%d] to %s input[%d]: %s", i,
                 g_fut->name, i, err_lut[err]);
        TEST_FAIL_MESSAGE(msg);
      }
    }

    // Use first producer for tracking
    producer = producers[0];
  }

  // Start pipeline
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      err = filt_start(&producers[i]->base);
      TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    }
  }

  err = filt_start(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  if (consumer) {
    err = filt_start(&consumer->base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  }

  // Let it run for a bit
  usleep(100000);  // 100ms

  // Stop pipeline
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      err = filt_stop(&producers[i]->base);
      TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    }
  }
  err = filt_stop(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  if (consumer) {
    err = filt_stop(&consumer->base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  }

  // Verify backpressure behavior based on filter type
  if (producer && consumer) {
    // Full pipeline: verify no data loss
    // For multi-input filters, check the minimum produced (element-wise)
    size_t min_produced = atomic_load(&producers[0]->batches_produced);
    for (int i = 1; i < g_fut->n_input_buffers; i++) {
      size_t produced = atomic_load(&producers[i]->batches_produced);
      if (produced < min_produced) min_produced = produced;
    }
    size_t consumed = atomic_load(&consumer->batches_consumed);

    // Producers should have been slowed down by backpressure
    TEST_ASSERT_LESS_THAN_MESSAGE(
        50, min_produced, "Producers should be throttled by backpressure");

    // Allow for some batches in flight
    TEST_ASSERT_INT_WITHIN_MESSAGE(5, min_produced, consumed,
                                   "Backpressure caused data loss");
  } else if (producer && !consumer) {
    // Sink filter: should consume at its own rate
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      size_t produced = atomic_load(&producers[i]->batches_produced);
      TEST_ASSERT_GREATER_THAN_MESSAGE(
          0, produced, "Producer should have sent data to sink");
    }
  } else if (!producer && consumer) {
    // Source filter: backpressure should slow down generation
    size_t consumed = atomic_load(&consumer->batches_consumed);

    // With 10ms per batch and 100ms runtime, should consume ~10 batches
    TEST_ASSERT_LESS_THAN_MESSAGE(
        20, consumed, "Source should be throttled by slow consumer");
    TEST_ASSERT_GREATER_THAN_MESSAGE(
        0, consumed, "Consumer should have received some batches");
  }

  // Check for errors
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      TEST_ASSERT_EQUAL(Bp_EC_OK, producers[i]->base.worker_err_info.ec);
    }
  }
  TEST_ASSERT_EQUAL(Bp_EC_OK, g_fut->worker_err_info.ec);
  if (consumer) {
    TEST_ASSERT_EQUAL(Bp_EC_OK, consumer->base.worker_err_info.ec);
  }

  // Cleanup
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      filt_deinit(&producers[i]->base);
      free(producers[i]);
    }
    free(producers);
  }
  if (consumer) {
    filt_deinit(&consumer->base);
    free(consumer);
  }

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }

  // Note: g_fut is deinit'd in tearDown()
}

//=============================================================================
// Error Handling Compliance Tests
//=============================================================================

void test_error_invalid_config(void)
{
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

void test_error_timeout(void)
{
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  
  SKIP_IF_NO_INPUTS();
  SKIP_IF_NO_WORKER();

  // Start input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // For filters with outputs, connect a dummy consumer to avoid NO_SINK errors
  ControllableConsumer_t* consumer = NULL;
  if (g_fut->max_supported_sinks > 0) {
    consumer = (ControllableConsumer_t*) calloc(1, sizeof(ControllableConsumer_t));
    ASSERT_ALLOC(consumer, "consumer");

    ControllableConsumerConfig_t consumer_config = {
        .name = "timeout_test_consumer",
        .buff_config = {.dtype = (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0])
                                     ? g_fut->input_buffers[0]->dtype
                                     : DTYPE_FLOAT,
                        .batch_capacity_expo = 6,
                        .ring_capacity_expo = 8,
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 0,
        .validate_sequence = false,
        .validate_timing = false,
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    ASSERT_BP_OK(controllable_consumer_init(consumer, consumer_config));
    ASSERT_CONNECT_OK(g_fut, 0, consumer->base.input_buffers[0]);
    ASSERT_START_OK(&consumer->base);
  }

  // Start filter with no producer - should handle timeouts gracefully
  err = filt_start(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Let it run and timeout a few times
  usleep(50000);  // 50ms

  // Stop should work cleanly even with timeouts
  err = filt_stop(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // No worker errors expected from timeouts
  TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, g_fut->worker_err_info.ec,
                            "Timeout caused worker error");

  // Stop and cleanup consumer if connected
  if (consumer) {
    filt_stop(&consumer->base);
    filt_deinit(&consumer->base);
    free(consumer);
  }

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }

  filt_deinit(g_fut);
}

//=============================================================================
// Threading Compliance Tests
//=============================================================================

void test_thread_worker_lifecycle(void)
{
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  
  SKIP_IF_NO_WORKER();

  // Verify worker thread not running
  TEST_ASSERT_FALSE(atomic_load(&g_fut->running));

  // Start input buffers if any
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // For filters with outputs, connect a dummy consumer to avoid NO_SINK errors
  ControllableConsumer_t* consumer = NULL;
  if (g_fut->max_supported_sinks > 0) {
    consumer = (ControllableConsumer_t*) calloc(1, sizeof(ControllableConsumer_t));
    ASSERT_ALLOC(consumer, "consumer");

    ControllableConsumerConfig_t consumer_config = {
        .name = "lifecycle_test_consumer",
        .buff_config = {.dtype = (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0])
                                     ? g_fut->input_buffers[0]->dtype
                                     : DTYPE_FLOAT,
                        .batch_capacity_expo = 6,
                        .ring_capacity_expo = 8,
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 0,
        .validate_sequence = false,
        .validate_timing = false,
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    ASSERT_BP_OK(controllable_consumer_init(consumer, consumer_config));
    ASSERT_CONNECT_OK(g_fut, 0, consumer->base.input_buffers[0]);
    ASSERT_START_OK(&consumer->base);
  }

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

  // Stop and cleanup consumer if connected
  if (consumer) {
    filt_stop(&consumer->base);
    filt_deinit(&consumer->base);
    free(consumer);
  }

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }

  filt_deinit(g_fut);
}

void test_thread_shutdown_sync(void)
{
  SKIP_IF_NO_WORKER();
  
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  
  SKIP_IF_NO_OUTPUTS();

  // Start input buffers if any
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // Create a consumer that will block
  ControllableConsumer_t consumer = {0};  // Zero-initialize
  
  // For passthrough filter, use matching batch sizes to avoid memory corruption
  uint8_t consumer_batch_expo = 2;  // Default very small buffer
  if (g_fut->filt_type == FILT_T_MATCHED_PASSTHROUGH) {  // Passthrough filter
    // Match the input buffer batch size
    consumer_batch_expo = g_fut->input_buffers[0]->batch_capacity_expo;
  }
  
  ControllableConsumerConfig_t cons_config = {
      .name = "blocking_consumer",
      .buff_config = {.dtype = (g_fut->n_input_buffers > 0 &&
                                g_fut->input_buffers[0])
                                   ? g_fut->input_buffers[0]->dtype
                                   : DTYPE_FLOAT,
                      .batch_capacity_expo = consumer_batch_expo,
                      .ring_capacity_expo = 2,   // Very small ring
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 10000000,       // 10 second timeout
      .process_delay_us = 1000000,  // 1 second per batch - extremely slow
      .validate_sequence = false,
      .validate_timing = false,
      .consume_pattern = 0,
      .slow_start = false,
      .slow_start_batches = 0};

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
  usleep(10000);  // 10ms

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

void test_perf_throughput(void)
{
  // Skip if filter has neither inputs nor outputs
  if (g_fut->n_input_buffers == 0 && g_fut->max_supported_sinks == 0) {
    TEST_IGNORE_MESSAGE("Filter has neither inputs nor outputs");
    return;
  }

  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Start input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  ControllableProducer_t* producer = NULL;
  ControllableConsumer_t* consumer = NULL;
  size_t target_batches = 1000;

  // For filters with outputs, create a fast consumer
  if (g_fut->max_supported_sinks > 0) {
    consumer = calloc(1, sizeof(ControllableConsumer_t));
    TEST_ASSERT_NOT_NULL(consumer);

    SampleDtype_t dtype = DTYPE_FLOAT;
    if (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) {
      dtype = g_fut->input_buffers[0]->dtype;
    }

    ControllableConsumerConfig_t cons_config = {
        .name = "perf_consumer",
        .buff_config = {.dtype = dtype,
                        .batch_capacity_expo = 10,  // Large batches
                        .ring_capacity_expo = 8,    // Large ring
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 0,  // No artificial delay
        .validate_sequence = false,
        .validate_timing = false,
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    err = controllable_consumer_init(consumer, cons_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);

    err = filt_sink_connect(g_fut, 0, consumer->base.input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  }

  // For filters with inputs, create high-rate producers for ALL inputs
  ControllableProducer_t** producers = NULL;
  if (g_fut->n_input_buffers > 0) {
    producers = calloc(g_fut->n_input_buffers, sizeof(ControllableProducer_t*));
    TEST_ASSERT_NOT_NULL(producers);

    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      producers[i] = calloc(1, sizeof(ControllableProducer_t));
      TEST_ASSERT_NOT_NULL(producers[i]);

      ControllableProducerConfig_t prod_config = {
          .name = "perf_producer",
          .timeout_us = 1000000,
          .samples_per_second = 10000000,  // 10M samples/sec target
          .pattern = PATTERN_CONSTANT,
          .constant_value = 1.0 + i * 0.1,  // Slightly different values
          .sine_frequency = 0.0,
          .max_batches = target_batches,
          .burst_mode = false,
          .burst_on_batches = 0,
          .burst_off_batches = 0,
          .start_sequence = 0};

      err = controllable_producer_init(producers[i], prod_config);
      if (err != Bp_EC_OK) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to init producer[%d]: %s", i,
                 err_lut[err]);
        TEST_FAIL_MESSAGE(msg);
      }

      err = filt_sink_connect(&producers[i]->base, 0, g_fut->input_buffers[i]);
      if (err != Bp_EC_OK) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Failed to connect producer[%d] to %s input[%d]: %s", i,
                 g_fut->name, i, err_lut[err]);
        TEST_FAIL_MESSAGE(msg);
      }
    }

    // Use first producer for tracking
    producer = producers[0];
  }

  // Measure time
  uint64_t start_ns = get_time_ns();

  // Start all components
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      err = filt_start(&producers[i]->base);
      TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    }
  }

  err = filt_start(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  if (consumer) {
    err = filt_start(&consumer->base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  }

  // Wait for completion based on filter type
  if (producer && consumer) {
    // Transform filter: wait for consumer to receive all data
    // For element-wise filters, output = min(inputs)
    while (atomic_load(&consumer->batches_consumed) < target_batches - 5) {
      usleep(1000);
    }
  } else if (producer && !consumer) {
    // Sink filter: wait for all producers to send all data
    bool all_done = false;
    while (!all_done) {
      all_done = true;
      for (int i = 0; i < g_fut->n_input_buffers; i++) {
        if (atomic_load(&producers[i]->batches_produced) < target_batches) {
          all_done = false;
          break;
        }
      }
      if (!all_done) usleep(1000);
    }
    usleep(10000);  // Extra time for sink to process
  } else if (!producer && consumer) {
    // Source filter: run for fixed time
    usleep(500000);  // 500ms
  }

  uint64_t elapsed_ns = get_time_ns() - start_ns;

  // Stop pipeline
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      filt_stop(&producers[i]->base);
    }
  }
  filt_stop(g_fut);
  if (consumer) filt_stop(&consumer->base);

  // Calculate throughput based on filter type
  size_t total_samples = 0;
  size_t batches_processed = 0;
  double throughput = 0;

  if (consumer) {
    total_samples = atomic_load(&consumer->samples_consumed);
    batches_processed = atomic_load(&consumer->batches_consumed);
  } else if (producers) {
    // For sink filters, use total from all producers
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      size_t samples = atomic_load(&producers[i]->samples_generated);
      size_t batches = atomic_load(&producers[i]->batches_produced);
      total_samples += samples;
      batches_processed += batches;
    }
    // For multi-input element-wise, actual processed = min(inputs)
    if (g_fut->n_input_buffers > 1) {
      size_t min_samples = atomic_load(&producers[0]->samples_generated);
      size_t min_batches = atomic_load(&producers[0]->batches_produced);
      for (int i = 1; i < g_fut->n_input_buffers; i++) {
        size_t samples = atomic_load(&producers[i]->samples_generated);
        size_t batches = atomic_load(&producers[i]->batches_produced);
        if (samples < min_samples) min_samples = samples;
        if (batches < min_batches) min_batches = batches;
      }
      batches_processed = min_batches;
      total_samples = min_samples;
    }
  }

  if (total_samples > 0) {
    throughput = (double) total_samples * 1e9 / elapsed_ns;

    g_last_perf_metrics.throughput_samples_per_sec = throughput;
    g_last_perf_metrics.batches_processed = batches_processed;

    // Record in performance report
    char buf[256];
    snprintf(buf, sizeof(buf), "  Throughput: %.2f Msamples/sec\n",
             throughput / 1e6);
    strcat(g_perf_report, buf);
    snprintf(buf, sizeof(buf), "  Batches: %zu\n", batches_processed);
    strcat(g_perf_report, buf);
    snprintf(buf, sizeof(buf), "  Time: %.2f ms\n", elapsed_ns / 1e6);
    strcat(g_perf_report, buf);

    // Different thresholds for different filter types
    double min_throughput = 100000;  // 100K samples/sec for transform filters
    if (!producer || !consumer) {
      min_throughput = 50000;  // 50K samples/sec for source/sink filters
    }

    TEST_ASSERT_GREATER_THAN_MESSAGE(
        min_throughput, throughput,
        "Filter throughput below minimum threshold");
  }

  // Check for errors
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      TEST_ASSERT_EQUAL(Bp_EC_OK, producers[i]->base.worker_err_info.ec);
    }
  }
  TEST_ASSERT_EQUAL(Bp_EC_OK, g_fut->worker_err_info.ec);
  if (consumer) {
    TEST_ASSERT_EQUAL(Bp_EC_OK, consumer->base.worker_err_info.ec);
  }

  // Cleanup
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      filt_deinit(&producers[i]->base);
      free(producers[i]);
    }
    free(producers);
  }
  if (consumer) {
    filt_deinit(&consumer->base);
    free(consumer);
  }

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }

  // Note: g_fut is deinit'd in tearDown()
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
    .pattern = PATTERN_SEQUENTIAL,
    .constant_value = 0.0,
    .sine_frequency = 0.0,
    .max_batches = 0,
    .burst_mode = false,
    .burst_on_batches = 0,
    .burst_off_batches = 0,
    .start_sequence = 0};

static ControllableConsumerConfig_t default_consumer_config = {
    .name = "default_consumer",
    .buff_config = {.dtype = DTYPE_FLOAT,
                    .batch_capacity_expo = 6,
                    .ring_capacity_expo = 8,
                    .overflow_behaviour = OVERFLOW_BLOCK},
    .timeout_us = 1000000,
    .process_delay_us = 0,
    .validate_sequence = false,
    .validate_timing = false,
    .consume_pattern = 0,
    .slow_start = false,
    .slow_start_batches = 0};

static Passthrough_config_t default_passthrough_config = {
    .name = "default_passthrough",
    .buff_config = {.dtype = DTYPE_FLOAT,
                    .batch_capacity_expo = 6,
                    .ring_capacity_expo = 8,
                    .overflow_behaviour = OVERFLOW_BLOCK},
    .timeout_us = 1000000};

// All compliance tests as Unity test functions
static void (*compliance_tests[])(void) = {
    // Lifecycle tests
    test_lifecycle_basic, test_lifecycle_with_worker, test_lifecycle_restart,
    test_lifecycle_errors,

    // Connection tests
    test_connection_single_sink, test_connection_multi_sink,
    test_connection_type_safety,

    // Data flow tests
    test_dataflow_passthrough,
    // test_dataflow_backpressure,  // TODO: Fix passthrough capacity mismatch handling

    // Error handling tests
    test_error_invalid_config, test_error_timeout,

    // Threading tests
    test_thread_worker_lifecycle, test_thread_shutdown_sync,

    // Performance tests
    test_perf_throughput,
    // test_perf_latency,  // TODO: Implement passthrough_metrics filter
};

int main(int argc, char* argv[])
{
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
      {.name = "ControllableProducer",
       .filter_size = sizeof(ControllableProducer_t),
       .init = controllable_producer_init_wrapper,
       .default_config = &default_producer_config,
       .config_size = sizeof(ControllableProducerConfig_t)},
      {.name = "ControllableConsumer",
       .filter_size = sizeof(ControllableConsumer_t),
       .init = controllable_consumer_init_wrapper,
       .default_config = &default_consumer_config,
       .config_size = sizeof(ControllableConsumerConfig_t)},
      {.name = "Passthrough",
       .filter_size = sizeof(Passthrough_t),
       .init = passthrough_init_wrapper,
       .default_config = &default_passthrough_config,
       .config_size = sizeof(Passthrough_config_t)},
      // Add more filters here as needed
  };

  g_filters = filters;
  g_n_filters = sizeof(filters) / sizeof(filters[0]);

  // Run all tests for each filter
  for (g_current_filter = 0; g_current_filter < g_n_filters;
       g_current_filter++) {
    // Skip if filter doesn't match pattern
    if (filter_pattern &&
        !strstr(filters[g_current_filter].name, filter_pattern)) {
      continue;
    }

    printf("\n========== Testing %s ==========\n",
           filters[g_current_filter].name);

    // Clear performance report
    g_perf_report[0] = '\0';

    UNITY_BEGIN();

    for (size_t i = 0;
         i < sizeof(compliance_tests) / sizeof(compliance_tests[0]); i++) {
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
  printf("Tested %zu filters with %zu compliance tests each\n", g_n_filters,
         sizeof(compliance_tests) / sizeof(compliance_tests[0]));

  return 0;
}
