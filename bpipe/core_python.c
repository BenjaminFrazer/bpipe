#include "core_python.h"
#include <string.h>  
#include <stddef.h>

// Forward declaration
PyObject* Bp_add_sink_py(PyObject *self, PyObject *args);

PyObject* Bp_set_sink(PyObject *self, PyObject *args){
    // Backward compatibility wrapper for add_sink
    return Bp_add_sink_py(self, args);
}

PyObject* Bp_start(PyObject* self, PyObject *args){
    BpFilterPy_t* obj_py = (BpFilterPy_t*)self;
    Bp_Filter_t* obj = &obj_py->base;
    
    Bp_EC result = Bp_Filter_Start(obj);
    if (result != Bp_EC_OK) {
        switch (result) {
            case BP_ERROR_NULL_FILTER:
                PyErr_SetString(PyExc_ValueError, "Filter object is null");
                break;
            case BP_ERROR_ALREADY_RUNNING:
                PyErr_SetString(PyExc_ValueError, obj->worker_err_info.err_msg ? 
                               obj->worker_err_info.err_msg : "Filter is already running");
                break;
            case BP_ERROR_THREAD_CREATE_FAIL:
                PyErr_SetString(PyExc_OSError, obj->worker_err_info.err_msg ? 
                               obj->worker_err_info.err_msg : "Failed to create worker thread");
                break;
            default:
                PyErr_SetString(PyExc_RuntimeError, "Unknown error starting filter");
                break;
        }
        return NULL;
    }
    
    Py_RETURN_NONE;
}

PyObject* Bp_stop(PyObject* self, PyObject *args){
    BpFilterPy_t* obj_py = (BpFilterPy_t*)self;
    Bp_Filter_t* obj = &obj_py->base;
    
    Bp_EC result = Bp_Filter_Stop(obj);
    if (result != Bp_EC_OK) {
        switch (result) {
            case BP_ERROR_NULL_FILTER:
                PyErr_SetString(PyExc_ValueError, "Filter object is null");
                break;
            case BP_ERROR_THREAD_JOIN_FAIL:
                PyErr_SetString(PyExc_OSError, obj->worker_err_info.err_msg ? 
                               obj->worker_err_info.err_msg : "Failed to join worker thread");
                break;
            default:
                PyErr_SetString(PyExc_RuntimeError, "Unknown error stopping filter");
                break;
        }
        return NULL;
    }
    
    Py_RETURN_NONE;
}

PyObject* Bp_add_sink_py(PyObject *self, PyObject *args){
    PyObject* consumer_obj;
    if (!PyArg_ParseTuple(args, "O", &consumer_obj))
        return NULL;
    BpFilterPy_t* consumer_py = (BpFilterPy_t*)consumer_obj;
    BpFilterPy_t* obj_py = (BpFilterPy_t*)self;
    Bp_Filter_t* consumer = &consumer_py->base;
    Bp_Filter_t* obj = &obj_py->base;
    
    Bp_EC result = Bp_add_sink(obj, consumer);
    if (result != Bp_EC_OK) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to add sink");
        return NULL;
    }
    
    Py_RETURN_NONE;
}

PyObject* Bp_remove_sink_py(PyObject *self, PyObject *args){
    PyObject* consumer_obj;
    if (!PyArg_ParseTuple(args, "O", &consumer_obj))
        return NULL;
    BpFilterPy_t* consumer_py = (BpFilterPy_t*)consumer_obj;
    BpFilterPy_t* obj_py = (BpFilterPy_t*)self;
    Bp_Filter_t* consumer = &consumer_py->base;
    Bp_Filter_t* obj = &obj_py->base;
    
    Bp_EC result = Bp_remove_sink(obj, consumer);
    if (result != Bp_EC_OK) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to remove sink");
        return NULL;
    }
    
    Py_RETURN_NONE;
}

