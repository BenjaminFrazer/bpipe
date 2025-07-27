#define _GNU_SOURCE  // For strdup
#include "debug_output_filter.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"

// Forward declaration
static Bp_EC debug_output_deinit(Filter_t* base);

static void* debug_output_worker(void* arg)
{
  DebugOutputFilter_t* filter = (DebugOutputFilter_t*) arg;
  Filter_t* base = &filter->base;

  // Note: Output sink is optional - filter can work as a pure inspector

  while (atomic_load(&base->running)) {
    // Get input batch
    Bp_EC err;
    Batch_t* in_batch = bb_get_tail(&base->input_buffers[0], 100, &err);
    if (!in_batch) {
      if (err == Bp_EC_STOPPED) {
        break;  // Graceful shutdown
      }
      continue;  // Timeout is normal
    }

    // Print batch if configured
    if (filter->config.show_metadata || filter->config.show_samples) {
      pthread_mutex_lock(&filter->file_mutex);

      // Print metadata if enabled
      if (filter->config.show_metadata) {
        fprintf(filter->output_file,
                "%s[Batch t=%lldns, period=%uns, samples=%zu, type=%s",
                filter->formatted_prefix, (long long) in_batch->t_ns,
                in_batch->period_ns, in_batch->head - in_batch->tail,
                base->input_buffers[0].dtype == DTYPE_FLOAT ? "FLOAT"
                : base->input_buffers[0].dtype == DTYPE_I32 ? "I32"
                                                            : "U32");

        if (in_batch->ec != Bp_EC_OK) {
          fprintf(filter->output_file, ", ec=%d", in_batch->ec);
        }
        fprintf(filter->output_file, "]\n");
      }

      // Print samples if enabled
      if (filter->config.show_samples && in_batch->head > in_batch->tail) {
        size_t num_samples = in_batch->head - in_batch->tail;
        int samples_to_print = filter->config.max_samples_per_batch;
        if (samples_to_print < 0 || samples_to_print > (int) num_samples) {
          samples_to_print = (int) num_samples;
        }

        for (int i = 0; i < samples_to_print; i++) {
          size_t idx = in_batch->tail + i;
          fprintf(filter->output_file, "%s  [%d] ", filter->formatted_prefix,
                  i);

          switch (base->input_buffers[0].dtype) {
            case DTYPE_NDEF:
            case DTYPE_MAX:
              // Should not happen
              break;
            case DTYPE_FLOAT: {
              float* data = (float*) in_batch->data;
              switch (filter->config.format) {
                case DEBUG_FMT_SCIENTIFIC:
                  fprintf(filter->output_file, "%e\n", data[idx]);
                  break;
                case DEBUG_FMT_HEX:
                  fprintf(filter->output_file, "0x%08X\n",
                          *(uint32_t*) &data[idx]);
                  break;
                case DEBUG_FMT_BINARY: {
                  uint32_t bits = *(uint32_t*) &data[idx];
                  fprintf(filter->output_file, "0b");
                  for (int b = 31; b >= 0; b--) {
                    fprintf(filter->output_file, "%d", (bits >> b) & 1);
                  }
                  fprintf(filter->output_file, "\n");
                  break;
                }
                case DEBUG_FMT_DECIMAL:
                default:
                  fprintf(filter->output_file, "%f\n", data[idx]);
                  break;
              }
              break;
            }
            case DTYPE_I32: {
              int32_t* data = (int32_t*) in_batch->data;
              switch (filter->config.format) {
                case DEBUG_FMT_HEX:
                  fprintf(filter->output_file, "0x%08X\n",
                          (uint32_t) data[idx]);
                  break;
                case DEBUG_FMT_BINARY: {
                  uint32_t bits = (uint32_t) data[idx];
                  fprintf(filter->output_file, "0b");
                  for (int b = 31; b >= 0; b--) {
                    fprintf(filter->output_file, "%d", (bits >> b) & 1);
                  }
                  fprintf(filter->output_file, "\n");
                  break;
                }
                case DEBUG_FMT_DECIMAL:
                case DEBUG_FMT_SCIENTIFIC:
                default:
                  fprintf(filter->output_file, "%d\n", data[idx]);
                  break;
              }
              break;
            }
            case DTYPE_U32: {
              uint32_t* data = (uint32_t*) in_batch->data;
              switch (filter->config.format) {
                case DEBUG_FMT_HEX:
                  fprintf(filter->output_file, "0x%08X\n", data[idx]);
                  break;
                case DEBUG_FMT_BINARY: {
                  fprintf(filter->output_file, "0b");
                  for (int b = 31; b >= 0; b--) {
                    fprintf(filter->output_file, "%d", (data[idx] >> b) & 1);
                  }
                  fprintf(filter->output_file, "\n");
                  break;
                }
                case DEBUG_FMT_DECIMAL:
                case DEBUG_FMT_SCIENTIFIC:
                default:
                  fprintf(filter->output_file, "%u\n", data[idx]);
                  break;
              }
              break;
            }
          }
        }

        if (samples_to_print < (int) num_samples) {
          fprintf(filter->output_file, "%s  ... (%zu more samples)\n",
                  filter->formatted_prefix, num_samples - samples_to_print);
        }
      }

      if (filter->config.flush_after_print) {
        fflush(filter->output_file);
      }

      pthread_mutex_unlock(&filter->file_mutex);
    }

    // Pass through data if we have an output sink
    if (base->n_sinks > 0 && base->sinks[0] != NULL) {
      // Get output buffer
      Batch_t* out_batch = bb_get_head(base->sinks[0]);

      // Copy entire batch (passthrough)
      out_batch->t_ns = in_batch->t_ns;
      out_batch->period_ns = in_batch->period_ns;
      out_batch->head = in_batch->head;
      out_batch->tail = in_batch->tail;
      out_batch->ec = in_batch->ec;

      size_t data_size = (in_batch->head - in_batch->tail) *
                         bb_getdatawidth(base->input_buffers[0].dtype);
      if (data_size > 0) {
        memcpy(
            (char*) out_batch->data +
                in_batch->tail * bb_getdatawidth(base->input_buffers[0].dtype),
            (char*) in_batch->data +
                in_batch->tail * bb_getdatawidth(base->input_buffers[0].dtype),
            data_size);
      }

      // Submit output
      bb_submit(base->sinks[0], base->timeout_us);
    }

    // Always delete input batch
    bb_del_tail(&base->input_buffers[0]);

    // Handle completion
    if (in_batch->ec == Bp_EC_COMPLETE) {
      if (filter->config.show_metadata) {
        pthread_mutex_lock(&filter->file_mutex);
        fprintf(filter->output_file, "%s[Stream completed]\n",
                filter->formatted_prefix);
        if (filter->config.flush_after_print) {
          fflush(filter->output_file);
        }
        pthread_mutex_unlock(&filter->file_mutex);
      }
    }
  }

  return NULL;
}

