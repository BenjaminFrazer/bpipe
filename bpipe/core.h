#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <assert.h>

#define MAX_SINKS 10
#define MAX_SOURCES 10
#define MAX_CAPACITY_EXPO 30 // max 1GB capacity


typedef enum _SampleType {
	DTYPE_NDEF=0,
	DTYPE_FLOAT,
	DTYPE_INT,
	DTYPE_UNSIGNED,
	DTYPE_MAX,
} SampleDtype_t;

typedef enum _OverflowBehaviour {
	OVERFLOW_BLOCK = 0,  // Block when buffer is full (default/current behavior)
	OVERFLOW_DROP = 1,   // Drop samples when buffer is full
} OverflowBehaviour_t;

extern const size_t _data_size_lut[];

typedef enum _Bp_EC {
        Bp_EC_OK = 0,
        Bp_EC_TIMEOUT = -1,
        Bp_EC_NOINPUT = -2,
        Bp_EC_NOSPACE = -3,
        EC_TYPE_MISMATCH = -4,
        Bp_EC_BAD_PYOBJECT = -5,
				BP_ERROR_COND_INIT_FAIL,
				BP_ERROR_MUTEX_INIT_FAIL,
        /* Filter lifecycle error codes */
        BP_ERROR_NULL_FILTER = -8,
        BP_ERROR_ALREADY_RUNNING = -9,
        BP_ERROR_THREAD_CREATE_FAIL = -10,
        BP_ERROR_THREAD_JOIN_FAIL = -11,
        /* Stream termination sentinel. Indicates no further data will be sent */
        Bp_EC_COMPLETE = 1,
} Bp_EC;

typedef struct _Batch {
        size_t head;
        size_t tail;
        int capacity;
        long long t_ns;
        unsigned period_ns;
        size_t batch_id;
        /* Error code or control indicator. Use Bp_EC_COMPLETE to signal end of stream */
        Bp_EC ec;
        void* meta;
        SampleDtype_t dtype;
        void* data;
} Bp_Batch_t;

/* Forward declarations */
typedef struct _DataPipe Bp_Filter_t;

typedef void (TransformFcn_t)(Bp_Filter_t* filt, Bp_Batch_t **input_batches, int n_inputs, Bp_Batch_t **output_batches, int n_outputs);

typedef struct _err_info {
	Bp_EC ec;
	int line_no;
	const char* filename;
	const char* function;
	const char* err_msg;
} Err_info;


typedef struct _Bp_BatchBuffer {
        void*        data_ring;
        Bp_Batch_t*  batch_ring;
        size_t       head;
        size_t       tail;
        size_t       ring_capacity_expo;
        size_t       batch_capacity_expo;
        pthread_mutex_t mutex;
        pthread_cond_t  not_empty;
        pthread_cond_t  not_full;
} Bp_BatchBuffer_t;

typedef struct _DataPipe {
        bool running;
        TransformFcn_t* transform;
        Err_info worker_err_info;
        struct timespec timeout;
        struct _DataPipe* sources[MAX_SOURCES];
        struct _DataPipe* sinks[MAX_SINKS];
        int n_sources;
        int n_sinks;
        size_t data_width;
        OverflowBehaviour_t overflow_behaviour;
        SampleDtype_t dtype;
        pthread_t worker_thread;
        pthread_mutex_t filter_mutex;  // Protects sinks/sources arrays
        Bp_BatchBuffer_t input_buffers[MAX_SOURCES];
} Bp_Filter_t;


Bp_EC BpFilter_Init(Bp_Filter_t *filter, TransformFcn_t transform_function, int initial_state,
                   size_t buffer_size, int batch_size, int number_of_batches_exponent, int number_of_input_filters);

Bp_EC BpFilter_Deinit(Bp_Filter_t *filter);

/* Multi-I/O connection functions */
Bp_EC Bp_add_sink(Bp_Filter_t *filter, Bp_Filter_t *sink);
Bp_EC Bp_add_source(Bp_Filter_t *filter, Bp_Filter_t *source);
Bp_EC Bp_remove_sink(Bp_Filter_t *filter, Bp_Filter_t *sink);
Bp_EC Bp_remove_source(Bp_Filter_t *filter, Bp_Filter_t *source);

