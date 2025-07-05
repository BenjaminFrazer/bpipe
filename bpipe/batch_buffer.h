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

	/* head/tail pointers */
	size_t head;
	size_t tail;

	/* Capacity information */
	size_t ring_capacity_expo;
	size_t batch_capacity_expo;

	pthread_mutex_t mutex;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
	bool running;

	OverflowBehaviour_t overflow_behaviour;
	unsigned long timeout_us;

	/* Statistics */
	uint64_t total_batches;
	uint64_t dropped_batches;
	uint64_t blocked_time_ns;
} Batch_buff_t;

static inline size_t get_tail_idx(Batch_buff_t* buff)
{
	unsigned long mask = (1u << buff->ring_capacity_expo) - 1u;
	return buff->tail & mask;
}

static inline size_t bb_get_head_idx(Batch_buff_t* buff)
{
	unsigned long mask = (1u << buff->ring_capacity_expo) - 1u;
	return buff->head & mask;
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


static inline bool bb_isempy(const Batch_buff_t* buf)
{
	return buf->head == buf->tail;
}

static inline bool bb_isfull(const Batch_buff_t* buff)
{
	return ((buff->head+1) & bb_modulo_mask(buff)) == buff->tail;
}

static inline size_t bb_space(const Batch_buff_t* buf)
{
	return (buf->head - buf->tail);
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

	while (bb_isfull(buff) && buff->running) {
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

	while (bb_isempy(buff) && buff->running) {
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
	return buff->batch_ring[buff->head];
}

/* Get the oldest consumable data batch. Doesn't change head or tail idx. */
static inline Bp_Batch_t bb_get_tail(Batch_buff_t* buff)
{
	bb_await_notempty(buff, buff->timeout_us);
	return buff->batch_ring[buff->tail];
}

/* Delete oldest batch and increment the tail poiter marking the slot as populateable.*/
static inline Bp_EC bb_del(Batch_buff_t* buff)
{
	Bp_EC ec;
	if (bb_isempy(buff)){
		ec = Bp_EC_BUFFER_EMPTY;
	}
	else {
		buff->tail = (buff->tail + 1) & bb_modulo_mask(buff);
		ec = Bp_EC_OK;
	}
}

/* logically bb_submit increments the active slot effectively marking the current slot as consumable. 
 * Dropping behaviour:
	 * IF the buffer is full and blocking overflow behaviour ==OVERFLOW_DROP the active slot is not 
	 * incremented and the next batch iwll write over the current batch. This is effectivey "Dropping" it
	 * if no space is available
 * Blocking behaviour:
	 * If the Buffer is full and overflow behaviour == OVERFLOW_BLOCK, this operation will block untill
	 * space is available.
*/
static inline Bp_EC bb_submit(Batch_buff_t* buff)
{
	Bp_EC rc; 
	if (buff->overflow_behaviour == OVERFLOW_DROP && bb_isfull(buff)) {
		buff->dropped_batches++;
		rc = Bp_EC_OK;
	}
	else{
		rc = bb_await_notfull(buff, buff->timeout_us);
		buff->head = (buff->head+1) & bb_modulo_mask(buff);
		buff->total_batches++;
	}
	return rc;
}
