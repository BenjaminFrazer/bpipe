#include <arpa/inet.h>
#include <assert.h>
#include <ctime>
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
#include <stdatomic.h>
#include "bperr.h"
#include "core.h"


typedef enum _SampleType {
	DTYPE_NDEF = 0,
	DTYPE_FLOAT,
	DTYPE_INT,
	DTYPE_UNSIGNED,
	DTYPE_MAX,
} SampleDtype_t;

typedef enum _OverflowBehaviour {
	OVERFLOW_BLOCK = 0,  // Block when buffer is full (default/current behavior)
	OVERFLOW_DROP = 1,   // Drop samples when buffer is full
} OverflowBehaviour_t;

size_t _data_size_lut[DTYPE_MAX] = {
	[DTYPE_NDEF]      = 0,
	[DTYPE_INT]       = sizeof(int),
	[DTYPE_FLOAT]     = sizeof(float),
	[DTYPE_UNSIGNED]  = sizeof(unsigned),
};


/* Data-width utilities. */

static inline size_t bb_getdatawidth(SampleDtype_t stype){
	assert(stype < DTYPE_MAX);
	return _data_size_lut[stype];
}

/* Time-stamp manipulation utilities */

static inline long long now_ns(clockid_t clock) {
	struct timespec ts;
	clock_gettime(clock, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static inline struct timespec ts_from_ns(long long time_ns){
	struct timespec ts = {
			.tv_sec = (time_t)(time_ns / 1000000000LL),
			.tv_nsec = (long)(time_ns % 1000000000LL)
	};
	return ts;
}

static inline struct timespec future_ts(long long time_ns, clockid_t clock){
	long long future_ns = now_ns(clock) + time_ns;
	return ts_from_ns(future_ns);
}


typedef struct _Batch {
	size_t head;
	size_t tail;
	int capacity;
	long long t_ns;
	unsigned period_ns;
	size_t batch_id;
	/* Error code or control indicator. Use Bp_EC_COMPLETE to signal end of
		 * stream */
	Bp_EC ec;
	void* meta;
	SampleDtype_t dtype;
	void* data;
} Bp_Batch_t;


typedef struct _Bp_BatchBuffer {
	/* Existing synchronization and storage */
	char name[32]; /* e.g., "filter1.input[0]" */
	SampleDtype_t dtype;

	void* data_ring;
	Bp_Batch_t* batch_ring;

	/* CRITICAL DESIGN DECISION: Producer and consumer fields are separated into
	 * different cache lines to prevent false sharing. False sharing occurs when
	 * different threads update variables on the same cache line, causing the
	 * entire cache line to bounce between CPU cores. This can degrade performance
	 * by 10-100x. The __attribute__((aligned(64))) ensures each structure starts
	 * on a new cache line (64 bytes is typical for x86_64).
	 */
	
	/* Producer-only fields - modified only by producer thread */
	struct {
		_Atomic size_t head;              /* Next slot to write */
		_Atomic uint64_t total_batches;   /* Total batches submitted */
		_Atomic uint64_t dropped_batches; /* Dropped due to overflow */
		uint64_t blocked_time_ns;         /* Time spent blocking */
	} producer __attribute__((aligned(64)));

	/* Consumer-only fields - modified only by consumer thread */
	struct {
		_Atomic size_t tail;              /* Next slot to read */
	} consumer __attribute__((aligned(64)));

	/* Shared fields - accessed by both threads but only on slow path */
	/* Capacity information */
	size_t ring_capacity_expo;
	size_t batch_capacity_expo;

	pthread_mutex_t mutex;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
	_Atomic bool running;

	OverflowBehaviour_t overflow_behaviour;
	unsigned long timeout_us;
} Batch_buff_t;

static inline size_t get_tail_idx(Batch_buff_t* buff)
{
	unsigned long mask = (1u << buff->ring_capacity_expo) - 1u;
	return atomic_load_explicit(&buff->consumer.tail, memory_order_relaxed) & mask;
}

static inline size_t bb_get_head_idx(Batch_buff_t* buff)
{
	unsigned long mask = (1u << buff->ring_capacity_expo) - 1u;
	return atomic_load_explicit(&buff->producer.head, memory_order_relaxed) & mask;
}

static inline unsigned long bb_capacity(Batch_buff_t* buf)
{
    return 1u << buf->ring_capacity_expo;
}

static inline unsigned long batch_size(Batch_buff_t* buf)
{
    return 1u << buf->batch_capacity_expo;
}


static inline unsigned long bb_modulo_mask(const Batch_buff_t* buff)
{
	return (1u << buff->ring_capacity_expo) - 1u;
}

/* Lock-free empty check for fast path */
static inline bool bb_isempy_lockfree(const Batch_buff_t* buf)
{
	/* Acquire ensures we see all writes from producer before reading data */
	size_t head = atomic_load_explicit(&buf->producer.head, memory_order_acquire);
	size_t tail = atomic_load_explicit(&buf->consumer.tail, memory_order_relaxed);
	return head == tail;
}

/* Lock-free full check for fast path */
static inline bool bb_isfull_lockfree(const Batch_buff_t* buff)
{
	size_t head = atomic_load_explicit(&buff->producer.head, memory_order_relaxed);
	/* Acquire ensures we see consumer's progress */
	size_t tail = atomic_load_explicit(&buff->consumer.tail, memory_order_acquire);
	return ((head + 1) & bb_modulo_mask(buff)) == tail;
}

/* Original functions for use within mutex-protected sections */
static inline bool bb_isempy(const Batch_buff_t* buf)
{
	return buf->producer.head == buf->consumer.tail;
}

static inline bool bb_isfull(const Batch_buff_t* buff)
{
	return ((buff->producer.head + 1) & bb_modulo_mask(buff)) == buff->consumer.tail;
}

static inline size_t bb_space(const Batch_buff_t* buf)
{
	size_t head = atomic_load_explicit(&buf->producer.head, memory_order_relaxed);
	size_t tail = atomic_load_explicit(&buf->consumer.tail, memory_order_acquire);
	return head - tail;
}


/* Wait for buffer to have space available
 * @param buf Buffer to wait on
 * @param timeout_us Timeout in microseconds (0 = wait indefinitely)
 * @return Bp_EC_OK if space available, Bp_EC_TIMEOUT on timeout, Bp_EC_STOPPED
 * if buffer stopped
 */
static inline Bp_EC bb_await_notfull(Batch_buff_t* buff, unsigned long timeout){
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

static inline Bp_EC bb_await_notempty(Batch_buff_t* buff, unsigned long timeout){
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

/* Get the active batch. Doesn't change head or tail idx. */
static inline Bp_Batch_t bb_get_head(Batch_buff_t* buff)
{
	size_t idx = bb_get_head_idx(buff);
	return buff->batch_ring[idx];
}

/* Get the oldest consumable data batch. Doesn't change head or tail idx. */
static inline Bp_Batch_t bb_get_tail(Batch_buff_t* buff)
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
static inline Bp_EC bb_del(Batch_buff_t* buff)
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
static inline Bp_EC bb_submit(Batch_buff_t* buff)
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