#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "batch_buffer.h"
#include "map.h"
#include "test_utils.h"
#include "unity.h"

#define BATCH_CAPACITY_EXPO 8  // 256 samples per batch
#define RING_CAPACITY_EXPO 6   // 63 batches in ring
#define RING_CAPACITY ((1 << RING_CAPACITY_EXPO) - 1)
#define BATCH_CAPACITY (1 << BATCH_CAPACITY_EXPO)

// Additional test configurations
#define SMALL_BATCH_CAPACITY_EXPO \
  4  // 16 samples per batch for edge case testing
#define SMALL_RING_CAPACITY_EXPO 3  // 7 batches in ring for wraparound testing

static const BatchBuffer_config test_config = {
    .dtype = DTYPE_FLOAT,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .ring_capacity_expo = RING_CAPACITY_EXPO,
    .batch_capacity_expo = BATCH_CAPACITY_EXPO,
};

static const struct timespec ts_10ms = {.tv_sec = 0, .tv_nsec = 10000000};

/* Test utilities */
static uint32_t count_in = 0;
static uint32_t count_out = 0;

/* Test map function - identity passthrough */
static Bp_EC test_identity_map(const void* in, void* out, size_t n_samples)
{
  if (!in || !out) return Bp_EC_NULL_POINTER;

  const float* input = (const float*) in;
  float* output = (float*) out;

  for (size_t i = 0; i < n_samples; i++) {
    output[i] = input[i];
  }

  return Bp_EC_OK;
}

/* Test map function - scale by 2.0 */
static Bp_EC test_scale_map(const void* in, void* out, size_t n_samples)
{
  if (!in || !out) return Bp_EC_NULL_POINTER;

  const float* input = (const float*) in;
  float* output = (float*) out;

  for (size_t i = 0; i < n_samples; i++) {
    output[i] = input[i] * 2.0f;
  }

  return Bp_EC_OK;
}

/* Test map function - add offset */
static Bp_EC test_offset_map(const void* in, void* out, size_t n_samples)
{
  if (!in || !out) return Bp_EC_NULL_POINTER;

  const float* input = (const float*) in;
  float* output = (float*) out;

  for (size_t i = 0; i < n_samples; i++) {
    output[i] = input[i] + 100.0f;
  }

  return Bp_EC_OK;
}

/* Test map function that returns error */
static Bp_EC test_error_map(const void* in, void* out, size_t n_samples)
{
  (void) in;
  (void) out;
  (void) n_samples;
  return Bp_EC_INVALID_CONFIG;  // Always return error for testing
}

void setUp(void)
{
  // Reset counters before each test
  count_in = 0;
  count_out = 0;
}

void tearDown(void)
{
  // Cleanup after each test
}

/* Test 1: Initialization & Configuration */
void test_map_init_valid_config(void)
{
  Map_filt_t filter;
  Map_config_t config = {.name = "test_map",
                         .buff_config = test_config,
                         .map_fcn = test_identity_map,
                         .timeout_us = 10000};

  Bp_EC result = map_init(&filter, config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, result);
  TEST_ASSERT_EQUAL_PTR(test_identity_map, filter.map_fcn);
  TEST_ASSERT_EQUAL_STRING("test_map", filter.base.name);
  TEST_ASSERT_EQUAL(FILT_T_MAP, filter.base.filt_type);

  // Cleanup
  filt_deinit(&filter.base);
}

void test_map_init_null_filter(void)
{
  Map_config_t config = {.name = "test_map",
                         .buff_config = test_config,
                         .map_fcn = test_identity_map,
                         .timeout_us = 10000};

  Bp_EC result = map_init(NULL, config);
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, result);
}

void test_map_init_null_function(void)
{
  Map_filt_t filter;
  Map_config_t config = {.name = "test_map",
                         .buff_config = test_config,
                         .map_fcn = NULL,
                         .timeout_us = 10000};

  Bp_EC result = map_init(&filter, config);
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, result);
}

