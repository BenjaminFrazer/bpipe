#ifndef BPIPE_AGGREGATOR_H
#define BPIPE_AGGREGATOR_H

#include <Python.h>
#include <stdbool.h>
#include "core.h"

/* Default max capacity: 1GB worth of samples */
#define DEFAULT_MAX_CAPACITY_BYTES (1ULL << 30)

/* Aggregator buffer for each input */
typedef struct {
    void* data;          /* Raw data buffer */
    size_t capacity;     /* Maximum number of elements */
    size_t size;         /* Current number of elements */
    size_t element_size; /* Size of each element */
    SampleDtype_t dtype; /* Data type */
} AggregatorBuffer_t;

/* Python aggregator filter type */
typedef struct {
    PyObject_HEAD Bp_Filter_t base; /* Base filter */
    AggregatorBuffer_t* buffers;    /* Array of buffers, one per input */
    size_t n_buffers;               /* Number of buffers */
    size_t max_capacity_bytes;      /* Max capacity in bytes per buffer */
    PyObject* arrays_cache;         /* Cached Python list of numpy arrays */
    bool arrays_dirty;              /* Whether arrays need to be recreated */
} BpAggregatorPy_t;

/* C-side aggregator transform function */
void BpAggregatorTransform(Bp_Filter_t* filt, Bp_Batch_t** input_batches,
                           int n_inputs, Bp_Batch_t* const* output_batches,
                           int n_outputs);

/* Buffer management functions */
Bp_EC AggregatorBuffer_Init(AggregatorBuffer_t* buffer, size_t element_size,
                            size_t max_capacity, SampleDtype_t dtype);
Bp_EC AggregatorBuffer_Deinit(AggregatorBuffer_t* buffer);
Bp_EC AggregatorBuffer_Append(AggregatorBuffer_t* buffer, const void* data,
                              size_t n_elements);
Bp_EC AggregatorBuffer_Resize(AggregatorBuffer_t* buffer, size_t new_capacity);

/* Python type methods */
int BpAggregatorPy_init(PyObject* self, PyObject* args, PyObject* kwds);
void BpAggregatorPy_dealloc(PyObject* self);
PyObject* BpAggregatorPy_get_arrays(PyObject* self, void* closure);

/* Python type object */
extern PyTypeObject BpAggregatorPy;

#endif /* BPIPE_AGGREGATOR_H */