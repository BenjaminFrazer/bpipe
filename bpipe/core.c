#include "core.h"
#include <stdbool.h>
#include <string.h>
#include "batch_buffer.h"
#include "bperr.h"
#define _GNU_SOURCE /* See feature_test_macros(7) */
#include <pthread.h>

/* Configuration-based initialization API */
Bp_EC filt_init(Filter_t *f, Core_filt_config_t config)
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
    strncpy(f->name, config.name, sizeof(f->name));
  } else {
    strncpy(f->name, "NDEF", sizeof(f->name));
  }

  f->worker_err_info.ec = Bp_EC_OK;

  return Bp_EC_OK;
}

Bp_EC filt_deinit(Filter_t *f)
{
  for (int i = 0; i < f->n_input_buffers; i++) {
    Bp_EC rc = bb_deinit(&f->input_buffers[i]);
    if (rc != Bp_EC_OK) {
      return rc;
    }
  }
  return Bp_EC_OK;
}

/* Multi-I/O connection functions */
Bp_EC filt_sink_connect(Filter_t *f, size_t sink_idx, Batch_buff_t *dest_buffer)
{
  if (f == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  if (dest_buffer == NULL) {
    return Bp_EC_NULL_BUFF;
  }
  if (sink_idx > MAX_SINKS) {
    return Bp_EC_INVALID_SINK_IDX;
  }
  if (f->sinks[sink_idx] != NULL) {
    return Bp_EC_CONNECTION_OCCUPIED;
  }

  f->sinks[sink_idx] = dest_buffer;

  return Bp_EC_OK;
}

Bp_EC filt_sink_disconnect(Filter_t *f, size_t sink_idx)
{
  if (f == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  if (sink_idx > MAX_SINKS) {
    return Bp_EC_INVALID_SINK_IDX;
  }
  f->sinks[sink_idx] = NULL;
  return Bp_EC_OK;
}

/* Filter lifecycle functions */
Bp_EC filt_start(Filter_t *f)

{
  if (!f) {
    return Bp_EC_NULL_FILTER;
  }
  if (f->running) {
    return Bp_EC_ALREADY_RUNNING;
  }

  f->running = true;

  if (pthread_create(&f->worker_thread, NULL, f->worker, (void *) f) != 0) {
    f->running = false;
    return Bp_EC_THREAD_CREATE_FAIL;
  }
  // if (pthread_setname_np(f->worker_thread, f->name)) {
  //	return Bp_EC_THREAD_CREATE_NAME_FAIL;
  // }
  return Bp_EC_OK;
}

Bp_EC filt_stop(Filter_t *f)
{
  if (!f) {
    return Bp_EC_NULL_FILTER;
  }

  if (!f->running) {
    return Bp_EC_OK;  // Already stopped, not an error
  }

  f->running = false;

  // Stop all input buffers to wake up any waiting threads
  for (int i = 0; i < MAX_INPUTS; i++) {
    if (f->input_buffers[i].data_ring != NULL) {
      bb_stop(&f->input_buffers[i]);
    }
  }

  if (pthread_join(f->worker_thread, NULL) != 0) {
    return Bp_EC_THREAD_JOIN_FAIL;
  }

  return Bp_EC_OK;
}

void *matched_passthroug(void *arg)
{
  Filter_t *f = (Filter_t *) arg;
  Batch_t *input;
  Batch_t *output;
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
      BP_WORKER_ASSERT(f, err = Bp_EC_OK, err);
      f->running = false;
      f->worker_err_info.ec = Bp_EC_COMPLETE;
      return NULL;
    }

    output = bb_get_head(f->sinks[0]);
    BP_WORKER_ASSERT(f, output != NULL, Bp_EC_GET_HEAD_NULL);

    BP_WORKER_ASSERT(f, memcpy(output->data, input->data, copy_size) != NULL,
                     Bp_EC_MALLOC_FAIL);

    err = bb_submit(f->sinks[0], f->timeout_us);
    BP_WORKER_ASSERT(f, err == Bp_EC_OK, err);

    err = bb_del_tail(&f->input_buffers[0]);
    BP_WORKER_ASSERT(f, err == Bp_EC_OK, err);

    f->metrics.n_batches++;
    input = bb_get_tail(&f->input_buffers[0], f->timeout_us, &err);
  }
  return NULL;
}