/* Test 2: Single-threaded linear ramp passthrough */
void test_single_threaded_linear_ramp(void)
{
  Map_filt_t filter;
  Map_config_t config = {.name = "test_linear_ramp",
                         .buff_config = test_config,
                         .map_fcn = test_identity_map,
                         .timeout_us = 10000};

  // Initialize filter
  CHECK_ERR(map_init(&filter, config));

  // Create output buffer
  Batch_buff_t output_buffer;
  CHECK_ERR(bb_init(&output_buffer, "test_output", config.buff_config));

  // Connect and start
  CHECK_ERR(filt_sink_connect(&filter.base, 0, &output_buffer));
  CHECK_ERR(bb_start(&output_buffer));
  CHECK_ERR(filt_start(&filter.base));

  // Reset counters
  count_in = 0;
  count_out = 0;
  Bp_EC err;

  // Main test loop - multiple batches across ring buffer
  for (int i = 0; i < (RING_CAPACITY * 2); i++) {
    // Get input batch and populate with incrementing data
    Batch_t* input_batch = bb_get_head(&filter.base.input_buffers[0]);
    TEST_ASSERT_NOT_NULL(input_batch);

    // Fill batch with incrementing float values
    for (int ii = 0; ii < BATCH_CAPACITY; ii++) {
      *((float*) input_batch->data + ii) = (float) count_in;
      count_in++;
    }
    input_batch->head = BATCH_CAPACITY;
    input_batch->t_ns = 1000000 + (i * 10000);  // Incrementing timestamp
    input_batch->period_ns = 1000;

    // Submit input batch
    CHECK_ERR(bb_submit(&filter.base.input_buffers[0], 10000));

    // Try to get any available output batches (non-blocking check first)
    Batch_t* output_batch;
    while ((output_batch = bb_get_tail(&output_buffer, 1, &err)) !=
           NULL) {  // 1us timeout for near-instant check
      // Verify output data matches input
      for (int ii = 0; ii < BATCH_CAPACITY; ii++) {
        float expected = (float) count_out;
        float actual = *((float*) output_batch->data + ii);
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(expected, actual,
                                        "Output data mismatch");
        count_out++;
      }

      // Verify timing preservation (adjusted for which batch this is)
      int batch_idx = (count_out / BATCH_CAPACITY) - 1;
      TEST_ASSERT_EQUAL(1000000 + (batch_idx * 10000), output_batch->t_ns);
      TEST_ASSERT_EQUAL(1000, output_batch->period_ns);

      CHECK_ERR(bb_del_tail(&output_buffer));
    }
  }

  // Get any remaining output batches
  nanosleep(&ts_10ms, NULL);
  Batch_t* output_batch;
  while ((output_batch = bb_get_tail(&output_buffer, 10000, &err)) != NULL) {
    // Verify output data matches input
    for (int ii = 0; ii < BATCH_CAPACITY; ii++) {
      float expected = (float) count_out;
      float actual = *((float*) output_batch->data + ii);
      TEST_ASSERT_EQUAL_FLOAT_MESSAGE(expected, actual, "Output data mismatch");
      count_out++;
    }
    CHECK_ERR(bb_del_tail(&output_buffer));
  }

  // Cleanup
  CHECK_ERR(filt_stop(&filter.base));
  CHECK_ERR(bb_stop(&output_buffer));
  CHECK_ERR(filt_deinit(&filter.base));
  CHECK_ERR(bb_deinit(&output_buffer));
}

/* Test 3: Multi-stage single-threaded passthrough */
void test_multi_stage_single_threaded(void)
{
  Map_filt_t filter1, filter2;
  Map_config_t config = {.name = "test_multi_stage",
                         .buff_config = test_config,
                         .map_fcn = test_identity_map,
                         .timeout_us = 10000};

  // Initialize filters
  CHECK_ERR(map_init(&filter1, config));
  CHECK_ERR(map_init(&filter2, config));

  // Create final output buffer
  Batch_buff_t output_buffer;
  CHECK_ERR(bb_init(&output_buffer, "test_output", config.buff_config));

  // Connect filters: filter1 -> filter2 -> output
  CHECK_ERR(
      filt_sink_connect(&filter1.base, 0, &filter2.base.input_buffers[0]));
  CHECK_ERR(filt_sink_connect(&filter2.base, 0, &output_buffer));

  // Start all components
  CHECK_ERR(filt_start(&filter1.base));
  CHECK_ERR(filt_start(&filter2.base));
  CHECK_ERR(bb_start(&output_buffer));

  // Reset counters
  count_in = 0;
  count_out = 0;
  Bp_EC err;

  // Main test loop - test cascade with multiple batches
  for (int i = 0; i < (RING_CAPACITY * 2); i++) {
    // Get input batch and populate
    Batch_t* input_batch = bb_get_head(&filter1.base.input_buffers[0]);
    TEST_ASSERT_NOT_NULL(input_batch);

    // Fill batch with incrementing values
    for (int ii = 0; ii < BATCH_CAPACITY; ii++) {
      *((float*) input_batch->data + ii) = (float) count_in;
      count_in++;
    }
    input_batch->head = BATCH_CAPACITY;
    input_batch->t_ns = 2000000 + (i * 10000);
    input_batch->period_ns = 1000;

    // Submit to first filter
    CHECK_ERR(bb_submit(&filter1.base.input_buffers[0], 10000));

    // Try to get any available output batches (non-blocking)
    Batch_t* output_batch;
    while ((output_batch = bb_get_tail(&output_buffer, 1, &err)) !=
           NULL) {  // 1us timeout for near-instant check
      // Verify data unchanged through cascade
      for (int ii = 0; ii < BATCH_CAPACITY; ii++) {
        float expected = (float) count_out;
        float actual = *((float*) output_batch->data + ii);
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(expected, actual,
                                        "Cascade data mismatch");
        count_out++;
      }

      // Verify timing preserved
      int batch_idx = (count_out / BATCH_CAPACITY) - 1;
      TEST_ASSERT_EQUAL(2000000 + (batch_idx * 10000), output_batch->t_ns);

      CHECK_ERR(bb_del_tail(&output_buffer));
    }
  }

  // Get any remaining output batches
  nanosleep(&ts_10ms, NULL);
  Batch_t* output_batch;
  while ((output_batch = bb_get_tail(&output_buffer, 10000, &err)) != NULL) {
    // Verify data unchanged through cascade
    for (int ii = 0; ii < BATCH_CAPACITY; ii++) {
      float expected = (float) count_out;
      float actual = *((float*) output_batch->data + ii);
      TEST_ASSERT_EQUAL_FLOAT_MESSAGE(expected, actual,
                                      "Cascade data mismatch");
      count_out++;
    }
    CHECK_ERR(bb_del_tail(&output_buffer));
  }

  // Cleanup
  CHECK_ERR(filt_stop(&filter1.base));
  CHECK_ERR(filt_stop(&filter2.base));
  CHECK_ERR(bb_stop(&output_buffer));
  CHECK_ERR(filt_deinit(&filter1.base));
  CHECK_ERR(filt_deinit(&filter2.base));
  CHECK_ERR(bb_deinit(&output_buffer));
}

