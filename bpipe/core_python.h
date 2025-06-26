#ifndef BPIPE_CORE_PYTHON_H
#define BPIPE_CORE_PYTHON_H

#include <Python.h>
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>
#include "core.h"

typedef struct {
    PyObject_HEAD
    Bp_Filter_t base;
} BpFilterPy_t;

int Bp_init(PyObject *self, PyObject *args, PyObject *kwds);
int BpFilterPy_init(PyObject *self, PyObject *args, PyObject *kwds);

PyObject* Bp_set_sink(PyObject *self, PyObject *args);
PyObject* Bp_start(PyObject* self, PyObject *args);
PyObject* Bp_stop(PyObject* self, PyObject *args);

PyObject* BpFilterPy_transform(PyObject *self, PyObject *args);
void BpPyTransform(Bp_Filter_t* filt, Bp_Batch_t **input_batches, int n_inputs, Bp_Batch_t **output_batches, int n_outputs);

extern PyTypeObject BpFilterBase;
extern PyTypeObject BpFilterPy;

PyMODINIT_FUNC PyInit_dpcore(void);

#endif /* BPIPE_CORE_PYTHON_H */
