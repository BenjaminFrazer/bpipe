#include "batch_matcher.h"
#include <stdlib.h>
#include <string.h>

// Custom filter operations
static Bp_EC batch_matcher_start_impl(Filter_t* self)
{
  BatchMatcher_t* bm = (BatchMatcher_t*) self;

  // Verify sink connected
  if (!bm->size_detected || self->sinks[0] == NULL) {
    return Bp_EC_NO_SINK;
  }

  // Do the default start actions
  if (self->running) {
    return Bp_EC_ALREADY_RUNNING;
  }
  self->running = true;
  if (pthread_create(&self->worker_thread, NULL, self->worker, (void*) self) !=
      0) {
    self->running = false;
    return Bp_EC_THREAD_CREATE_FAIL;
  }
  return Bp_EC_OK;
}

static Bp_EC batch_matcher_stop(Filter_t* self)
{
  // Do default stop actions
  if (!self->running) {
    return Bp_EC_OK;  // Already stopped, not an error
  }

  self->running = false;

  // Stop all input buffers to wake up any waiting threads
  for (int i = 0; i < self->n_input_buffers; i++) {
    if (self->input_buffers[i].data_ring != NULL) {
      bb_stop(&self->input_buffers[i]);
    }
  }

  if (pthread_join(self->worker_thread, NULL) != 0) {
    return Bp_EC_THREAD_JOIN_FAIL;
  }

  return Bp_EC_OK;
}

static Bp_EC batch_matcher_deinit(Filter_t* self)
{
  BatchMatcher_t* bm = (BatchMatcher_t*) self;

  // Free accumulator
  if (bm->accumulator != NULL) {
    free(bm->accumulator);
    bm->accumulator = NULL;
  }

  // Do default deinit actions
  for (int i = 0; i < self->n_input_buffers; i++) {
    Bp_EC rc = bb_deinit(&self->input_buffers[i]);
    if (rc != Bp_EC_OK) {
      return rc;
    }
  }

  // Destroy mutex
  pthread_mutex_destroy(&self->filter_mutex);

  return Bp_EC_OK;
}

static Bp_EC batch_matcher_get_stats(Filter_t* self, void* stats_out)
{
  BatchMatcher_t* bm = (BatchMatcher_t*) self;

  if (stats_out == NULL) {
    return Bp_EC_NULL_POINTER;
  }

  // Custom stats structure
  typedef struct {
    Filt_metrics base;
    uint64_t samples_processed;
    uint64_t batches_matched;
    uint64_t samples_skipped;
  } BatchMatcherStats;

  BatchMatcherStats* stats = (BatchMatcherStats*) stats_out;
  stats->base = self->metrics;
  stats->samples_processed = bm->samples_processed;
  stats->batches_matched = bm->batches_matched;
  stats->samples_skipped = bm->samples_skipped;

  return Bp_EC_OK;
}

static Bp_EC batch_matcher_describe(Filter_t* self, char* buffer,
                                    size_t buffer_size)
{
  BatchMatcher_t* bm = (BatchMatcher_t*) self;

  if (buffer == NULL) {
    return Bp_EC_NULL_POINTER;
  }

  snprintf(buffer, buffer_size,
           "BatchMatcher: %s\n"
           "  Output batch size: %zu samples\n"
           "  Batch period: %lu ns\n"
           "  Samples processed: %lu\n"
           "  Batches matched: %lu\n"
           "  Samples skipped: %lu",
           self->name, bm->output_batch_samples, bm->batch_period_ns,
           bm->samples_processed, bm->batches_matched, bm->samples_skipped);

  return Bp_EC_OK;
}