/* Producer thread context */
typedef struct {
  Batch_buff_t* target_buffer;
  size_t n_batches_to_produce;
  size_t batch_size;
  volatile bool* should_stop;
  pthread_t thread;
  Bp_EC result;
} producer_context_t;

/* Producer thread function */
static void* producer_thread(void* arg)
{
  producer_context_t* ctx = (producer_context_t*) arg;
  uint32_t value = 0;

  for (size_t i = 0; i < ctx->n_batches_to_produce && !*ctx->should_stop; i++) {
    Batch_t* batch = bb_get_head(ctx->target_buffer);
    if (!batch) {
      // Buffer full, wait a bit
      struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};  // 1ms
      nanosleep(&ts, NULL);
      i--;  // Retry this batch
      continue;
    }

    // Fill batch with incrementing values
    for (size_t j = 0; j < ctx->batch_size; j++) {
      *((float*) batch->data + j) = (float) value++;
    }
    batch->head = ctx->batch_size;
    batch->t_ns = 1000000 * i;
    batch->period_ns = 1000;

    Bp_EC err = bb_submit(ctx->target_buffer, 10000);
    if (err != Bp_EC_OK) {
      ctx->result = err;
      return NULL;
    }

    // Slow producer - simulates real data source
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 2000000};  // 2ms
    nanosleep(&ts, NULL);
  }

  ctx->result = Bp_EC_OK;
  return NULL;
}

