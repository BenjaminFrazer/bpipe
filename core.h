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

typedef enum _Bp_EC {
	Bp_EC_OK = 0,
	Bp_EC_TIMEOUT = -1,
	Bp_EC_NOINPUT = -2,
	Bp_EC_NOSPACE = -3,
	EC_TYPE_MISMATCH = -4,
	Bp_EC_BAD_PYOBJECT = -5,
} Bp_EC;

typedef struct _Batch {
	size_t head;
	size_t tail;
	int capacity;
	long long t_ns;
	unsigned period_ns;
	size_t batch_id;
	Bp_EC ec;
	void* meta;
	SampleDtype_t dtype;
	void* data;
} Bp_Batch_t;

/* Forward declarations */
typedef struct _DataPipe Bp_Filter_t;
static PyTypeObject BpFilterBase;
static PyTypeObject BpFilterPy;

typedef void (TransformFcn_t)(Bp_Filter_t* filt, Bp_Batch_t *input_batch, Bp_Batch_t *output_batch);

typedef struct _err_info {
	Bp_EC ec;
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
	Bp_Batch_t* batch_ring;
} Bp_Filter_t;


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

typedef struct {
	Bp_Filter_t base;
}BpFilterPy_t;

static inline size_t Bp_tail_idx(Bp_Filter_t* dpipe){
	return atomic_load(&dpipe->n_out) & dpipe->modulo_mask;
}

static inline size_t Bp_head_idx(Bp_Filter_t* dpipe){
	return atomic_load(&dpipe->n_in) & dpipe->modulo_mask;
}

static inline unsigned long Bp_ring_capacity(Bp_Filter_t* pipe){
	return 1u << pipe->ring_capacity_expo;
}

static inline unsigned long Bp_batch_capacity(Bp_Filter_t* pipe){
	return 1u << pipe->batch_capacity_expo;
}

static inline Bp_EC Bp_allocate_buffers(Bp_Filter_t* dpipe){
	assert(dpipe->dtype != DTYPE_NDEF);

	dpipe->data_ring = malloc(dpipe->data_width * Bp_ring_capacity(dpipe));
	dpipe->batch_ring = malloc(sizeof(Bp_Batch_t) * Bp_ring_capacity(dpipe));

	assert(dpipe->data_ring != NULL);
	assert(dpipe->batch_ring != NULL);
	return Bp_EC_OK;
}


static inline Bp_EC Bp_deallocate_buffers(Bp_Filter_t* dpipe){

	assert(dpipe->dtype != DTYPE_NDEF);

	free(dpipe->data_ring);
	free(dpipe->batch_ring);

	dpipe->data_ring = NULL;
	dpipe->batch_ring = NULL;
	return Bp_EC_OK;
}

//static int UdpCom_init(PyObject *self, PyObject *args, PyObject *kwds) {
static inline int Bp_init(PyObject *self, PyObject *args, PyObject *kwds){
	//size_t data_size, size_t capacity_expo
	Bp_Filter_t* dpipe =  (Bp_Filter_t*)self;

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
	
	return Bp_allocate_buffers(dpipe);

}

/* Applies a transform using a python filter */
TransformFcn_t BpPyTransform;

static inline int BpFilterPy_init(PyObject *self, PyObject *args, PyObject *kwds){
 if (BpFilterBase.tp_init) {
        if (BpFilterBase.tp_init(self, args, kwds) < 0) {
            return -1;  // propagate base init failure
        }
    }
	BpFilterPy_t* filter = (BpFilterPy_t*)self;
	filter->base.transform = BpPyTransform;
	return 0;
}


static inline bool Bp_empty(Bp_Filter_t* dpipe){
	size_t n_in = atomic_load(&dpipe->n_in);
	size_t n_out = atomic_load(&dpipe->n_out);
	return n_in == n_out;
}

static inline bool Bp_full(Bp_Filter_t* dpipe){
	size_t n_in = atomic_load(&dpipe->n_in);
	size_t n_out = atomic_load(&dpipe->n_out);
	return (n_in - n_out) >= Bp_ring_capacity(dpipe);
}

