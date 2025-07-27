#define _GNU_SOURCE  // For usleep
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../bpipe/csv_sink.h"
#include "../bpipe/signal_generator.h"
#include "../lib/Unity/src/unity.h"

// Check error macro
#define CHECK_ERR(expr)                                        \
  do {                                                         \
    Bp_EC _err = (expr);                                       \
    if (_err != Bp_EC_OK) {                                    \
      printf("Error %d at %s:%d\n", _err, __FILE__, __LINE__); \
      TEST_FAIL();                                             \
    }                                                          \
  } while (0)

// Test helpers
static bool file_exists(const char* path) { return access(path, F_OK) == 0; }

static size_t count_lines(const char* path)
{
  FILE* f = fopen(path, "r");
  if (!f) return 0;

  size_t lines = 0;
  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), f)) {
    lines++;
  }
  fclose(f);
  return lines;
}

static bool file_contains(const char* path, const char* text)
{
  FILE* f = fopen(path, "r");
  if (!f) return false;

  char buffer[1024];
  bool found = false;
  while (fgets(buffer, sizeof(buffer), f)) {
    if (strstr(buffer, text)) {
      found = true;
      break;
    }
  }
  fclose(f);
  return found;
}

// Test basic CSV write functionality
void test_basic_csv_write(void)
{
  const char* output_file = "test_basic.csv";

  // Remove file if exists
  unlink(output_file);

  // Create signal generator as source
  SignalGenerator_t source;
  SignalGenerator_config_t source_cfg = {
      .name = "test_source",
      .waveform_type = WAVEFORM_SINE,
      .frequency_hz = 1.0,
      .sample_period_ns = 1000000,  // 1ms
      .amplitude = 1.0,
      .max_samples = 100,
      .buff_config = {
          .dtype = DTYPE_FLOAT,
          .batch_capacity_expo = 6,  // 64 samples
          .ring_capacity_expo = 2    // 4 batches
      }};
  CHECK_ERR(signal_generator_init(&source, source_cfg));

  // Create CSV sink
  CSVSink_t sink;
  CSVSink_config_t sink_cfg = {.name = "test_sink",
                               .output_path = output_file,
                               .format = CSV_FORMAT_SIMPLE,
                               .write_header = true,
                               .precision = 3,
                               .buff_config = {.dtype = DTYPE_FLOAT,
                                               .batch_capacity_expo = 6,
                                               .ring_capacity_expo = 2}};
  CHECK_ERR(csv_sink_init(&sink, sink_cfg));

  // Connect source -> sink
  CHECK_ERR(filt_sink_connect(&source.base, 0, &sink.base.input_buffers[0]));

  // Start filters
  CHECK_ERR(filt_start(&source.base));
  CHECK_ERR(filt_start(&sink.base));

  // Wait for completion
  while (atomic_load(&source.base.running) || atomic_load(&sink.base.running)) {
    usleep(10000);  // 10ms
  }

  // Stop sink
  CHECK_ERR(filt_stop(&sink.base));
  CHECK_ERR(filt_stop(&source.base));

  // Verify file exists
  TEST_ASSERT_TRUE_MESSAGE(file_exists(output_file), "Output file not created");

  // Verify header
  TEST_ASSERT_TRUE_MESSAGE(file_contains(output_file, "timestamp_ns,value"),
                           "Header not found in output file");

  // Verify line count (header + 100 samples)
  size_t lines = count_lines(output_file);
  TEST_ASSERT_EQUAL_MESSAGE(101, lines, "Incorrect number of lines in output");

  // Verify nanosecond timestamps
  TEST_ASSERT_TRUE_MESSAGE(file_contains(output_file, "000000,"),
                           "Nanosecond timestamps not found");

  // Check worker errors
  CHECK_ERR(source.base.worker_err_info.ec);
  CHECK_ERR(sink.base.worker_err_info.ec);

  // Cleanup
  unlink(output_file);
}

// Test multi-column output
void test_multi_column_output(void)
{
  // Skip this test for now - needs proper multi-channel source
  // The signal generator produces single values, not vector data
  TEST_IGNORE_MESSAGE(
      "Multi-column test needs multi-channel source implementation");
}

