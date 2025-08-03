/**
 * @file common.c
 * @brief Common implementation for filter compliance tests
 */

#include "common.h"

// Global state for current test
FilterRegistration_t* g_filters = NULL;
size_t g_n_filters = 0;
size_t g_current_filter = 0;

// Current filter under test
Filter_t* g_fut = NULL;
void* g_fut_config = NULL;
FilterInitFunc g_fut_init = NULL;
const char* g_filter_name = NULL;

// Performance metrics storage
PerfMetrics_t g_last_perf_metrics;
char g_perf_report[8192];

// Test timing
uint64_t g_test_start_ns = 0;

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
        printf("WARNING: filt_deinit failed with error %d (%s)\n", err,
               err_lut[err]);
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