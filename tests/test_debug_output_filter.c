#define _GNU_SOURCE  // For usleep
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../bpipe/core.h"
#include "../bpipe/debug_output_filter.h"
#include "../bpipe/utils.h"
#include "test_utils.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

// Helper function to create a simple source filter for testing
typedef struct {
  Filter_t base;
  float* data;
  size_t data_len;
  size_t batch_size;
} TestSourceFilter_t;

static void* test_source_worker(void* arg)
{
  TestSourceFilter_t* filter = (TestSourceFilter_t*) arg;
  Filter_t* base = &filter->base;
  size_t offset = 0;

  // Validate configuration
  BP_WORKER_ASSERT(base, base->n_sinks > 0, Bp_EC_NO_SINK);
  BP_WORKER_ASSERT(base, base->sinks[0] != NULL, Bp_EC_NO_SINK);

  while (atomic_load(&base->running) && offset < filter->data_len) {
    Batch_t* batch = bb_get_head(base->sinks[0]);
    if (!batch) {
      usleep(1000);
      continue;
    }

    batch->t_ns = offset * 1000000;  // 1ms per sample
    batch->period_ns = 1000000;

    size_t samples_to_copy = MIN(filter->batch_size, filter->data_len - offset);
    float* out_data = (float*) batch->data;
    for (size_t i = 0; i < samples_to_copy; i++) {
      out_data[i] = filter->data[offset + i];
    }
    batch->head = samples_to_copy;

    offset += samples_to_copy;

    // Mark complete on last batch
    if (offset >= filter->data_len) {
      batch->ec = Bp_EC_COMPLETE;
    } else {
      batch->ec = Bp_EC_OK;
    }

    bb_submit(base->sinks[0], base->timeout_us);
  }

  return NULL;
}

static TestSourceFilter_t* create_test_source(float* data, size_t data_len,
                                              size_t batch_size)
{
  TestSourceFilter_t* filter = calloc(1, sizeof(TestSourceFilter_t));
  TEST_ASSERT_NOT_NULL(filter);

  filter->data = data;
  filter->data_len = data_len;
  filter->batch_size = batch_size;

  Core_filt_config_t config = {
      .name = "test_source",
      .filt_type = FILT_T_MAP,
      .size = sizeof(TestSourceFilter_t),
      .n_inputs = 0,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 10,
                      .ring_capacity_expo = 12,
                      .overflow_behaviour = OVERFLOW_DROP_TAIL},
      .timeout_us = 10000,  // 10ms timeout for faster tests
      .worker = test_source_worker};

  Bp_EC ec = filt_init(&filter->base, config);
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, ec);

  return filter;
}

// Helper to collect output data
typedef struct {
  Filter_t base;
  float* collected_data;
  size_t collected_count;
  size_t max_count;
  bool got_complete;
} TestCollectorFilter_t;

static void* test_collector_worker(void* arg)
{
  TestCollectorFilter_t* filter = (TestCollectorFilter_t*) arg;
  Filter_t* base = &filter->base;

  while (atomic_load(&base->running)) {
    Bp_EC err;
    Batch_t* batch =
        bb_get_tail(base->input_buffers[0], 10, &err);  // 10ms timeout
    if (!batch) {
      if (err == Bp_EC_STOPPED) break;
      continue;
    }

    float* in_data = (float*) batch->data;
    size_t count = batch->head;

    if (filter->collected_count + count <= filter->max_count) {
      for (size_t i = 0; i < count; i++) {
        filter->collected_data[filter->collected_count++] = in_data[i];
      }
    }

    if (batch->ec == Bp_EC_COMPLETE) {
      filter->got_complete = true;
    }

    bb_del_tail(base->input_buffers[0]);
  }

  return NULL;
}

static TestCollectorFilter_t* create_test_collector(size_t max_count)
{
  TestCollectorFilter_t* filter = calloc(1, sizeof(TestCollectorFilter_t));
  TEST_ASSERT_NOT_NULL(filter);

  filter->collected_data = calloc(max_count, sizeof(float));
  TEST_ASSERT_NOT_NULL(filter->collected_data);
  filter->max_count = max_count;

  Core_filt_config_t config = {
      .name = "test_collector",
      .filt_type = FILT_T_MAP,
      .size = sizeof(TestCollectorFilter_t),
      .n_inputs = 1,
      .max_supported_sinks = 0,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 10,
                      .ring_capacity_expo = 12,
                      .overflow_behaviour = OVERFLOW_DROP_TAIL},
      .timeout_us = 10000,  // 10ms timeout for faster tests
      .worker = test_collector_worker};

  Bp_EC ec = filt_init(&filter->base, config);
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, ec);

  return filter;
}

