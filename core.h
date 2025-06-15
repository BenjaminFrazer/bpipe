#include <Python.h>
#include <numpy/arrayobject.h>
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
#include <stdatomic.h>
#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <assert.h>

#define MAX_SINKS 10
#define MAX_CAPACITY_EXPO 30 // max 1GB capacity

typedef enum _SampleType {
	DTYPE_NDEF=0,
	DTYPE_FLOAT,
	DTYPE_INT,
	DTYPE_UNSIGNED,
	DTYPE_MAX,
} SampleDtype_t;

size_t _data_size_lut [] = {
	[DTYPE_FLOAT]  	= sizeof(float),
	[DTYPE_INT]    	= sizeof(int),
	[DTYPE_UNSIGNED] = sizeof(unsigned),
};

typedef enum _DP_EC {
	DP_EC_OK = 0,
	DP_EC_TIMEOUT = -1,
	DP_EC_NOINPUT = -2,
	DP_EC_NOSPACE = -3,
	EC_TYPE_MISMATCH = -4,
	DP_EC_BAD_PYOBJECT = -5,
} DP_EC;

typedef struct _Batch {
	size_t head;
	size_t tail;
	int capacity;
	long long t_ns;
	unsigned period_ns;
	size_t batch_id;
	DP_EC ec;
	void* meta;
	SampleDtype_t dtype;
	void* data;
} DP_Batch_t;

/* Forward declarations */
typedef struct _DataPipe DP_Filter_t;
static PyTypeObject DpFilterBase;
static PyTypeObject DpFilterPy;

typedef void (TransformFcn_t)(DP_Filter_t* filt, DP_Batch_t *input_batch, DP_Batch_t *output_batch);

typedef struct _err_info {
	DP_EC ec;
	int line_no;
	const char* filename;
	const char* function;
	const char* err_msg;
} Err_info;


typedef struct _DataPipe {
	PyObject_HEAD
	bool running;
	TransformFcn_t* transform;
	Err_info worker_err_info;
	struct timespec timeout;
	struct _DataPipe* source;
	struct _DataPipe* sink;
	atomic_uintmax_t n_in;
	atomic_uintmax_t n_out;
	pthread_mutex_t cond_mutex;
	size_t ring_capacity_expo;
	size_t batch_capacity_expo;
	unsigned long modulo_mask;
	size_t data_width;
	bool has_input; // does this filter operate on an input buffer?
	SampleDtype_t dtype;
	pthread_cond_t cond_not_full;
	pthread_cond_t cond_not_empty;
	pthread_t worker_thread;
	void* data_ring;
	DP_Batch_t* batch_ring;
} DP_Filter_t;


static inline void set_filter_error(DP_Filter_t* filt, DP_EC code, const char* msg, const char* file, int line, const char* func) {
	filt->worker_err_info.ec = code;
	filt->worker_err_info.line_no = line;
	filt->worker_err_info.filename = file;
	filt->worker_err_info.function = func;
	filt->worker_err_info.err_msg = msg;
	filt->running = false;
}

#define SET_FILTER_ERROR(filt, code, msg) \
    set_filter_error((filt), (code), (msg), __FILE__, __LINE__, __func__)


#define DP_ASSERT(filt, condition, code, msg)      \
    do {                                                           \
        if (!(condition)) {                                        \
            SET_FILTER_ERROR((filt), (code), (msg));               \
        }                                                          \
    } while (0)

typedef struct {
	DP_Filter_t base;
}DpFilterPy_t;

static inline size_t DP_tail_idx(DP_Filter_t* dpipe){
	return atomic_load(&dpipe->n_out) & dpipe->modulo_mask;
}

static inline size_t DP_head_idx(DP_Filter_t* dpipe){
	return atomic_load(&dpipe->n_in) & dpipe->modulo_mask;
}

static inline unsigned long DP_ring_capacity(DP_Filter_t* pipe){
	return 1u << pipe->ring_capacity_expo;
}

static inline unsigned long DP_batch_capacity(DP_Filter_t* pipe){
	return 1u << pipe->batch_capacity_expo;
}

static inline DP_EC DP_allocate_buffers(DP_Filter_t* dpipe){
	assert(dpipe->dtype != DTYPE_NDEF);

	dpipe->data_ring = malloc(dpipe->data_width * DP_ring_capacity(dpipe));
	dpipe->batch_ring = malloc(sizeof(DP_Batch_t) * DP_ring_capacity(dpipe));

	assert(dpipe->data_ring != NULL);
	assert(dpipe->batch_ring != NULL);
	return DP_EC_OK;
}


static inline DP_EC DP_deallocate_buffers(DP_Filter_t* dpipe){

	assert(dpipe->dtype != DTYPE_NDEF);

	free(dpipe->data_ring);
	free(dpipe->batch_ring);

	dpipe->data_ring = NULL;
	dpipe->batch_ring = NULL;
	return DP_EC_OK;
}

