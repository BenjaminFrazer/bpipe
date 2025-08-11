#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "batch_buffer.h"
#include "map.h"
#include "pipeline.h"
#include "signal_generator.h"
#include "tee.h"
#include "test_utils.h"
#include "unity.h"

#define BATCH_CAPACITY_EXPO 6  // 64 samples per batch
#define RING_CAPACITY_EXPO 4   // 15 batches in ring
#define RING_CAPACITY ((1 << RING_CAPACITY_EXPO) - 1)
#define BATCH_CAPACITY (1 << BATCH_CAPACITY_EXPO)

static BatchBuffer_config default_buffer_config(void)
{
  BatchBuffer_config config = {
      .dtype = DTYPE_FLOAT,
      .overflow_behaviour = OVERFLOW_BLOCK,
      .ring_capacity_expo = RING_CAPACITY_EXPO,
      .batch_capacity_expo = BATCH_CAPACITY_EXPO,
  };
  return config;
}

/* Test map functions */
static Bp_EC multiply_by_2(const void* in, void* out, size_t n_samples)
{
  if (!in || !out) return Bp_EC_NULL_POINTER;
  const float* input = (const float*) in;
  float* output = (float*) out;
  for (size_t i = 0; i < n_samples; i++) {
    output[i] = input[i] * 2.0f;
  }
  return Bp_EC_OK;
}

static Bp_EC add_10(const void* in, void* out, size_t n_samples)
{
  if (!in || !out) return Bp_EC_NULL_POINTER;
  const float* input = (const float*) in;
  float* output = (float*) out;
  for (size_t i = 0; i < n_samples; i++) {
    output[i] = input[i] + 10.0f;
  }
  return Bp_EC_OK;
}

static Bp_EC multiply_by_3(const void* in, void* out, size_t n_samples)
{
  if (!in || !out) return Bp_EC_NULL_POINTER;
  const float* input = (const float*) in;
  float* output = (float*) out;
  for (size_t i = 0; i < n_samples; i++) {
    output[i] = input[i] * 3.0f;
  }
  return Bp_EC_OK;
}

/* Test setup/teardown */
void setUp(void)
{
  // Nothing to set up
}

void tearDown(void)
{
  // Nothing to tear down
}

void test_pipeline_linear_chain(void)
{
  // Create component filters
  Map_filt_t gain_filter, offset_filter;

  Map_config_t multiply_by_2_config = {.name = "gain_x2",
                                       .buff_config = default_buffer_config(),
                                       .map_fcn = multiply_by_2,
                                       .timeout_us = 1000000};

  Map_config_t add_10_config = {.name = "offset_10",
                                .buff_config = default_buffer_config(),
                                .map_fcn = add_10,
                                .timeout_us = 1000000};

  CHECK_ERR(map_init(&gain_filter, multiply_by_2_config));
  CHECK_ERR(map_init(&offset_filter, add_10_config));

  // Create linear chain pipeline (direct pointers)
  Filter_t* filters[] = {&gain_filter.base, &offset_filter.base};

  Connection_t connections[] = {{&gain_filter.base, 0, &offset_filter.base, 0}};

  Pipeline_config_t config = {.name = "linear_chain",
                              .buff_config = default_buffer_config(),
                              .timeout_us = 1000000,
                              .filters = filters,
                              .n_filters = 2,
                              .connections = connections,
                              .n_connections = 1,
                              .input_filter = &gain_filter.base,
                              .input_port = 0,
                              .output_filter = &offset_filter.base,
                              .output_port = 0};

  Pipeline_t pipeline;
  CHECK_ERR(pipeline_init(&pipeline, config));

  // Verify structure
  TEST_ASSERT_EQUAL_STRING("linear_chain", pipeline.base.name);
  TEST_ASSERT_EQUAL(FILT_T_PIPELINE, pipeline.base.filt_type);
  TEST_ASSERT_EQUAL(2, pipeline.n_filters);
  TEST_ASSERT_EQUAL(1, pipeline.n_connections);
  TEST_ASSERT_EQUAL_PTR(&gain_filter.base, pipeline.input_filter);
  TEST_ASSERT_EQUAL_PTR(&offset_filter.base, pipeline.output_filter);

  filt_deinit(&pipeline.base);
}