Bp_EC debug_output_filter_init(DebugOutputFilter_t* filter,
                               const DebugOutputConfig_t* config)
{
  if (!filter || !config) {
    return Bp_EC_NULL_POINTER;
  }

  // Clear the structure
  memset(filter, 0, sizeof(DebugOutputFilter_t));

  // Explicitly clear atomic bool
  atomic_store(&filter->base.running, false);

  // Copy config
  filter->config = *config;

  // Set defaults
  if (!filter->config.prefix) {
    filter->config.prefix = "DEBUG: ";
  }
  if (filter->config.max_samples_per_batch == 0) {
    filter->config.max_samples_per_batch = 10;
  }

  // Allocate formatted prefix
  filter->formatted_prefix = strdup(filter->config.prefix);
  if (!filter->formatted_prefix) {
    fprintf(stderr, "debug_output_filter_init: Failed to strdup prefix\n");
    return Bp_EC_ALLOC;
  }

  // Open output file
  if (filter->config.filename) {
    filter->output_file =
        fopen(filter->config.filename, filter->config.append_mode ? "a" : "w");
    if (!filter->output_file) {
      free(filter->formatted_prefix);
      return Bp_EC_NOSPACE;
    }
  } else {
    filter->output_file = stdout;
  }

  // Initialize mutex
  if (pthread_mutex_init(&filter->file_mutex, NULL) != 0) {
    if (filter->output_file != stdout) {
      fclose(filter->output_file);
    }
    free(filter->formatted_prefix);
    return Bp_EC_MUTEX_INIT_FAIL;
  }

  // Build core config
  Core_filt_config_t core_config = {
      .name = "debug_output",
      .filt_type = FILT_T_MAP,
      .size = sizeof(DebugOutputFilter_t),
      .n_inputs = 1,
      .max_supported_sinks = 1,  // Optional - can work with 0 or 1 sink
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 10,
                      .ring_capacity_expo = 12,
                      .overflow_behaviour = OVERFLOW_DROP_TAIL},
      .timeout_us = 100000,
      .worker = debug_output_worker};

  // Initialize base filter
  Bp_EC ec = filt_init(&filter->base, core_config);
  if (ec != Bp_EC_OK) {
    pthread_mutex_destroy(&filter->file_mutex);
    if (filter->output_file != stdout) {
      fclose(filter->output_file);
    }
    free(filter->formatted_prefix);
    return ec;
  }

  // Set custom destructor
  filter->base.ops.deinit = debug_output_deinit;

  return Bp_EC_OK;
}

static Bp_EC debug_output_deinit(Filter_t* base)
{
  DebugOutputFilter_t* filter = (DebugOutputFilter_t*) base;

  if (filter->formatted_prefix) {
    free(filter->formatted_prefix);
    filter->formatted_prefix = NULL;  // Prevent double free
  }

  if (filter->output_file && filter->output_file != stdout) {
    fclose(filter->output_file);
    filter->output_file = NULL;  // Prevent double close
  }

  pthread_mutex_destroy(&filter->file_mutex);

  // Don't call filt_deinit here - that would cause infinite recursion
  return Bp_EC_OK;
}