/* Test 4: True multi-threaded producer-consumer with cascaded filters */
void test_multi_threaded_slow_consumer(void)
{
  // Use small buffers to test backpressure
  BatchBuffer_config small_config = {
      .dtype = DTYPE_FLOAT,
      .overflow_behaviour = OVERFLOW_BLOCK,
      .ring_capacity_expo = SMALL_RING_CAPACITY_EXPO,
      .batch_capacity_expo = SMALL_BATCH_CAPACITY_EXPO,
  };

  // Create cascade: producer -> scale filter -> offset filter -> consumer
  Map_filt_t scale_filter, offset_filter;
  Map_config_t scale_config = {.name = "test_scale_threaded",
                               .buff_config = small_config,
                               .map_fcn = test_scale_map,
                               .timeout_us = 10000};
  Map_config_t offset_config = {.name = "test_offset_threaded",
                                .buff_config = small_config,
                                .map_fcn = test_offset_map,
                                .timeout_us = 10000};

  CHECK_ERR(map_init(&scale_filter, scale_config));
  CHECK_ERR(map_init(&offset_filter, offset_config));

  Batch_buff_t output_buffer;
  CHECK_ERR(bb_init(&output_buffer, "test_output", small_config));

  // Connect cascade
  CHECK_ERR(filt_sink_connect(&scale_filter.base, 0,
                              &offset_filter.base.input_buffers[0]));
  CHECK_ERR(filt_sink_connect(&offset_filter.base, 0, &output_buffer));

  // Start all components
  CHECK_ERR(filt_start(&scale_filter.base));
  CHECK_ERR(filt_start(&offset_filter.base));
  CHECK_ERR(bb_start(&output_buffer));

  // Setup producer thread
  volatile bool should_stop = false;
  producer_context_t producer_ctx = {
      .target_buffer = &scale_filter.base.input_buffers[0],
      .n_batches_to_produce = 5,  // Reduced for faster test
      .batch_size = 1 << SMALL_BATCH_CAPACITY_EXPO,
      .should_stop = &should_stop,
      .result = Bp_EC_OK};

  // Start producer thread
  TEST_ASSERT_EQUAL(0, pthread_create(&producer_ctx.thread, NULL,
                                      producer_thread, &producer_ctx));

  // Consumer (main thread) - verify data
  uint32_t expected_value = 0;
  size_t batches_consumed = 0;
  const size_t small_batch_size = 1 << SMALL_BATCH_CAPACITY_EXPO;

  // Consume batches with slow consumer (slower than producer)
  while (batches_consumed < producer_ctx.n_batches_to_produce) {
    Bp_EC err;
    Batch_t* out = bb_get_tail(&output_buffer, 10000, &err);  // 10ms timeout

    if (err == Bp_EC_TIMEOUT) {
      // Check if producer had an error
      if (producer_ctx.result != Bp_EC_OK) {
        should_stop = true;
        break;
      }
      continue;
    }

    CHECK_ERR(err);
    TEST_ASSERT_NOT_NULL(out);

    // Verify transformed data: (x * 2) + 100
    for (size_t i = 0; i < small_batch_size; i++) {
      float expected = ((float) expected_value * 2.0f) + 100.0f;
      float actual = *((float*) out->data + i);
      TEST_ASSERT_EQUAL_FLOAT_MESSAGE(expected, actual,
                                      "Data mismatch in multi-threaded test");
      expected_value++;
    }

    CHECK_ERR(bb_del_tail(&output_buffer));
    batches_consumed++;

    // Slow consumer - simulate processing time
    struct timespec ts = {.tv_sec = 0,
                          .tv_nsec = 5000000};  // 5ms (slower than producer)
    nanosleep(&ts, NULL);
  }

  // Signal producer to stop and wait for it
  should_stop = true;
  pthread_join(producer_ctx.thread, NULL);

  // Verify producer succeeded
  CHECK_ERR(producer_ctx.result);

  // Verify we consumed all produced data
  TEST_ASSERT_EQUAL(producer_ctx.n_batches_to_produce, batches_consumed);
  TEST_ASSERT_EQUAL(producer_ctx.n_batches_to_produce * small_batch_size,
                    expected_value);

  // Cleanup
  CHECK_ERR(filt_stop(&scale_filter.base));
  CHECK_ERR(filt_stop(&offset_filter.base));
  CHECK_ERR(bb_stop(&output_buffer));
  CHECK_ERR(filt_deinit(&scale_filter.base));
  CHECK_ERR(filt_deinit(&offset_filter.base));
  CHECK_ERR(bb_deinit(&output_buffer));
}

/* Test error handling */
void test_map_error_handling(void)
{
  Map_filt_t filter;
  Map_config_t config = {
      .name = "test_error_map",
      .buff_config = test_config,
      .map_fcn = test_error_map,  // Function that always returns error
      .timeout_us = 10000};

  CHECK_ERR(map_init(&filter, config));

  Batch_buff_t output_buffer;
  CHECK_ERR(bb_init(&output_buffer, "test_output", config.buff_config));
  CHECK_ERR(filt_sink_connect(&filter.base, 0, &output_buffer));
  CHECK_ERR(bb_start(&output_buffer));

  // Start filter
  CHECK_ERR(filt_start(&filter.base));

  // Submit data that will trigger error
  Batch_t* input_batch = bb_get_head(&filter.base.input_buffers[0]);
  TEST_ASSERT_NOT_NULL(input_batch);

  // Fill batch with data
  for (int ii = 0; ii < BATCH_CAPACITY; ii++) {
    *((float*) input_batch->data + ii) = (float) ii;
  }
  input_batch->head = BATCH_CAPACITY;

  CHECK_ERR(bb_submit(&filter.base.input_buffers[0], 10000));

  // Wait for processing - should encounter error
  nanosleep(&ts_10ms, NULL);

  // Filter should have stopped due to error
  TEST_ASSERT_FALSE(atomic_load(&filter.base.running));
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, filter.base.worker_err_info.ec);

  // Cleanup
  CHECK_ERR(filt_stop(&filter.base));
  CHECK_ERR(bb_stop(&output_buffer));
  CHECK_ERR(filt_deinit(&filter.base));
  CHECK_ERR(bb_deinit(&output_buffer));
}

