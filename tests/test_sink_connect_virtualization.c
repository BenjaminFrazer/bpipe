#define _GNU_SOURCE
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "batch_buffer.h"
#include "core.h"
#include "pipeline.h"
#include "test_utils.h"
#include "unity.h"
#include "utils.h"

void setUp(void) {}
void tearDown(void) {}

// Test worker that does nothing
void* test_worker(void* arg)
{
  Filter_t* f = (Filter_t*) arg;
  while (atomic_load(&f->running)) {
    usleep(1000);
  }
  return NULL;
}

// Test backward compatibility - default behavior unchanged
void test_default_sink_connect_unchanged(void)
{
  Filter_t filter;

  Core_filt_config_t config = {
      .name = "test_filter",
      .filt_type = FILT_T_MAP,
      .size = sizeof(Filter_t),
      .n_inputs = 1,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000,
      .worker = test_worker};

  CHECK_ERR(filt_init(&filter, config));

  // Create a sink buffer
  Batch_buff_t sink_buffer;
  BatchBuffer_config buff_config = {.dtype = DTYPE_FLOAT,
                                    .batch_capacity_expo = 6,
                                    .ring_capacity_expo = 4,
                                    .overflow_behaviour = OVERFLOW_BLOCK};
  CHECK_ERR(bb_init(&sink_buffer, "sink", buff_config));

  // Connect using filt_sink_connect - should work exactly as before
  CHECK_ERR(filt_sink_connect(&filter, 0, &sink_buffer));

  // Verify connection was made
  TEST_ASSERT_EQUAL(1, filter.n_sinks);
  TEST_ASSERT_EQUAL_PTR(&sink_buffer, filter.sinks[0]);

  // Try to connect again - should fail with CONNECTION_OCCUPIED
  Batch_buff_t another_sink;
  CHECK_ERR(bb_init(&another_sink, "another", buff_config));
  TEST_ASSERT_EQUAL(Bp_EC_CONNECTION_OCCUPIED,
                    filt_sink_connect(&filter, 0, &another_sink));

  // Clean up
  bb_deinit(&sink_buffer);
  bb_deinit(&another_sink);
  filt_deinit(&filter);
}

// Test custom sink_connect implementation
typedef struct {
  Filter_t base;
  int custom_connect_called;
  Batch_buff_t* forwarded_sink;
  size_t forwarded_port;
} TestFilter_t;

static Bp_EC test_custom_sink_connect(Filter_t* self, size_t output_port,
                                      Batch_buff_t* sink)
{
  TestFilter_t* tf = (TestFilter_t*) self;
  tf->custom_connect_called = 1;
  tf->forwarded_sink = sink;
  tf->forwarded_port = output_port;

  // For testing, we don't actually connect anything
  return Bp_EC_OK;
}

void test_custom_sink_connect_override(void)
{
  TestFilter_t filter;
  memset(&filter, 0, sizeof(TestFilter_t));

  Core_filt_config_t config = {
      .name = "custom_filter",
      .filt_type = FILT_T_MAP,
      .size = sizeof(TestFilter_t),
      .n_inputs = 1,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000,
      .worker = test_worker};

  CHECK_ERR(filt_init(&filter.base, config));

  // Override sink_connect
  filter.base.ops.sink_connect = test_custom_sink_connect;

  // Create a sink buffer
  Batch_buff_t sink_buffer;
  BatchBuffer_config buff_config = {.dtype = DTYPE_FLOAT,
                                    .batch_capacity_expo = 6,
                                    .ring_capacity_expo = 4,
                                    .overflow_behaviour = OVERFLOW_BLOCK};
  CHECK_ERR(bb_init(&sink_buffer, "sink", buff_config));

  // Connect - should call our custom function
  CHECK_ERR(filt_sink_connect(&filter.base, 42, &sink_buffer));

  // Verify custom function was called
  TEST_ASSERT_EQUAL(1, filter.custom_connect_called);
  TEST_ASSERT_EQUAL_PTR(&sink_buffer, filter.forwarded_sink);
  TEST_ASSERT_EQUAL(42, filter.forwarded_port);

  // Note: base filter sinks array should NOT be updated since our custom
  // function didn't call the default implementation
  TEST_ASSERT_EQUAL(0, filter.base.n_sinks);
  TEST_ASSERT_NULL(filter.base.sinks[0]);

  // Clean up
  bb_deinit(&sink_buffer);
  filt_deinit(&filter.base);
}

