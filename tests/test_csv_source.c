#define _GNU_SOURCE  // For usleep
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "csv_source.h"
#include "test_utils.h"
#include "unity.h"

// Test data directory
#define TEST_DATA_DIR "tests/data/"

// Helper to create test CSV files
static void create_test_csv(const char* filename, const char* content)
{
  FILE* f = fopen(filename, "w");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "Failed to create test CSV file");
  fprintf(f, "%s", content);
  fclose(f);
}

// Helper to connect CSV source to a test sink
static Batch_buff_t* create_test_sink(SampleDtype_t dtype,
                                      uint8_t batch_capacity_expo)
{
  Batch_buff_t* sink = malloc(sizeof(Batch_buff_t));
  TEST_ASSERT_NOT_NULL(sink);

  BatchBuffer_config config = {.dtype = dtype,
                               .batch_capacity_expo = batch_capacity_expo,
                               .ring_capacity_expo = 8,  // 256 batches
                               .overflow_behaviour = OVERFLOW_BLOCK};

  CHECK_ERR(bb_init(sink, "test_sink", config));
  CHECK_ERR(bb_start(sink));

  return sink;
}

void setUp(void)
{
  // Create test data directory if it doesn't exist
  mkdir(TEST_DATA_DIR, 0755);
}

void tearDown(void)
{
  // Clean up test files
}

void test_csv_source_init_valid_config(void)
{
  CsvSource_t source;

  CsvSource_config_t config = {.name = "test_csv",
                               .file_path = TEST_DATA_DIR "test.csv",
                               .delimiter = ',',
                               .has_header = true,
                               .ts_column_name = "ts_ns",
                               .data_column_names = {"value1", "value2", NULL},
                               .detect_regular_timing = true,
                               .regular_threshold_ns = 1000,
                               .timeout_us = 1000000};

  // Create test CSV file
  create_test_csv(config.file_path, "ts_ns,value1,value2\n1000,1.0,2.0\n");

  CHECK_ERR(csvsource_init(&source, config));

  csvsource_destroy(&source);
  unlink(config.file_path);
}

// This test is no longer applicable since batch_size was removed from config
void test_csv_source_init_invalid_config_placeholder(void)
{
  // Placeholder test - batch_size validation removed from API
  TEST_ASSERT_TRUE(true);
}

void test_csv_source_init_missing_file(void)
{
  CsvSource_t source;

  CsvSource_config_t config = {.name = "test_csv",
                               .file_path = TEST_DATA_DIR "nonexistent.csv",
                               .ts_column_name = "ts_ns",
                               .data_column_names = {"value", NULL},
                               .timeout_us = 1000000};

  Bp_EC err = csvsource_init(&source, config);
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, err);
}

void test_csv_source_parse_header(void)
{
  CsvSource_t source;

  const char* csv_content =
      "ts_ns,sensor1,sensor2,sensor3\n"
      "1000,1.1,2.2,3.3\n"
      "2000,4.4,5.5,6.6\n";

  CsvSource_config_t config = {
      .name = "test_csv",
      .file_path = TEST_DATA_DIR "header_test.csv",
      .delimiter = ',',
      .has_header = true,
      .ts_column_name = "ts_ns",
      .data_column_names = {"sensor1", "sensor3", NULL},
      .timeout_us = 1000000};

  create_test_csv(config.file_path, csv_content);

  CHECK_ERR(csvsource_init(&source, config));

  // Check column mappings
  TEST_ASSERT_EQUAL(0, source.ts_column_index);
  TEST_ASSERT_EQUAL(1, source.data_column_indices[0]);  // sensor1
  TEST_ASSERT_EQUAL(3, source.data_column_indices[1]);  // sensor3
  TEST_ASSERT_EQUAL(2, source.n_data_columns);

  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_missing_column(void)
{
  CsvSource_t source;

  const char* csv_content =
      "ts_ns,sensor1,sensor2\n"
      "1000,1.1,2.2\n";

  CsvSource_config_t config = {
      .name = "test_csv",
      .file_path = TEST_DATA_DIR "missing_col.csv",
      .delimiter = ',',
      .has_header = true,
      .ts_column_name = "ts_ns",
      .data_column_names = {"sensor1", "sensor3",
                            NULL},  // sensor3 doesn't exist
      .timeout_us = 1000000};

  create_test_csv(config.file_path, csv_content);

  Bp_EC err = csvsource_init(&source, config);
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, err);

  unlink(config.file_path);
}