/* Test: Scale transform */
void test_scale_transform(void)
{
  Map_filt_t filter;
  Map_config_t config = {.name = "test_scale",
                         .buff_config = test_config,
                         .map_fcn = test_scale_map,
                         .timeout_us = 10000};

  CHECK_ERR(map_init(&filter, config));

  Batch_buff_t output_buffer;
  CHECK_ERR(bb_init(&output_buffer, "test_output", config.buff_config));
  CHECK_ERR(filt_sink_connect(&filter.base, 0, &output_buffer));
  CHECK_ERR(bb_start(&output_buffer));
  CHECK_ERR(filt_start(&filter.base));

  // Submit test data
  Batch_t* input_batch = bb_get_head(&filter.base.input_buffers[0]);
  TEST_ASSERT_NOT_NULL(input_batch);

  // Fill with test values
  for (int i = 0; i < BATCH_CAPACITY; i++) {
    *((float*) input_batch->data + i) = (float) i;
  }
  input_batch->head = BATCH_CAPACITY;

  CHECK_ERR(bb_submit(&filter.base.input_buffers[0], 10000));
  nanosleep(&ts_10ms, NULL);

  // Get output
  Bp_EC err;
  Batch_t* output_batch = bb_get_tail(&output_buffer, 10000, &err);
  CHECK_ERR(err);
  TEST_ASSERT_NOT_NULL(output_batch);

  // Verify scaling
  for (int i = 0; i < BATCH_CAPACITY; i++) {
    float expected = (float) i * 2.0f;
    float actual = *((float*) output_batch->data + i);
    TEST_ASSERT_EQUAL_FLOAT(expected, actual);
  }

  CHECK_ERR(bb_del_tail(&output_buffer));
  CHECK_ERR(filt_stop(&filter.base));
  CHECK_ERR(bb_stop(&output_buffer));
  CHECK_ERR(filt_deinit(&filter.base));
  CHECK_ERR(bb_deinit(&output_buffer));
}

/* Test: Chained transforms (scale then offset) */
void test_chained_transforms(void)
{
  Map_filt_t scale_filter, offset_filter;
  Map_config_t scale_config = {.name = "test_scale_chained",
                               .buff_config = test_config,
                               .map_fcn = test_scale_map,
                               .timeout_us = 10000};
  Map_config_t offset_config = {.name = "test_offset_chained",
                                .buff_config = test_config,
                                .map_fcn = test_offset_map,
                                .timeout_us = 10000};

  CHECK_ERR(map_init(&scale_filter, scale_config));
  CHECK_ERR(map_init(&offset_filter, offset_config));

  Batch_buff_t output_buffer;
  CHECK_ERR(bb_init(&output_buffer, "test_output", test_config));

  // Connect: scale -> offset -> output
  CHECK_ERR(filt_sink_connect(&scale_filter.base, 0,
                              &offset_filter.base.input_buffers[0]));
  CHECK_ERR(filt_sink_connect(&offset_filter.base, 0, &output_buffer));

  CHECK_ERR(filt_start(&scale_filter.base));
  CHECK_ERR(filt_start(&offset_filter.base));
  CHECK_ERR(bb_start(&output_buffer));

  // Submit test data
  Batch_t* input_batch = bb_get_head(&scale_filter.base.input_buffers[0]);
  TEST_ASSERT_NOT_NULL(input_batch);

  for (int i = 0; i < BATCH_CAPACITY; i++) {
    *((float*) input_batch->data + i) = (float) i;
  }
  input_batch->head = BATCH_CAPACITY;

  CHECK_ERR(bb_submit(&scale_filter.base.input_buffers[0], 10000));
  nanosleep(&ts_10ms, NULL);

  // Get output
  Bp_EC err;
  Batch_t* output_batch = bb_get_tail(&output_buffer, 10000, &err);
  CHECK_ERR(err);
  TEST_ASSERT_NOT_NULL(output_batch);

  // Verify chained transform: (x * 2) + 100
  for (int i = 0; i < BATCH_CAPACITY; i++) {
    float expected = ((float) i * 2.0f) + 100.0f;
    float actual = *((float*) output_batch->data + i);
    TEST_ASSERT_EQUAL_FLOAT(expected, actual);
  }

  CHECK_ERR(bb_del_tail(&output_buffer));
  CHECK_ERR(filt_stop(&scale_filter.base));
  CHECK_ERR(filt_stop(&offset_filter.base));
  CHECK_ERR(bb_stop(&output_buffer));
  CHECK_ERR(filt_deinit(&scale_filter.base));
  CHECK_ERR(filt_deinit(&offset_filter.base));
  CHECK_ERR(bb_deinit(&output_buffer));
}

