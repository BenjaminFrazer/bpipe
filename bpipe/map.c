#include "map.h"
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include "batch_buffer.h"
#include "bperr.h"
#include "core.h"

/* Helper macros for cleaner code */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define NEEDS_NEW_BATCH(batch) (!batch || batch->tail >= batch->head)
#define BATCH_FULL(batch, size) (batch->head >= size)

void* map_worker(void* arg)
{
  Map_filt_t* f = (Map_filt_t*) arg;
  Batch_t *input = NULL, *output = NULL;
  Bp_EC err = Bp_EC_OK;

  // Validate all configuration at once
  if (!f->base.sinks[0] || !f->map_fcn ||
      f->base.input_buffers[0].dtype != f->base.sinks[0]->dtype ||
      f->base.input_buffers[0].dtype == DTYPE_NDEF ||
      f->base.input_buffers[0].dtype >= DTYPE_MAX) {
    f->base.worker_err_info.ec = Bp_EC_INVALID_CONFIG;
    return NULL;
  }

  // Cache frequently used values
  const size_t data_width = bb_getdatawidth(f->base.input_buffers[0].dtype);
  const size_t batch_size = bb_batch_size(f->base.sinks[0]);

  // Main processing loop
  while (atomic_load(&f->base.running)) {
    // Get new input batch if needed
    if (NEEDS_NEW_BATCH(input)) {
      if (input && (err = bb_del_tail(&f->base.input_buffers[0])) != Bp_EC_OK)
        break;
      
      input = bb_get_tail(&f->base.input_buffers[0], f->base.timeout_us, &err);
      if (!input) {
        if (err == Bp_EC_TIMEOUT) {
          continue;  // Normal timeout, keep waiting for data
        } else if (err == Bp_EC_STOPPED) {
          break;     // Buffer was stopped, exit gracefully
        } else {
          break;     // Real error, exit
        }
      }
    }

    // Get new output batch if needed
    if (!output) {
      output = bb_get_head(f->base.sinks[0]);
      if (!output) break;  // No output buffer available
      output->head = 0;
    }

    // Process available data if we have both input and output
    if (!input || !output) continue;  // Wait for both buffers
    
    size_t n = MIN(input->head - input->tail, batch_size - output->head);
    if (n > 0) {
      err = f->map_fcn(input->data + input->tail * data_width,
                       output->data + output->head * data_width, n);
      if (err != Bp_EC_OK) break;

      input->tail += n;
      output->head += n;

      // Update samples processed metric
      f->base.metrics.samples_processed += n;

      // Preserve timing information
      if (output->head == n) {  // First samples in this batch
        output->t_ns = input->t_ns;
        output->period_ns = input->period_ns;
      }
    }

    // Submit output if batch is full
    if (output && output->head >= batch_size) {
      if ((err = bb_submit(f->base.sinks[0], f->base.timeout_us)) != Bp_EC_OK)
        break;
      output = NULL;  // Force getting a new output batch
      f->base.metrics.n_batches++;  // Increment batch count only when submitting
    }
  }

  // Handle errors and cleanup
  if (err != Bp_EC_OK && err != Bp_EC_STOPPED && err != Bp_EC_TIMEOUT) {
    f->base.worker_err_info.ec = err;
    atomic_store(&f->base.running, false);  // Stop filter on error
  }

  if (output && output->head > 0) bb_submit(f->base.sinks[0], f->base.timeout_us);

  return NULL;
}

/* Map-specific operations */
static Bp_EC map_flush(Filter_t* self)
{
  // Submit any pending output batches
  if (self->sinks[0] != NULL) {
    Batch_t* current_batch = bb_get_head(self->sinks[0]);
    if (current_batch && current_batch->head > 0) {
      return bb_submit(self->sinks[0], self->timeout_us);
    }
  }

  return Bp_EC_OK;
}

static Bp_EC map_describe(Filter_t* self, char* buffer, size_t buffer_size)
{
  Map_filt_t* map = (Map_filt_t*) self;

  if (buffer == NULL) {
    return Bp_EC_NULL_POINTER;
  }

  snprintf(buffer, buffer_size,
           "Map Filter: %s\n"
           "  Input dtype: %d\n"
           "  Map function: %p\n"
           "  Running: %s\n"
           "  Batches processed: %zu",
           self->name, self->input_buffers[0].dtype, (void*) map->map_fcn,
           self->running ? "true" : "false", self->metrics.n_batches);

  return Bp_EC_OK;
}

static Bp_EC map_get_stats(Filter_t* self, void* stats_out)
{
  Filt_metrics* stats = (Filt_metrics*) stats_out;

  if (stats_out == NULL) {
    return Bp_EC_NULL_POINTER;
  }

  *stats = self->metrics;

  return Bp_EC_OK;
}

static Bp_EC map_dump_state(Filter_t* self, char* buffer, size_t buffer_size)
{
  Map_filt_t* map = (Map_filt_t*) self;

  if (buffer == NULL) {
    return Bp_EC_NULL_POINTER;
  }

  snprintf(buffer, buffer_size,
           "Map Filter State: %s\n"
           "  Filter type: %d\n"
           "  Running: %s\n"
           "  Batches processed: %zu\n"
           "  Input buffer occupancy: %zu\n"
           "  Output buffer occupancy: %zu\n"
           "  Map function: %p\n"
           "  Data width: %zu bytes\n"
           "  Timeout: %lu us",
           self->name, self->filt_type, self->running ? "true" : "false",
           self->metrics.n_batches, bb_occupancy(&self->input_buffers[0]),
           self->sinks[0] ? bb_occupancy(self->sinks[0]) : 0,
           (void*) map->map_fcn, self->data_width, self->timeout_us);

  return Bp_EC_OK;
}

Bp_EC map_init(Map_filt_t* f, Map_config_t config)
{
  Core_filt_config_t core_config;
  if (f == NULL) {
    return Bp_EC_INVALID_CONFIG;
  }

  /* copy Batch Buffer config */
  core_config.buff_config = config.buff_config;
  core_config.worker = &map_worker;

  /* Map is always a 1->1 filter */
  core_config.n_inputs = 1;
  core_config.max_supported_sinks = 1;
  core_config.filt_type = FILT_T_MAP;
  core_config.size = sizeof(Map_filt_t);
  core_config.name = "MAP_FILTER";
  core_config.timeout_us = 10000;  // TODO: this should be in config

  Bp_EC err = filt_init(&f->base, core_config);

  if (err != Bp_EC_OK) {
    return err;
  }
  if (config.map_fcn == NULL) {
    return Bp_EC_INVALID_CONFIG;
  }
  f->map_fcn = config.map_fcn;

  // Override specific operations with map-specific implementations
  f->base.ops.flush = map_flush;
  f->base.ops.describe = map_describe;
  f->base.ops.get_stats = map_get_stats;
  f->base.ops.dump_state = map_dump_state;

  return Bp_EC_OK;
};