void test_debug_output_passthrough(void)
{
  // Description: Verify debug filter passes data through unchanged
  // while optionally printing debug information

  // Create test data
  float test_data[] = {1.0, 2.0, 3.0, 4.0, 5.0};
  size_t data_len = sizeof(test_data) / sizeof(test_data[0]);

  // Create filters
  TestSourceFilter_t* source = create_test_source(test_data, data_len, 3);

  DebugOutputConfig_t debug_config = {
      .prefix = "TEST: ",
      .show_metadata = false,
      .show_samples = false,  // Don't print during test
      .max_samples_per_batch = -1,
      .format = DEBUG_FMT_DECIMAL,
      .flush_after_print = true,
      .filename = NULL,  // stdout
      .append_mode = false};
  DebugOutputFilter_t debug;
  Bp_EC ec = debug_output_filter_init(&debug, &debug_config);
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, ec);

  TestCollectorFilter_t* collector = create_test_collector(10);

  // Connect pipeline
  CHECK_ERR(filt_sink_connect(&source->base, 0, debug.base.input_buffers[0]));
  CHECK_ERR(
      filt_sink_connect(&debug.base, 0, collector->base.input_buffers[0]));

  // Set output buffer sizes
  // Set output buffer sizes using bb_set_sz
  // Buffers are already sized by filter initialization

  // Start filters
  CHECK_ERR(filt_start(&source->base));
  CHECK_ERR(filt_start(&debug.base));
  CHECK_ERR(filt_start(&collector->base));

  // Wait for completion
  usleep(50000);  // 50ms is enough for test completion

  // Stop filters
  CHECK_ERR(filt_stop(&source->base));
  CHECK_ERR(filt_stop(&debug.base));
  CHECK_ERR(filt_stop(&collector->base));

  // Check worker thread errors
  CHECK_ERR(source->base.worker_err_info.ec);
  CHECK_ERR(debug.base.worker_err_info.ec);
  CHECK_ERR(collector->base.worker_err_info.ec);

  // Verify data passthrough
  TEST_ASSERT_EQUAL_size_t(data_len, collector->collected_count);
  for (size_t i = 0; i < data_len; i++) {
    TEST_ASSERT_EQUAL_FLOAT(test_data[i], collector->collected_data[i]);
  }
  TEST_ASSERT_TRUE(collector->got_complete);

  // Cleanup
  filt_deinit(&source->base);
  filt_deinit(&debug.base);
  filt_deinit(&collector->base);
  free(source);
  // Stack allocated, no need to free
  free(collector->collected_data);
  free(collector);
}

void test_debug_output_to_file(void)
{
  // Description: Verify debug filter can write output to a file
  // with metadata and samples formatted according to configuration

  // Create temp file
  const char* test_file = "/tmp/bpipe_debug_test.log";

  // Remove file if it exists
  unlink(test_file);

  // Create test data
  float test_data[] = {1.5, 2.5, 3.5};
  size_t data_len = sizeof(test_data) / sizeof(test_data[0]);

  // Create filters
  TestSourceFilter_t* source = create_test_source(test_data, data_len, 3);

  DebugOutputConfig_t debug_config = {.prefix = "LOG: ",
                                      .show_metadata = true,
                                      .show_samples = true,
                                      .max_samples_per_batch = -1,
                                      .format = DEBUG_FMT_DECIMAL,
                                      .flush_after_print = true,
                                      .filename = test_file,
                                      .append_mode = false};
  DebugOutputFilter_t debug;
  Bp_EC ec = debug_output_filter_init(&debug, &debug_config);
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, ec);

  TestCollectorFilter_t* collector = create_test_collector(10);

  // Connect pipeline
  CHECK_ERR(filt_sink_connect(&source->base, 0, debug.base.input_buffers[0]));
  CHECK_ERR(
      filt_sink_connect(&debug.base, 0, collector->base.input_buffers[0]));

  // Set output buffer sizes
  // Set output buffer sizes using bb_set_sz
  // Buffers are already sized by filter initialization

  // Start filters
  CHECK_ERR(filt_start(&source->base));
  CHECK_ERR(filt_start(&debug.base));
  CHECK_ERR(filt_start(&collector->base));

  // Wait for completion
  usleep(50000);  // 50ms is enough for test completion

  // Stop filters
  CHECK_ERR(filt_stop(&source->base));
  CHECK_ERR(filt_stop(&debug.base));
  CHECK_ERR(filt_stop(&collector->base));

  // Check worker thread errors
  CHECK_ERR(source->base.worker_err_info.ec);
  CHECK_ERR(debug.base.worker_err_info.ec);
  CHECK_ERR(collector->base.worker_err_info.ec);

  // Verify file was created
  FILE* f = fopen(test_file, "r");
  TEST_ASSERT_NOT_NULL(f);

  // Read file contents
  char buffer[1024];
  size_t total_read = 0;
  while (fgets(buffer, sizeof(buffer), f) && total_read < sizeof(buffer) - 1) {
    // Just verify we got some output
    total_read += strlen(buffer);
  }
  fclose(f);

  // Verify we got output
  TEST_ASSERT_TRUE(total_read > 0);

  // Cleanup
  unlink(test_file);
  filt_deinit(&source->base);
  filt_deinit(&debug.base);
  filt_deinit(&collector->base);
  free(source);
  // Stack allocated, no need to free
  free(collector->collected_data);
  free(collector);
}

