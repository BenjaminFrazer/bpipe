#include "core.h"
#include <stdbool.h>
#include <string.h>
#include "batch_buffer.h"
#include "batch_matcher.h"
#include "bperr.h"
#define _GNU_SOURCE /* See feature_test_macros(7) */
#include <pthread.h>

/* Forward declarations */
static Bp_EC default_start(Filter_t* self);
static Bp_EC default_stop(Filter_t* self);
static Bp_EC default_deinit(Filter_t* self);
static Bp_EC default_flush(Filter_t* self);
static Bp_EC default_drain(Filter_t* self);
static Bp_EC default_reset(Filter_t* self);
static Bp_EC default_get_stats(Filter_t* self, void* stats_out);
static FilterHealth_t default_get_health(Filter_t* self);
static size_t default_get_backlog(Filter_t* self);
static Bp_EC default_reconfigure(Filter_t* self, void* config);
static Bp_EC default_validate_connection(Filter_t* self, size_t sink_idx);
static Bp_EC default_describe(Filter_t* self, char* buffer, size_t buffer_size);
static Bp_EC default_dump_state(Filter_t* self, char* buffer,
                                size_t buffer_size);
static Bp_EC default_handle_error(Filter_t* self, Bp_EC error);
static Bp_EC default_recover(Filter_t* self);

/* Default filter operations implementations */
static Bp_EC default_start(Filter_t* self) { return filt_start(self); }

static Bp_EC default_stop(Filter_t* self) { return filt_stop(self); }

static Bp_EC default_deinit(Filter_t* self) { return filt_deinit(self); }

static Bp_EC default_flush(Filter_t* self) { return Bp_EC_OK; }

static Bp_EC default_drain(Filter_t* self) { return Bp_EC_OK; }

static Bp_EC default_reset(Filter_t* self) { return Bp_EC_OK; }

static Bp_EC default_get_stats(Filter_t* self, void* stats_out)
{
  if (stats_out == NULL) {
    return Bp_EC_NULL_POINTER;
  }
  memcpy(stats_out, &self->metrics, sizeof(Filt_metrics));
  return Bp_EC_OK;
}

static FilterHealth_t default_get_health(Filter_t* self)
{
  if (self->worker_err_info.ec != Bp_EC_OK) {
    return FILTER_HEALTH_FAILED;
  }
  return FILTER_HEALTH_HEALTHY;
}

static size_t default_get_backlog(Filter_t* self)
{
  size_t total_backlog = 0;
  for (int i = 0; i < self->n_input_buffers; i++) {
    total_backlog += bb_occupancy(&self->input_buffers[i]);
  }
  return total_backlog;
}

static Bp_EC default_reconfigure(Filter_t* self, void* config)
{
  return Bp_EC_NOT_IMPLEMENTED;
}

static Bp_EC default_validate_connection(Filter_t* self, size_t sink_idx)
{
  if (sink_idx >= self->max_suppported_sinks) {
    return Bp_EC_INVALID_SINK_IDX;
  }
  return Bp_EC_OK;
}

static Bp_EC default_describe(Filter_t* self, char* buffer, size_t buffer_size)
{
  if (buffer == NULL) {
    return Bp_EC_NULL_POINTER;
  }
  snprintf(buffer, buffer_size, "Filter: %s, Type: %d, Running: %s", self->name,
           self->filt_type, self->running ? "true" : "false");
  return Bp_EC_OK;
}

static Bp_EC default_dump_state(Filter_t* self, char* buffer,
                                size_t buffer_size)
{
  if (buffer == NULL) {
    return Bp_EC_NULL_POINTER;
  }
  snprintf(buffer, buffer_size,
           "Filter State: %s\n"
           "  Running: %s\n"
           "  Batches processed: %zu\n"
           "  Input buffers: %d\n"
           "  Sinks: %d\n",
           self->name, self->running ? "true" : "false",
           self->metrics.n_batches, self->n_input_buffers, self->n_sinks);
  return Bp_EC_OK;
}

static Bp_EC default_handle_error(Filter_t* self, Bp_EC error)
{
  self->worker_err_info.ec = error;
  return Bp_EC_OK;
}