/* Test: Buffer wraparound with small buffers */
void test_buffer_wraparound(void)
{
  BatchBuffer_config small_config = {
      .dtype = DTYPE_FLOAT,
      .overflow_behaviour = OVERFLOW_BLOCK,
      .ring_capacity_expo = SMALL_RING_CAPACITY_EXPO,
      .batch_capacity_expo = SMALL_BATCH_CAPACITY_EXPO,
  };

  Map_filt_t filter;
  Map_config_t config = {.name = "test_wraparound",
                         .buff_config = small_config,
                         .map_fcn = test_identity_map,
                         .timeout_us = 10000};

  CHECK_ERR(map_init(&filter, config));

  Batch_buff_t output_buffer;
  CHECK_ERR(bb_init(&output_buffer, "test_output", small_config));
  CHECK_ERR(filt_sink_connect(&filter.base, 0, &output_buffer));
  CHECK_ERR(bb_start(&output_buffer));
  CHECK_ERR(filt_start(&filter.base));

  const size_t small_batch_size = 1 << SMALL_BATCH_CAPACITY_EXPO;
  const size_t small_ring_size = (1 << SMALL_RING_CAPACITY_EXPO) - 1;

  // Submit exactly enough batches to wrap the ring buffer once
  for (size_t batch = 0; batch < small_ring_size + 2; batch++) {
    Batch_t* input_batch = bb_get_head(&filter.base.input_buffers[0]);
    TEST_ASSERT_NOT_NULL(input_batch);

    // Fill with known pattern
    for (size_t i = 0; i < small_batch_size; i++) {
      *((float*) input_batch->data + i) = (float) (batch * 1000 + i);
    }
    input_batch->head = small_batch_size;

    CHECK_ERR(bb_submit(&filter.base.input_buffers[0], 10000));

    // Give filter time to process
    nanosleep(&ts_10ms, NULL);
  }

  // Verify output in order
  uint32_t verified_batches = 0;
  Bp_EC err;
  Batch_t* out;
  while ((out = bb_get_tail(&output_buffer, 10000, &err)) != NULL &&
         err == Bp_EC_OK) {
    // Verify batch pattern
    for (size_t i = 0; i < small_batch_size; i++) {
      float expected = (float) (verified_batches * 1000 + i);
      float actual = *((float*) out->data + i);
      TEST_ASSERT_EQUAL_FLOAT(expected, actual);
    }
    CHECK_ERR(bb_del_tail(&output_buffer));
    verified_batches++;
  }

  // Should have processed at least small_ring_size batches (buffer may drop
  // some due to overflow)
  TEST_ASSERT_GREATER_OR_EQUAL(small_ring_size, verified_batches);

  CHECK_ERR(filt_stop(&filter.base));
  CHECK_ERR(bb_stop(&output_buffer));
  CHECK_ERR(filt_deinit(&filter.base));
  CHECK_ERR(bb_deinit(&output_buffer));
}

/* Helper functions for mixed batch size tests */
static void verify_sequence_continuity(Batch_buff_t* buffer,
                                       uint32_t start_value,
                                       size_t total_samples,
                                       const char* test_name)
{
  uint32_t expected = start_value;
  Bp_EC err;
  size_t batches_consumed = 0;

  while (expected < start_value + total_samples) {
    Batch_t* batch = bb_get_tail(buffer, 10000, &err);
    if (err == Bp_EC_TIMEOUT) {
      printf("%s: Timeout waiting for batch, expected %u, got %u samples\n",
             test_name, (unsigned) total_samples,
             (unsigned) (expected - start_value));
      break;
    }
    if (!batch) break;

    float* data = (float*) batch->data;
    for (size_t i = 0; i < batch->head; i++) {
      TEST_ASSERT_EQUAL_FLOAT_MESSAGE((float) expected, data[i],
                                      "Sequence continuity broken");
      expected++;
    }

    // Print timing info for first few batches
    if (batches_consumed < 3) {
      printf("  Batch %zu: %zu samples, t_ns=%lld\n", batches_consumed,
             batch->head, batch->t_ns);
    }

    CHECK_ERR(bb_del_tail(buffer));
    batches_consumed++;
  }

  printf("%s: Consumed %zu batches, %u samples\n", test_name, batches_consumed,
         expected - start_value);
  TEST_ASSERT_EQUAL_MESSAGE(start_value + total_samples, expected,
                            "Not all samples were received");
}

static void fill_sequential_batches(Batch_buff_t* buffer, uint32_t* counter,
                                    size_t n_batches, size_t samples_per_batch)
{
  for (size_t b = 0; b < n_batches; b++) {
    Batch_t* batch = bb_get_head(buffer);
    TEST_ASSERT_NOT_NULL(batch);

    float* data = (float*) batch->data;
    for (size_t i = 0; i < samples_per_batch; i++) {
      data[i] = (float) (*counter)++;
    }
    batch->head = samples_per_batch;
    batch->t_ns = 1000000 * b;
    batch->period_ns = 1000;

    CHECK_ERR(bb_submit(buffer, 10000));
  }
}

