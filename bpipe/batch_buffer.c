#include "batch_buffer.h"
#include <bits/types/struct_iovec.h>
#include <string.h>

size_t _data_size_lut[] = {
    [DTYPE_NDEF] = 0,
    [DTYPE_INT] = sizeof(int),
    [DTYPE_FLOAT] = sizeof(float),
    [DTYPE_UNSIGNED] = sizeof(unsigned),
};

/* Wait for buffer to have space available
 * @param buf Buffer to wait on
 * @param timeout_us Timeout in microseconds (0 = wait indefinitely)
 * @return Bp_EC_OK if space available, Bp_EC_TIMEOUT on timeout, Bp_EC_STOPPED
 * if buffer stopped
 */
Bp_EC bb_await_notfull(Batch_buff_t *buff, unsigned long timeout) {
  Bp_EC ec = Bp_EC_OK;
  pthread_mutex_lock(&buff->mutex);

  struct timespec abs_timeout = future_ts(timeout, CLOCK_MONOTONIC);

  while (bb_isfull(buff) && atomic_load(&buff->running)) {
    int ret =
        pthread_cond_timedwait(&buff->not_full, &buff->mutex, &abs_timeout);
    if (ret == ETIMEDOUT) {
      ec = Bp_EC_TIMEOUT;
      break;
    }
  }
  pthread_mutex_unlock(&buff->mutex);
  return ec;
}

Bp_EC bb_await_notempty(Batch_buff_t *buff, unsigned long timeout) {
  Bp_EC ec = Bp_EC_OK;
  pthread_mutex_lock(&buff->mutex);

  struct timespec abs_timeout = future_ts(timeout, CLOCK_MONOTONIC);

  while (bb_isempy(buff) && atomic_load(&buff->running)) {
    int ret =
        pthread_cond_timedwait(&buff->not_empty, &buff->mutex, &abs_timeout);
    if (ret == ETIMEDOUT) {
      ec = Bp_EC_TIMEOUT;
      break;
    }
  }
  pthread_mutex_unlock(&buff->mutex);
  return ec;
}

/* Get the oldest consumable data batch. Doesn't change head or tail idx. */
Bp_Batch_t bb_get_tail(Batch_buff_t *buff) {
  /* Fast path - check if data available without locks */
  if (!bb_isempy_lockfree(buff)) {
    size_t idx = get_tail_idx(buff);
    /* Memory fence ensures we see the batch data written by producer */
    atomic_thread_fence(memory_order_acquire);
    return buff->batch_ring[idx];
  }

  /* Slow path - wait for data */
  bb_await_notempty(buff, buff->timeout_us);
  size_t idx = get_tail_idx(buff);
  return buff->batch_ring[idx];
}

/* Delete oldest batch and increment the tail pointer marking the slot as
 * populateable. Lock-free implementation for SPSC scenario.
 */
Bp_EC bb_del(Batch_buff_t *buff) {
  /* Fast path - check without locks */
  size_t current_head =
      atomic_load_explicit(&buff->producer.head, memory_order_acquire);
  size_t current_tail =
      atomic_load_explicit(&buff->consumer.tail, memory_order_relaxed);

  if (current_tail == current_head) {
    /* Buffer is empty - need to wait or return error */
    if (buff->timeout_us == 0) {
      return Bp_EC_BUFFER_EMPTY;
    }

    /* Slow path - wait for data */
    Bp_EC ec = bb_await_notempty(buff, buff->timeout_us);
    if (ec != Bp_EC_OK) {
      return ec;
    }

    /* Re-check after wait */
    current_head =
        atomic_load_explicit(&buff->producer.head, memory_order_acquire);
    current_tail =
        atomic_load_explicit(&buff->consumer.tail, memory_order_relaxed);
    if (current_tail == current_head) {
      return Bp_EC_BUFFER_EMPTY;
    }
  }

  /* Fast path - we have data, increment tail */
  size_t new_tail = (current_tail + 1) & bb_modulo_mask(buff);
  atomic_store_explicit(&buff->consumer.tail, new_tail, memory_order_release);

  /* Signal producer if buffer was previously full */
  if (((current_head + 1) & bb_modulo_mask(buff)) == current_tail) {
    pthread_mutex_lock(&buff->mutex);
    pthread_cond_signal(&buff->not_full);
    pthread_mutex_unlock(&buff->mutex);
  }

  return Bp_EC_OK;
}

