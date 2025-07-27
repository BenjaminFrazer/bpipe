#include "batch_buffer.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "bperr.h"

/* Branch prediction hints */
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

size_t _data_size_lut[] = {
    [DTYPE_NDEF] = 0,
    [DTYPE_I32] = sizeof(int32_t),
    [DTYPE_FLOAT] = sizeof(float),
    [DTYPE_U32] = sizeof(uint32_t),
};

/* Wait for buffer to have space available
 * @param buf Buffer to wait on
 * @param timeout_us Timeout in microseconds (0 = wait indefinitely)
 * @return Bp_EC_OK if space available, Bp_EC_TIMEOUT on timeout, Bp_EC_STOPPED
 * if buffer stopped
 */
Bp_EC bb_await_notfull(Batch_buff_t *buff, long long timeout_us)
{
  Bp_EC ec = Bp_EC_OK;
  pthread_mutex_lock(&buff->mutex);

  /* Calculate absolute timeout once, before the loop */
  struct timespec abs_timeout;
  if (timeout_us > 0) {
    abs_timeout = future_ts(timeout_us * 1000, CLOCK_REALTIME);
    /* DEBUG: Uncomment to debug timeout issues
    long long now = now_ns(CLOCK_REALTIME);
    long long abs_ns = abs_timeout.tv_sec * 1000000000LL + abs_timeout.tv_nsec;
    fprintf(stderr, "bb_await_notfull: timeout_us=%lld, wait_ns=%lld,
    current_time=%lld, abs_time=%lld\n", timeout_us, abs_ns - now, now, abs_ns);
    */
  }

  while (bb_isfull(buff) && atomic_load(&buff->running) &&
         !atomic_load(&buff->force_return_head)) {
    if (timeout_us == 0) {
      // Wait indefinitely
      pthread_cond_wait(&buff->not_full, &buff->mutex);
    } else {
      int ret =
          pthread_cond_timedwait(&buff->not_full, &buff->mutex, &abs_timeout);
      if (ret == ETIMEDOUT) {
        ec = Bp_EC_TIMEOUT;
        break;
      } else if (ret != 0) {
        /* Some other error occurred - treat as timeout for safety */
        ec = Bp_EC_TIMEOUT;
        break;
      }
      /* Continue looping on spurious wakeup (ret == 0 but condition still true)
       */
    }
  }

  /* Check if we were forced to return */
  if (atomic_load(&buff->force_return_head)) {
    ec = buff->force_return_head_code;
    atomic_store(&buff->force_return_head, false); /* Clear flag */
  }

  /* Check why we exited the loop */
  if (ec == Bp_EC_OK && !atomic_load(&buff->running)) {
    ec = Bp_EC_STOPPED;
  }

  pthread_mutex_unlock(&buff->mutex);
  return ec;
}

Bp_EC bb_await_notempty(Batch_buff_t *buff, long long timeout_us)
{
  Bp_EC ec = Bp_EC_OK;
  pthread_mutex_lock(&buff->mutex);

  /* Calculate absolute timeout once, before the loop */
  struct timespec abs_timeout;
  if (timeout_us > 0) {
    abs_timeout = future_ts(timeout_us * 1000, CLOCK_REALTIME);
  }

  while (bb_isempy(buff) && atomic_load(&buff->running) &&
         !atomic_load(&buff->force_return_tail)) {
    int ret = 0;
    if (timeout_us == 0) {
      // Wait indefinitely
      ret = pthread_cond_wait(&buff->not_empty, &buff->mutex);
    } else {
      ret =
          pthread_cond_timedwait(&buff->not_empty, &buff->mutex, &abs_timeout);
    }
    if (ret == ETIMEDOUT) {
      ec = Bp_EC_TIMEOUT;
      break;
    } else if (ret != 0) {
      /* Some other error occurred - treat as timeout for safety */
      ec = Bp_EC_PTHREAD_UNKOWN;
      break;
    }
    /* Continue looping on spurious wakeup (ret == 0 but condition still true)
     */
  }

  /* Check if we were forced to return */
  if (atomic_load(&buff->force_return_tail)) {
    ec = buff->force_return_tail_code;
    atomic_store(&buff->force_return_tail, false); /* Clear flag */
  }

  /* Check why we exited the loop */
  if (ec == Bp_EC_OK && !atomic_load(&buff->running)) {
    ec = Bp_EC_STOPPED;
  }

  pthread_mutex_unlock(&buff->mutex);
  return ec;
}

