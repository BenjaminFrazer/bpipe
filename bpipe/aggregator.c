#include "core.h"
#include "aggregator.h"
#include <string.h>
#include <stdio.h>

/* Get size in bytes for a given dtype */
static size_t dtype_size(SampleDtype_t dtype) {
    switch (dtype) {
        case DTYPE_INT:      return sizeof(int);
        case DTYPE_UNSIGNED: return sizeof(unsigned int);
        case DTYPE_FLOAT:    return sizeof(float);
        default:       return 0;
    }
}

/* Initialize an aggregator buffer */
Bp_EC AggregatorBuffer_Init(AggregatorBuffer_t* buffer, size_t element_size, 
                           size_t max_capacity, SampleDtype_t dtype) {
    if (!buffer || element_size == 0 || max_capacity == 0) {
        return BP_ERROR_NULL_FILTER;
    }
    
    buffer->element_size = element_size;
    buffer->capacity = max_capacity;
    buffer->size = 0;
    buffer->dtype = dtype;
    
    /* Start with small initial allocation */
    size_t initial_capacity = 1024;
    if (initial_capacity > max_capacity) {
        initial_capacity = max_capacity;
    }
    
    buffer->data = calloc(initial_capacity, element_size);
    if (!buffer->data) {
        return BP_ERROR_NULL_FILTER;
    }
    
    buffer->capacity = initial_capacity;
    return Bp_EC_OK;
}

/* Deinitialize an aggregator buffer */
Bp_EC AggregatorBuffer_Deinit(AggregatorBuffer_t* buffer) {
    if (!buffer) {
        return BP_ERROR_NULL_FILTER;
    }
    
    if (buffer->data) {
        free(buffer->data);
        buffer->data = NULL;
    }
    
    buffer->capacity = 0;
    buffer->size = 0;
    return Bp_EC_OK;
}

/* Resize buffer capacity (up to max_capacity) */
Bp_EC AggregatorBuffer_Resize(AggregatorBuffer_t* buffer, size_t new_capacity) {
    if (!buffer || !buffer->data) {
        return BP_ERROR_NULL_FILTER;
    }
    
    if (new_capacity == buffer->capacity) {
        return Bp_EC_OK;
    }
    
    void* new_data = realloc(buffer->data, new_capacity * buffer->element_size);
    if (!new_data) {
        return BP_ERROR_NULL_FILTER;
    }
    
    buffer->data = new_data;
    buffer->capacity = new_capacity;
    
    /* Truncate size if necessary */
    if (buffer->size > new_capacity) {
        buffer->size = new_capacity;
    }
    
    return Bp_EC_OK;
}

/* Append data to buffer, resizing if necessary */
Bp_EC AggregatorBuffer_Append(AggregatorBuffer_t* buffer, const void* data, 
                             size_t n_elements) {
    if (!buffer || !buffer->data || !data) {
        return BP_ERROR_NULL_FILTER;
    }
    
    size_t required_capacity = buffer->size + n_elements;
    
    /* Check if resize is needed */
    if (required_capacity > buffer->capacity) {
        /* Double capacity, but don't exceed max */
        size_t new_capacity = buffer->capacity * 2;
        while (new_capacity < required_capacity && new_capacity > 0) {
            new_capacity *= 2;
        }
        
        /* Clamp to actual requirement if we overflowed */
        if (new_capacity < required_capacity) {
            new_capacity = required_capacity;
        }
        
        Bp_EC ec = AggregatorBuffer_Resize(buffer, new_capacity);
        if (ec != Bp_EC_OK) {
            return ec;
        }
    }
    
    /* Copy data */
    void* dest = (char*)buffer->data + (buffer->size * buffer->element_size);
    memcpy(dest, data, n_elements * buffer->element_size);
    buffer->size += n_elements;
    
    return Bp_EC_OK;
}