PyObject* Bp_add_source_py(PyObject *self, PyObject *args){
    PyObject* source_obj;
    if (!PyArg_ParseTuple(args, "O", &source_obj))
        return NULL;
    BpFilterPy_t* source_py = (BpFilterPy_t*)source_obj;
    BpFilterPy_t* obj_py = (BpFilterPy_t*)self;
    Bp_Filter_t* source = &source_py->base;
    Bp_Filter_t* obj = &obj_py->base;
    
    Bp_EC result = Bp_add_source(obj, source);
    if (result != Bp_EC_OK) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to add source");
        return NULL;
    }
    
    Py_RETURN_NONE;
}

PyObject* Bp_remove_source_py(PyObject *self, PyObject *args){
    PyObject* source_obj;
    if (!PyArg_ParseTuple(args, "O", &source_obj))
        return NULL;
    BpFilterPy_t* source_py = (BpFilterPy_t*)source_obj;
    BpFilterPy_t* obj_py = (BpFilterPy_t*)self;
    Bp_Filter_t* source = &source_py->base;
    Bp_Filter_t* obj = &obj_py->base;
    
    Bp_EC result = Bp_remove_source(obj, source);
    if (result != Bp_EC_OK) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to remove source");
        return NULL;
    }
    
    Py_RETURN_NONE;
}

static PyMethodDef BpFilterBase_methods[] = {
    {"set_sink", Bp_set_sink, METH_VARARGS, "Connect sink filter (backward compatibility)"},
    {"add_sink", Bp_add_sink_py, METH_VARARGS, "Add sink filter"},
    {"add_source", Bp_add_source_py, METH_VARARGS, "Add source filter"},
    {"remove_sink", Bp_remove_sink_py, METH_VARARGS, "Remove sink filter"},
    {"remove_source", Bp_remove_source_py, METH_VARARGS, "Remove source filter"},
    {"run",      Bp_start,   METH_VARARGS, "Start worker thread"},
    {"stop",     Bp_stop,    METH_VARARGS, "Stop worker thread"},
    {NULL}
};

static void Bp_dealoc(PyObject *self) {
    BpFilterPy_t* obj_py = (BpFilterPy_t*)self;
    BpFilter_Deinit(&obj_py->base);
    Py_TYPE(self)->tp_free(self);
}

int Bp_init(PyObject *self, PyObject *args, PyObject *kwds){
    BpFilterPy_t* filter = (BpFilterPy_t*)self;
    Bp_Filter_t* dpipe = &filter->base;
    static char *kwlist[] = {"capacity_exp", "dtype" , NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|$i", kwlist,
                                     (int*)&dpipe->input_buffers[0].ring_capacity_expo, &dpipe->dtype))
        return -1;
    dpipe->data_width = _data_size_lut[dpipe->dtype];
    pthread_mutex_init(&dpipe->input_buffers[0].mutex, NULL);
    pthread_cond_init(&dpipe->input_buffers[0].not_full, NULL);
    pthread_cond_init(&dpipe->input_buffers[0].not_empty, NULL);
    Bp_EC result = Bp_allocate_buffers(dpipe, 0);
    return (result == Bp_EC_OK) ? 0 : -1;
}

int BpFilterPy_init(PyObject *self, PyObject *args, PyObject *kwds){
    if (BpFilterBase.tp_init) {
        if (BpFilterBase.tp_init(self, args, kwds) < 0)
            return -1;
    }
    BpFilterPy_t* filter = (BpFilterPy_t*)self;
    filter->base.transform = BpPyTransform;
    return 0;
}

PyObject* BpFilterPy_transform(PyObject *self, PyObject *args){
    long long ts;
    PyArrayObject *input;
    PyArrayObject *output;
    if (!PyArg_ParseTuple(args, "O!O!Li", &PyArray_Type, &output, &PyArray_Type, &input, &ts))
        return NULL;
    memcpy(PyArray_DATA(output), PyArray_DATA(input), PyArray_NBYTES(input));
    Py_RETURN_NONE;
}