Bp_EC Bp_BatchBuffer_Init(Bp_BatchBuffer_t *buffer, size_t batch_size, size_t number_of_batches);

Bp_EC Bp_BatchBuffer_Deinit(Bp_BatchBuffer_t *buffer);

/* Filter lifecycle functions */
Bp_EC Bp_Filter_Start(Bp_Filter_t *filter);
Bp_EC Bp_Filter_Stop(Bp_Filter_t *filter);


static inline void set_filter_error(Bp_Filter_t* filt, Bp_EC code, const char* msg, const char* file, int line, const char* func) {
	filt->worker_err_info.ec = code;
	filt->worker_err_info.line_no = line;
	filt->worker_err_info.filename = file;
	filt->worker_err_info.function = func;
	filt->worker_err_info.err_msg = msg;
	filt->running = false;
}

#define SET_FILTER_ERROR(filt, code, msg) \
    set_filter_error((filt), (code), (msg), __FILE__, __LINE__, __func__)


#define Bp_ASSERT(filt, condition, code, msg)      \
    do {                                                           \
        if (!(condition)) {                                        \
            SET_FILTER_ERROR((filt), (code), (msg));               \
        }                                                          \
    } while (0)


static inline size_t Bp_tail_idx(Bp_Filter_t* dpipe, int buffer_idx){
        unsigned long mask = (1u << dpipe->input_buffers[buffer_idx].ring_capacity_expo) - 1u;
        return dpipe->input_buffers[buffer_idx].tail & mask;
}

static inline size_t Bp_head_idx(Bp_Filter_t* dpipe, int buffer_idx){
        unsigned long mask = (1u << dpipe->input_buffers[buffer_idx].ring_capacity_expo) - 1u;
        return dpipe->input_buffers[buffer_idx].head & mask;
}

static inline unsigned long Bp_ring_capacity(Bp_BatchBuffer_t* buf){
        return 1u << buf->ring_capacity_expo;
}

static inline unsigned long Bp_batch_capacity(Bp_BatchBuffer_t* buf){
        return 1u << buf->batch_capacity_expo;
}

static inline Bp_EC Bp_allocate_buffers(Bp_Filter_t* dpipe, int buffer_idx){
        assert(dpipe->dtype != DTYPE_NDEF);

        dpipe->input_buffers[buffer_idx].data_ring  = malloc(dpipe->data_width * Bp_ring_capacity(&dpipe->input_buffers[buffer_idx]));
        dpipe->input_buffers[buffer_idx].batch_ring = malloc(sizeof(Bp_Batch_t) * Bp_ring_capacity(&dpipe->input_buffers[buffer_idx]));
        dpipe->input_buffers[buffer_idx].head = 0;
        dpipe->input_buffers[buffer_idx].tail = 0;

        assert(dpipe->input_buffers[buffer_idx].data_ring != NULL);
        assert(dpipe->input_buffers[buffer_idx].batch_ring != NULL);
        return Bp_EC_OK;
}


static inline Bp_EC Bp_deallocate_buffers(Bp_Filter_t* dpipe, int buffer_idx){

        assert(dpipe->dtype != DTYPE_NDEF);

        free(dpipe->input_buffers[buffer_idx].data_ring);
        free(dpipe->input_buffers[buffer_idx].batch_ring);

        dpipe->input_buffers[buffer_idx].data_ring = NULL;
        dpipe->input_buffers[buffer_idx].batch_ring = NULL;
        dpipe->input_buffers[buffer_idx].head = 0;
        dpipe->input_buffers[buffer_idx].tail = 0;
        return Bp_EC_OK;
}

/* Applies a transform using a python filter */
TransformFcn_t BpPyTransform;
/* Multi-input/output pass-through transform */
TransformFcn_t BpPassThroughTransform;


static inline bool Bp_empty(Bp_BatchBuffer_t* buf){
        return buf->head == buf->tail;
}

static inline bool Bp_full(Bp_Filter_t* dpipe, Bp_BatchBuffer_t* buf){
        (void)dpipe;
        return (buf->head - buf->tail) >= Bp_ring_capacity(buf);
}