Bp_EC batch_matcher_init(BatchMatcher_t* matcher, BatchMatcher_config_t config)
{
  if (matcher == NULL) {
    return Bp_EC_NULL_FILTER;
  }

  // Initialize base filter
  Core_filt_config_t core_config = {.name = config.name,
                                    .filt_type = FILT_T_BATCH_MATCHER,
                                    .size = sizeof(BatchMatcher_t),
                                    .n_inputs = 1,
                                    .max_supported_sinks = 1,
                                    .buff_config = config.buff_config,
                                    .timeout_us = 1000000,  // 1 second default
                                    .worker = batch_matcher_worker};

  Bp_EC ec = filt_init(&matcher->base, core_config);
  if (ec != Bp_EC_OK) {
    return ec;
  }

  // Set custom operations
  matcher->base.ops.start = batch_matcher_start_impl;
  matcher->base.ops.stop = batch_matcher_stop;
  matcher->base.ops.deinit = batch_matcher_deinit;
  matcher->base.ops.get_stats = batch_matcher_get_stats;
  matcher->base.ops.describe = batch_matcher_describe;

  // Initialize BatchMatcher specific fields
  matcher->output_batch_samples = 0;
  matcher->size_detected = false;
  matcher->period_ns = 0;
  matcher->batch_period_ns = 0;
  matcher->next_boundary_ns = 0;
  matcher->accumulator = NULL;
  matcher->accumulator_capacity = 0;
  matcher->accumulated = 0;
  matcher->data_width = bb_getdatawidth(config.buff_config.dtype);
  matcher->samples_processed = 0;
  matcher->batches_matched = 0;
  matcher->samples_skipped = 0;

  return Bp_EC_OK;
}

