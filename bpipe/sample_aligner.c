#include "sample_aligner.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "bperr.h"
#include "utils.h"

// Forward declarations
static void* sample_aligner_worker(void* arg);
static Bp_EC sample_aligner_start(Filter_t* self);
static Bp_EC sample_aligner_deinit(Filter_t* self);
static Bp_EC sample_aligner_describe(Filter_t* self, char* buffer, size_t size);
static Bp_EC sample_aligner_get_stats(Filter_t* self, void* stats_out);

// Helper function to check if data type supports interpolation
static bool dtype_supports_interpolation(SampleDtype_t dtype)
{
  switch (dtype) {
    case DTYPE_FLOAT:
    case DTYPE_I32:
    case DTYPE_U32:
      return true;
    default:
      return false;
  }
}

// Get history size needed for each interpolation method
static size_t get_history_size_for_method(InterpolationMethod_e method)
{
  switch (method) {
    case INTERP_NEAREST:
      return 2;  // Current and next sample
    case INTERP_LINEAR:
      return 2;  // For linear interpolation between two points
    case INTERP_CUBIC:
      return 4;  // 4 points for cubic interpolation
    case INTERP_SINC:
      return 64;  // Default, will be adjusted based on sinc_taps
    default:
      return 2;
  }
}

// TODO: Implement interpolation functions when needed

// Custom start operation
static Bp_EC sample_aligner_start(Filter_t* self)
{
  SampleAligner_t* sa = (SampleAligner_t*) self;

  // Check if we have a sink
  if (self->sinks[0] == NULL) {
    self->worker_err_info.ec = Bp_EC_NO_SINK;
    self->worker_err_info.err_msg = "SampleAligner requires connected sink";
    return Bp_EC_NO_SINK;
  }

  // Check if input data type supports interpolation
  SampleDtype_t input_dtype = self->input_buffers[0].dtype;
  if (!dtype_supports_interpolation(input_dtype)) {
    self->worker_err_info.ec = Bp_EC_TYPE_ERROR;
    self->worker_err_info.err_msg = "SampleAligner requires numeric data type";
    return Bp_EC_TYPE_ERROR;
  }

  // Allocate history buffer based on data type and history size
  size_t sample_size = bb_getdatawidth(input_dtype);
  size_t history_size = get_history_size_for_method(sa->method);
  if (sa->method == INTERP_SINC && sa->sinc_taps > 0) {
    history_size = sa->sinc_taps;
  }
  sa->history_size = history_size;

  sa->history_buffer = calloc(history_size, sample_size);
  if (!sa->history_buffer) {
    self->worker_err_info.ec = Bp_EC_ALLOC;
    self->worker_err_info.err_msg = "Failed to allocate history buffer";
    return Bp_EC_ALLOC;
  }

  // Start worker thread
  self->running = true;
  if (pthread_create(&self->worker_thread, NULL, self->worker, (void*) self) !=
      0) {
    self->running = false;
    free(sa->history_buffer);
    sa->history_buffer = NULL;
    return Bp_EC_THREAD_CREATE_FAIL;
  }

  return Bp_EC_OK;
}

