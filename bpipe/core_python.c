#include "core_python.h"
#include <string.h>

PyObject* Bp_set_sink(PyObject *self, PyObject *args){
    PyObject* consumer_obj;
    if (!PyArg_ParseTuple(args, "O", &consumer_obj))
        return NULL;
    BpFilterPy_t* consumer_py = (BpFilterPy_t*)consumer_obj;
    BpFilterPy_t* obj_py = (BpFilterPy_t*)self;
    Bp_Filter_t* consumer = &consumer_py->base;
    Bp_Filter_t* obj = &obj_py->base;
    if (consumer->dtype == DTYPE_NDEF)
        consumer->dtype = obj->dtype;
    obj->sink = consumer;
    Py_RETURN_NONE;
}

PyObject* Bp_start(PyObject* self, PyObject *args){
    BpFilterPy_t* obj_py = (BpFilterPy_t*)self;
    Bp_Filter_t* obj = &obj_py->base;
    if (obj->running){
        PyErr_SetString(PyExc_ValueError, "Already running.");
        return NULL;
    }
    obj->running = true;
    if (pthread_create(&obj->worker_thread, NULL, &Bp_Worker, (void*)obj) != 0) {
        perror("pthread_create worker");
        return PyErr_SetFromErrno(PyExc_OSError);
    }
    Py_RETURN_NONE;
}

PyObject* Bp_stop(PyObject* self, PyObject *args){
    BpFilterPy_t* obj_py = (BpFilterPy_t*)self;
    Bp_Filter_t* obj = &obj_py->base;
    obj->running = false;
    if(pthread_join(obj->worker_thread, NULL)<0)
        return PyErr_SetFromErrno(PyExc_OSError);
    Py_RETURN_NONE;
}

static PyMethodDef BpFilterBase_methods[] = {
    {"set_sink", Bp_set_sink, METH_VARARGS, "Connect sink filter"},
    {"run",      Bp_start,   METH_VARARGS, "Start worker thread"},
    {"stop",     Bp_stop,    METH_VARARGS, "Stop worker thread"},
    {NULL}
};

static void Bp_dealoc(PyObject *self) {
    BpFilterPy_t* obj_py = (BpFilterPy_t*)self;
    Bp_deallocate_buffers(&obj_py->base);
    Py_TYPE(self)->tp_free(self);
}

int Bp_init(PyObject *self, PyObject *args, PyObject *kwds){
    BpFilterPy_t* filter = (BpFilterPy_t*)self;
    Bp_Filter_t* dpipe = &filter->base;
    static char *kwlist[] = {"capacity_exp", "dtype" , NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "u|$i", kwlist,
                                     &dpipe->buffer.ring_capacity_expo, &dpipe->dtype))
        return -1;
    dpipe->data_width = _data_size_lut[dpipe->dtype];
    pthread_mutex_init(&dpipe->buffer.mutex, NULL);
    pthread_cond_init(&dpipe->buffer.not_full, NULL);
    pthread_cond_init(&dpipe->buffer.not_empty, NULL);
    return Bp_allocate_buffers(dpipe);
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

void BpPyTransform(Bp_Filter_t* filt, Bp_Batch_t *input_batch, Bp_Batch_t *output_batch){
    PyGILState_STATE gstate = PyGILState_Ensure();
    size_t input_len = input_batch->head - input_batch->tail;
    void* input_start = (char*)input_batch->data + input_batch->tail*filt->data_width;
    npy_intp dims[1] = {input_len};
    PyObject* input_arr = PyArray_SimpleNewFromData(1, dims, NPY_FLOAT32, input_start);
    PyObject* result = PyObject_CallMethod((PyObject*)filt, "transform", "O", input_arr);
    if (result) Py_DECREF(result);
    Py_DECREF(input_arr);
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

