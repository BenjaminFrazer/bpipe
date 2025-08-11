/**
 * @file main.c
 * @brief Main test runner for filter compliance tests
 */

#include <stddef.h>  // for offsetof
#include "common.h"

// Declare all test functions
void test_lifecycle_basic(void);
void test_lifecycle_with_worker(void);
void test_lifecycle_restart(void);
void test_lifecycle_errors(void);
void test_connection_single_sink(void);
void test_connection_multi_sink(void);
void test_connection_type_safety(void);
void test_dataflow_passthrough(void);
void test_dataflow_backpressure(void);
void test_error_invalid_config(void);
void test_error_timeout(void);
void test_thread_worker_lifecycle(void);
void test_thread_shutdown_sync(void);
void test_perf_throughput(void);
void test_buffer_minimum_size(void);
void test_buffer_overflow_drop_head(void);
void test_buffer_overflow_drop_tail(void);
void test_buffer_large_batches(void);

// Behavioral compliance tests
void test_partial_batch_handling(void);
void test_data_type_compatibility(void);
void test_sample_rate_preservation(void);
void test_data_integrity(void);

// Example default configurations for testing
static ControllableProducerConfig_t default_producer_config = {
    .name = "default_producer",
    .timeout_us = 1000000,
    .samples_per_second = 50000,
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
                    .ring_capacity_expo =
                        5,  // Reduced to 32 slots for backpressure testing
                    .overflow_behaviour = OVERFLOW_BLOCK},
    .timeout_us = 1000000};

// Structure to hold test function and its name
typedef struct {
  void (*test_func)(void);
  const char* test_name;
  const char* test_file;
} ComplianceTest_t;

// All compliance tests as Unity test functions with names
static ComplianceTest_t compliance_tests[] = {
    // Lifecycle tests
    {test_lifecycle_basic, "test_lifecycle_basic",
     "tests/filter_compliance/test_lifecycle_basic.c"},
    {test_lifecycle_with_worker, "test_lifecycle_with_worker",
     "tests/filter_compliance/test_lifecycle_with_worker.c"},
    {test_lifecycle_restart, "test_lifecycle_restart",
     "tests/filter_compliance/test_lifecycle_restart.c"},
    {test_lifecycle_errors, "test_lifecycle_errors",
     "tests/filter_compliance/test_lifecycle_errors.c"},

    // Connection tests
    {test_connection_single_sink, "test_connection_single_sink",
     "tests/filter_compliance/test_connection_single_sink.c"},
    {test_connection_multi_sink, "test_connection_multi_sink",
     "tests/filter_compliance/test_connection_multi_sink.c"},
    {test_connection_type_safety, "test_connection_type_safety",
     "tests/filter_compliance/test_connection_type_safety.c"},

    // Data flow tests
    {test_dataflow_passthrough, "test_dataflow_passthrough",
     "tests/filter_compliance/test_dataflow_passthrough.c"},
    {test_dataflow_backpressure, "test_dataflow_backpressure",
     "tests/filter_compliance/test_dataflow_backpressure.c"},

    // Error handling tests
    {test_error_invalid_config, "test_error_invalid_config",
     "tests/filter_compliance/test_error_invalid_config.c"},
    {test_error_timeout, "test_error_timeout",
     "tests/filter_compliance/test_error_timeout.c"},

    // Threading tests
    {test_thread_worker_lifecycle, "test_thread_worker_lifecycle",
     "tests/filter_compliance/test_thread_worker_lifecycle.c"},
    {test_thread_shutdown_sync, "test_thread_shutdown_sync",
     "tests/filter_compliance/test_thread_shutdown_sync.c"},

    // Performance tests
    {test_perf_throughput, "test_perf_throughput",
     "tests/filter_compliance/test_perf_throughput.c"},
    // {test_perf_latency, "test_perf_latency",
    // "tests/filter_compliance/test_perf_latency.c"},  // TODO: Implement
    // passthrough_metrics filter

    // Buffer configuration tests
    {test_buffer_minimum_size, "test_buffer_minimum_size",
     "tests/filter_compliance/test_buffer_edge_cases.c"},
    {test_buffer_overflow_drop_head, "test_buffer_overflow_drop_head",
     "tests/filter_compliance/test_buffer_edge_cases.c"},
    {test_buffer_overflow_drop_tail, "test_buffer_overflow_drop_tail",
     "tests/filter_compliance/test_buffer_edge_cases.c"},
    {test_buffer_large_batches, "test_buffer_large_batches",
     "tests/filter_compliance/test_buffer_edge_cases.c"},

    // Behavioral compliance tests
    {test_partial_batch_handling, "test_partial_batch_handling",
     "tests/filter_compliance/test_behavioral_compliance.c"},
    {test_data_type_compatibility, "test_data_type_compatibility",
     "tests/filter_compliance/test_behavioral_compliance.c"},
    {test_sample_rate_preservation, "test_sample_rate_preservation",
     "tests/filter_compliance/test_behavioral_compliance.c"},
    {test_data_integrity, "test_data_integrity",
     "tests/filter_compliance/test_behavioral_compliance.c"},
};

int main(int argc, char* argv[])
{
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
      // Example: Test the mock filters themselves
      {.name = "ControllableProducer",
       .filter_size = sizeof(ControllableProducer_t),
       .init = controllable_producer_init_wrapper,
       .default_config = &default_producer_config,
       .config_size = sizeof(ControllableProducerConfig_t),
       .buff_config_offset = 0,  // Producer has no buffer config
       .has_buff_config = false},
      {.name = "ControllableConsumer",
       .filter_size = sizeof(ControllableConsumer_t),
       .init = controllable_consumer_init_wrapper,
       .default_config = &default_consumer_config,
       .config_size = sizeof(ControllableConsumerConfig_t),
       .buff_config_offset =
           offsetof(ControllableConsumerConfig_t, buff_config),
       .has_buff_config = true},
      {.name = "Passthrough",
       .filter_size = sizeof(Passthrough_t),
       .init = passthrough_init_wrapper,
       .default_config = &default_passthrough_config,
       .config_size = sizeof(Passthrough_config_t),
       .buff_config_offset = offsetof(Passthrough_config_t, buff_config),
       .has_buff_config = true},
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
      // Skip if test doesn't match pattern
      if (test_pattern &&
          !strstr(compliance_tests[i].test_name, test_pattern)) {
        continue;
      }

      // Set the correct file for this test
      UnitySetTestFile(compliance_tests[i].test_file);

      // Call UnityDefaultTestRun directly with our test name
      UnityDefaultTestRun(compliance_tests[i].test_func,
                          compliance_tests[i].test_name, __LINE__);
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