// Custom deinit operation
static Bp_EC sample_aligner_deinit(Filter_t* self)
{
  SampleAligner_t* sa = (SampleAligner_t*) self;

  // Free history buffer
  if (sa->history_buffer) {
    free(sa->history_buffer);
    sa->history_buffer = NULL;
  }

  // Deinit input buffers
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

// Worker function - simplified implementation
static void* sample_aligner_worker(void* arg)
{
  SampleAligner_t* sa = (SampleAligner_t*) arg;
  Filter_t* f = &sa->base;
  Bp_EC err = Bp_EC_OK;

  while (f->running) {
    // Get input batch
    Batch_t* input = bb_get_tail(&f->input_buffers[0], f->timeout_us, &err);
    if (!input) {
      if (err == Bp_EC_TIMEOUT) continue;
      if (err == Bp_EC_STOPPED) break;
      BP_WORKER_ASSERT(f, false, err);
    }

    // Check for completion
    if (input->ec == Bp_EC_COMPLETE) {
      bb_del_tail(&f->input_buffers[0]);
      // Propagate completion to sink
      Batch_t* output = bb_get_head(f->sinks[0]);
      if (output) {
        output->ec = Bp_EC_COMPLETE;
        output->head = 0;
        err = bb_submit(f->sinks[0], f->timeout_us);
        BP_WORKER_ASSERT(f, input->ec == Bp_EC_OK, input->ec);
      }
      break;
    }

    // Validate input
    BP_WORKER_ASSERT(f, input->ec == Bp_EC_OK, input->ec);
    BP_WORKER_ASSERT(f, input->period_ns > 0, Bp_EC_INVALID_DATA);

    // Initialize on first batch
    if (!sa->initialized) {
      sa->period_ns = input->period_ns;
      uint64_t phase_offset = input->t_ns % sa->period_ns;

      // Update max phase correction
      if (phase_offset > sa->max_phase_correction_ns) {
        sa->max_phase_correction_ns = phase_offset;
      }

      // Determine initial alignment
      switch (sa->alignment) {
        case ALIGN_NEAREST:
          if (phase_offset < sa->period_ns / 2) {
            sa->next_output_ns = input->t_ns - phase_offset;
          } else {
            sa->next_output_ns = input->t_ns + (sa->period_ns - phase_offset);
          }
          break;
        case ALIGN_BACKWARD:
          sa->next_output_ns = input->t_ns - phase_offset;
          break;
        case ALIGN_FORWARD:
          sa->next_output_ns = input->t_ns + (sa->period_ns - phase_offset);
          break;
      }

      sa->initialized = true;
    }

    // Get output batch
    Batch_t* output = bb_get_head(f->sinks[0]);
    BP_WORKER_ASSERT(f, output != NULL, Bp_EC_NULL_POINTER);

    // Simple passthrough with corrected timestamp
    output->t_ns = sa->next_output_ns;
    output->period_ns = sa->period_ns;
    output->batch_id = input->batch_id;
    output->ec = Bp_EC_OK;

    // Copy data
    size_t samples = input->head;
    size_t output_capacity = (1 << f->sinks[0]->batch_capacity_expo);
    size_t to_copy = MIN(samples, output_capacity);

    memcpy(output->data, input->data,
           to_copy * bb_getdatawidth(f->input_buffers[0].dtype));
    output->head = to_copy;

    // Update state
    sa->next_output_ns += to_copy * sa->period_ns;
    sa->samples_interpolated += to_copy;
    f->metrics.samples_processed += to_copy;
    f->metrics.n_batches++;

    // Submit output and consume input
    err = bb_submit(f->sinks[0], f->timeout_us);
    BP_WORKER_ASSERT(f, err == Bp_EC_OK, err);

    err = bb_del_tail(&f->input_buffers[0]);
    BP_WORKER_ASSERT(f, err == Bp_EC_OK, err);
  }

  return NULL;
}

// Describe operation
static Bp_EC sample_aligner_describe(Filter_t* self, char* buffer, size_t size)
{
  SampleAligner_t* sa = (SampleAligner_t*) self;

  const char* method_str = "UNKNOWN";
  switch (sa->method) {
    case INTERP_NEAREST:
      method_str = "NEAREST";
      break;
    case INTERP_LINEAR:
      method_str = "LINEAR";
      break;
    case INTERP_CUBIC:
      method_str = "CUBIC";
      break;
    case INTERP_SINC:
      method_str = "SINC";
      break;
  }

  const char* align_str = "UNKNOWN";
  switch (sa->alignment) {
    case ALIGN_NEAREST:
      align_str = "NEAREST";
      break;
    case ALIGN_BACKWARD:
      align_str = "BACKWARD";
      break;
    case ALIGN_FORWARD:
      align_str = "FORWARD";
      break;
  }

  snprintf(buffer, size,
           "SampleAligner: %s\n"
           "  Method: %s\n"
           "  Alignment: %s\n"
           "  Period: %lu ns\n"
           "  Samples interpolated: %lu\n"
           "  Max phase correction: %lu ns\n",
           self->name, method_str, align_str, sa->period_ns,
           sa->samples_interpolated, sa->max_phase_correction_ns);

  return Bp_EC_OK;
}

// Get statistics
static Bp_EC sample_aligner_get_stats(Filter_t* self, void* stats_out)
{
  // TODO: Return custom statistics
  return Bp_EC_OK;
}

// Initialize function
Bp_EC sample_aligner_init(SampleAligner_t* f, SampleAligner_config_t config)
{
  if (f == NULL) return Bp_EC_INVALID_CONFIG;

  // Build core config
  Core_filt_config_t core_config = {
      .name = config.name,
      .filt_type = FILT_T_SAMPLE_ALIGNER,
      .size = sizeof(SampleAligner_t),
      .n_inputs = 1,
      .max_supported_sinks = 1,
      .buff_config = config.buff_config,
      .timeout_us = config.timeout_us > 0 ? config.timeout_us : 1000000,
      .worker = sample_aligner_worker};

  // Initialize base filter
  Bp_EC err = filt_init(&f->base, core_config);
  if (err != Bp_EC_OK) return err;

  // Initialize filter-specific state
  f->method = config.method;
  f->alignment = config.alignment;
  f->boundary = config.boundary;
  f->sinc_taps = config.sinc_taps;
  f->sinc_cutoff = config.sinc_cutoff > 0 ? config.sinc_cutoff : 0.9f;
  f->initialized = false;
  f->period_ns = 0;
  f->next_output_ns = 0;
  f->history_buffer = NULL;
  f->history_size = 0;
  f->history_samples = 0;
  f->samples_interpolated = 0;
  f->max_phase_correction_ns = 0;
  f->total_phase_correction_ns = 0;

  // Override operations
  f->base.ops.start = sample_aligner_start;
  f->base.ops.deinit = sample_aligner_deinit;
  f->base.ops.describe = sample_aligner_describe;
  f->base.ops.get_stats = sample_aligner_get_stats;

  return Bp_EC_OK;
}