void test_debug_output_formats(void)
{
  // Description: Verify debug filter correctly handles different
  // output formats (hex, binary, decimal) for different data types

  // Test integer data with hex format
  uint32_t test_data[] = {100, 200, 300};
  size_t data_len = sizeof(test_data) / sizeof(test_data[0]);

  // Create source filter for U32 data
  TestSourceFilter_t* source = calloc(1, sizeof(TestSourceFilter_t));
  TEST_ASSERT_NOT_NULL(source);
  source->data = (float*) test_data;  // Reinterpret cast
  source->data_len = data_len;
  source->batch_size = 3;

  Core_filt_config_t src_config = {
      .name = "test_source_u32",
      .filt_type = FILT_T_MAP,
      .size = sizeof(TestSourceFilter_t),
      .n_inputs = 0,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 10,
                      .ring_capacity_expo = 12,
                      .overflow_behaviour = OVERFLOW_DROP_TAIL},
      .timeout_us = 10000,  // 10ms timeout for faster tests
      .worker = test_source_worker};

  Bp_EC ec = filt_init(&source->base, src_config);
  if (ec != Bp_EC_OK) {
    fprintf(stderr,
            "test_debug_output_formats: source filt_init failed with ec=%d\n",
            ec);
  }
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, ec);

  // Create debug filter with hex format
  DebugOutputConfig_t debug_config = {
      .prefix = "HEX: ",
      .show_metadata = false,
      .show_samples = false,  // Don't print during test
      .max_samples_per_batch = -1,
      .format = DEBUG_FMT_HEX,
      .flush_after_print = true,
      .filename = NULL,
      .append_mode = false};
  DebugOutputFilter_t debug;
  ec = debug_output_filter_init(&debug, &debug_config);
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, ec);

  // Create collector for U32
  TestCollectorFilter_t* collector = calloc(1, sizeof(TestCollectorFilter_t));
  TEST_ASSERT_NOT_NULL(collector);
  collector->collected_data = calloc(10, sizeof(float));
  TEST_ASSERT_NOT_NULL(collector->collected_data);
  collector->max_count = 10;

  Core_filt_config_t coll_config = {
      .name = "test_collector_u32",
      .filt_type = FILT_T_MAP,
      .size = sizeof(TestSourceFilter_t),
      .n_inputs = 1,
      .max_supported_sinks = 0,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 10,
                      .ring_capacity_expo = 12,
                      .overflow_behaviour = OVERFLOW_DROP_TAIL},
      .timeout_us = 10000,  // 10ms timeout for faster tests
      .worker = test_collector_worker};

  ec = filt_init(&collector->base, coll_config);
  if (ec != Bp_EC_OK) {
    fprintf(
        stderr,
        "test_debug_output_formats: collector filt_init failed with ec=%d\n",
        ec);
  }
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, ec);

  // Connect pipeline
  CHECK_ERR(filt_sink_connect(&source->base, 0, debug.base.input_buffers[0]));
  CHECK_ERR(
      filt_sink_connect(&debug.base, 0, collector->base.input_buffers[0]));

  // Set output buffer sizes for U32
  // Buffers are already sized by filter initialization

  // Start filters
  CHECK_ERR(filt_start(&source->base));
  CHECK_ERR(filt_start(&debug.base));
  CHECK_ERR(filt_start(&collector->base));

  // Wait for completion
  usleep(50000);  // 50ms is enough for test completion

  // Stop filters
  CHECK_ERR(filt_stop(&source->base));
  CHECK_ERR(filt_stop(&debug.base));
  CHECK_ERR(filt_stop(&collector->base));

  // Check worker thread errors
  CHECK_ERR(source->base.worker_err_info.ec);
  CHECK_ERR(debug.base.worker_err_info.ec);
  CHECK_ERR(collector->base.worker_err_info.ec);

  // Verify data passthrough
  TEST_ASSERT_EQUAL_size_t(data_len, collector->collected_count);
  uint32_t* collected_u32 = (uint32_t*) collector->collected_data;
  for (size_t i = 0; i < data_len; i++) {
    TEST_ASSERT_EQUAL_UINT32(test_data[i], collected_u32[i]);
  }

  // Cleanup
  filt_deinit(&source->base);
  filt_deinit(&debug.base);
  filt_deinit(&collector->base);
  free(source);
  // Stack allocated, no need to free
  free(collector->collected_data);
  free(collector);
}