/* Test: Large to small batch cascade */
void test_large_to_small_batch_cascade(void)
{
  printf("\n=== Testing Large to Small Batch Cascade ===\n");
  printf("Input: 256 samples/batch, Output: 64 samples/batch\n");

  // Stage 1: Large batches (256 samples)
  BatchBuffer_config large_config = {
      .dtype = DTYPE_FLOAT,
      .overflow_behaviour = OVERFLOW_BLOCK,
      .ring_capacity_expo = 4,   // 15 batches
      .batch_capacity_expo = 8,  // 256 samples
  };

  // Stage 2: Small batches (64 samples)
  BatchBuffer_config small_config = {
      .dtype = DTYPE_FLOAT,
      .overflow_behaviour = OVERFLOW_BLOCK,
      .ring_capacity_expo = 5,   // 31 batches
      .batch_capacity_expo = 6,  // 64 samples
  };

  // Create filter with large input config
  Map_filt_t filter;
  Map_config_t config = {.name = "test_large_to_small",
                         .buff_config = large_config,
                         .map_fcn = test_identity_map,
                         .timeout_us = 10000};

  CHECK_ERR(map_init(&filter, config));

  // Create output buffer with small config
  Batch_buff_t output;
  CHECK_ERR(bb_init(&output, "output", small_config));

  // Try to connect with mismatched sizes
  // This tests the current behavior - it may fail or behave unexpectedly
  Bp_EC connect_err = filt_sink_connect(&filter.base, 0, &output);
  if (connect_err != Bp_EC_OK) {
    printf("Connection failed with mismatched batch sizes: %d\n", connect_err);
    // Document this limitation
    TEST_ASSERT_EQUAL(Bp_EC_OK,
                      connect_err);  // This will fail, documenting the issue
  }

  CHECK_ERR(filt_start(&filter.base));
  CHECK_ERR(bb_start(&output));

  // Submit test data - 3 large batches = 768 samples
  uint32_t counter = 0;
  fill_sequential_batches(&filter.base.input_buffers[0], &counter, 3, 256);

  // Wait for processing
  nanosleep(&ts_10ms, NULL);

  // Verify output - should get 768 samples in small batches
  printf("Submitted %u samples in %d large batches\n", counter, 3);
  printf("Expected output: %u small batches of 64 samples each\n",
         counter / 64);
  verify_sequence_continuity(&output, 0, counter, "large_to_small");

  // Cleanup
  CHECK_ERR(filt_stop(&filter.base));
  CHECK_ERR(bb_stop(&output));
  CHECK_ERR(filt_deinit(&filter.base));
  CHECK_ERR(bb_deinit(&output));
}

/* Test: Small to large batch cascade */
void test_small_to_large_batch_cascade(void)
{
  printf("\n=== Testing Small to Large Batch Cascade ===\n");

  // Stage 1: Small batches (64 samples)
  BatchBuffer_config small_config = {
      .dtype = DTYPE_FLOAT,
      .overflow_behaviour = OVERFLOW_BLOCK,
      .ring_capacity_expo = 5,   // 31 batches
      .batch_capacity_expo = 6,  // 64 samples
  };

  // Stage 2: Large batches (256 samples)
  BatchBuffer_config large_config = {
      .dtype = DTYPE_FLOAT,
      .overflow_behaviour = OVERFLOW_BLOCK,
      .ring_capacity_expo = 4,   // 15 batches
      .batch_capacity_expo = 8,  // 256 samples
  };

  Map_filt_t filter;
  Map_config_t config = {.name = "test_small_to_large",
                         .buff_config = small_config,
                         .map_fcn = test_identity_map,
                         .timeout_us = 10000};

  CHECK_ERR(map_init(&filter, config));

  Batch_buff_t output;
  CHECK_ERR(bb_init(&output, "output", large_config));

  // Try to connect - this will likely expose issues
  Bp_EC connect_err = filt_sink_connect(&filter.base, 0, &output);
  if (connect_err != Bp_EC_OK) {
    printf("Connection failed with mismatched batch sizes: %d\n", connect_err);
    TEST_ASSERT_EQUAL(Bp_EC_OK, connect_err);
  }

  CHECK_ERR(filt_start(&filter.base));
  CHECK_ERR(bb_start(&output));

  // Submit 8 small batches = 512 samples (should fill 2 large batches)
  uint32_t counter = 0;
  fill_sequential_batches(&filter.base.input_buffers[0], &counter, 8, 64);

  // Wait for processing
  nanosleep(&ts_10ms, NULL);

  // Verify output
  verify_sequence_continuity(&output, 0, counter, "small_to_large");

  // Cleanup
  CHECK_ERR(filt_stop(&filter.base));
  CHECK_ERR(bb_stop(&output));
  CHECK_ERR(filt_deinit(&filter.base));
  CHECK_ERR(bb_deinit(&output));
}

