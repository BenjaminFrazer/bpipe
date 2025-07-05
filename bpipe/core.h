#ifndef BPIPE_CORE_H
#define BPIPE_CORE_H

#include "bperr.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_SINKS 10
#define MAX_SOURCES 10
#define MAX_CAPACITY_EXPO 30 // max 1GB capacity

/* Forward declarations */
typedef struct _DataPipe Bp_Filter_t;

/* Transform function signature
 * Note: Transforms should only write to output_batches[0]. The framework
 * automatically distributes data to additional outputs when n_outputs > 1.
 * For explicit control over multi-output distribution, use BpTeeFilter.
 */
typedef void(Worker_t)(Bp_Filter_t *filt);

typedef struct _DataPipe {
	char name[16];
	bool running;
	Worker_t *worker;
	Err_info worker_err_info;
	struct timespec timeout;
	int n_input_buffers;
	int n_sinks;
	size_t data_width;
	pthread_t worker_thread;
	pthread_mutex_t filter_mutex; // Protects sinks arrays
	Bp_BatchBuffer_t input_buffers[MAX_SOURCES];
} Bp_Filter_t;

/* Configuration-based initialization API */
Bp_EC BpFilter_Init_Core(Bp_Filter_t *filter, SampleDtype_t dtype,
                         size_t n_inputs, size_t batch_size_expo,
                         size_t n_batch_expo);

Bp_EC BpFilter_Deinit(Bp_Filter_t *filter);

/* Multi-I/O connection functions */
Bp_EC Bp_add_sink(Bp_Filter_t *filter, Bp_Filter_t *sink);

Bp_EC Bp_remove_sink(Bp_Filter_t *filter, const Bp_Filter_t *sink);

/* Filter lifecycle functions */
Bp_EC Bp_Filter_Start(Bp_Filter_t *filter);
Bp_EC Bp_Filter_Stop(Bp_Filter_t *filter);

static inline void set_filter_error(Bp_Filter_t *filt, Bp_EC code,
                                    const char *msg, const char *file, int line,
                                    const char *func) {
  filt->worker_err_info.ec = code;
  filt->worker_err_info.line_no = line;
  filt->worker_err_info.filename = file;
  filt->worker_err_info.function = func;
  filt->worker_err_info.err_msg = msg;
  filt->running = false;
}

#define SET_FILTER_ERROR(filt, code, msg)                                      \
  set_filter_error((filt), (code), (msg), __FILE__, __LINE__, __func__)

#define Bp_ASSERT(filt, condition, code, msg)                                  \
  do {                                                                         \
    if (!(condition)) {                                                        \
      SET_FILTER_ERROR((filt), (code), (msg));                                 \
    }                                                                          \
  } while (0)

static inline Bp_EC Bp_allocate_buffers(Bp_Filter_t *dpipe, int buffer_idx) {
  Bp_BatchBuffer_t *buf = &dpipe->input_buffers[buffer_idx];

  /* For backward compatibility: if buffer dtype not set, copy from filter */
  if (buf->dtype == DTYPE_NDEF && dpipe->dtype != DTYPE_NDEF) {
    buf->dtype = dpipe->dtype;
    buf->data_width = dpipe->data_width;
  }

  assert(buf->dtype != DTYPE_NDEF);

  buf->data_ring = malloc(buf->data_width * Bp_ring_capacity(buf));
  buf->batch_ring = malloc(sizeof(Bp_Batch_t) * Bp_ring_capacity(buf));
  buf->head = 0;
  buf->tail = 0;

  assert(buf->data_ring != NULL);
  assert(buf->batch_ring != NULL);
  return Bp_EC_OK;
}

static inline Bp_EC Bp_deallocate_buffers(Bp_Filter_t *dpipe, int buffer_idx) {
  Bp_BatchBuffer_t *buf = &dpipe->input_buffers[buffer_idx];
  assert(buf->dtype != DTYPE_NDEF);

  free(buf->data_ring);
  free(buf->batch_ring);

  buf->data_ring = NULL;
  buf->batch_ring = NULL;
  buf->head = 0;
  buf->tail = 0;
  return Bp_EC_OK;
}

/* Applies a transform using a python filter */
TransformFcn_t BpPyTransform;
/* Pass-through transform - copies first input to first output
 * Framework handles distribution to additional outputs */
TransformFcn_t BpPassThroughTransform;

static inline bool Bp_empty(Bp_BatchBuffer_t *buf) {
  return buf->head == buf->tail;
}

