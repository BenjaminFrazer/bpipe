#include "map.h"
#include <bits/types/struct_iovec.h>
#include <stdbool.h>
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
      if (!(input = bb_get_tail(&f->base.input_buffers[0], f->base.timeout_us,
                                &err)))
        break;
    }

    // Get new output batch if needed
    if (!output || BATCH_FULL(output, batch_size)) {
      if (output &&
          (err = bb_submit(f->base.sinks[0], f->base.timeout_us)) != Bp_EC_OK)
        break;
      output = bb_get_head(f->base.sinks[0]);
      output->head = 0;
    }

    // Process available data
    size_t n = MIN(input->head - input->tail, batch_size - output->head);
    if (n > 0) {
      err = f->map_fcn(output->data + output->head * data_width,
                       input->data + input->tail * data_width, n);
      if (err != Bp_EC_OK) break;

      input->tail += n;
      output->head += n;
    }
  }

  // Handle errors and cleanup
  if (err != Bp_EC_OK && err != Bp_EC_STOPPED) f->base.worker_err_info.ec = err;

  if (output && output->head > 0) bb_submit(f->base.sinks[0], 0);

  return NULL;
}

Bp_EC map_init(Map_filt_t* f, Map_config_t config)
{
  Core_filt_config_t core_config;
  if (f == NULL) {
    return Bp_EC_INVALID_CONFIG;
  }

  /* copy Batch Buffer config */
  core_config.buff_config = config.buff_config;
  f->base.worker = &map_worker;

  /* Map is always a 1->1 filter */
  core_config.n_inputs = 1;
  core_config.max_supported_sinks = 1;
  core_config.filt_type = FILT_T_MAP;
  core_config.size = sizeof(Map_filt_t);

  Bp_EC err = filt_init(&f->base, core_config);

  if (err != Bp_EC_OK) {
    return err;
  }
  if (config.map_fcn == NULL) {
    return Bp_EC_INVALID_CONFIG;
  }
  f->map_fcn = config.map_fcn;

  return Bp_EC_OK;
};