void* batch_matcher_worker(void* arg)
{
  BatchMatcher_t* bm = (BatchMatcher_t*) arg;
  Filter_t* f = &bm->base;

  // Verify sink connected (should be checked in filt_start)
  if (!bm->size_detected) {
    f->worker_err_info.ec = Bp_EC_NO_SINK;
    f->worker_err_info.function = __FUNCTION__;
    f->worker_err_info.filename = __FILE__;
    f->worker_err_info.line_no = __LINE__;
    f->running = false;
    return NULL;
  }

  Bp_EC err = Bp_EC_OK;
  Batch_t* input_batch = NULL;
  Batch_t* output_batch = NULL;
  bool first_batch = true;

  while (f->running) {
    // Get input batch
    input_batch = bb_get_tail(&f->input_buffers[0], f->timeout_us, &err);
    if (err != Bp_EC_OK) {
      if (err == Bp_EC_TIMEOUT) {
        continue;
      } else if (err == Bp_EC_COMPLETE || err == Bp_EC_STOPPED) {
        // Propagate completion
        if (bm->accumulated > 0 && output_batch != NULL) {
          // Flush partial batch
          output_batch->head = 0;
          output_batch->tail = bm->accumulated;
          output_batch->batch_id = bm->batches_matched++;
          bb_submit(f->sinks[0], f->timeout_us);
          output_batch = NULL;
        }
        // Send completion signal
        Batch_t* complete_batch = bb_get_head(f->sinks[0]);
        if (complete_batch != NULL) {
          complete_batch->head = 0;
          complete_batch->tail = 0;
          complete_batch->ec = Bp_EC_COMPLETE;
          bb_submit(f->sinks[0], f->timeout_us);
        }
        break;
      }
      BP_WORKER_ASSERT(f, false, err);
    }

    // Initialize on first batch
    if (first_batch) {
      bm->period_ns = input_batch->period_ns;

      // Validate period_ns
      if (bm->period_ns == 0) {
        bb_del_tail(&f->input_buffers[0]);
        f->worker_err_info.ec = Bp_EC_INVALID_CONFIG;
        f->worker_err_info.err_msg =
            "BatchMatcher requires regular sampling (period_ns > 0)";
        f->worker_err_info.function = __FUNCTION__;
        f->worker_err_info.filename = __FILE__;
        f->worker_err_info.line_no = __LINE__;
        f->running = false;
        return NULL;
      }

      // Validate phase alignment
      uint64_t phase_offset = input_batch->t_ns % bm->period_ns;
      if (phase_offset != 0) {
        bb_del_tail(&f->input_buffers[0]);
        f->worker_err_info.ec = Bp_EC_PHASE_ERROR;
        f->worker_err_info.err_msg =
            "Input has non-integer sample phase. "
            "Use SampleAligner filter to correct phase offset.";
        f->worker_err_info.function = __FUNCTION__;
        f->worker_err_info.filename = __FILE__;
        f->worker_err_info.line_no = __LINE__;
        f->running = false;
        return NULL;
      }

      bm->batch_period_ns = bm->period_ns * bm->output_batch_samples;

      // Align to t=0 (or nearest batch boundary before first sample)
      bm->next_boundary_ns = 0;
      while (bm->next_boundary_ns + bm->batch_period_ns <= input_batch->t_ns) {
        bm->next_boundary_ns += bm->batch_period_ns;
      }

      // Allocate accumulator
      bm->accumulator_capacity = bm->output_batch_samples * bm->data_width;
      bm->accumulator = malloc(bm->accumulator_capacity);
      if (bm->accumulator == NULL) {
        bb_del_tail(&f->input_buffers[0]);
        f->worker_err_info.ec = Bp_EC_MALLOC_FAIL;
        f->worker_err_info.function = __FUNCTION__;
        f->worker_err_info.filename = __FILE__;
        f->worker_err_info.line_no = __LINE__;
        f->running = false;
        return NULL;
      }

      first_batch = false;
    }

    // Process input batch
    size_t input_samples = input_batch->head - input_batch->tail;
    size_t input_idx = 0;
    uint64_t current_timestamp =
        input_batch->t_ns + (input_batch->tail * bm->period_ns);

    while (input_idx < input_samples && f->running) {
      // Skip samples before next boundary
      if (current_timestamp < bm->next_boundary_ns) {
        bm->samples_skipped++;
        input_idx++;
        current_timestamp += bm->period_ns;
        continue;
      }

      // Get output batch if needed
      if (output_batch == NULL) {
        output_batch = bb_get_head(f->sinks[0]);
        if (output_batch == NULL) {
          bb_del_tail(&f->input_buffers[0]);
          BP_WORKER_ASSERT(f, false, Bp_EC_NOSPACE);
        }

        // Set output batch metadata
        output_batch->t_ns = bm->next_boundary_ns;
        output_batch->period_ns = bm->period_ns;
        output_batch->tail = 0;
        output_batch->head = 0;
        output_batch->ec = Bp_EC_OK;
        bm->accumulated = 0;
      }

      // Copy samples to accumulator
      size_t samples_to_copy = input_samples - input_idx;
      size_t space_in_accumulator = bm->output_batch_samples - bm->accumulated;
      if (samples_to_copy > space_in_accumulator) {
        samples_to_copy = space_in_accumulator;
      }

      // Check if we'll cross a boundary
      uint64_t end_timestamp =
          current_timestamp + (samples_to_copy * bm->period_ns);
      if (end_timestamp > bm->next_boundary_ns + bm->batch_period_ns) {
        // Only copy up to the boundary
        samples_to_copy =
            (bm->next_boundary_ns + bm->batch_period_ns - current_timestamp) /
            bm->period_ns;
      }

      // Copy data
      void* src = (char*) input_batch->data +
                  ((input_batch->tail + input_idx) * bm->data_width);
      void* dst =
          (char*) output_batch->data + (bm->accumulated * bm->data_width);
      memcpy(dst, src, samples_to_copy * bm->data_width);

      bm->accumulated += samples_to_copy;
      bm->samples_processed += samples_to_copy;
      input_idx += samples_to_copy;
      current_timestamp += samples_to_copy * bm->period_ns;

      // Submit output batch if full
      if (bm->accumulated == bm->output_batch_samples) {
        output_batch->head = bm->accumulated;
        output_batch->batch_id = bm->batches_matched++;
        bb_submit(f->sinks[0], f->timeout_us);
        output_batch = NULL;
        bm->next_boundary_ns += bm->batch_period_ns;
      }
    }

    // Release input batch
    bb_del_tail(&f->input_buffers[0]);
  }

  // Cleanup
  if (bm->accumulator != NULL) {
    free(bm->accumulator);
    bm->accumulator = NULL;
  }

  return NULL;
}