/* Get the oldest consumable data batch. Doesn't change head or tail idx.
 * Returns NULL on timeout. */
Batch_t *bb_get_tail(Batch_buff_t *buff, unsigned long timeout_us, Bp_EC *err)
{
  /* Fast path - check if data available without locks */
  if (!bb_isempy_lockfree(buff)) {
    size_t idx = bb_get_tail_idx(buff);
    /* Memory fence ensures we see the batch data written by producer */
    atomic_thread_fence(memory_order_acquire);
    *err = Bp_EC_OK;
    return &buff->batch_ring[idx];
  }

  /* Slow path - wait for data */
  Bp_EC rc = bb_await_notempty(buff, timeout_us);
  if (err != NULL) {
    *err = rc;
  }
  /* Return NULL on error.*/
  if (rc != Bp_EC_OK) {
    return NULL;
  }
  size_t idx = bb_get_tail_idx(buff);
  return &buff->batch_ring[idx];
}

/* Delete oldest batch and increment the tail pointer marking the slot as
 * populateable. Fails if run on an empty buffer.
 */
Bp_EC bb_del_tail(Batch_buff_t *buff)
{
  /* Fast path - check without locks */
  size_t current_head =
      atomic_load_explicit(&buff->producer.head, memory_order_acquire);
  size_t current_tail =
      atomic_load_explicit(&buff->consumer.tail, memory_order_relaxed);

  if (current_tail == current_head) {
    /* Buffer is empty - deletion isn't possible. */
    return Bp_EC_BUFFER_EMPTY;
  }

  /* Not empty, increment tail */
  size_t new_tail = (current_tail + 1) & bb_modulo_mask(buff);
  atomic_store_explicit(&buff->consumer.tail, new_tail, memory_order_release);

  /* Signal producer that buffer isn't full. Mutex does not need to be aquired
   * for this step given SPSC*/
  pthread_cond_signal(&buff->not_full);

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
Bp_EC bb_submit(Batch_buff_t *buff, unsigned long timeout_us)
{
  /* Fast path - check if full without locks */
  size_t current_head =
      atomic_load_explicit(&buff->producer.head, memory_order_relaxed);
  size_t current_tail =
      atomic_load_explicit(&buff->consumer.tail, memory_order_acquire);

  size_t next_head = (current_head + 1) & bb_modulo_mask(buff);

  if (next_head == current_tail) {
    /* Buffer is full */
    if (buff->overflow_behaviour == OVERFLOW_DROP_HEAD) {
      /* Drop the new batch - just update statistics */
      atomic_fetch_add(&buff->producer.dropped_batches, 1);
      return Bp_EC_OK;
    }

    if (unlikely(buff->overflow_behaviour == OVERFLOW_DROP_TAIL)) {
      /* Drop oldest batch - need mutex for safety */
      pthread_mutex_lock(&buff->mutex);

      /* Re-check under lock */
      if (bb_isfull(buff)) {
        /* Force tail advance */
        size_t new_tail =
            (atomic_load(&buff->consumer.tail) + 1) & bb_modulo_mask(buff);
        atomic_store(&buff->consumer.tail, new_tail);
        atomic_fetch_add(&buff->consumer.dropped_by_producer, 1);

        /* Wake consumer if blocked */
        pthread_cond_signal(&buff->not_empty);
      }

      pthread_mutex_unlock(&buff->mutex);
    } else {
      /* OVERFLOW_BLOCK - wait for space */
      Bp_EC rc = bb_await_notfull(buff, timeout_us);
      if (rc != Bp_EC_OK) {
        return rc;
      }
    }

    /* Re-read tail after waiting/dropping */
    atomic_load_explicit(&buff->consumer.tail, memory_order_acquire);
  }

  /* Fast path - we have space, update head */
  atomic_store_explicit(&buff->producer.head, next_head, memory_order_release);
  atomic_fetch_add(&buff->producer.total_batches, 1);

  pthread_cond_signal(&buff->not_empty);

  return Bp_EC_OK;
}

/* Initialize a batch buffer with specified parameters
 * @param buff Buffer to initialize
 * @param name Buffer name (e.g., "filter1.input[0]")
 * @param dtype Data type for buffer elements
 * @param ring_capacity_expo Ring buffer capacity as power of 2 (e.g., 10 = 1024
 * slots)
 * @param batch_capacity_expo Batch size as power of 2 (e.g., 8 = 256 elements
 * per batch)
 * @param overflow_behaviour How to handle buffer overflow (BLOCK or DROP)
 * @param timeout_us Default timeout in microseconds for blocking operations
 * @return Bp_EC_OK on success, error code on failure
 */
Bp_EC bb_init(Batch_buff_t *buff, const char *name, BatchBuffer_config config)
{
  if (!buff) {
    return Bp_EC_NULL_FILTER;
  }

  if (config.dtype >= DTYPE_MAX || config.dtype == DTYPE_NDEF) {
    return Bp_EC_INVALID_DTYPE;
  }

  if (config.ring_capacity_expo > 30 || config.batch_capacity_expo > 20) {
    return Bp_EC_INVALID_CONFIG;
  }

  if (config.overflow_behaviour > OVERFLOW_MAX) {
    return Bp_EC_INVALID_CONFIG;
  }
  /* Clear the structure */
  memset(buff, 0, sizeof(Batch_buff_t));

  /* Copy name */
  strncpy(buff->name, name ? name : "unnamed", sizeof(buff->name) - 1);
  buff->name[sizeof(buff->name) - 1] = '\0';

  /* Set configuration */
  buff->dtype = config.dtype;
  buff->ring_capacity_expo = config.ring_capacity_expo;
  buff->batch_capacity_expo = config.batch_capacity_expo;
  buff->overflow_behaviour = config.overflow_behaviour;

  /* Calculate sizes */
  size_t ring_capacity = 1UL << config.ring_capacity_expo;
  size_t batch_capacity = 1UL << config.batch_capacity_expo;
  size_t data_width = bb_getdatawidth(config.dtype);

  /* Allocate ring buffers */
  buff->batch_ring = calloc(ring_capacity, sizeof(Batch_t));
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
  atomic_store(&buff->consumer.dropped_by_producer, 0);
  atomic_store(&buff->running, true);

  /* Initialize force return fields */
  atomic_store(&buff->force_return_head, false);
  atomic_store(&buff->force_return_tail, false);
  buff->force_return_head_code = Bp_EC_OK;
  buff->force_return_tail_code = Bp_EC_OK;

  /* Populate key batch data*/
  for (int i = 0; i < bb_n_batches(buff); i++) {
    buff->batch_ring[i].head = 0;
    buff->batch_ring[i].t_ns = -1;
    buff->batch_ring[i].data =
        (char *) buff->data_ring + (bb_batch_size(buff) * data_width * i);
  }

  return Bp_EC_OK;
}

/* Deinitialize and free resources used by a batch buffer
 * @param buff Buffer to deinitialize
 * @return Bp_EC_OK on success, error code on failure
 */
Bp_EC bb_deinit(Batch_buff_t *buff)
{
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
  struct timespec delay = {
      .tv_sec = 0,
      .tv_nsec = 1000000 /* 1 millisecond = 1,000,000 nanoseconds */
  };
  nanosleep(&delay, NULL);

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
Bp_EC bb_start(Batch_buff_t *buff)
{
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
Bp_EC bb_stop(Batch_buff_t *buff)
{
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

/**
 * Force producer to return from bb_submit/bb_get_head with error code
 * This does not stop the buffer - it only wakes the producer once
 * @param buff Buffer to signal
 * @param return_code Error code to return to producer
 * @return Bp_EC_OK on success
 */
Bp_EC bb_force_return_head(Batch_buff_t *buff, Bp_EC return_code)
{
  if (!buff) {
    return Bp_EC_NULL_FILTER;
  }

  pthread_mutex_lock(&buff->mutex);
  buff->force_return_head_code = return_code;
  atomic_store(&buff->force_return_head, true);
  pthread_cond_signal(&buff->not_full); /* Wake producer if waiting */
  pthread_mutex_unlock(&buff->mutex);

  return Bp_EC_OK;
}

/**
 * Force consumer to return from bb_get_tail with error code
 * This does not stop the buffer - it only wakes the consumer once
 * @param buff Buffer to signal
 * @param return_code Error code to return to consumer
 * @return Bp_EC_OK on success
 */
Bp_EC bb_force_return_tail(Batch_buff_t *buff, Bp_EC return_code)
{
  if (!buff) {
    return Bp_EC_NULL_FILTER;
  }

  pthread_mutex_lock(&buff->mutex);
  buff->force_return_tail_code = return_code;
  atomic_store(&buff->force_return_tail, true);
  pthread_cond_signal(&buff->not_empty); /* Wake consumer if waiting */
  pthread_mutex_unlock(&buff->mutex);

  return Bp_EC_OK;
}