void test_pipeline_multi_branch_dag(void)
{
  // Create filters for multi-branch topology
  Tee_filt_t splitter;
  Map_filt_t gain1, gain2;

  // Note: We'll simulate a mixer with a map filter for now
  Map_filt_t combiner;

  // Tee configuration
  BatchBuffer_config output_configs[] = {default_buffer_config(),
                                         default_buffer_config()};

  Tee_config_t tee_config_2_outputs = {.name = "splitter",
                                       .buff_config = default_buffer_config(),
                                       .n_outputs = 2,
                                       .output_configs = output_configs,
                                       .timeout_us = 1000000,
                                       .copy_data = true};

  Map_config_t multiply_by_2_config = {.name = "gain_x2",
                                       .buff_config = default_buffer_config(),
                                       .map_fcn = multiply_by_2,
                                       .timeout_us = 1000000};

  Map_config_t multiply_by_3_config = {.name = "gain_x3",
                                       .buff_config = default_buffer_config(),
                                       .map_fcn = multiply_by_3,
                                       .timeout_us = 1000000};

  // Use identity for combiner (mixer) simulation
  Map_config_t combiner_config = {.name = "combiner",
                                  .buff_config = default_buffer_config(),
                                  .map_fcn = map_identity_f32,
                                  .timeout_us = 1000000};

  CHECK_ERR(tee_init(&splitter, tee_config_2_outputs));
  CHECK_ERR(map_init(&gain1, multiply_by_2_config));
  CHECK_ERR(map_init(&gain2, multiply_by_3_config));
  CHECK_ERR(map_init(&combiner, combiner_config));

  // Define DAG topology: splitter -> [gain1, gain2] -> combiner (direct
  // pointers)
  Filter_t* filters[] = {&splitter.base, &gain1.base, &gain2.base,
                         &combiner.base};

  Connection_t connections[] = {
      {&splitter.base, 0, &gain1.base, 0},  // Split left channel
      {&splitter.base, 1, &gain2.base, 0},  // Split right channel
      {&gain1.base, 0, &combiner.base, 0},  // Combine processed left
      // Note: Since we're using a simple map as combiner, we can't have
      // multiple inputs This is a limitation for this test - in real usage,
      // we'd use a proper mixer
  };

  Pipeline_config_t config = {
      .name = "parallel_processor",
      .buff_config = default_buffer_config(),
      .timeout_us = 1000000,
      .filters = filters,
      .n_filters = 4,
      .connections = connections,
      .n_connections = 3,  // Adjusted for our test limitation
      .input_filter = &splitter.base,
      .input_port = 0,
      .output_filter = &combiner.base,
      .output_port = 0};

  Pipeline_t pipeline;
  CHECK_ERR(pipeline_init(&pipeline, config));

  // Verify complex topology
  TEST_ASSERT_EQUAL(4, pipeline.n_filters);
  TEST_ASSERT_EQUAL(3, pipeline.n_connections);
  TEST_ASSERT_EQUAL_PTR(&splitter.base, pipeline.input_filter);
  TEST_ASSERT_EQUAL_PTR(&combiner.base, pipeline.output_filter);

  filt_deinit(&pipeline.base);
}

void test_pipeline_lifecycle_and_errors(void)
{
  // Create simple pipeline for lifecycle testing with a source
  SignalGenerator_t source;
  Map_filt_t gain_filter, offset_filter;

  // Add a source filter to make this a valid root pipeline
  SignalGenerator_config_t source_config = {
      .name = "test_source",
      .buff_config = default_buffer_config(),
      .waveform_type = WAVEFORM_SINE,
      .frequency_hz = 100.0,
      .sample_period_ns = 1000000,  // 1ms
      .amplitude = 1.0,
      .offset = 0.0,
      .phase_rad = 0.0,
      .max_samples = 1000,  // Limit output for testing
      .timeout_us = 1000000
  };
  CHECK_ERR(signal_generator_init(&source, source_config));

  Map_config_t multiply_by_2_config = {.name = "gain_x2",
                                       .buff_config = default_buffer_config(),
                                       .map_fcn = multiply_by_2,
                                       .timeout_us = 1000000};

  Map_config_t add_10_config = {.name = "offset_10",
                                .buff_config = default_buffer_config(),
                                .map_fcn = add_10,
                                .timeout_us = 1000000};

  CHECK_ERR(map_init(&gain_filter, multiply_by_2_config));
  CHECK_ERR(map_init(&offset_filter, add_10_config));

  Filter_t* filters[] = {&source.base, &gain_filter.base, &offset_filter.base};

  Connection_t connections[] = {
      {&source.base, 0, &gain_filter.base, 0},
      {&gain_filter.base, 0, &offset_filter.base, 0}
  };

  Pipeline_config_t config = {.name = "lifecycle_test",
                              .buff_config = default_buffer_config(),
                              .timeout_us = 1000000,
                              .filters = filters,
                              .n_filters = 3,
                              .connections = connections,
                              .n_connections = 2,
                              .input_filter = &source.base,
                              .input_port = 0,
                              .output_filter = &offset_filter.base,
                              .output_port = 0};

  Pipeline_t pipeline;
  CHECK_ERR(pipeline_init(&pipeline, config));

  // Test start/stop lifecycle
  CHECK_ERR(filt_start(&pipeline.base));
  TEST_ASSERT_TRUE(atomic_load(&pipeline.base.running));

  // Test enhanced describe function (uses filter->name from pointers)
  char buffer[1024];
  CHECK_ERR(filt_describe(&pipeline.base, buffer, sizeof(buffer)));
  TEST_ASSERT_TRUE(strstr(buffer, "lifecycle_test") != NULL);
  TEST_ASSERT_TRUE(strstr(buffer, gain_filter.base.name) != NULL);
  TEST_ASSERT_TRUE(strstr(buffer, offset_filter.base.name) != NULL);
  TEST_ASSERT_TRUE(strstr(buffer, "running") != NULL);

  CHECK_ERR(filt_stop(&pipeline.base));
  TEST_ASSERT_FALSE(atomic_load(&pipeline.base.running));

  filt_deinit(&pipeline.base);
  filt_deinit(&source.base);
  filt_deinit(&gain_filter.base);
  filt_deinit(&offset_filter.base);
}