static inline bool Bp_full(Bp_BatchBuffer_t *buf) {
  return (buf->head - buf->tail) >= Bp_ring_capacity(buf);
}

/* Wait for buffer to have data available
 * @param buf Buffer to wait on
 * @param timeout_us Timeout in microseconds (0 = wait indefinitely)
 * @return Bp_EC_OK if data available, Bp_EC_TIMEOUT on timeout, Bp_EC_STOPPED
 * if buffer stopped
 */
static inline Bp_EC Bp_await_not_empty(Bp_BatchBuffer_t *buf,
                                       unsigned long timeout_us) {
  Bp_EC ec = Bp_EC_OK;
  pthread_mutex_lock(&buf->mutex);

  struct timespec abs_timeout;
  if (timeout_us > 0) {
    clock_gettime(CLOCK_REALTIME, &abs_timeout);
    abs_timeout.tv_sec += timeout_us / 1000000;
    abs_timeout.tv_nsec += (timeout_us % 1000000) * 1000;
    if (abs_timeout.tv_nsec >= 1000000000) {
      abs_timeout.tv_sec += 1;
      abs_timeout.tv_nsec -= 1000000000;
    }
  }

  while (Bp_empty(buf) && !buf->stopped) {
    if (timeout_us == 0) {
      // No timeout - wait indefinitely
      pthread_cond_wait(&buf->not_empty, &buf->mutex);
    } else {
      int ret =
          pthread_cond_timedwait(&buf->not_empty, &buf->mutex, &abs_timeout);
      if (ret == ETIMEDOUT) {
        ec = Bp_EC_TIMEOUT;
        break;
      }
    }
  }

  if (buf->stopped && Bp_empty(buf)) {
    ec = Bp_EC_STOPPED;
  }

  pthread_mutex_unlock(&buf->mutex);
  return ec;
}

/* Wait for buffer to have space available
 * @param buf Buffer to wait on
 * @param timeout_us Timeout in microseconds (0 = wait indefinitely)
 * @return Bp_EC_OK if space available, Bp_EC_TIMEOUT on timeout, Bp_EC_STOPPED
 * if buffer stopped
 */
static inline Bp_EC Bp_await_not_full(Bp_BatchBuffer_t *buf,
                                      unsigned long timeout_us) {
  Bp_EC ec = Bp_EC_OK;
  pthread_mutex_lock(&buf->mutex);

  struct timespec abs_timeout;
  if (timeout_us > 0) {
    clock_gettime(CLOCK_REALTIME, &abs_timeout);
    abs_timeout.tv_sec += timeout_us / 1000000;
    abs_timeout.tv_nsec += (timeout_us % 1000000) * 1000;
    if (abs_timeout.tv_nsec >= 1000000000) {
      abs_timeout.tv_sec += 1;
      abs_timeout.tv_nsec -= 1000000000;
    }
  }

  while ((buf->head - buf->tail) >= Bp_ring_capacity(buf) && !buf->stopped) {
    if (timeout_us == 0) {
      // No timeout - wait indefinitely
      pthread_cond_wait(&buf->not_full, &buf->mutex);
    } else {
      int ret =
          pthread_cond_timedwait(&buf->not_full, &buf->mutex, &abs_timeout);
      if (ret == ETIMEDOUT) {
        ec = Bp_EC_TIMEOUT;
        break;
      }
    }
  }

  if (buf->stopped) {
    ec = Bp_EC_STOPPED;
  }

  pthread_mutex_unlock(&buf->mutex);
  return ec;
}

static inline Bp_Batch_t Bp_allocate(Bp_Filter_t *dpipe,
                                     Bp_BatchBuffer_t *buf) {
  Bp_Batch_t batch = {0};

  // Check overflow behavior before waiting - now using buffer's config
  if (buf->overflow_behaviour == OVERFLOW_DROP && Bp_full(buf)) {
    batch.ec = Bp_EC_NOSPACE; // Signal that allocation failed due to overflow
    return batch;
  }

  // Use buffer's timeout configuration
  Bp_EC wait_result = Bp_await_not_full(buf, buf->timeout_us);
  if (wait_result == Bp_EC_OK) {
    size_t idx = buf->head & ((1u << buf->ring_capacity_expo) - 1u);
    void *data_ptr =
        (char *)buf->data_ring + idx * buf->data_width * Bp_batch_capacity(buf);
    batch.capacity = Bp_batch_capacity(buf);
    batch.data = data_ptr;
    batch.batch_id = idx;
    batch.dtype = buf->dtype;

    // Update statistics
    buf->total_batches++;
  } else {
    batch.ec = wait_result; // Could be TIMEOUT, STOPPED, etc
    if (wait_result == Bp_EC_NOSPACE) {
      buf->dropped_batches++;
    }
  }
  return batch;
}