/* Submit new batch - lock-free implementation for SPSC scenario.
 *
 * Dropping behaviour:
 *   If the buffer is full and overflow behaviour == OVERFLOW_DROP, the
 * statistics are updated but head is not incremented, causing the next write to
 * overwrite the current batch.
 *
 * Blocking behaviour:
 *   If the buffer is full and overflow behaviour == OVERFLOW_BLOCK, this
 * operation will block until space is available.
 */
Bp_EC bb_submit(Batch_buff_t *buff) {
  /* Fast path - check if full without locks */
  size_t current_head =
      atomic_load_explicit(&buff->producer.head, memory_order_relaxed);
  size_t current_tail =
      atomic_load_explicit(&buff->consumer.tail, memory_order_acquire);

  size_t next_head = (current_head + 1) & bb_modulo_mask(buff);

  if (next_head == current_tail) {
    /* Buffer is full */
    if (buff->overflow_behaviour == OVERFLOW_DROP) {
      /* Drop the batch - just update statistics */
      atomic_fetch_add(&buff->producer.dropped_batches, 1);
      return Bp_EC_OK;
    }

    /* Slow path - need to wait for space */
    Bp_EC rc = bb_await_notfull(buff, buff->timeout_us);
    if (rc != Bp_EC_OK) {
      return rc;
    }

    /* Re-read tail after waiting */
    current_tail =
        atomic_load_explicit(&buff->consumer.tail, memory_order_acquire);
  }

  /* Fast path - we have space, update head */
  atomic_store_explicit(&buff->producer.head, next_head, memory_order_release);
  atomic_fetch_add(&buff->producer.total_batches, 1);

  /* Signal consumer if buffer was previously empty */
  if (current_head == current_tail) {
    pthread_mutex_lock(&buff->mutex);
    pthread_cond_signal(&buff->not_empty);
    pthread_mutex_unlock(&buff->mutex);
  }

  return Bp_EC_OK;
}

/* Initialize a batch buffer with specified parameters
 * @param buff Buffer to initialize
 * @param name Buffer name (e.g., "filter1.input[0]")
 * @param dtype Data type for buffer elements
 * @param ring_capacity_expo Ring buffer capacity as power of 2 (e.g., 10 = 1024 slots)
 * @param batch_capacity_expo Batch size as power of 2 (e.g., 8 = 256 elements per batch)
 * @param overflow_behaviour How to handle buffer overflow (BLOCK or DROP)
 * @param timeout_us Default timeout in microseconds for blocking operations
 * @return Bp_EC_OK on success, error code on failure
 */