void test_pipeline_connection_validation(void)
{
  Map_filt_t gain_filter;
  Map_filt_t other_filter;  // Filter not in pipeline

  Map_config_t multiply_by_2_config = {.name = "gain_x2",
                                       .buff_config = default_buffer_config(),
                                       .map_fcn = multiply_by_2,
                                       .timeout_us = 1000000};

  Map_config_t multiply_by_3_config = {.name = "gain_x3",
                                       .buff_config = default_buffer_config(),
                                       .map_fcn = multiply_by_3,
                                       .timeout_us = 1000000};

  CHECK_ERR(map_init(&gain_filter, multiply_by_2_config));
  CHECK_ERR(map_init(&other_filter, multiply_by_3_config));

  Filter_t* filters[] = {&gain_filter.base};

  // Test invalid connection - filter not in pipeline
  Connection_t bad_connections[] = {
      {&gain_filter.base, 0, &other_filter.base,
       0}  // other_filter not in pipeline
  };

  Pipeline_config_t config = {.name = "validation_test",
                              .buff_config = default_buffer_config(),
                              .timeout_us = 1000000,
                              .filters = filters,
                              .n_filters = 1,
                              .connections = bad_connections,
                              .n_connections = 1,
                              .input_filter = &gain_filter.base,
                              .input_port = 0,
                              .output_filter = &gain_filter.base,
                              .output_port = 0};

  Pipeline_t pipeline;
  // Should fail due to invalid connection (other_filter not in pipeline)
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, pipeline_init(&pipeline, config));

  filt_deinit(&gain_filter.base);
  filt_deinit(&other_filter.base);
}

void test_pipeline_null_checks(void)
{
  // Test null pipeline pointer
  Pipeline_config_t config = {.name = "null_test",
                              .buff_config = default_buffer_config(),
                              .timeout_us = 1000000,
                              .filters = NULL,
                              .n_filters = 0,
                              .connections = NULL,
                              .n_connections = 0,
                              .input_filter = NULL,
                              .input_port = 0,
                              .output_filter = NULL,
                              .output_port = 0};

  TEST_ASSERT_EQUAL(Bp_EC_NULL_POINTER, pipeline_init(NULL, config));

  // Test null filters array
  Pipeline_t pipeline;
  TEST_ASSERT_EQUAL(Bp_EC_NULL_POINTER, pipeline_init(&pipeline, config));

  // Test null input/output filter
  Map_filt_t test_filter;
  Map_config_t test_config = {.name = "test",
                              .buff_config = default_buffer_config(),
                              .map_fcn = map_identity_f32,
                              .timeout_us = 1000000};
  CHECK_ERR(map_init(&test_filter, test_config));

  Filter_t* filters[] = {&test_filter.base};
  config.filters = filters;
  config.n_filters = 1;

  // Null input filter
  config.input_filter = NULL;
  config.output_filter = &test_filter.base;
  TEST_ASSERT_EQUAL(Bp_EC_NULL_POINTER, pipeline_init(&pipeline, config));

  // Null output filter
  config.input_filter = &test_filter.base;
  config.output_filter = NULL;
  TEST_ASSERT_EQUAL(Bp_EC_NULL_POINTER, pipeline_init(&pipeline, config));

  filt_deinit(&test_filter.base);
}

/* Unity test runner */
int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_pipeline_linear_chain);
  RUN_TEST(test_pipeline_multi_branch_dag);
  RUN_TEST(test_pipeline_lifecycle_and_errors);
  RUN_TEST(test_pipeline_connection_validation);
  RUN_TEST(test_pipeline_null_checks);
  return UNITY_END();
}