static inline void Bp_submit_batch(Bp_Filter_t *dpipe, Bp_BatchBuffer_t *buf,
                                   const Bp_Batch_t *batch) {
  size_t idx = buf->head & ((1u << buf->ring_capacity_expo) - 1u);
  buf->batch_ring[idx] = *batch;
  buf->head++;
  pthread_cond_signal(&buf->not_empty);
}

static inline Bp_Batch_t Bp_head(Bp_Filter_t *dpipe, Bp_BatchBuffer_t *buf) {
  Bp_Batch_t batch = {0};
  // Use buffer's timeout configuration
  Bp_EC wait_result = Bp_await_not_empty(buf, buf->timeout_us);
  if (wait_result == Bp_EC_OK) {
    size_t idx = buf->tail & ((1u << buf->ring_capacity_expo) - 1u);
    batch = buf->batch_ring[idx];
  } else {
    batch.ec = wait_result; // Could be TIMEOUT, STOPPED, etc
  }
  return batch;
}

static inline void Bp_delete_tail(Bp_Filter_t *dpipe, Bp_BatchBuffer_t *buf) {
  buf->tail++;
  pthread_cond_signal(&buf->not_full);
}

/* Buffer-centric inline operations */
static inline Bp_Batch_t BpBatchBuffer_Allocate_Inline(Bp_BatchBuffer_t *buf) {
  Bp_Batch_t batch = {0};

  // Check overflow behavior before waiting
  if (buf->overflow_behaviour == OVERFLOW_DROP && Bp_full(buf)) {
    batch.ec = Bp_EC_NOSPACE;
    buf->dropped_batches++;
    return batch;
  }

  Bp_EC wait_result = Bp_await_not_full(buf, buf->timeout_us);
  if (wait_result == Bp_EC_OK) {
    size_t idx = buf->head & ((1u << buf->ring_capacity_expo) - 1u);
    void *data_ptr =
        (char *)buf->data_ring + idx * buf->data_width * Bp_batch_capacity(buf);
    batch.capacity = Bp_batch_capacity(buf);
    batch.data = data_ptr;
    batch.batch_id = idx;
    batch.dtype = buf->dtype;

    // Update statistics
    buf->total_batches++;
  } else {
    batch.ec = wait_result;
  }
  return batch;
}

static inline Bp_EC BpBatchBuffer_Submit_Inline(Bp_BatchBuffer_t *buf,
                                                const Bp_Batch_t *batch) {
  size_t idx = buf->head & ((1u << buf->ring_capacity_expo) - 1u);
  buf->batch_ring[idx] = *batch;
  buf->head++;
  pthread_cond_signal(&buf->not_empty);
  return Bp_EC_OK;
}

static inline Bp_Batch_t BpBatchBuffer_Head_Inline(Bp_BatchBuffer_t *buf) {
  Bp_Batch_t batch = {0};
  Bp_EC wait_result = Bp_await_not_empty(buf, buf->timeout_us);
  if (wait_result == Bp_EC_OK) {
    size_t idx = buf->tail & ((1u << buf->ring_capacity_expo) - 1u);
    batch = buf->batch_ring[idx];
  } else {
    batch.ec = wait_result;
  }
  return batch;
}

static inline Bp_EC BpBatchBuffer_DeleteTail_Inline(Bp_BatchBuffer_t *buf) {
  buf->tail++;
  pthread_cond_signal(&buf->not_full);
  return Bp_EC_OK;
}

static inline bool bb_empty(const Bp_BatchBuffer_t *buf) {
  return buf->head == buf->tail;
}

static inline bool is_full(const Bp_BatchBuffer_t *buff) {
  return (buff->head - buf->tail) & (buff->)
}

static inline size_t
BpBatchBuffer_Available_Inline(const Bp_BatchBuffer_t *buf) {
  return buf->head - buf->tail;
}

static inline size_t
BpBatchBuffer_Capacity_Inline(const Bp_BatchBuffer_t *buf) {
  return Bp_ring_capacity((Bp_BatchBuffer_t *)buf);
}

/* Worker thread entry point */
void *Bp_Worker(void *filter);

#endif /* BPIPE_CORE_H */
