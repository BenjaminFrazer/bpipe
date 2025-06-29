#ifndef BPIPE_CORE_PYTHON_H
#define BPIPE_CORE_PYTHON_H

#include <Python.h>
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>
#include "core.h"

typedef struct {
    PyObject_HEAD Bp_Filter_t base;
} BpFilterPy_t;

/* Parameter structure for Python initialization */
typedef struct {
    int capacity_exp;        /* Python parameter */
    SampleDtype_t dtype;     /* Python parameter */
    int batch_size;          /* Optional, with default */
    int buffer_size;         /* Calculated from batch_size */
} BpPythonInitParams;

/* Default parameters that match current behavior */
#define BP_PYTHON_INIT_PARAMS_DEFAULT { \
    .capacity_exp = 10,                 \
    .dtype = DTYPE_FLOAT,               \
    .batch_size = 64,                   \
    .buffer_size = 128                  \
}

/* Parameter parsing and mapping functions (implemented in core_python.c) */

int BpFilterBase_init(PyObject *self, PyObject *args, PyObject *kwds);
int Bp_init(PyObject *self, PyObject *args, PyObject *kwds);
int BpFilterPy_init(PyObject *self, PyObject *args, PyObject *kwds);

PyObject *Bp_set_sink(PyObject *self, PyObject *args);
PyObject *Bp_start(PyObject *self, PyObject *args);
PyObject *Bp_stop(PyObject *self, PyObject *args);
PyObject *Bp_remove_sink_py(PyObject *self, PyObject *args);

/* NumPy helper functions for aggregator */
PyObject *create_numpy_array_for_buffer(size_t size, int dtype, void *data,
                                        size_t element_size);
int buffer_dtype_to_numpy(int dtype);

PyObject *BpFilterPy_transform(PyObject *self, PyObject *args);
void BpPyTransform(Bp_Filter_t *filt, Bp_Batch_t **input_batches, int n_inputs,
                   Bp_Batch_t * const* output_batches, int n_outputs);

extern PyTypeObject BpFilterBase;
extern PyTypeObject BpFilterPy;

PyMODINIT_FUNC PyInit_dpcore(void);

#endif /* BPIPE_CORE_PYTHON_H */