static Bp_EC default_recover(Filter_t* self)
{
  self->worker_err_info.ec = Bp_EC_OK;
  return Bp_EC_OK;
}

/* Default ops instance */
static const FilterOps default_ops = {
    .start = default_start,
    .stop = default_stop,
    .deinit = default_deinit,
    .flush = default_flush,
    .drain = default_drain,
    .reset = default_reset,
    .get_stats = default_get_stats,
    .get_health = default_get_health,
    .get_backlog = default_get_backlog,
    .reconfigure = default_reconfigure,
    .validate_connection = default_validate_connection,
    .describe = default_describe,
    .dump_state = default_dump_state,
    .handle_error = default_handle_error,
    .recover = default_recover};

/* Configuration-based initialization API */
Bp_EC filt_init(Filter_t* f, Core_filt_config_t config)
{
  if (f == NULL) {
    return Bp_EC_NULL_FILTER;
  }

  if (memset(f, 0, sizeof(Filter_t)) == NULL) {
    return Bp_EC_MEMSET_FAIL;
  }

  if (config.timeout_us < 0) {
    return Bp_EC_INVALID_CONFIG_TIMEOUT;
  }
  f->timeout_us = config.timeout_us;

  if (config.filt_type >= FILT_T_MAX) {
    return Bp_EC_INVALID_CONFIG_FILTER_T;
  }
  f->filt_type = config.filt_type;

  if (config.size < sizeof(Filter_t)) {
    return Bp_EC_INVALID_CONFIG_FILTER_SIZE;
  }
  f->size = config.size;

  if (config.max_supported_sinks > MAX_SINKS) {
    return Bp_EC_INVALID_CONFIG_MAX_SINKS;
  }
  f->max_suppported_sinks = config.max_supported_sinks;

  if (config.n_inputs > MAX_INPUTS) {
    return Bp_EC_INVALID_CONFIG_MAX_INPUTS;
  }
  f->n_input_buffers = config.n_inputs;

  if (config.worker == NULL) {
    return Bp_EC_INVALID_CONFIG_WORKER;
  }
  f->worker = config.worker;

  for (int i = 0; i < config.n_inputs; i++) {
    Bp_EC rc = bb_init(&f->input_buffers[i], "NDEF", config.buff_config);
    if (rc != Bp_EC_OK) {
      return rc;
    }
  }

  if (config.name != NULL) {
    strncpy(f->name, config.name, sizeof(f->name) - 1);
    f->name[sizeof(f->name) - 1] = '\0';
  } else {
    strncpy(f->name, "NDEF", sizeof(f->name) - 1);
    f->name[sizeof(f->name) - 1] = '\0';
  }

  f->worker_err_info.ec = Bp_EC_OK;

  // Initialize mutex
  if (pthread_mutex_init(&f->filter_mutex, NULL) != 0) {
    // Clean up already allocated buffers
    for (int i = 0; i < config.n_inputs; i++) {
      bb_deinit(&f->input_buffers[i]);
    }
    return Bp_EC_MUTEX_INIT_FAIL;
  }

  // Initialize other fields
  f->data_width = bb_getdatawidth(config.buff_config.dtype);
  f->n_sinks = 0;
  f->n_sink_buffers = 0;
  f->running = false;

  // Initialize metrics
  f->metrics.n_batches = 0;

  // Initialize operations interface with defaults
  f->ops = default_ops;

  return Bp_EC_OK;
}

Bp_EC filt_deinit(Filter_t* f)
{
  if (f == NULL) {
    return Bp_EC_NULL_FILTER;
  }

  // Use custom deinit operation if available
  if (f->ops.deinit != NULL && f->ops.deinit != default_deinit) {
    return f->ops.deinit(f);
  }

  // Deinitialize all input buffers
  for (int i = 0; i < f->n_input_buffers; i++) {
    Bp_EC rc = bb_deinit(&f->input_buffers[i]);
    if (rc != Bp_EC_OK) {
      return rc;
    }
  }

  // Destroy mutex
  pthread_mutex_destroy(&f->filter_mutex);

  return Bp_EC_OK;
}

