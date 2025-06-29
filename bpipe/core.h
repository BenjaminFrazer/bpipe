#ifndef BPIPE_CORE_H
#define BPIPE_CORE_H

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
#define MAX_CAPACITY_EXPO 30  // max 1GB capacity

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

extern const size_t _data_size_lut[];

typedef enum _Bp_EC {
    /* Success */
    Bp_EC_OK = 0,
    /* Positive status codes */
    Bp_EC_COMPLETE = 1,  /* Stream termination sentinel */
    Bp_EC_STOPPED = 2,   /* Buffer has been stopped */
    /* Negative error codes */
    Bp_EC_TIMEOUT = -1,
    Bp_EC_NOINPUT = -2,
    Bp_EC_NOSPACE = -3,
    Bp_EC_TYPE_MISMATCH = -4,
    Bp_EC_BAD_PYOBJECT = -5,
    Bp_EC_COND_INIT_FAIL = -6,
    Bp_EC_MUTEX_INIT_FAIL = -7,
    Bp_EC_NULL_FILTER = -8,
    Bp_EC_ALREADY_RUNNING = -9,
    Bp_EC_THREAD_CREATE_FAIL = -10,
    Bp_EC_THREAD_JOIN_FAIL = -11,
    Bp_EC_DTYPE_MISMATCH = -12,     /* Source/sink data types don't match */
    Bp_EC_WIDTH_MISMATCH = -13,     /* Data width mismatch */
    Bp_EC_INVALID_DTYPE = -14,      /* Invalid or unsupported data type */
    Bp_EC_INVALID_CONFIG = -15,     /* Invalid configuration parameters */
    Bp_EC_CONFIG_REQUIRED = -16,    /* Configuration missing required fields */
} Bp_EC;

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

/* Forward declarations */
typedef struct _DataPipe Bp_Filter_t;

/* Transform function signature
 * Note: Transforms should only write to output_batches[0]. The framework
 * automatically distributes data to additional outputs when n_outputs > 1.
 * For explicit control over multi-output distribution, use BpTeeFilter.
 */
typedef void(TransformFcn_t)(Bp_Filter_t* filt, Bp_Batch_t** input_batches,
                             int n_inputs, Bp_Batch_t* const* output_batches,
                             int n_outputs);

/* Configuration structure for filter initialization */
typedef struct {
    TransformFcn_t* transform;
    SampleDtype_t dtype;
    size_t buffer_size;
    int batch_size;
    int number_of_batches_exponent;
    int number_of_input_filters;
    
    /* Future extensibility without breaking API */
    OverflowBehaviour_t overflow_behaviour;  /* Default: OVERFLOW_BLOCK */
    bool auto_allocate_buffers;             /* Default: true */
    void* memory_pool;                      /* Default: NULL (use malloc) */
    size_t alignment;                       /* Default: 0 (natural alignment) */
    unsigned long timeout_us;               /* Default: 1000000 (1 second) */
} BpFilterConfig;

/* Default configuration helper */
#define BP_FILTER_CONFIG_DEFAULT { \
    .transform = NULL, \
    .dtype = DTYPE_NDEF, \
    .buffer_size = 128, \
    .batch_size = 64, \
    .number_of_batches_exponent = 6, \
    .number_of_input_filters = 1, \
    .overflow_behaviour = OVERFLOW_BLOCK, \
    .auto_allocate_buffers = true, \
    .memory_pool = NULL, \
    .alignment = 0, \
    .timeout_us = 1000000 \
}

/* Type error structure for enhanced error reporting */
typedef struct {
    Bp_EC code;
    const char* message;
    SampleDtype_t expected_type;
    SampleDtype_t actual_type;
} BpTypeError;

typedef struct _err_info {
    Bp_EC ec;
    int line_no;
    const char* filename;
    const char* function;
    const char* err_msg;
} Err_info;

/* Configuration structure for buffer initialization */
typedef struct {
    size_t batch_size;
    size_t number_of_batches;
    size_t data_width;
    SampleDtype_t dtype;
    OverflowBehaviour_t overflow_behaviour;
    unsigned long timeout_us;
    const char* name;  /* Optional debug name */
} BpBufferConfig_t;