//static int UdpCom_init(PyObject *self, PyObject *args, PyObject *kwds) {
static inline int DP_init(PyObject *self, PyObject *args, PyObject *kwds){
	//size_t data_size, size_t capacity_expo
	DP_Filter_t* dpipe =  (DP_Filter_t*)self;

	static char *kwlist[] = {"capacity_exp", "dtype" , NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "u|$i", kwlist,
																	&dpipe->ring_capacity_expo, &dpipe->dtype)) {
		return -1;  // Signal failure
	}

	assert(dpipe->ring_capacity_expo < MAX_CAPACITY_EXPO);
	assert(dpipe->dtype < DTYPE_MAX);


	assert(dpipe->ring_capacity_expo > 0 && dpipe->ring_capacity_expo < MAX_CAPACITY_EXPO);

	dpipe->n_in = 0;
	dpipe->n_out = 0;
	dpipe->modulo_mask = (1u << dpipe->ring_capacity_expo) - 1;
	dpipe->data_width = _data_size_lut[dpipe->dtype];

	pthread_mutex_init(&dpipe->cond_mutex, NULL);
	pthread_cond_init(&dpipe->cond_not_full, NULL);
	pthread_cond_init(&dpipe->cond_not_empty, NULL);
	
	return DP_allocate_buffers(dpipe);

}

/* Applies a transform using a python filter */
TransformFcn_t DpPyTransform;

static inline int DpFilterPy_init(PyObject *self, PyObject *args, PyObject *kwds){
 if (DpFilterBase.tp_init) {
        if (DpFilterBase.tp_init(self, args, kwds) < 0) {
            return -1;  // propagate base init failure
        }
    }
	DpFilterPy_t* filter = (DpFilterPy_t*)self;
	filter->base.transform = DpPyTransform;
	return 0;
}


static inline bool DP_empty(DP_Filter_t* dpipe){
	size_t n_in = atomic_load(&dpipe->n_in);
	size_t n_out = atomic_load(&dpipe->n_out);
	return n_in == n_out;
}

static inline bool DP_full(DP_Filter_t* dpipe){
	size_t n_in = atomic_load(&dpipe->n_in);
	size_t n_out = atomic_load(&dpipe->n_out);
	return (n_in - n_out) >= DP_ring_capacity(dpipe);
}

static inline DP_EC DP_await_not_empty(DP_Filter_t* dpipe){
	DP_EC ec = DP_EC_OK;
	pthread_mutex_lock(&dpipe->cond_mutex);
	while (DP_empty(dpipe)) {
		if (pthread_cond_timedwait(&dpipe->cond_not_empty, &dpipe->cond_mutex, &dpipe->timeout) == ETIMEDOUT) {
			ec = DP_EC_TIMEOUT;
			break;
		}
	}
	pthread_mutex_unlock(&dpipe->cond_mutex);
	return ec;
}

static inline DP_EC DP_await_not_full(DP_Filter_t* dpipe){
	DP_EC ec = DP_EC_OK;
	pthread_mutex_lock(&dpipe->cond_mutex);
	while (DP_full(dpipe)) {
		if (pthread_cond_timedwait(&dpipe->cond_not_full, &dpipe->cond_mutex, &dpipe->timeout) == ETIMEDOUT) {
			ec = DP_EC_TIMEOUT;
			break;
		}
	}
	pthread_mutex_unlock(&dpipe->cond_mutex);
	return ec;
}

static inline DP_Batch_t DP_allocate(DP_Filter_t* dpipe){
	DP_Batch_t batch = {0};
	if (DP_await_not_full(dpipe) == DP_EC_OK) {
		size_t idx = DP_head_idx(dpipe);
		void* data_ptr = (char*)dpipe->data_ring + idx * dpipe->data_width * DP_batch_capacity(dpipe);
		batch.capacity = DP_batch_capacity(dpipe);
		batch.data = data_ptr;
		batch.batch_id = idx;
		batch.dtype = dpipe->dtype;
	} else {
		batch.ec = DP_EC_TIMEOUT;
	}
	return batch;
}

static inline void DP_submit_batch(DP_Filter_t* dpipe, DP_Batch_t* batch) {
	size_t idx = DP_head_idx(dpipe);
	dpipe->batch_ring[idx] = *batch;
	atomic_fetch_add(&dpipe->n_in, 1);
	pthread_cond_signal(&dpipe->cond_not_empty);
}

static inline DP_Batch_t DP_head(DP_Filter_t* dpipe) {
	DP_Batch_t batch = {0};
	if (DP_await_not_empty(dpipe) == DP_EC_OK) {
		size_t idx = DP_tail_idx(dpipe);
		batch = dpipe->batch_ring[idx];
	} else {
		batch.ec = DP_EC_TIMEOUT;
	}
	return batch;
}

static inline void DP_delete_tail(DP_Filter_t* dpipe) {
	atomic_fetch_add(&dpipe->n_out, 1);
	pthread_cond_signal(&dpipe->cond_not_full);
}

void* DataPipe_Worker(void* filter) {
	DP_Filter_t* f = (DP_Filter_t*)filter;
	DP_Batch_t input_batch = f->has_input ? DP_head(f)       : (DP_Batch_t){ .data = NULL, .capacity = 0, .ec=DP_EC_NOINPUT};
	DP_Batch_t output_batch = f->sink ? DP_allocate(f->sink) : (DP_Batch_t){ .data = malloc(1024 * f->data_width), .capacity = 1024 };

	while (f->running) {
		f->transform(filter, &input_batch, &output_batch);

		if (f->has_input && (input_batch.head >= input_batch.capacity)) {
			DP_delete_tail(f);
			input_batch = DP_head(f);
		}
		assert(output_batch.head <= output_batch.capacity);
		assert(output_batch.tail <= output_batch.capacity);
		assert(output_batch.tail <= output_batch.head);

		if (output_batch.head >= output_batch.capacity) {
			if (f->sink) {
				DP_submit_batch(f->sink, &output_batch);
				output_batch = DP_allocate(f->sink);
			} else {
				output_batch.head = 0;
				output_batch.tail = 0;
			}
		}
	}
	return;
}