void test_csv_source_regular_data(void)
{
  CsvSource_t source;

  // Regular 1kHz data
  const char* csv_content =
      "ts_ns,value\n"
      "1000000,1.0\n"
      "2000000,2.0\n"
      "3000000,3.0\n"
      "4000000,4.0\n"
      "5000000,5.0\n";

  CsvSource_config_t config = {.name = "test_csv",
                               .file_path = TEST_DATA_DIR "regular.csv",
                               .delimiter = ',',
                               .has_header = true,
                               .ts_column_name = "ts_ns",
                               .data_column_names = {"value", NULL},
                               .detect_regular_timing = true,
                               .regular_threshold_ns = 1000,
                               .timeout_us = 1000000};

  create_test_csv(config.file_path, csv_content);

  CHECK_ERR(csvsource_init(&source, config));

  // Create and connect sink
  Batch_buff_t* sink = create_test_sink(DTYPE_FLOAT, 2);  // 2^2 = 4 samples
  CHECK_ERR(filt_sink_connect(&source.base, 0, sink));

  // Start filter
  CHECK_ERR(filt_start(&source.base));

  // Read first batch
  Bp_EC read_err;
  Batch_t* batch = bb_get_tail(sink, 1000000, &read_err);
  TEST_ASSERT_EQUAL(Bp_EC_OK, read_err);
  TEST_ASSERT_NOT_NULL(batch);

  // Check batch metadata
  TEST_ASSERT_EQUAL(4, batch->head - batch->tail);  // Number of samples
  TEST_ASSERT_EQUAL(1000000, batch->t_ns);
  TEST_ASSERT_EQUAL(1000000, batch->period_ns);  // 1ms period

  // Check data values
  float* data = (float*) batch->data;
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, data[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 2.0, data[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 3.0, data[2]);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 4.0, data[3]);

  bb_del_tail(sink);

  // Stop and cleanup
  filt_stop(&source.base);
  bb_stop(sink);
  bb_deinit(sink);
  free(sink);
  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_irregular_data(void)
{
  CsvSource_t source;

  // Irregular event data
  const char* csv_content =
      "ts_ns,event_value\n"
      "1000000,10.5\n"
      "1500000,20.5\n"
      "3000000,30.5\n"
      "3100000,40.5\n";

  CsvSource_config_t config = {
      .name = "test_csv",
      .file_path = TEST_DATA_DIR "irregular.csv",
      .delimiter = ',',
      .has_header = true,
      .ts_column_name = "ts_ns",
      .data_column_names = {"event_value", NULL},
      .detect_regular_timing = false,  // Force irregular mode
      .timeout_us = 1000000};

  create_test_csv(config.file_path, csv_content);

  CHECK_ERR(csvsource_init(&source, config));

  // Create and connect sink
  Batch_buff_t* sink = create_test_sink(DTYPE_FLOAT, 6);  // 2^6 = 64 samples
  CHECK_ERR(filt_sink_connect(&source.base, 0, sink));

  // Start filter
  CHECK_ERR(filt_start(&source.base));

  // Read batches - should be one sample per batch
  for (int i = 0; i < 4; i++) {
    Bp_EC read_err;
    Batch_t* batch = bb_get_tail(sink, 1000000, &read_err);
    TEST_ASSERT_EQUAL(Bp_EC_OK, read_err);
    TEST_ASSERT_NOT_NULL(batch);

    // Each batch should have exactly 1 sample
    TEST_ASSERT_EQUAL(1, batch->head - batch->tail);  // Number of samples
    TEST_ASSERT_EQUAL(0, batch->period_ns);           // Irregular data

    bb_del_tail(sink);
  }

  // Stop and cleanup
  filt_stop(&source.base);
  bb_stop(sink);
  bb_deinit(sink);
  free(sink);
  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_timing_gap(void)
{
  CsvSource_t source;

  // Regular data with a gap
  const char* csv_content =
      "ts_ns,value\n"
      "1000000,1.0\n"
      "2000000,2.0\n"
      "3000000,3.0\n"
      "8000000,8.0\n"  // 5ms gap
      "9000000,9.0\n";

  CsvSource_config_t config = {.name = "test_csv",
                               .file_path = TEST_DATA_DIR "gap.csv",
                               .delimiter = ',',
                               .has_header = true,
                               .ts_column_name = "ts_ns",
                               .data_column_names = {"value", NULL},
                               .detect_regular_timing = true,
                               .regular_threshold_ns = 10000,  // 10μs tolerance
                               .timeout_us = 1000000};

  create_test_csv(config.file_path, csv_content);

  CHECK_ERR(csvsource_init(&source, config));

  // Create and connect sink
  Batch_buff_t* sink = create_test_sink(DTYPE_FLOAT, 3);  // 2^3 = 8 samples
  CHECK_ERR(filt_sink_connect(&source.base, 0, sink));

  // Start filter
  CHECK_ERR(filt_start(&source.base));

  // First batch - regular data before gap
  Bp_EC read_err;
  Batch_t* batch1 = bb_get_tail(sink, 1000000, &read_err);
  TEST_ASSERT_EQUAL(Bp_EC_OK, read_err);
  TEST_ASSERT_NOT_NULL(batch1);
  TEST_ASSERT_EQUAL(3, batch1->head - batch1->tail);  // Number of samples
  TEST_ASSERT_EQUAL(1000000, batch1->t_ns);
  TEST_ASSERT_EQUAL(1000000, batch1->period_ns);
  bb_del_tail(sink);

  // Second batch - after gap
  Batch_t* batch2 = bb_get_tail(sink, 1000000, &read_err);
  TEST_ASSERT_EQUAL(Bp_EC_OK, read_err);
  TEST_ASSERT_NOT_NULL(batch2);
  TEST_ASSERT_EQUAL(8000000, batch2->t_ns);
  TEST_ASSERT_EQUAL(1000000, batch2->period_ns);  // Should maintain same period
  bb_del_tail(sink);

  // Stop and cleanup
  filt_stop(&source.base);
  bb_stop(sink);
  bb_deinit(sink);
  free(sink);
  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_loop_mode(void)
{
  CsvSource_t source;

  const char* csv_content =
      "ts_ns,value\n"
      "1000,1.0\n"
      "2000,2.0\n";

  CsvSource_config_t config = {
      .name = "test_csv",
      .file_path = TEST_DATA_DIR "loop.csv",
      .delimiter = ',',
      .has_header = true,
      .ts_column_name = "ts_ns",
      .data_column_names = {"value", NULL},
      .detect_regular_timing = false,
      .loop = true,         // Enable looping
      .timeout_us = 100000  // 100ms timeout
  };

  create_test_csv(config.file_path, csv_content);

  CHECK_ERR(csvsource_init(&source, config));

  // Create and connect sink
  Batch_buff_t* sink = create_test_sink(DTYPE_FLOAT, 0);  // 2^0 = 1 sample
  CHECK_ERR(filt_sink_connect(&source.base, 0, sink));

  // Start filter
  CHECK_ERR(filt_start(&source.base));

  // Read more batches than in file to test looping
  float expected_values[] = {1.0, 2.0, 1.0, 2.0, 1.0};
  for (int i = 0; i < 5; i++) {
    Bp_EC read_err;
    Batch_t* batch = bb_get_tail(sink, 100000, &read_err);
    TEST_ASSERT_EQUAL(Bp_EC_OK, read_err);
    TEST_ASSERT_NOT_NULL(batch);

    float* data = (float*) batch->data;
    TEST_ASSERT_FLOAT_WITHIN(0.001, expected_values[i], data[0]);

    bb_del_tail(sink);
  }

  // Stop and cleanup
  filt_stop(&source.base);
  bb_stop(sink);
  bb_deinit(sink);
  free(sink);
  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_skip_invalid_rows(void)
{
  CsvSource_t source;

  const char* csv_content =
      "ts_ns,value\n"
      "1000,1.0\n"
      "2000,invalid\n"  // Invalid number
      "3000,3.0\n"
      "bad_timestamp,4.0\n"  // Invalid timestamp
      "5000,5.0\n";

  CsvSource_config_t config = {.name = "test_csv",
                               .file_path = TEST_DATA_DIR "invalid.csv",
                               .delimiter = ',',
                               .has_header = true,
                               .ts_column_name = "ts_ns",
                               .data_column_names = {"value", NULL},
                               .detect_regular_timing = false,
                               .skip_invalid = true,  // Skip invalid rows
                               .timeout_us = 1000000};

  create_test_csv(config.file_path, csv_content);

  CHECK_ERR(csvsource_init(&source, config));

  // Create and connect sink
  Batch_buff_t* sink = create_test_sink(DTYPE_FLOAT, 6);  // 2^6 = 64 samples
  CHECK_ERR(filt_sink_connect(&source.base, 0, sink));

  // Start filter
  CHECK_ERR(filt_start(&source.base));

  // Should only get valid rows
  float expected_values[] = {1.0, 3.0, 5.0};
  uint64_t expected_ts[] = {1000, 3000, 5000};

  for (int i = 0; i < 3; i++) {
    Bp_EC read_err;
    Batch_t* batch = bb_get_tail(sink, 1000000, &read_err);
    TEST_ASSERT_EQUAL(Bp_EC_OK, read_err);
    TEST_ASSERT_NOT_NULL(batch);

    TEST_ASSERT_EQUAL(expected_ts[i], batch->t_ns);
    float* data = (float*) batch->data;
    TEST_ASSERT_FLOAT_WITHIN(0.001, expected_values[i], data[0]);

    bb_del_tail(sink);
  }

  // Stop and cleanup
  filt_stop(&source.base);
  bb_stop(sink);
  bb_deinit(sink);
  free(sink);
  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_multi_channel(void)
{
  CsvSource_t source;

  const char* csv_content =
      "ts_ns,x,y,z,temp\n"
      "1000,1.1,2.2,3.3,25.5\n"
      "2000,4.4,5.5,6.6,26.0\n";

  CsvSource_config_t config = {
      .name = "test_csv",
      .file_path = TEST_DATA_DIR "multi_channel.csv",
      .delimiter = ',',
      .has_header = true,
      .ts_column_name = "ts_ns",
      .data_column_names = {"x", "y", "z", "temp", NULL},
      .detect_regular_timing = true,
      .regular_threshold_ns = 100,
      .timeout_us = 1000000};

  create_test_csv(config.file_path, csv_content);

  CHECK_ERR(csvsource_init(&source, config));
  TEST_ASSERT_EQUAL(4, source.n_data_columns);

  // Create and connect separate sinks for each data column (as per spec)
  Batch_buff_t* sinks[4];
  for (int i = 0; i < 4; i++) {
    sinks[i] = create_test_sink(DTYPE_FLOAT, 1);  // 2^1 = 2 samples
    CHECK_ERR(filt_sink_connect(&source.base, i, sinks[i]));
  }

  // Start filter
  CHECK_ERR(filt_start(&source.base));

  // Read batch from each column sink
  Bp_EC read_err;
  Batch_t* batches[4];

  for (int i = 0; i < 4; i++) {
    batches[i] = bb_get_tail(sinks[i], 1000000, &read_err);
    TEST_ASSERT_EQUAL(Bp_EC_OK, read_err);
    TEST_ASSERT_NOT_NULL(batches[i]);
    TEST_ASSERT_EQUAL(
        2, batches[i]->head - batches[i]->tail);     // Number of samples
    TEST_ASSERT_EQUAL(1000, batches[i]->period_ns);  // Regular timing detected
  }

  // Verify data for each column
  float* x_data = (float*) batches[0]->data;
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.1, x_data[0]);  // First x
  TEST_ASSERT_FLOAT_WITHIN(0.001, 4.4, x_data[1]);  // Second x

  float* y_data = (float*) batches[1]->data;
  TEST_ASSERT_FLOAT_WITHIN(0.001, 2.2, y_data[0]);  // First y
  TEST_ASSERT_FLOAT_WITHIN(0.001, 5.5, y_data[1]);  // Second y

  float* z_data = (float*) batches[2]->data;
  TEST_ASSERT_FLOAT_WITHIN(0.001, 3.3, z_data[0]);  // First z
  TEST_ASSERT_FLOAT_WITHIN(0.001, 6.6, z_data[1]);  // Second z

  float* temp_data = (float*) batches[3]->data;
  TEST_ASSERT_FLOAT_WITHIN(0.001, 25.5, temp_data[0]);  // First temp
  TEST_ASSERT_FLOAT_WITHIN(0.001, 26.0, temp_data[1]);  // Second temp

  // Clean up batches
  for (int i = 0; i < 4; i++) {
    bb_del_tail(sinks[i]);
  }

  // Stop and cleanup
  filt_stop(&source.base);
  for (int i = 0; i < 4; i++) {
    bb_stop(sinks[i]);
    bb_deinit(sinks[i]);
    free(sinks[i]);
  }
  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_line_too_long(void)
{
  CsvSource_t source;

  // Create a line that exceeds LINE_BUFFER_SIZE (4096)
  char long_line[5000];
  for (int i = 0; i < 4999; i++) {
    long_line[i] = '1';
  }
  long_line[4999] = '\0';

  char* csv_content = malloc(6000);
  TEST_ASSERT_NOT_NULL(csv_content);
  sprintf(csv_content, "ts_ns,value\n1000,%s\n", long_line);

  CsvSource_config_t config = {.name = "test_csv",
                               .file_path = TEST_DATA_DIR "long_line.csv",
                               .delimiter = ',',
                               .has_header = true,
                               .ts_column_name = "ts_ns",
                               .data_column_names = {"value", NULL},
                               .skip_invalid = false,
                               .timeout_us = 100000};

  create_test_csv(config.file_path, csv_content);
  free(csv_content);

  CHECK_ERR(csvsource_init(&source, config));

  // Create and connect sink
  Batch_buff_t* sink = create_test_sink(DTYPE_FLOAT, 6);  // 2^6 = 64 samples
  CHECK_ERR(filt_sink_connect(&source.base, 0, sink));

  // Start filter
  CHECK_ERR(filt_start(&source.base));

  // Wait a bit for worker to process and hit the error
  usleep(200000);

  // Check that worker detected the error
  TEST_ASSERT_FALSE(atomic_load(&source.base.running));
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_DATA, source.base.worker_err_info.ec);

  // Cleanup
  bb_stop(sink);
  bb_deinit(sink);
  free(sink);
  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_describe_operation(void)
{
  CsvSource_t source;
  char buffer[1024];

  const char* csv_content = "ts_ns,sensor1,sensor2\n1000,1.1,2.2\n";

  CsvSource_config_t config = {
      .name = "test_csv_describe",
      .file_path = TEST_DATA_DIR "describe_test.csv",
      .delimiter = ',',
      .has_header = true,
      .ts_column_name = "ts_ns",
      .data_column_names = {"sensor1", "sensor2", NULL},
      .timeout_us = 1000000};

  create_test_csv(config.file_path, csv_content);

  CHECK_ERR(csvsource_init(&source, config));

  // Test describe operation
  CHECK_ERR(source.base.ops.describe(&source.base, buffer, sizeof(buffer)));

  // Verify description contains expected information
  TEST_ASSERT_NOT_NULL(strstr(buffer, "CsvSource"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "test_csv_describe"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, config.file_path));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "ts_ns"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "sensor1"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "sensor2"));

  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_get_stats_operation(void)
{
  CsvSource_t source;
  Filt_metrics stats;

  const char* csv_content = "ts_ns,value\n1000,1.0\n2000,2.0\n3000,3.0\n";

  CsvSource_config_t config = {.name = "test_csv_stats",
                               .file_path = TEST_DATA_DIR "stats_test.csv",
                               .delimiter = ',',
                               .has_header = true,
                               .ts_column_name = "ts_ns",
                               .data_column_names = {"value", NULL},
                               .timeout_us = 100000};

  create_test_csv(config.file_path, csv_content);

  CHECK_ERR(csvsource_init(&source, config));

  // Create and connect sink
  Batch_buff_t* sink = create_test_sink(DTYPE_FLOAT, 1);  // 2^1 = 2 samples
  CHECK_ERR(filt_sink_connect(&source.base, 0, sink));

  // Start filter
  CHECK_ERR(filt_start(&source.base));

  // Read batches
  Bp_EC read_err;
  Batch_t* batch1 = bb_get_tail(sink, 1000000, &read_err);
  TEST_ASSERT_EQUAL(Bp_EC_OK, read_err);
  TEST_ASSERT_NOT_NULL(batch1);
  bb_del_tail(sink);

  // Get stats
  CHECK_ERR(source.base.ops.get_stats(&source.base, &stats));

  // Verify stats
  TEST_ASSERT_TRUE(stats.samples_processed >= 2);
  TEST_ASSERT_TRUE(stats.n_batches >= 1);

  // Stop and cleanup
  filt_stop(&source.base);
  bb_stop(sink);
  bb_deinit(sink);
  free(sink);
  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_different_sink_batch_sizes(void)
{
  CsvSource_t source;

  const char* csv_content =
      "ts_ns,value1,value2\n"
      "1000,1.0,2.0\n"
      "2000,1.1,2.1\n";

  CsvSource_config_t config = {.name = "test_csv_batch_mismatch",
                               .file_path = TEST_DATA_DIR "batch_mismatch.csv",
                               .delimiter = ',',
                               .has_header = true,
                               .ts_column_name = "ts_ns",
                               .data_column_names = {"value1", "value2", NULL},
                               .timeout_us = 100000};

  create_test_csv(config.file_path, csv_content);
  CHECK_ERR(csvsource_init(&source, config));

  // Create sinks with different batch capacities
  Batch_buff_t* sink1 = create_test_sink(DTYPE_FLOAT, 6);  // 64 samples
  Batch_buff_t* sink2 =
      create_test_sink(DTYPE_FLOAT, 4);  // 16 samples - DIFFERENT!

  CHECK_ERR(filt_sink_connect(&source.base, 0, sink1));
  CHECK_ERR(filt_sink_connect(&source.base, 1, sink2));

  // Start filter - worker should detect batch size mismatch
  CHECK_ERR(filt_start(&source.base));

  // Let worker thread start and detect the mismatch
  usleep(10000);  // 10ms

  // Stop filter
  filt_stop(&source.base);

  // Check that worker detected the mismatch
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, source.base.worker_err_info.ec);

  // Cleanup
  bb_stop(sink1);
  bb_stop(sink2);
  bb_deinit(sink1);
  bb_deinit(sink2);
  free(sink1);
  free(sink2);
  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_concurrent_stop(void)
{
  CsvSource_t source;

  // Create a large CSV file that takes time to process
  const char* csv_header = "ts_ns,value\n";

  CsvSource_config_t config = {
      .name = "test_csv_concurrent",
      .file_path = TEST_DATA_DIR "concurrent_test.csv",
      .delimiter = ',',
      .has_header = true,
      .ts_column_name = "ts_ns",
      .data_column_names = {"value", NULL},
      .loop = true,  // Enable looping to ensure continuous processing
      .timeout_us = 100000};

  // Create test file with many rows
  FILE* f = fopen(config.file_path, "w");
  TEST_ASSERT_NOT_NULL(f);
  fprintf(f, "%s", csv_header);
  for (int i = 0; i < 1000; i++) {
    fprintf(f, "%d,%f\n", i * 1000, (float) i);
  }
  fclose(f);

  CHECK_ERR(csvsource_init(&source, config));

  // Create and connect sink
  Batch_buff_t* sink = create_test_sink(DTYPE_FLOAT, 6);  // 2^6 = 64 samples
  CHECK_ERR(filt_sink_connect(&source.base, 0, sink));

  // Start filter
  CHECK_ERR(filt_start(&source.base));

  // Let it process for a bit
  usleep(50000);

  // Verify it's running
  TEST_ASSERT_TRUE(atomic_load(&source.base.running));

  // Stop from main thread while worker is processing
  filt_stop(&source.base);

  // Verify clean shutdown
  TEST_ASSERT_FALSE(atomic_load(&source.base.running));
  TEST_ASSERT_EQUAL(Bp_EC_STOPPED, source.base.worker_err_info.ec);

  // Cleanup
  bb_stop(sink);
  bb_deinit(sink);
  free(sink);
  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_empty_file(void)
{
  CsvSource_t source;

  // Test with header only
  const char* csv_content = "ts_ns,value\n";

  CsvSource_config_t config = {.name = "test_csv_empty",
                               .file_path = TEST_DATA_DIR "empty_test.csv",
                               .delimiter = ',',
                               .has_header = true,
                               .ts_column_name = "ts_ns",
                               .data_column_names = {"value", NULL},
                               .timeout_us = 100000};

  create_test_csv(config.file_path, csv_content);

  CHECK_ERR(csvsource_init(&source, config));

  // Create and connect sink
  Batch_buff_t* sink = create_test_sink(DTYPE_FLOAT, 6);  // 2^6 = 64 samples
  CHECK_ERR(filt_sink_connect(&source.base, 0, sink));

  // Start filter
  CHECK_ERR(filt_start(&source.base));

  // Should complete immediately without error
  usleep(500000);  // Give more time for worker to complete

  // Join the worker thread to ensure it's finished
  filt_stop(&source.base);

  // Verify filter stopped cleanly
  TEST_ASSERT_FALSE(atomic_load(&source.base.running));

  // Cleanup
  bb_stop(sink);
  bb_deinit(sink);
  free(sink);
  csvsource_destroy(&source);
  unlink(config.file_path);
}

void test_csv_source_worker_error_info(void)
{
  CsvSource_t source;

  // Create CSV with invalid data when skip_invalid=false
  const char* csv_content = "ts_ns,value\n1000,invalid_number\n";

  CsvSource_config_t config = {
      .name = "test_csv_error",
      .file_path = TEST_DATA_DIR "error_test.csv",
      .delimiter = ',',
      .has_header = true,
      .ts_column_name = "ts_ns",
      .data_column_names = {"value", NULL},
      .skip_invalid = false,  // Will cause error on invalid data
      .timeout_us = 100000};

  create_test_csv(config.file_path, csv_content);

  CHECK_ERR(csvsource_init(&source, config));

  // Create and connect sink
  Batch_buff_t* sink = create_test_sink(DTYPE_FLOAT, 6);  // 2^6 = 64 samples
  CHECK_ERR(filt_sink_connect(&source.base, 0, sink));

  // Start filter
  CHECK_ERR(filt_start(&source.base));

  // Wait for error
  usleep(200000);

  // Verify error info is set
  TEST_ASSERT_FALSE(atomic_load(&source.base.running));
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_DATA, source.base.worker_err_info.ec);
  TEST_ASSERT_NOT_NULL(source.base.worker_err_info.filename);
  TEST_ASSERT_NOT_NULL(source.base.worker_err_info.function);
  TEST_ASSERT_TRUE(source.base.worker_err_info.line_no > 0);

  // Cleanup
  bb_stop(sink);
  bb_deinit(sink);
  free(sink);
  csvsource_destroy(&source);
  unlink(config.file_path);
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_csv_source_init_valid_config);
  RUN_TEST(test_csv_source_init_invalid_config_placeholder);
  RUN_TEST(test_csv_source_init_missing_file);
  RUN_TEST(test_csv_source_parse_header);
  RUN_TEST(test_csv_source_missing_column);
  RUN_TEST(test_csv_source_regular_data);
  RUN_TEST(test_csv_source_irregular_data);
  RUN_TEST(test_csv_source_timing_gap);
  RUN_TEST(test_csv_source_loop_mode);
  RUN_TEST(test_csv_source_skip_invalid_rows);
  RUN_TEST(test_csv_source_multi_channel);

  // New error path tests
  RUN_TEST(test_csv_source_line_too_long);
  RUN_TEST(test_csv_source_worker_error_info);
  RUN_TEST(test_csv_source_empty_file);

  // New operations tests
  RUN_TEST(test_csv_source_describe_operation);
  RUN_TEST(test_csv_source_get_stats_operation);

  // New thread safety tests
  RUN_TEST(test_csv_source_concurrent_stop);
  RUN_TEST(test_csv_source_different_sink_batch_sizes);

  return UNITY_END();
}