/* Test: Mismatched ring capacity */
void test_mismatched_ring_capacity(void)
{
  printf("\n=== Testing Mismatched Ring Capacity ===\n");

  // Same batch size, different ring capacity
  BatchBuffer_config few_buffers = {
      .dtype = DTYPE_FLOAT,
      .overflow_behaviour = OVERFLOW_BLOCK,
      .ring_capacity_expo = 3,   // 7 batches
      .batch_capacity_expo = 7,  // 128 samples
  };

  BatchBuffer_config many_buffers = {
      .dtype = DTYPE_FLOAT,
      .overflow_behaviour = OVERFLOW_BLOCK,
      .ring_capacity_expo = 6,   // 63 batches
      .batch_capacity_expo = 7,  // 128 samples (same size)
  };

  Map_filt_t filter1, filter2;
  Map_config_t config1 = {.name = "test_few_buffers",
                          .buff_config = few_buffers,
                          .map_fcn = test_scale_map,
                          .timeout_us = 10000};
  Map_config_t config2 = {.name = "test_many_buffers",
                          .buff_config = many_buffers,
                          .map_fcn = test_offset_map,
                          .timeout_us = 10000};

  CHECK_ERR(map_init(&filter1, config1));
  CHECK_ERR(map_init(&filter2, config2));

  Batch_buff_t output;
  CHECK_ERR(bb_init(&output, "output", many_buffers));

  // Connect cascade
  CHECK_ERR(
      filt_sink_connect(&filter1.base, 0, &filter2.base.input_buffers[0]));
  CHECK_ERR(filt_sink_connect(&filter2.base, 0, &output));

  CHECK_ERR(filt_start(&filter1.base));
  CHECK_ERR(filt_start(&filter2.base));
  CHECK_ERR(bb_start(&output));

  // Burst submit to test backpressure
  uint32_t counter = 0;
  printf("Submitting burst of 10 batches to small ring buffer (capacity 7)\n");

  for (int i = 0; i < 10; i++) {
    Batch_t* batch = bb_get_head(&filter1.base.input_buffers[0]);
    if (!batch) {
      printf("Buffer full at batch %d\n", i);
      break;
    }

    float* data = (float*) batch->data;
    for (size_t j = 0; j < 128; j++) {
      data[j] = (float) (counter++);
    }
    batch->head = 128;

    CHECK_ERR(bb_submit(&filter1.base.input_buffers[0], 10000));
  }

  // Consume output slowly to test buffering
  nanosleep(&ts_10ms, NULL);

  // Verify transformed data: (x * 2) + 100
  uint32_t verified = 0;
  Bp_EC err;
  while (verified < counter) {
    Batch_t* out = bb_get_tail(&output, 1000, &err);
    if (err == Bp_EC_TIMEOUT) break;
    TEST_ASSERT_NOT_NULL(out);

    float* data = (float*) out->data;
    for (size_t i = 0; i < out->head; i++) {
      float expected = ((float) verified * 2.0f) + 100.0f;
      TEST_ASSERT_EQUAL_FLOAT(expected, data[i]);
      verified++;
    }

    CHECK_ERR(bb_del_tail(&output));
  }

  printf("Processed %u samples through mismatched ring capacities\n", verified);

  // Cleanup
  CHECK_ERR(filt_stop(&filter1.base));
  CHECK_ERR(filt_stop(&filter2.base));
  CHECK_ERR(bb_stop(&output));
  CHECK_ERR(filt_deinit(&filter1.base));
  CHECK_ERR(filt_deinit(&filter2.base));
  CHECK_ERR(bb_deinit(&output));
}

/* Main test runner */
int main(void)
{
  UNITY_BEGIN();

  // Basic configuration tests
  RUN_TEST(test_map_init_valid_config);
  RUN_TEST(test_map_init_null_filter);
  RUN_TEST(test_map_init_null_function);

  // Functional tests
  RUN_TEST(test_single_threaded_linear_ramp);
  RUN_TEST(test_scale_transform);
  RUN_TEST(test_chained_transforms);
  RUN_TEST(test_buffer_wraparound);

  // Multi-threaded tests
  RUN_TEST(test_multi_stage_single_threaded);
  RUN_TEST(test_multi_threaded_slow_consumer);

  // Mixed batch size tests
  RUN_TEST(test_large_to_small_batch_cascade);
  RUN_TEST(test_small_to_large_batch_cascade);
  RUN_TEST(test_mismatched_ring_capacity);

  // Error handling
  RUN_TEST(test_map_error_handling);

  return UNITY_END();
}