static inline Bp_EC Bp_await_not_empty(Bp_Filter_t* dpipe){
	Bp_EC ec = Bp_EC_OK;
	pthread_mutex_lock(&dpipe->cond_mutex);
	while (Bp_empty(dpipe)) {
		if (pthread_cond_timedwait(&dpipe->cond_not_empty, &dpipe->cond_mutex, &dpipe->timeout) == ETIMEDOUT) {
			ec = Bp_EC_TIMEOUT;
			break;
		}
	}
	pthread_mutex_unlock(&dpipe->cond_mutex);
	return ec;
}

static inline Bp_EC Bp_await_not_full(Bp_Filter_t* dpipe){
	Bp_EC ec = Bp_EC_OK;
	pthread_mutex_lock(&dpipe->cond_mutex);
	while (Bp_full(dpipe)) {
		if (pthread_cond_timedwait(&dpipe->cond_not_full, &dpipe->cond_mutex, &dpipe->timeout) == ETIMEDOUT) {
			ec = Bp_EC_TIMEOUT;
			break;
		}
	}
	pthread_mutex_unlock(&dpipe->cond_mutex);
	return ec;
}

static inline Bp_Batch_t Bp_allocate(Bp_Filter_t* dpipe){
	Bp_Batch_t batch = {0};
	if (Bp_await_not_full(dpipe) == Bp_EC_OK) {
		size_t idx = Bp_head_idx(dpipe);
		void* data_ptr = (char*)dpipe->data_ring + idx * dpipe->data_width * Bp_batch_capacity(dpipe);
		batch.capacity = Bp_batch_capacity(dpipe);
		batch.data = data_ptr;
		batch.batch_id = idx;
		batch.dtype = dpipe->dtype;
	} else {
		batch.ec = Bp_EC_TIMEOUT;
	}
	return batch;
}

static inline void Bp_submit_batch(Bp_Filter_t* dpipe, Bp_Batch_t* batch) {
	size_t idx = Bp_head_idx(dpipe);
	dpipe->batch_ring[idx] = *batch;
	atomic_fetch_add(&dpipe->n_in, 1);
	pthread_cond_signal(&dpipe->cond_not_empty);
}

static inline Bp_Batch_t Bp_head(Bp_Filter_t* dpipe) {
	Bp_Batch_t batch = {0};
	if (Bp_await_not_empty(dpipe) == Bp_EC_OK) {
		size_t idx = Bp_tail_idx(dpipe);
		batch = dpipe->batch_ring[idx];
	} else {
		batch.ec = Bp_EC_TIMEOUT;
	}
	return batch;
}

static inline void Bp_delete_tail(Bp_Filter_t* dpipe) {
	atomic_fetch_add(&dpipe->n_out, 1);
	pthread_cond_signal(&dpipe->cond_not_full);
}

void* Bp_Worker(void* filter) {
	Bp_Filter_t* f = (Bp_Filter_t*)filter;
	Bp_Batch_t input_batch = f->has_input ? Bp_head(f)       : (Bp_Batch_t){ .data = NULL, .capacity = 0, .ec=Bp_EC_NOINPUT};
	Bp_Batch_t output_batch = f->sink ? Bp_allocate(f->sink) : (Bp_Batch_t){ .data = malloc(1024 * f->data_width), .capacity = 1024 };

	while (f->running) {
		f->transform(filter, &input_batch, &output_batch);

		if (f->has_input && (input_batch.head >= input_batch.capacity)) {
			Bp_delete_tail(f);
			input_batch = Bp_head(f);
		}
		assert(output_batch.head <= output_batch.capacity);
		assert(output_batch.tail <= output_batch.capacity);
		assert(output_batch.tail <= output_batch.head);

		if (output_batch.head >= output_batch.capacity) {
			if (f->sink) {
				Bp_submit_batch(f->sink, &output_batch);
				output_batch = Bp_allocate(f->sink);
			} else {
				output_batch.head = 0;
				output_batch.tail = 0;
			}
		}
	}
	return NULL;
}
