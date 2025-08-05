/**
 * @file common.h
 * @brief Common types and helpers for filter compliance tests
 */

#ifndef TEST_FILTER_COMPLIANCE_COMMON_H
#define TEST_FILTER_COMPLIANCE_COMMON_H

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

// Filter registration for test harness
typedef struct {
  const char* name;      // Filter name for reporting
  size_t filter_size;    // sizeof(MyFilter_t)
  FilterInitFunc init;   // Filter's init function
  void* default_config;  // Default configuration
  size_t config_size;    // sizeof(MyFilterConfig_t)
  // Buffer configuration metadata
  size_t buff_config_offset;  // Offset of BatchBuffer_config in filter's config struct
  bool has_buff_config;       // Whether this filter uses buffer configuration
} FilterRegistration_t;

// Predefined buffer profiles for different test scenarios
typedef enum {
  BUFF_PROFILE_DEFAULT,      // Standard config (6/8)
  BUFF_PROFILE_TINY,         // Minimum sizes (2/2) - edge case testing
  BUFF_PROFILE_SMALL,        // Small buffers (4/3) - backpressure testing
  BUFF_PROFILE_LARGE,        // Large buffers (10/10) - performance testing
  BUFF_PROFILE_BACKPRESSURE, // Normal batch, tiny ring (6/2)
  BUFF_PROFILE_PERF,         // Optimized for throughput (10/8)
} BufferProfile_t;

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
extern FilterRegistration_t* g_filters;
extern size_t g_n_filters;
extern size_t g_current_filter;

// Current filter under test
extern Filter_t* g_fut;
extern void* g_fut_config;
extern FilterInitFunc g_fut_init;
extern const char* g_filter_name;

// Performance metrics storage
extern PerfMetrics_t g_last_perf_metrics;
extern char g_perf_report[8192];

// Test timing
extern uint64_t g_test_start_ns;

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
static inline uint64_t get_time_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Wrapper functions for init
static inline Bp_EC passthrough_init_wrapper(void* filter, void* config)
{
  return passthrough_init((Passthrough_t*) filter,
                          (Passthrough_config_t*) config);
}

static inline Bp_EC controllable_producer_init_wrapper(void* filter,
                                                       void* config)
{
  if (!config) {
    return Bp_EC_NULL_POINTER;
  }
  ControllableProducerConfig_t* cfg = (ControllableProducerConfig_t*) config;
  return controllable_producer_init((ControllableProducer_t*) filter, *cfg);
}

static inline Bp_EC controllable_consumer_init_wrapper(void* filter,
                                                       void* config)
{
  if (!config) {
    return Bp_EC_NULL_POINTER;
  }
  ControllableConsumerConfig_t* cfg = (ControllableConsumerConfig_t*) config;
  return controllable_consumer_init((ControllableConsumer_t*) filter, *cfg);
}

// Apply buffer profile to filter configuration
void apply_buffer_profile(void* filter_config, size_t buff_config_offset, 
                         BufferProfile_t profile);

// Unity setUp/tearDown - implemented in common.c
void setUp(void);
void tearDown(void);

#endif  // TEST_FILTER_COMPLIANCE_COMMON_H