void BpPyTransform(Bp_Filter_t* filt, Bp_Batch_t **input_batches, int n_inputs, Bp_Batch_t **output_batches, int n_outputs){
    PyGILState_STATE gstate = PyGILState_Ensure();
    
    // Create Python list of input arrays
    PyObject* input_list = PyList_New(n_inputs);
    for (int i = 0; i < n_inputs; i++) {
        if (input_batches[i] && input_batches[i]->data) {
            size_t input_len = input_batches[i]->head - input_batches[i]->tail;
            void* input_start = (char*)input_batches[i]->data + input_batches[i]->tail * filt->data_width;
            npy_intp dims[1] = {input_len};
            PyObject* input_arr = PyArray_SimpleNewFromData(1, dims, NPY_FLOAT32, input_start);
            PyList_SetItem(input_list, i, input_arr);
        } else {
            PyList_SetItem(input_list, i, Py_None);
            Py_INCREF(Py_None);
        }
    }
    
    // Create Python list of output arrays
    PyObject* output_list = PyList_New(n_outputs);
    for (int i = 0; i < n_outputs; i++) {
        if (output_batches[i] && output_batches[i]->data) {
            size_t output_len = output_batches[i]->capacity - output_batches[i]->head;
            void* output_start = (char*)output_batches[i]->data + output_batches[i]->head * filt->data_width;
            npy_intp dims[1] = {output_len};
            PyObject* output_arr = PyArray_SimpleNewFromData(1, dims, NPY_FLOAT32, output_start);
            PyList_SetItem(output_list, i, output_arr);
        } else {
            PyList_SetItem(output_list, i, Py_None);
            Py_INCREF(Py_None);
        }
    }
    
    // Find the Python object that contains this filter
    // This is a simplified approach - in practice we'd need to store a reference
    BpFilterPy_t* py_filter = (BpFilterPy_t*)((char*)filt - offsetof(BpFilterPy_t, base));
    PyObject* result = PyObject_CallMethod((PyObject*)py_filter, "transform", "OO", input_list, output_list);
    if (result) Py_DECREF(result);
    Py_DECREF(input_list);
    Py_DECREF(output_list);
    PyGILState_Release(gstate);
}

static PyMethodDef DPFilterPy_methods[] = {
    {"transform", BpFilterPy_transform, METH_VARARGS, "Transform data"},
    {NULL}
};

PyTypeObject BpFilterBase = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "dpcore.BpFilterBase",
    .tp_basicsize = sizeof(BpFilterPy_t),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "Data Pipe Filter Base class",
    .tp_methods = BpFilterBase_methods,
    .tp_new = PyType_GenericNew,
    .tp_init = Bp_init,
    .tp_dealloc = Bp_dealoc,
};

PyTypeObject BpFilterPy = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "dpcore.BpFilterPy",
    .tp_basicsize = sizeof(BpFilterPy_t),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "Python transform filter",
    .tp_methods = DPFilterPy_methods,
    .tp_new = PyType_GenericNew,
    .tp_init = BpFilterPy_init,
    .tp_dealloc = Bp_dealoc,
    .tp_base = &BpFilterBase,
};

static struct PyModuleDef dpcore_module = {
    PyModuleDef_HEAD_INIT,
    "dpcore",
    NULL,
    -1,
    NULL
};

PyMODINIT_FUNC PyInit_dpcore(void) {
    import_array();
    if (PyType_Ready(&BpFilterBase) < 0)
        return NULL;
    if (PyType_Ready(&BpFilterPy) < 0)
        return NULL;
    PyObject *m = PyModule_Create(&dpcore_module);
    if (!m) return NULL;
    Py_INCREF(&BpFilterBase);
    Py_INCREF(&BpFilterPy);
    PyModule_AddObject(m, "BpFilterBase", (PyObject *)&BpFilterBase);
    PyModule_AddObject(m, "BpFilterPy", (PyObject *)&BpFilterPy);
    return m;
}