/* Default buffer configuration helper */
#define BP_BUFFER_CONFIG_DEFAULT { \
    .batch_size = 64, \
    .number_of_batches = 64, \
    .data_width = sizeof(float), \
    .dtype = DTYPE_FLOAT, \
    .overflow_behaviour = OVERFLOW_BLOCK, \
    .timeout_us = 1000000, \
    .name = NULL \
}

typedef struct _Bp_BatchBuffer {
    /* Existing synchronization and storage */
    void* data_ring;
    Bp_Batch_t* batch_ring;
    size_t head;
    size_t tail;
    size_t ring_capacity_expo;
    size_t batch_capacity_expo;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    bool stopped;
    
    /* Configuration moved from filter - makes buffer self-contained */
    size_t data_width;
    SampleDtype_t dtype;
    OverflowBehaviour_t overflow_behaviour;
    unsigned long timeout_us;
    
    /* Optional: back-reference for debugging */
    char name[32];  /* e.g., "filter1.input[0]" */
    
    /* Optional: statistics */
    uint64_t total_batches;
    uint64_t dropped_batches;
    uint64_t blocked_time_ns;
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

/* Configuration-based initialization API */
Bp_EC BpFilter_Init(Bp_Filter_t* filter, const BpFilterConfig* config);

/* Configuration validation function */
Bp_EC BpFilterConfig_Validate(const BpFilterConfig* config);

/* Predefined configurations for common use cases */
extern const BpFilterConfig BP_CONFIG_FLOAT_STANDARD;
extern const BpFilterConfig BP_CONFIG_INT_STANDARD;
extern const BpFilterConfig BP_CONFIG_HIGH_THROUGHPUT;
extern const BpFilterConfig BP_CONFIG_LOW_LATENCY;

Bp_EC BpFilter_Deinit(Bp_Filter_t* filter);

/* Multi-I/O connection functions */
Bp_EC Bp_add_sink(Bp_Filter_t* filter, Bp_Filter_t* sink);
Bp_EC Bp_add_source(Bp_Filter_t* filter, Bp_Filter_t* source);

/* Enhanced connection function with detailed error reporting */
Bp_EC Bp_add_sink_with_error(Bp_Filter_t* source, Bp_Filter_t* sink, BpTypeError* error);
Bp_EC Bp_remove_sink(Bp_Filter_t* filter, const Bp_Filter_t* sink);
Bp_EC Bp_remove_source(Bp_Filter_t* filter, const Bp_Filter_t* source);

/* Legacy buffer initialization (deprecated) */
Bp_EC Bp_BatchBuffer_Init(Bp_BatchBuffer_t* buffer, size_t batch_size,
                          size_t number_of_batches);

/* New buffer-centric API */
Bp_EC BpBatchBuffer_InitFromConfig(Bp_BatchBuffer_t* buffer, const BpBufferConfig_t* config);
Bp_BatchBuffer_t* BpBatchBuffer_Create(const BpBufferConfig_t* config);
void BpBatchBuffer_Destroy(Bp_BatchBuffer_t* buffer);

/* Core buffer operations - buffer-centric API */
Bp_Batch_t BpBatchBuffer_Allocate(Bp_BatchBuffer_t* buf);
Bp_EC BpBatchBuffer_Submit(Bp_BatchBuffer_t* buf, const Bp_Batch_t* batch);
Bp_Batch_t BpBatchBuffer_Head(Bp_BatchBuffer_t* buf);
Bp_EC BpBatchBuffer_DeleteTail(Bp_BatchBuffer_t* buf);

/* Utility operations */
bool BpBatchBuffer_IsEmpty(const Bp_BatchBuffer_t* buf);
bool BpBatchBuffer_IsFull(const Bp_BatchBuffer_t* buf);
size_t BpBatchBuffer_Available(const Bp_BatchBuffer_t* buf);
size_t BpBatchBuffer_Capacity(const Bp_BatchBuffer_t* buf);

/* Control operations */
void BpBatchBuffer_Stop(Bp_BatchBuffer_t* buf);
void BpBatchBuffer_Reset(Bp_BatchBuffer_t* buf);

/* Configuration updates (thread-safe) */
Bp_EC BpBatchBuffer_SetTimeout(Bp_BatchBuffer_t* buf, unsigned long timeout_us);
Bp_EC BpBatchBuffer_SetOverflowBehaviour(Bp_BatchBuffer_t* buf, OverflowBehaviour_t behaviour);

Bp_EC Bp_BatchBuffer_Deinit(Bp_BatchBuffer_t* buffer);

void BpBatchBuffer_stop(Bp_BatchBuffer_t* buffer);

/* Filter lifecycle functions */
Bp_EC Bp_Filter_Start(Bp_Filter_t* filter);
Bp_EC Bp_Filter_Stop(Bp_Filter_t* filter);

static inline void set_filter_error(Bp_Filter_t* filt, Bp_EC code,
                                    const char* msg, const char* file, int line,
                                    const char* func)
{
    filt->worker_err_info.ec = code;
    filt->worker_err_info.line_no = line;
    filt->worker_err_info.filename = file;
    filt->worker_err_info.function = func;
    filt->worker_err_info.err_msg = msg;
    filt->running = false;
}

#define SET_FILTER_ERROR(filt, code, msg) \
    set_filter_error((filt), (code), (msg), __FILE__, __LINE__, __func__)

#define Bp_ASSERT(filt, condition, code, msg)        \
    do {                                             \
        if (!(condition)) {                          \
            SET_FILTER_ERROR((filt), (code), (msg)); \
        }                                            \
    } while (0)

static inline size_t Bp_tail_idx(Bp_Filter_t* dpipe, int buffer_idx)
{
    unsigned long mask =
        (1u << dpipe->input_buffers[buffer_idx].ring_capacity_expo) - 1u;
    return dpipe->input_buffers[buffer_idx].tail & mask;
}

static inline size_t Bp_head_idx(Bp_Filter_t* dpipe, int buffer_idx)
{
    unsigned long mask =
        (1u << dpipe->input_buffers[buffer_idx].ring_capacity_expo) - 1u;
    return dpipe->input_buffers[buffer_idx].head & mask;
}

static inline unsigned long Bp_ring_capacity(Bp_BatchBuffer_t* buf)
{
    return 1u << buf->ring_capacity_expo;
}

static inline unsigned long Bp_batch_capacity(Bp_BatchBuffer_t* buf)
{
    return 1u << buf->batch_capacity_expo;
}

static inline Bp_EC Bp_allocate_buffers(Bp_Filter_t* dpipe, int buffer_idx)
{
    Bp_BatchBuffer_t* buf = &dpipe->input_buffers[buffer_idx];
    
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

static inline Bp_EC Bp_deallocate_buffers(Bp_Filter_t* dpipe, int buffer_idx)
{
    Bp_BatchBuffer_t* buf = &dpipe->input_buffers[buffer_idx];
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

static inline bool Bp_empty(Bp_BatchBuffer_t* buf)
{
    return buf->head == buf->tail;
}

static inline bool Bp_full(Bp_BatchBuffer_t* buf)
{
    return (buf->head - buf->tail) >= Bp_ring_capacity(buf);
}

/* Wait for buffer to have data available
 * @param buf Buffer to wait on
 * @param timeout_us Timeout in microseconds (0 = wait indefinitely)
 * @return Bp_EC_OK if data available, Bp_EC_TIMEOUT on timeout, Bp_EC_STOPPED if buffer stopped
 */
static inline Bp_EC Bp_await_not_empty(Bp_BatchBuffer_t* buf, unsigned long timeout_us)
{
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
            int ret = pthread_cond_timedwait(&buf->not_empty, &buf->mutex, &abs_timeout);
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
 * @return Bp_EC_OK if space available, Bp_EC_TIMEOUT on timeout, Bp_EC_STOPPED if buffer stopped
 */
static inline Bp_EC Bp_await_not_full(Bp_BatchBuffer_t* buf, unsigned long timeout_us)
{
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
            int ret = pthread_cond_timedwait(&buf->not_full, &buf->mutex, &abs_timeout);
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

static inline Bp_Batch_t Bp_allocate(Bp_Filter_t* dpipe, Bp_BatchBuffer_t* buf)
{
    Bp_Batch_t batch = {0};

    // Check overflow behavior before waiting - now using buffer's config
    if (buf->overflow_behaviour == OVERFLOW_DROP && Bp_full(buf)) {
        batch.ec = Bp_EC_NOSPACE;  // Signal that allocation failed due to overflow
        return batch;
    }

    // Use buffer's timeout configuration
    Bp_EC wait_result = Bp_await_not_full(buf, buf->timeout_us);
    if (wait_result == Bp_EC_OK) {
        size_t idx = buf->head & ((1u << buf->ring_capacity_expo) - 1u);
        void* data_ptr = (char*) buf->data_ring +
                         idx * buf->data_width * Bp_batch_capacity(buf);
        batch.capacity = Bp_batch_capacity(buf);
        batch.data = data_ptr;
        batch.batch_id = idx;
        batch.dtype = buf->dtype;
        
        // Update statistics
        buf->total_batches++;
    } else {
        batch.ec = wait_result;  // Could be TIMEOUT, STOPPED, etc
        if (wait_result == Bp_EC_NOSPACE) {
            buf->dropped_batches++;
        }
    }
    return batch;
}

static inline void Bp_submit_batch(Bp_Filter_t* dpipe, Bp_BatchBuffer_t* buf,
                                   const Bp_Batch_t* batch)
{
    size_t idx = buf->head & ((1u << buf->ring_capacity_expo) - 1u);
    buf->batch_ring[idx] = *batch;
    buf->head++;
    pthread_cond_signal(&buf->not_empty);
}

static inline Bp_Batch_t Bp_head(Bp_Filter_t* dpipe, Bp_BatchBuffer_t* buf)
{
    Bp_Batch_t batch = {0};
    // Use buffer's timeout configuration
    Bp_EC wait_result = Bp_await_not_empty(buf, buf->timeout_us);
    if (wait_result == Bp_EC_OK) {
        size_t idx = buf->tail & ((1u << buf->ring_capacity_expo) - 1u);
        batch = buf->batch_ring[idx];
    } else {
        batch.ec = wait_result;  // Could be TIMEOUT, STOPPED, etc
    }
    return batch;
}

static inline void Bp_delete_tail(Bp_Filter_t* dpipe, Bp_BatchBuffer_t* buf)
{
    buf->tail++;
    pthread_cond_signal(&buf->not_full);
}

/* Buffer-centric inline operations */
static inline Bp_Batch_t BpBatchBuffer_Allocate_Inline(Bp_BatchBuffer_t* buf)
{
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
        void* data_ptr = (char*) buf->data_ring +
                         idx * buf->data_width * Bp_batch_capacity(buf);
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

static inline Bp_EC BpBatchBuffer_Submit_Inline(Bp_BatchBuffer_t* buf, const Bp_Batch_t* batch)
{
    size_t idx = buf->head & ((1u << buf->ring_capacity_expo) - 1u);
    buf->batch_ring[idx] = *batch;
    buf->head++;
    pthread_cond_signal(&buf->not_empty);
    return Bp_EC_OK;
}

static inline Bp_Batch_t BpBatchBuffer_Head_Inline(Bp_BatchBuffer_t* buf)
{
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

static inline Bp_EC BpBatchBuffer_DeleteTail_Inline(Bp_BatchBuffer_t* buf)
{
    buf->tail++;
    pthread_cond_signal(&buf->not_full);
    return Bp_EC_OK;
}

static inline bool BpBatchBuffer_IsEmpty_Inline(const Bp_BatchBuffer_t* buf)
{
    return buf->head == buf->tail;
}

static inline bool BpBatchBuffer_IsFull_Inline(const Bp_BatchBuffer_t* buf)
{
    return (buf->head - buf->tail) >= Bp_ring_capacity((Bp_BatchBuffer_t*)buf);
}

static inline size_t BpBatchBuffer_Available_Inline(const Bp_BatchBuffer_t* buf)
{
    return buf->head - buf->tail;
}

static inline size_t BpBatchBuffer_Capacity_Inline(const Bp_BatchBuffer_t* buf)
{
    return Bp_ring_capacity((Bp_BatchBuffer_t*)buf);
}

/* Worker thread entry point */
void* Bp_Worker(void* filter);

#endif /* BPIPE_CORE_H */