static inline Bp_EC Bp_await_not_empty(Bp_Filter_t* dpipe, Bp_BatchBuffer_t* buf){
        Bp_EC ec = Bp_EC_OK;
        pthread_mutex_lock(&buf->mutex);
        while (Bp_empty(buf) && dpipe->running) {
                // Use a short timeout to check the running flag periodically
                struct timespec timeout;
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_nsec += 100000000; // 100ms
                if (timeout.tv_nsec >= 1000000000) {
                    timeout.tv_sec += 1;
                    timeout.tv_nsec -= 1000000000;
                }
                
                if (pthread_cond_timedwait(&buf->not_empty, &buf->mutex, &timeout) == ETIMEDOUT) {
                        // Timeout is expected - just check running flag again
                        continue;
                }
        }
        if (!dpipe->running) {
                ec = Bp_EC_COMPLETE; // Signal filter is stopping
        }
        pthread_mutex_unlock(&buf->mutex);
        return ec;
}

static inline Bp_EC Bp_await_not_full(Bp_Filter_t* dpipe, Bp_BatchBuffer_t* buf){
        Bp_EC ec = Bp_EC_OK;
        pthread_mutex_lock(&buf->mutex);
        while (Bp_full(dpipe, buf) && dpipe->running) {
                // Use a short timeout to check the running flag periodically
                struct timespec timeout;
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_nsec += 100000000; // 100ms
                if (timeout.tv_nsec >= 1000000000) {
                    timeout.tv_sec += 1;
                    timeout.tv_nsec -= 1000000000;
                }
                
                if (pthread_cond_timedwait(&buf->not_full, &buf->mutex, &timeout) == ETIMEDOUT) {
                        // Timeout is expected - just check running flag again
                        continue;
                }
        }
        if (!dpipe->running) {
                ec = Bp_EC_COMPLETE; // Signal filter is stopping
        }
        pthread_mutex_unlock(&buf->mutex);
        return ec;
}

static inline Bp_Batch_t Bp_allocate(Bp_Filter_t* dpipe, Bp_BatchBuffer_t* buf){
        Bp_Batch_t batch = {0};
        
        // Check overflow behavior before waiting
        if (dpipe->overflow_behaviour == OVERFLOW_DROP && Bp_full(dpipe, buf)) {
                batch.ec = Bp_EC_NOSPACE; // Signal that allocation failed due to overflow
                return batch;
        }
        
        if (Bp_await_not_full(dpipe, buf) == Bp_EC_OK) {
                size_t idx = buf->head & ((1u << buf->ring_capacity_expo) - 1u);
                void* data_ptr = (char*)buf->data_ring + idx * dpipe->data_width * Bp_batch_capacity(buf);
                batch.capacity = Bp_batch_capacity(buf);
                batch.data = data_ptr;
                batch.batch_id = idx;
                batch.dtype = dpipe->dtype;
        } else {
                batch.ec = Bp_EC_TIMEOUT;
        }
        return batch;
}

static inline void Bp_submit_batch(Bp_Filter_t* dpipe, Bp_BatchBuffer_t* buf, Bp_Batch_t* batch) {
        size_t idx = buf->head & ((1u << buf->ring_capacity_expo) - 1u);
        buf->batch_ring[idx] = *batch;
        buf->head++;
        pthread_cond_signal(&buf->not_empty);
}

static inline Bp_Batch_t Bp_head(Bp_Filter_t* dpipe, Bp_BatchBuffer_t* buf) {
        Bp_Batch_t batch = {0};
        if (Bp_await_not_empty(dpipe, buf) == Bp_EC_OK) {
                size_t idx = buf->tail & ((1u << buf->ring_capacity_expo) - 1u);
                batch = buf->batch_ring[idx];
        } else {
                batch.ec = Bp_EC_TIMEOUT;
        }
        return batch;
}

static inline void Bp_delete_tail(Bp_Filter_t* dpipe, Bp_BatchBuffer_t* buf) {
        buf->tail++;
        pthread_cond_signal(&buf->not_full);
}

/* Worker thread entry point */
void* Bp_Worker(void* filter);