// Test error handling
void test_sink_connect_error_handling(void)
{
  Filter_t filter;

  Core_filt_config_t config = {
      .name = "test_filter",
      .filt_type = FILT_T_MAP,
      .size = sizeof(Filter_t),
      .n_inputs = 1,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000,
      .worker = test_worker};

  CHECK_ERR(filt_init(&filter, config));

  // Test NULL filter
  Batch_buff_t sink_buffer;
  BatchBuffer_config buff_config = {.dtype = DTYPE_FLOAT,
                                    .batch_capacity_expo = 6,
                                    .ring_capacity_expo = 4,
                                    .overflow_behaviour = OVERFLOW_BLOCK};
  CHECK_ERR(bb_init(&sink_buffer, "sink", buff_config));

  TEST_ASSERT_EQUAL(Bp_EC_NULL_FILTER,
                    filt_sink_connect(NULL, 0, &sink_buffer));

  // Test NULL sink
  TEST_ASSERT_EQUAL(Bp_EC_NULL_BUFF, filt_sink_connect(&filter, 0, NULL));

  // Test invalid sink index
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_SINK_IDX,
                    filt_sink_connect(&filter, MAX_SINKS, &sink_buffer));

  // Clean up
  bb_deinit(&sink_buffer);
  filt_deinit(&filter);
}

// Test pipeline connection forwarding
void test_pipeline_connection_forwarding(void)
{
  // Create internal filters for the pipeline
  Filter_t internal_input;
  Filter_t internal_output;

  Core_filt_config_t filter_config = {
      .name = "internal_filter",
      .filt_type = FILT_T_MAP,
      .size = sizeof(Filter_t),
      .n_inputs = 1,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000,
      .worker = test_worker};

  CHECK_ERR(filt_init(&internal_input, filter_config));
  filter_config.name = "internal_output";
  CHECK_ERR(filt_init(&internal_output, filter_config));

  // Create pipeline
  Pipeline_t pipeline;
  Filter_t* filters[] = {&internal_input, &internal_output};
  Connection_t connections[] = {{.from_filter = &internal_input,
                                 .from_port = 0,
                                 .to_filter = &internal_output,
                                 .to_port = 0}};

  Pipeline_config_t pipe_config = {
      .name = "test_pipeline",
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000,
      .filters = filters,
      .n_filters = 2,
      .connections = connections,
      .n_connections = 1,
      .input_filter = &internal_input,
      .input_port = 0,
      .output_filter = &internal_output,
      .output_port = 0};

  CHECK_ERR(pipeline_init(&pipeline, pipe_config));

  // Create external sink
  Batch_buff_t external_sink;
  BatchBuffer_config buff_config = {.dtype = DTYPE_FLOAT,
                                    .batch_capacity_expo = 6,
                                    .ring_capacity_expo = 4,
                                    .overflow_behaviour = OVERFLOW_BLOCK};
  CHECK_ERR(bb_init(&external_sink, "external_sink", buff_config));

  // Connect external sink to pipeline - should forward to internal_output
  CHECK_ERR(filt_sink_connect(&pipeline.base, 0, &external_sink));

  // Verify connection was forwarded to internal output filter
  TEST_ASSERT_EQUAL(1, internal_output.n_sinks);
  TEST_ASSERT_EQUAL_PTR(&external_sink, internal_output.sinks[0]);

  // Pipeline itself should have no sinks
  TEST_ASSERT_EQUAL(0, pipeline.base.n_sinks);
  TEST_ASSERT_NULL(pipeline.base.sinks[0]);

  // Test invalid port
  Batch_buff_t another_sink;
  CHECK_ERR(bb_init(&another_sink, "another", buff_config));
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_SINK_IDX,
                    filt_sink_connect(&pipeline.base, 1, &another_sink));

  // Clean up
  bb_deinit(&external_sink);
  bb_deinit(&another_sink);
  filt_deinit(&pipeline.base);
  filt_deinit(&internal_input);
  filt_deinit(&internal_output);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_default_sink_connect_unchanged);
  RUN_TEST(test_custom_sink_connect_override);
  RUN_TEST(test_sink_connect_error_handling);
  RUN_TEST(test_pipeline_connection_forwarding);
  return UNITY_END();
}