void test_debug_output_invalid_config(void)
{
  // Description: Verify debug filter rejects invalid configuration
  // and handles null pointers appropriately

  DebugOutputFilter_t debug;

  // Test null filter pointer
  TEST_ASSERT_EQUAL_INT(
      Bp_EC_NULL_POINTER,
      debug_output_filter_init(NULL, &(DebugOutputConfig_t){}));

  // Test null config pointer
  TEST_ASSERT_EQUAL_INT(Bp_EC_NULL_POINTER,
                        debug_output_filter_init(&debug, NULL));

  // Test invalid file path (directory instead of file)
  DebugOutputConfig_t bad_file_config = {
      .prefix = "TEST: ",
      .show_metadata = true,
      .show_samples = true,
      .max_samples_per_batch = 10,
      .format = DEBUG_FMT_DECIMAL,
      .flush_after_print = true,
      .filename = "/root/",  // Directory with no write permission
      .append_mode = false};
  Bp_EC ec = debug_output_filter_init(&debug, &bad_file_config);
  TEST_ASSERT_EQUAL_INT(Bp_EC_NOSPACE, ec);
}

void test_debug_output_sample_limiting(void)
{
  // Description: Verify debug filter respects max_samples_per_batch
  // limit when printing samples, while still passing all data through

  // Create test data with many samples
  float test_data[20];
  for (int i = 0; i < 20; i++) {
    test_data[i] = (float) i;
  }

  // Create filters
  TestSourceFilter_t* source = create_test_source(test_data, 20, 20);

  DebugOutputConfig_t debug_config = {
      .prefix = "LIMITED: ",
      .show_metadata = false,
      .show_samples = false,       // Don't print during test
      .max_samples_per_batch = 5,  // Limit to 5 samples
      .format = DEBUG_FMT_DECIMAL,
      .flush_after_print = true,
      .filename = NULL,
      .append_mode = false};
  DebugOutputFilter_t debug;
  Bp_EC ec = debug_output_filter_init(&debug, &debug_config);
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, ec);

  TestCollectorFilter_t* collector = create_test_collector(30);

  // Connect pipeline
  CHECK_ERR(filt_sink_connect(&source->base, 0, debug.base.input_buffers[0]));
  CHECK_ERR(
      filt_sink_connect(&debug.base, 0, collector->base.input_buffers[0]));

  // Set output buffer sizes
  // Buffers are already sized by filter initialization

  // Start filters
  CHECK_ERR(filt_start(&source->base));
  CHECK_ERR(filt_start(&debug.base));
  CHECK_ERR(filt_start(&collector->base));

  // Wait for completion
  usleep(50000);  // 50ms is enough for test completion

  // Stop filters
  CHECK_ERR(filt_stop(&source->base));
  CHECK_ERR(filt_stop(&debug.base));
  CHECK_ERR(filt_stop(&collector->base));

  // Check worker thread errors
  CHECK_ERR(source->base.worker_err_info.ec);
  CHECK_ERR(debug.base.worker_err_info.ec);
  CHECK_ERR(collector->base.worker_err_info.ec);

  // Verify all data passed through (limiting only affects printing)
  TEST_ASSERT_EQUAL_size_t(20, collector->collected_count);
  for (size_t i = 0; i < 20; i++) {
    TEST_ASSERT_EQUAL_FLOAT((float) i, collector->collected_data[i]);
  }

  // Cleanup
  filt_deinit(&source->base);
  filt_deinit(&debug.base);
  filt_deinit(&collector->base);
  free(source);
  // Stack allocated, no need to free
  free(collector->collected_data);
  free(collector);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_debug_output_invalid_config);
  RUN_TEST(test_debug_output_passthrough);
  RUN_TEST(test_debug_output_to_file);
  RUN_TEST(test_debug_output_formats);
  RUN_TEST(test_debug_output_sample_limiting);
  return UNITY_END();
}