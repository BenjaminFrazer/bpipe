#include "batch_buffer.h"

/* Wait for buffer to have space available
 * @param buf Buffer to wait on
 * @param timeout_us Timeout in microseconds (0 = wait indefinitely)
 * @return Bp_EC_OK if space available, Bp_EC_TIMEOUT on timeout, Bp_EC_STOPPED
 * if buffer stopped
 */
Bp_EC bb_await_notfull(Batch_buff_t* buff, unsigned long timeout)
{
	Bp_EC ec = Bp_EC_OK;
	pthread_mutex_lock(&buff->mutex);

	struct timespec abs_timeout = future_ts(timeout, CLOCK_MONOTONIC);

	while (bb_isfull(buff) && atomic_load(&buff->running)) {
		int ret = pthread_cond_timedwait(&buff->not_full, &buff->mutex, &abs_timeout);
		if (ret == ETIMEDOUT) {
			ec = Bp_EC_TIMEOUT;
			break;
		}
	}
	pthread_mutex_unlock(&buff->mutex);
	return ec;
}

Bp_EC bb_await_notempty(Batch_buff_t* buff, unsigned long timeout)
{
	Bp_EC ec = Bp_EC_OK;
	pthread_mutex_lock(&buff->mutex);

	struct timespec abs_timeout = future_ts(timeout, CLOCK_MONOTONIC);

	while (bb_isempy(buff) && atomic_load(&buff->running)) {
		int ret = pthread_cond_timedwait(&buff->not_empty, &buff->mutex, &abs_timeout);
		if (ret == ETIMEDOUT) {
			ec = Bp_EC_TIMEOUT;
			break;
		}
	}
	pthread_mutex_unlock(&buff->mutex);
	return ec;
}

/* Get the oldest consumable data batch. Doesn't change head or tail idx. */
Bp_Batch_t bb_get_tail(Batch_buff_t* buff)
{
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

/* Delete oldest batch and increment the tail pointer marking the slot as populateable.
 * Lock-free implementation for SPSC scenario.
 */
Bp_EC bb_del(Batch_buff_t* buff)
{
	/* Fast path - check without locks */
	size_t current_head = atomic_load_explicit(&buff->producer.head, memory_order_acquire);
	size_t current_tail = atomic_load_explicit(&buff->consumer.tail, memory_order_relaxed);
	
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
		current_head = atomic_load_explicit(&buff->producer.head, memory_order_acquire);
		current_tail = atomic_load_explicit(&buff->consumer.tail, memory_order_relaxed);
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
 *   If the buffer is full and overflow behaviour == OVERFLOW_DROP, the statistics
 *   are updated but head is not incremented, causing the next write to overwrite
 *   the current batch.
 * 
 * Blocking behaviour:
 *   If the buffer is full and overflow behaviour == OVERFLOW_BLOCK, this operation
 *   will block until space is available.
 */
Bp_EC bb_submit(Batch_buff_t* buff)
{
	/* Fast path - check if full without locks */
	size_t current_head = atomic_load_explicit(&buff->producer.head, memory_order_relaxed);
	size_t current_tail = atomic_load_explicit(&buff->consumer.tail, memory_order_acquire);
	
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
		current_tail = atomic_load_explicit(&buff->consumer.tail, memory_order_acquire);
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