/* Aggregator transform function */
void BpAggregatorTransform(Bp_Filter_t* filt, Bp_Batch_t **input_batches, 
                          int n_inputs, Bp_Batch_t **output_batches, int n_outputs) {
    BpAggregatorPy_t* agg = (BpAggregatorPy_t*)filt;
    
    /* This is a sink - no outputs */
    (void)output_batches;
    (void)n_outputs;
    
    /* Process each input */
    for (int i = 0; i < n_inputs && i < agg->n_buffers; i++) {
        if (!input_batches[i]) continue;
        
        Bp_Batch_t* batch = input_batches[i];
        AggregatorBuffer_t* buffer = &agg->buffers[i];
        
        /* Calculate number of elements */
        size_t batch_size = batch->tail - batch->head;
        size_t n_elements = batch_size / buffer->element_size;
        
        /* Check if we would exceed max capacity */
        size_t max_elements = agg->max_capacity_bytes / buffer->element_size;
        if (buffer->size + n_elements > max_elements) {
            /* Drop samples that would exceed capacity */
            n_elements = max_elements - buffer->size;
            if (n_elements == 0) {
                continue;  /* Buffer is full */
            }
        }
        
        /* Append data to buffer */
        Bp_EC ec = AggregatorBuffer_Append(buffer, batch->data, n_elements);
        if (ec != Bp_EC_OK) {
            set_filter_error(filt, ec, "Failed to append to aggregator buffer", 
                           __FILE__, __LINE__, __func__);
        }
        
        /* Mark arrays as dirty so they'll be recreated on next Python access */
        agg->arrays_dirty = true;
    }
}

/* Python type methods implementation */
int BpAggregatorPy_init(PyObject *self, PyObject *args, PyObject *kwds) {
    BpAggregatorPy_t *agg = (BpAggregatorPy_t *)self;
    
    static char *kwlist[] = {"n_inputs", "dtype", "max_capacity_bytes", NULL};
    int n_inputs = 1;
    int dtype = DTYPE_FLOAT;
    Py_ssize_t max_capacity_bytes = DEFAULT_MAX_CAPACITY_BYTES;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|iin", kwlist, 
                                     &n_inputs, &dtype, &max_capacity_bytes)) {
        return -1;
    }
    
    if (n_inputs <= 0 || n_inputs > MAX_SOURCES) {
        PyErr_Format(PyExc_ValueError, "n_inputs must be between 1 and %d", MAX_SOURCES);
        return -1;
    }
    
    if (dtype < DTYPE_FLOAT || dtype > DTYPE_UNSIGNED) {
        PyErr_SetString(PyExc_ValueError, "Invalid dtype");
        return -1;
    }
    
    if (max_capacity_bytes <= 0) {
        PyErr_SetString(PyExc_ValueError, "max_capacity_bytes must be positive");
        return -1;
    }
    
    /* Initialize base filter as sink-only (no transform output) */
    size_t element_size = dtype_size((SampleDtype_t)dtype);
    if (element_size == 0) {
        PyErr_SetString(PyExc_ValueError, "Invalid dtype or zero element size");
        return -1;
    }
    
    Bp_EC ec = BpFilter_Init(&agg->base, BpAggregatorTransform, 0, 
                            element_size * 1024, 1024, 10, n_inputs);
    if (ec != Bp_EC_OK) {
        PyErr_Format(PyExc_RuntimeError, "Failed to initialize filter: %d", ec);
        return -1;
    }
    
    /* Allocate buffer array */
    agg->buffers = calloc(n_inputs, sizeof(AggregatorBuffer_t));
    if (!agg->buffers) {
        BpFilter_Deinit(&agg->base);
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate buffers");
        return -1;
    }
    
    agg->n_buffers = n_inputs;
    agg->max_capacity_bytes = max_capacity_bytes;
    agg->arrays_cache = NULL;
    agg->arrays_dirty = true;
    
    /* Initialize each buffer */
    size_t max_elements = max_capacity_bytes / element_size;
    for (int i = 0; i < n_inputs; i++) {
        ec = AggregatorBuffer_Init(&agg->buffers[i], element_size, 
                                  max_elements, (SampleDtype_t)dtype);
        if (ec != Bp_EC_OK) {
            /* Clean up previously allocated buffers */
            for (int j = 0; j < i; j++) {
                AggregatorBuffer_Deinit(&agg->buffers[j]);
            }
            free(agg->buffers);
            BpFilter_Deinit(&agg->base);
            PyErr_Format(PyExc_RuntimeError, "Failed to initialize buffer %d", i);
            return -1;
        }
    }
    
    return 0;
}

void BpAggregatorPy_dealloc(PyObject *self) {
    BpAggregatorPy_t *agg = (BpAggregatorPy_t *)self;
    
    /* Stop filter if running */
    if (agg->base.running) {
        Bp_Filter_Stop(&agg->base);
    }
    
    /* Clean up buffers */
    if (agg->buffers) {
        for (size_t i = 0; i < agg->n_buffers; i++) {
            AggregatorBuffer_Deinit(&agg->buffers[i]);
        }
        free(agg->buffers);
    }
    
    /* Clean up cached arrays */
    Py_XDECREF(agg->arrays_cache);
    
    /* Deinit base filter */
    BpFilter_Deinit(&agg->base);
    
    /* Free the object */
    Py_TYPE(self)->tp_free(self);
}