/* Multi-I/O connection functions */
Bp_EC filt_sink_connect(Filter_t* f, size_t sink_idx, Batch_buff_t* dest_buffer)
{
  if (f == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  if (dest_buffer == NULL) {
    return Bp_EC_NULL_BUFF;
  }
  if (sink_idx >= MAX_SINKS) {
    return Bp_EC_INVALID_SINK_IDX;
  }

  pthread_mutex_lock(&f->filter_mutex);

  if (f->sinks[sink_idx] != NULL) {
    pthread_mutex_unlock(&f->filter_mutex);
    return Bp_EC_CONNECTION_OCCUPIED;
  }

  f->sinks[sink_idx] = dest_buffer;
  f->n_sinks++;

  // Special handling for BatchMatcher auto-detection
  if (f->filt_type == FILT_T_BATCH_MATCHER && sink_idx == 0) {
    BatchMatcher_t* matcher = (BatchMatcher_t*) f;
    matcher->output_batch_samples = 1 << dest_buffer->batch_capacity_expo;
    matcher->size_detected = true;
  }

  pthread_mutex_unlock(&f->filter_mutex);

  return Bp_EC_OK;
}

Bp_EC filt_sink_disconnect(Filter_t* f, size_t sink_idx)
{
  if (f == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  if (sink_idx >= MAX_SINKS) {
    return Bp_EC_INVALID_SINK_IDX;
  }

  pthread_mutex_lock(&f->filter_mutex);

  if (f->sinks[sink_idx] != NULL) {
    f->sinks[sink_idx] = NULL;
    f->n_sinks--;
  }

  pthread_mutex_unlock(&f->filter_mutex);

  return Bp_EC_OK;
}

/* Filter lifecycle functions */
Bp_EC filt_start(Filter_t* f)

{
  if (!f) {
    return Bp_EC_NULL_FILTER;
  }

  // Use custom start operation if available
  if (f->ops.start != NULL && f->ops.start != default_start) {
    return f->ops.start(f);
  }

  if (f->running) {
    return Bp_EC_ALREADY_RUNNING;
  }

  f->running = true;

  if (pthread_create(&f->worker_thread, NULL, f->worker, (void*) f) != 0) {
    f->running = false;
    return Bp_EC_THREAD_CREATE_FAIL;
  }
  // if (pthread_setname_np(f->worker_thread, f->name)) {
  //	return Bp_EC_THREAD_CREATE_NAME_FAIL;
  // }
  return Bp_EC_OK;
}

Bp_EC filt_stop(Filter_t* f)
{
  if (!f) {
    return Bp_EC_NULL_FILTER;
  }

  // Use custom stop operation if available
  if (f->ops.stop != NULL && f->ops.stop != default_stop) {
    return f->ops.stop(f);
  }

  if (!atomic_load(&f->running)) {
    return Bp_EC_OK;  // Already stopped, not an error
  }

  atomic_store(&f->running, false);

  // Force return on input buffers to wake up upstream writers
  for (int i = 0; i < f->n_input_buffers; i++) {
    if (f->input_buffers[i].data_ring != NULL) {
      bb_force_return_head(&f->input_buffers[i], Bp_EC_FILTER_STOPPING);
    }
  }

  // Force return on output buffers to wake up this filter if blocked
  for (int i = 0; i < f->n_sinks; i++) {
    if (f->sinks[i] != NULL) {
      bb_force_return_head(f->sinks[i], Bp_EC_FILTER_STOPPING);
    }
  }

  // Also force return on our own input buffers if we're blocked reading
  for (int i = 0; i < f->n_input_buffers; i++) {
    if (f->input_buffers[i].data_ring != NULL) {
      bb_force_return_tail(&f->input_buffers[i], Bp_EC_FILTER_STOPPING);
    }
  }

  if (pthread_join(f->worker_thread, NULL) != 0) {
    return Bp_EC_THREAD_JOIN_FAIL;
  }

  return Bp_EC_OK;
}

void* matched_passthroug(void* arg)
{
  Filter_t* f = (Filter_t*) arg;
  Batch_t* input;
  Batch_t* output;
  Bp_EC err;
  BP_WORKER_ASSERT(f, f->n_input_buffers == 1, Bp_EC_INVALID_CONFIG_MAX_INPUTS);
  BP_WORKER_ASSERT(f, f->sinks[0] != NULL, Bp_EC_NULL_BUFF);
  BP_WORKER_ASSERT(f, f->sinks[0]->dtype == f->input_buffers[0].dtype,
                   Bp_EC_DTYPE_MISMATCH);
  BP_WORKER_ASSERT(f,
                   f->sinks[0]->batch_capacity_expo ==
                       f->input_buffers[0].batch_capacity_expo,
                   Bp_EC_CAPACITY_MISMATCH);
  BP_WORKER_ASSERT(f, f->sinks[0]->dtype < DTYPE_MAX, Bp_EC_DTYPE_INVALID);

  size_t copy_size =
      bb_batch_size(f->sinks[0]) * _data_size_lut[f->sinks[0]->dtype];

  while (f->running) {
    input = bb_get_tail(&f->input_buffers[0], f->timeout_us, &err);
    BP_WORKER_ASSERT(f, input != NULL, err);

    if (input->ec == Bp_EC_COMPLETE) {
      output = bb_get_head(f->sinks[0]);
      output->ec = Bp_EC_COMPLETE;
      err = bb_submit(f->sinks[0], f->timeout_us);
      BP_WORKER_ASSERT(f, err == Bp_EC_OK, err);
      f->running = false;
      f->worker_err_info.ec = Bp_EC_COMPLETE;
      return NULL;
    }

    output = bb_get_head(f->sinks[0]);
    BP_WORKER_ASSERT(f, output != NULL, Bp_EC_GET_HEAD_NULL);

    BP_WORKER_ASSERT(f, memcpy(output->data, input->data, copy_size) != NULL,
                     Bp_EC_MALLOC_FAIL);

    // Copy batch metadata
    output->head = input->head;
    output->tail = input->tail;
    output->t_ns = input->t_ns;
    output->period_ns = input->period_ns;
    output->batch_id = input->batch_id;

    err = bb_submit(f->sinks[0], f->timeout_us);
    BP_WORKER_ASSERT(f, err == Bp_EC_OK, err);

    err = bb_del_tail(&f->input_buffers[0]);
    BP_WORKER_ASSERT(f, err == Bp_EC_OK, err);

    f->metrics.n_batches++;
    input = bb_get_tail(&f->input_buffers[0], f->timeout_us, &err);
  }
  return NULL;
}

/* Public wrapper functions for filter operations */
Bp_EC filt_flush(Filter_t* filter)
{
  if (filter == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  return filter->ops.flush(filter);
}

Bp_EC filt_drain(Filter_t* filter)
{
  if (filter == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  return filter->ops.drain(filter);
}

Bp_EC filt_reset(Filter_t* filter)
{
  if (filter == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  return filter->ops.reset(filter);
}

Bp_EC filt_get_stats(Filter_t* filter, void* stats_out)
{
  if (filter == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  return filter->ops.get_stats(filter, stats_out);
}

FilterHealth_t filt_get_health(Filter_t* filter)
{
  if (filter == NULL) {
    return FILTER_HEALTH_UNKNOWN;
  }
  return filter->ops.get_health(filter);
}

size_t filt_get_backlog(Filter_t* filter)
{
  if (filter == NULL) {
    return 0;
  }
  return filter->ops.get_backlog(filter);
}

Bp_EC filt_reconfigure(Filter_t* filter, void* config)
{
  if (filter == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  return filter->ops.reconfigure(filter, config);
}

Bp_EC filt_validate_connection(Filter_t* filter, size_t sink_idx)
{
  if (filter == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  return filter->ops.validate_connection(filter, sink_idx);
}

Bp_EC filt_describe(Filter_t* filter, char* buffer, size_t buffer_size)
{
  if (filter == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  return filter->ops.describe(filter, buffer, buffer_size);
}

Bp_EC filt_dump_state(Filter_t* filter, char* buffer, size_t buffer_size)
{
  if (filter == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  return filter->ops.dump_state(filter, buffer, buffer_size);
}

Bp_EC filt_handle_error(Filter_t* filter, Bp_EC error)
{
  if (filter == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  return filter->ops.handle_error(filter, error);
}

Bp_EC filt_recover(Filter_t* filter)
{
  if (filter == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  return filter->ops.recover(filter);
}