Bp_EC bb_init(Batch_buff_t *buff, const char *name, SampleDtype_t dtype,
              size_t ring_capacity_expo, size_t batch_capacity_expo,
              OverflowBehaviour_t overflow_behaviour, unsigned long timeout_us) {
  
  if (!buff) {
    return Bp_EC_NULL_FILTER;
  }
  
  if (dtype >= DTYPE_MAX || dtype == DTYPE_NDEF) {
    return Bp_EC_INVALID_DTYPE;
  }
  
  if (ring_capacity_expo > 30 || batch_capacity_expo > 20) {
    return Bp_EC_INVALID_CONFIG;
  }
  
  /* Clear the structure */
  memset(buff, 0, sizeof(Batch_buff_t));
  
  /* Copy name */
  strncpy(buff->name, name ? name : "unnamed", sizeof(buff->name) - 1);
  buff->name[sizeof(buff->name) - 1] = '\0';
  
  /* Set configuration */
  buff->dtype = dtype;
  buff->ring_capacity_expo = ring_capacity_expo;
  buff->batch_capacity_expo = batch_capacity_expo;
  buff->overflow_behaviour = overflow_behaviour;
  buff->timeout_us = timeout_us;
  
  /* Calculate sizes */
  size_t ring_capacity = 1UL << ring_capacity_expo;
  size_t batch_capacity = 1UL << batch_capacity_expo;
  size_t data_width = bb_getdatawidth(dtype);
  
  /* Allocate ring buffers */
  buff->batch_ring = calloc(ring_capacity, sizeof(Bp_Batch_t));
  if (!buff->batch_ring) {
    return Bp_EC_MALLOC_FAIL;
  }
  
  buff->data_ring = calloc(ring_capacity * batch_capacity, data_width);
  if (!buff->data_ring) {
    free(buff->batch_ring);
    buff->batch_ring = NULL;
    return Bp_EC_MALLOC_FAIL;
  }
  
  /* Initialize synchronization primitives */
  if (pthread_mutex_init(&buff->mutex, NULL) != 0) {
    free(buff->data_ring);
    free(buff->batch_ring);
    return Bp_EC_MUTEX_INIT_FAIL;
  }
  
  if (pthread_cond_init(&buff->not_empty, NULL) != 0) {
    pthread_mutex_destroy(&buff->mutex);
    free(buff->data_ring);
    free(buff->batch_ring);
    return Bp_EC_COND_INIT_FAIL;
  }
  
  if (pthread_cond_init(&buff->not_full, NULL) != 0) {
    pthread_cond_destroy(&buff->not_empty);
    pthread_mutex_destroy(&buff->mutex);
    free(buff->data_ring);
    free(buff->batch_ring);
    return Bp_EC_COND_INIT_FAIL;
  }
  
  /* Initialize atomic variables */
  atomic_store(&buff->producer.head, 0);
  atomic_store(&buff->consumer.tail, 0);
  atomic_store(&buff->producer.total_batches, 0);
  atomic_store(&buff->producer.dropped_batches, 0);
  atomic_store(&buff->running, true);
  
  return Bp_EC_OK;
}

/* Deinitialize and free resources used by a batch buffer
 * @param buff Buffer to deinitialize
 * @return Bp_EC_OK on success, error code on failure
 */
Bp_EC bb_deinit(Batch_buff_t *buff) {
  if (!buff) {
    return Bp_EC_NULL_FILTER;
  }
  
  /* Stop the buffer to wake any waiting threads */
  atomic_store(&buff->running, false);
  
  /* Wake up any threads blocked on conditions */
  pthread_mutex_lock(&buff->mutex);
  pthread_cond_broadcast(&buff->not_empty);
  pthread_cond_broadcast(&buff->not_full);
  pthread_mutex_unlock(&buff->mutex);
  
  /* Give threads a moment to exit their wait states */
  usleep(1000);
  
  /* Destroy synchronization primitives */
  pthread_cond_destroy(&buff->not_full);
  pthread_cond_destroy(&buff->not_empty);
  pthread_mutex_destroy(&buff->mutex);
  
  /* Free memory */
  if (buff->data_ring) {
    free(buff->data_ring);
    buff->data_ring = NULL;
  }
  
  if (buff->batch_ring) {
    free(buff->batch_ring);
    buff->batch_ring = NULL;
  }
  
  /* Clear the structure */
  memset(buff, 0, sizeof(Batch_buff_t));
  
  return Bp_EC_OK;
}

/* Start the buffer (set running flag)
 * @param buff Buffer to start
 * @return Bp_EC_OK on success
 */
Bp_EC bb_start(Batch_buff_t *buff) {
  if (!buff) {
    return Bp_EC_NULL_FILTER;
  }
  
  atomic_store(&buff->running, true);
  return Bp_EC_OK;
}

/* Stop the buffer (clear running flag and wake waiting threads)
 * @param buff Buffer to stop
 * @return Bp_EC_OK on success
 */
Bp_EC bb_stop(Batch_buff_t *buff) {
  if (!buff) {
    return Bp_EC_NULL_FILTER;
  }
  
  atomic_store(&buff->running, false);
  
  /* Wake up any waiting threads */
  pthread_mutex_lock(&buff->mutex);
  pthread_cond_broadcast(&buff->not_empty);
  pthread_cond_broadcast(&buff->not_full);
  pthread_mutex_unlock(&buff->mutex);
  
  return Bp_EC_OK;
}
