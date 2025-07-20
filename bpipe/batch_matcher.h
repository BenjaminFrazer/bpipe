#ifndef BPIPE_BATCH_MATCHER_H
#define BPIPE_BATCH_MATCHER_H

#include "batch_buffer.h"
#include "core.h"

typedef struct _BatchMatcher_config_t {
  const char* name;
  BatchBuffer_config buff_config;  // For input buffer only
} BatchMatcher_config_t;

typedef struct _BatchMatcher_t {
  Filter_t base;

  // Auto-detected configuration
  size_t output_batch_samples;  // From sink's batch_capacity_expo
  bool size_detected;           // Has sink been connected?

  // Runtime state
  uint64_t period_ns;         // From first input batch
  uint64_t batch_period_ns;   // period_ns * output_batch_samples
  uint64_t next_boundary_ns;  // Next output batch start time

  // Accumulation
  void* accumulator;            // Building current output batch
  size_t accumulator_capacity;  // Allocated size of accumulator
  size_t accumulated;           // Samples in accumulator
  size_t data_width;            // Size of each sample

  // Statistics
  uint64_t samples_processed;
  uint64_t batches_matched;
  uint64_t samples_skipped;  // Before first boundary
} BatchMatcher_t;

// Filter operations
Bp_EC batch_matcher_init(BatchMatcher_t* matcher, BatchMatcher_config_t config);
void* batch_matcher_worker(void* arg);

#endif /* BPIPE_BATCH_MATCHER_H */
