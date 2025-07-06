#ifndef BPIPE_CORE_H
#define BPIPE_CORE_H

#include "bperr.h"
#include "batch_buffer.h"
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
#define MAX_INPUTS 10
#define MAX_CAPACITY_EXPO 30 // max 1GB capacity
#define MAX_RING_CAPACITY_EXPO 12 // max 4016 entries in ring buffer

/* Transform function signature
 * Note: Transforms should only write to output_batches[0]. The framework
 * automatically distributes data to additional outputs when n_outputs > 1.
 * For explicit control over multi-output distribution, use BpTeeFilter.
 */
typedef void* (Worker_t)(void *);

typedef struct _Core_filt_config_t {
  const char *name;
	size_t size; // size of the whole filter struct (needed for inheritance).
	SampleDtype_t dtype;
	size_t n_inputs;
	size_t batch_capacity_expo;
	size_t ring_capacity_expo;
	OverflowBehaviour_t overflow;
	size_t timeout;
}Core_filt_config_t;

typedef struct _Filter_t {
	char name[32];
	size_t size;
	bool running;
	Worker_t *worker;
	Err_info worker_err_info;
	struct timespec timeout;
	int n_input_buffers;
	int n_sinks;
	size_t data_width;
	pthread_t worker_thread;
	pthread_mutex_t filter_mutex; // Protects sinks arrays
	Batch_buff_t input_buffers[MAX_INPUTS];
	Batch_buff_t* sinks[MAX_SINKS];
} Filter_t;

/* Configuration-based initialization API */
Bp_EC filt_init(Filter_t *filter, Core_filt_config_t config);

Bp_EC filt_deinit(Filter_t *filter);

/* Multi-I/O connection functions */
Bp_EC filt_addsink(Filter_t *filter, size_t pad_idx, Batch_buff_t *dest_buffer);

Bp_EC filt_delsink(Filter_t *filter, size_t pad_idx);

/* Filter lifecycle functions */
Bp_EC filt_start(Filter_t *filter);
Bp_EC filt_stop(Filter_t *filter);

#endif /* BPIPE_CORE_H */