// Test file size limit
void test_file_size_limit(void)
{
  const char* output_file = "test_size_limit.csv";
  unlink(output_file);

  // Create source with timeout to detect when sink stops
  SignalGenerator_t source;
  SignalGenerator_config_t source_cfg = {
      .name = "test_source",
      .waveform_type = WAVEFORM_SINE,
      .frequency_hz = 1.0,
      .sample_period_ns = 1000000,
      .amplitude = 1.0,
      .max_samples = 1000,   // Many samples
      .timeout_us = 100000,  // 100ms timeout
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 2}};
  CHECK_ERR(signal_generator_init(&source, source_cfg));

  // Create sink with small size limit
  CSVSink_t sink;
  CSVSink_config_t sink_cfg = {.name = "test_sink",
                               .output_path = output_file,
                               .format = CSV_FORMAT_SIMPLE,
                               .write_header = true,
                               .max_file_size_bytes = 1000,  // Very small limit
                               .buff_config = {.dtype = DTYPE_FLOAT,
                                               .batch_capacity_expo = 6,
                                               .ring_capacity_expo = 2}};
  CHECK_ERR(csv_sink_init(&sink, sink_cfg));

  // Connect and run
  CHECK_ERR(filt_sink_connect(&source.base, 0, &sink.base.input_buffers[0]));
  CHECK_ERR(filt_start(&source.base));
  CHECK_ERR(filt_start(&sink.base));

  // Wait for sink to hit the file size limit
  // The sink will stop on its own when it hits the limit
  int wait_count = 0;
  while (atomic_load(&sink.base.running)) {
    usleep(10000);
    wait_count++;
    if (wait_count > 200) {  // 2 second timeout
      printf("WARNING: Sink did not stop within 2 seconds\n");
      break;
    }
  }

  // Stop both filters - this will unblock the source if it's waiting
  CHECK_ERR(filt_stop(&sink.base));
  CHECK_ERR(filt_stop(&source.base));

  // Verify sink stopped due to file size limit
  TEST_ASSERT_EQUAL(Bp_EC_NO_SPACE, sink.base.worker_err_info.ec);

  // Source should get FILTER_STOPPING error when downstream filter stops
  TEST_ASSERT_EQUAL_MESSAGE(
      Bp_EC_FILTER_STOPPING, source.base.worker_err_info.ec,
      "Source should get FILTER_STOPPING error when sink stops");

  // Verify file exists and is around the limit
  TEST_ASSERT_TRUE(file_exists(output_file));

  // Get actual file size
  FILE* f = fopen(output_file, "r");
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fclose(f);

  TEST_ASSERT_TRUE_MESSAGE(size <= 1100, "File size exceeded limit too much");

  unlink(output_file);
}

// Test error handling
void test_error_handling(void)
{
  // Test invalid output path (directory that doesn't exist)
  CSVSink_t sink;
  CSVSink_config_t cfg = {.name = "test_sink",
                          .output_path = "/nonexistent/directory/test.csv",
                          .format = CSV_FORMAT_SIMPLE,
                          .buff_config = {.dtype = DTYPE_FLOAT,
                                          .batch_capacity_expo = 6,
                                          .ring_capacity_expo = 2}};

  Bp_EC err = csv_sink_init(&sink, cfg);
  TEST_ASSERT_NOT_EQUAL(Bp_EC_OK, err);

  // Test permission denied (root directory)
  cfg.output_path = "/test_permission_denied.csv";
  err = csv_sink_init(&sink, cfg);
  TEST_ASSERT_NOT_EQUAL(Bp_EC_OK, err);

  // Test null path
  cfg.output_path = NULL;
  err = csv_sink_init(&sink, cfg);
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, err);
}

// Test completion handling
void test_completion_handling(void)
{
  const char* output_file = "test_completion.csv";
  unlink(output_file);

  // Create source with small sample count
  SignalGenerator_t source;
  SignalGenerator_config_t source_cfg = {
      .name = "test_source",
      .waveform_type = WAVEFORM_SINE,
      .frequency_hz = 1.0,
      .sample_period_ns = 1000000,
      .amplitude = 1.0,
      .max_samples = 50,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 2}};
  CHECK_ERR(signal_generator_init(&source, source_cfg));

  // Create sink
  CSVSink_t sink;
  CSVSink_config_t sink_cfg = {.name = "test_sink",
                               .output_path = output_file,
                               .format = CSV_FORMAT_SIMPLE,
                               .write_header = true,
                               .buff_config = {.dtype = DTYPE_FLOAT,
                                               .batch_capacity_expo = 6,
                                               .ring_capacity_expo = 2}};
  CHECK_ERR(csv_sink_init(&sink, sink_cfg));

  // Connect and run
  CHECK_ERR(filt_sink_connect(&source.base, 0, &sink.base.input_buffers[0]));
  CHECK_ERR(filt_start(&source.base));
  CHECK_ERR(filt_start(&sink.base));

  // Wait for completion
  while (atomic_load(&source.base.running) || atomic_load(&sink.base.running)) {
    usleep(10000);
  }

  CHECK_ERR(filt_stop(&sink.base));
  CHECK_ERR(filt_stop(&source.base));

  // Verify all samples written
  size_t lines = count_lines(output_file);
  TEST_ASSERT_EQUAL(51, lines);  // Header + 50 samples

  // Verify metrics
  TEST_ASSERT_EQUAL(50, sink.samples_written);
  TEST_ASSERT_EQUAL(50, sink.base.metrics.samples_processed);

  CHECK_ERR(source.base.worker_err_info.ec);
  CHECK_ERR(sink.base.worker_err_info.ec);

  unlink(output_file);
}

// Unity test runner
void setUp(void) {}
void tearDown(void) {}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_basic_csv_write);
  RUN_TEST(test_multi_column_output);
  RUN_TEST(test_file_size_limit);
  RUN_TEST(test_error_handling);
  RUN_TEST(test_completion_handling);

  return UNITY_END();
}