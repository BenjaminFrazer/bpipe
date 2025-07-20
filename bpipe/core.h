#ifndef BPIPE_CORE_H
#define BPIPE_CORE_H

#include <arpa/inet.h>
#include <assert.h>
#include <bits/types/struct_iovec.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "batch_buffer.h"
#include "bperr.h"
#include "utils.h"

#define MAX_SINKS 10
#define MAX_INPUTS 10
#define MAX_CAPACITY_EXPO 30       // max 1GB capacity
#define MAX_RING_CAPACITY_EXPO 12  // max 4016 entries in ring buffer
//

typedef enum _CORE_FILT_T {
  FILT_T_NDEF = 0, /* Un-initialised filter Guard */
  FILT_T_MAP = 1, /* map a single function across all input sample to all output
                     samples.*/
  FILT_T_MATCHED_PASSTHROUGH,
  FILT_T_CAST,             /* Convert one type into another */
  FILT_T_MAP_STATE = 1,    /* Function will be passed a state scratchpad */
  FILT_T_MAP_MP,           /* Map will be applied to batches in paralel.*/
  FILT_T_SIMO_TEE,         /* Map a single input to multiple consumers */
  FILT_T_MIMO_SYNCRONISER, /* Produce batches aligned to the same sample times
                            */
  FILT_T_MISO_ELEMENTWISE, /* Map multiple inputs to a single ouptput, assumes
                              time alignment. */
  FILT_T_OVERLAP_BATCHES,  /* Used to create repeating regions at the start of
                              the batch. Used for batched convolution.*/
  FILT_T_BATCH_MATCHER,  /* Matches batch sizes and zeros phase for element-wise
                            ops */
  FILT_T_SAMPLE_ALIGNER, /* Corrects phase offset in regular data to align to
                            sample grid */
  FILT_T_MAX,            /* Overflow guard. */
} CORE_FILT_T;

/* Forward declaration */
struct _Filter_t;

/* Filter health states */
typedef enum _FilterHealth_t {
  FILTER_HEALTH_HEALTHY = 0,
  FILTER_HEALTH_DEGRADED,
  FILTER_HEALTH_FAILED,
  FILTER_HEALTH_UNKNOWN
} FilterHealth_t;

/* Filter operations interface */
typedef struct _FilterOps {
  /* Lifecycle operations */
  Bp_EC (*start)(struct _Filter_t *self);
  Bp_EC (*stop)(struct _Filter_t *self);
  Bp_EC (*deinit)(struct _Filter_t *self);

  /* Data flow operations */
  Bp_EC (*flush)(struct _Filter_t *self);
  Bp_EC (*drain)(struct _Filter_t *self);
  Bp_EC (*reset)(struct _Filter_t *self);

  /* Diagnostics operations */
  Bp_EC (*get_stats)(struct _Filter_t *self, void *stats_out);
  FilterHealth_t (*get_health)(struct _Filter_t *self);
  size_t (*get_backlog)(struct _Filter_t *self);

  /* Configuration operations */
  Bp_EC (*reconfigure)(struct _Filter_t *self, void *config);
  Bp_EC (*validate_connection)(struct _Filter_t *self, size_t sink_idx);

  /* Debugging operations */
  Bp_EC (*describe)(struct _Filter_t *self, char *buffer, size_t buffer_size);
  Bp_EC (*dump_state)(struct _Filter_t *self, char *buffer, size_t buffer_size);

  /* Error handling */
  Bp_EC (*handle_error)(struct _Filter_t *self, Bp_EC error);
  Bp_EC (*recover)(struct _Filter_t *self);
} FilterOps;

/* Transform function signature
 * Note: Transforms should only write to output_batches[0]. The framework
 * automatically distributes data to additional outputs when n_outputs > 1.
 * For explicit control over multi-output distribution, use BpTeeFilter.
 */
typedef void *(Worker_t) (void *);

typedef struct _Core_filt_config_t {
  const char *name;
  CORE_FILT_T filt_type;
  size_t size;      // size of the whole filter struct (needed for inheritance).
  size_t n_inputs;  //
  size_t max_supported_sinks;  // some filters only support 1->1 mapping other
                               // like the T support one -> many or many -> many
  BatchBuffer_config buff_config;
  long timeout_us;
  Worker_t *worker;
} Core_filt_config_t;

typedef struct _Filt_metrics {
  size_t n_batches;
  size_t samples_processed;
} Filt_metrics;

typedef struct _Filter_t {
  char name[32];
  size_t size;
  CORE_FILT_T filt_type;
  atomic_bool running;
  Worker_t *worker;
  Err_info worker_err_info;
  Filt_metrics metrics;
  unsigned long timeout_us;
  size_t max_suppported_sinks;
  int n_input_buffers;
  size_t n_sink_buffers;
  int n_sinks;
  size_t data_width;
  pthread_t worker_thread;
  pthread_mutex_t filter_mutex;  // Protects sinks arrays
  Batch_buff_t input_buffers[MAX_INPUTS];
  Batch_buff_t *sinks[MAX_SINKS];
  FilterOps ops;  // Embedded operations interface
} Filter_t;

Worker_t matched_passthroug;

/* Configuration-based initialization API */
Bp_EC filt_init(Filter_t *filter, Core_filt_config_t config);

Bp_EC filt_deinit(Filter_t *filter);

/* Multi-I/O connection functions */
Bp_EC filt_sink_connect(Filter_t *f, size_t sink_idx,
                        Batch_buff_t *dest_buffer);

Bp_EC filt_sink_disconnect(Filter_t *f, size_t sink_idx);

/* Filter lifecycle functions */
Bp_EC filt_start(Filter_t *filter);
Bp_EC filt_stop(Filter_t *filter);

/* Filter operations API */
Bp_EC filt_flush(Filter_t *filter);
Bp_EC filt_drain(Filter_t *filter);
Bp_EC filt_reset(Filter_t *filter);
Bp_EC filt_get_stats(Filter_t *filter, void *stats_out);
FilterHealth_t filt_get_health(Filter_t *filter);
size_t filt_get_backlog(Filter_t *filter);
Bp_EC filt_reconfigure(Filter_t *filter, void *config);
Bp_EC filt_validate_connection(Filter_t *filter, size_t sink_idx);
Bp_EC filt_describe(Filter_t *filter, char *buffer, size_t buffer_size);
Bp_EC filt_dump_state(Filter_t *filter, char *buffer, size_t buffer_size);
Bp_EC filt_handle_error(Filter_t *filter, Bp_EC error);
Bp_EC filt_recover(Filter_t *filter);

#endif /* BPIPE_CORE_H */
