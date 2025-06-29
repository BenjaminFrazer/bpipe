#include "core_python.h"
#include <stddef.h>
#include <string.h>
#include "aggregator.h"

/* =====================================================
 * Parameter Mapping Infrastructure
 * ===================================================== */

/* Parse Python initialization parameters from args/kwargs */
static int parse_python_init_params(PyObject* args, PyObject* kwds, 
                                   BpPythonInitParams* params)
{
    if (!params) {
        return -1;
    }
    
    /* Initialize with defaults */
    *params = (BpPythonInitParams)BP_PYTHON_INIT_PARAMS_DEFAULT;
    
    /* Parse arguments - matching current Bp_init interface */
    static char* kwlist[] = {"capacity_exp", "dtype", NULL};
    if (!PyArg_ParseTupleAndKeywords(
            args, kwds, "i|$i", kwlist,
            &params->capacity_exp, &params->dtype)) {
        return -1;
    }
    
    /* Validate capacity_exp - allow wide range but warn about large values */
    if (params->capacity_exp < 4 || params->capacity_exp > MAX_CAPACITY_EXPO) {
        PyErr_SetString(PyExc_ValueError, "capacity_exp must be between 4 and 30");
        return -1;
    }
    
    /* Validate dtype */
    if (params->dtype <= DTYPE_NDEF || params->dtype >= DTYPE_MAX) {
        PyErr_SetString(PyExc_ValueError, "Invalid dtype value");
        return -1;
    }
    
    return 0; /* Success */
}

/* Convert Python parameters to C filter configuration */
static void python_params_to_config(const BpPythonInitParams* params,
                                   BpFilterConfig* config)
{
    if (!params || !config) {
        return;
    }
    
    /* Start with default configuration */
    *config = (BpFilterConfig)BP_FILTER_CONFIG_DEFAULT;
    
    /* Map Python parameters to C configuration */
    config->dtype = params->dtype;
    config->batch_size = params->batch_size;
    config->buffer_size = params->buffer_size;
    config->number_of_batches_exponent = params->capacity_exp;
    config->number_of_input_filters = 1; /* Single input for basic Python filters */
    
    /* Set transform to NULL initially - will be set by subclass */
    config->transform = NULL;
}

/* Include waveform constants from signal_gen.h without full inclusion */
#define BP_WAVE_SQUARE 0
#define BP_WAVE_SINE 1
#define BP_WAVE_TRIANGLE 2
#define BP_WAVE_SAWTOOTH 3

// Forward declaration
PyObject* Bp_add_sink_py(PyObject* self, PyObject* args);

PyObject* Bp_set_sink(PyObject* self, PyObject* args)
{
    // Backward compatibility wrapper for add_sink
    return Bp_add_sink_py(self, args);
}

PyObject* Bp_start(PyObject* self, PyObject* args)
{
    BpFilterPy_t* obj_py = (BpFilterPy_t*) self;
    Bp_Filter_t* obj = &obj_py->base;

    Bp_EC result = Bp_Filter_Start(obj);
    if (result != Bp_EC_OK) {
        switch (result) {
            case Bp_EC_NULL_FILTER:
                PyErr_SetString(PyExc_ValueError, "Filter object is null");
                break;
            case Bp_EC_ALREADY_RUNNING:
                PyErr_SetString(PyExc_ValueError,
                                obj->worker_err_info.err_msg
                                    ? obj->worker_err_info.err_msg
                                    : "Filter is already running");
                break;
            case Bp_EC_THREAD_CREATE_FAIL:
                PyErr_SetString(PyExc_OSError,
                                obj->worker_err_info.err_msg
                                    ? obj->worker_err_info.err_msg
                                    : "Failed to create worker thread");
                break;
            default:
                PyErr_SetString(PyExc_RuntimeError,
                                "Unknown error starting filter");
                break;
        }
        return NULL;
    }

    Py_RETURN_NONE;
}

PyObject* Bp_stop(PyObject* self, PyObject* args)
{
    BpFilterPy_t* obj_py = (BpFilterPy_t*) self;
    Bp_Filter_t* obj = &obj_py->base;

    Bp_EC result = Bp_Filter_Stop(obj);
    if (result != Bp_EC_OK) {
        switch (result) {
            case Bp_EC_NULL_FILTER:
                PyErr_SetString(PyExc_ValueError, "Filter object is null");
                break;
            case Bp_EC_THREAD_JOIN_FAIL:
                PyErr_SetString(PyExc_OSError,
                                obj->worker_err_info.err_msg
                                    ? obj->worker_err_info.err_msg
                                    : "Failed to join worker thread");
                break;
            default:
                PyErr_SetString(PyExc_RuntimeError,
                                "Unknown error stopping filter");
                break;
        }
        return NULL;
    }

    Py_RETURN_NONE;
}

PyObject* Bp_add_sink_py(PyObject* self, PyObject* args)
{
    PyObject* consumer_obj;
    if (!PyArg_ParseTuple(args, "O", &consumer_obj)) return NULL;
    BpFilterPy_t* consumer_py = (BpFilterPy_t*) consumer_obj;
    BpFilterPy_t* obj_py = (BpFilterPy_t*) self;
    Bp_Filter_t* consumer = &consumer_py->base;
    Bp_Filter_t* obj = &obj_py->base;

    Bp_EC result = Bp_add_sink(obj, consumer);
    if (result != Bp_EC_OK) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to add sink");
        return NULL;
    }

    Py_RETURN_NONE;
}

PyObject* Bp_remove_sink_py(PyObject* self, PyObject* args)
{
    PyObject* consumer_obj;
    if (!PyArg_ParseTuple(args, "O", &consumer_obj)) return NULL;
    BpFilterPy_t* consumer_py = (BpFilterPy_t*) consumer_obj;
    BpFilterPy_t* obj_py = (BpFilterPy_t*) self;
    Bp_Filter_t* consumer = &consumer_py->base;
    Bp_Filter_t* obj = &obj_py->base;

    Bp_EC result = Bp_remove_sink(obj, consumer);
    if (result != Bp_EC_OK) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to remove sink");
        return NULL;
    }

    Py_RETURN_NONE;
}

PyObject* Bp_add_source_py(PyObject* self, PyObject* args)
{
    PyObject* source_obj;
    if (!PyArg_ParseTuple(args, "O", &source_obj)) return NULL;
    BpFilterPy_t* source_py = (BpFilterPy_t*) source_obj;
    BpFilterPy_t* obj_py = (BpFilterPy_t*) self;
    Bp_Filter_t* source = &source_py->base;
    Bp_Filter_t* obj = &obj_py->base;

    Bp_EC result = Bp_add_source(obj, source);
    if (result != Bp_EC_OK) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to add source");
        return NULL;
    }

    Py_RETURN_NONE;
}

PyObject* Bp_remove_source_py(PyObject* self, PyObject* args)
{
    PyObject* source_obj;
    if (!PyArg_ParseTuple(args, "O", &source_obj)) return NULL;
    BpFilterPy_t* source_py = (BpFilterPy_t*) source_obj;
    BpFilterPy_t* obj_py = (BpFilterPy_t*) self;
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
    {"set_sink", Bp_set_sink, METH_VARARGS,
     "Connect sink filter (backward compatibility)"},
    {"add_sink", Bp_add_sink_py, METH_VARARGS, "Add sink filter"},
    {"add_source", Bp_add_source_py, METH_VARARGS, "Add source filter"},
    {"remove_sink", Bp_remove_sink_py, METH_VARARGS, "Remove sink filter"},
    {"remove_source", Bp_remove_source_py, METH_VARARGS,
     "Remove source filter"},
    {"run", Bp_start, METH_VARARGS, "Start worker thread"},
    {"stop", Bp_stop, METH_VARARGS, "Stop worker thread"},
    {NULL}};

static void Bp_dealoc(PyObject* self)
{
    BpFilterPy_t* obj_py = (BpFilterPy_t*) self;
    BpFilter_Deinit(&obj_py->base);
    Py_TYPE(self)->tp_free(self);
}

/* Base class initialization - enhanced version of original Bp_init */
int BpFilterBase_init(PyObject* self, PyObject* args, PyObject* kwds)
{
    BpFilterPy_t* filter = (BpFilterPy_t*) self;
    Bp_Filter_t* dpipe = &filter->base;
    BpPythonInitParams params = BP_PYTHON_INIT_PARAMS_DEFAULT;
    
    /* 1. Parse Python arguments using new infrastructure */
    if (parse_python_init_params(args, kwds, &params) < 0)
        return -1;
    
    /* 2. Initialize filter with original logic but add missing pieces */
    /* Set basic filter properties */
    dpipe->dtype = params.dtype;
    dpipe->data_width = _data_size_lut[dpipe->dtype];
    dpipe->running = false;
    dpipe->n_sources = 0;
    dpipe->n_sinks = 0;
    dpipe->transform = BpPassThroughTransform;
    
    /* Initialize the filter mutex that was missing in original code */
    if (pthread_mutex_init(&dpipe->filter_mutex, NULL) != 0) {
        PyErr_SetString(PyExc_OSError, "Failed to initialize filter mutex");
        return -1;
    }
    
    /* Initialize input buffer[0] using original logic */
    dpipe->input_buffers[0].ring_capacity_expo = params.capacity_exp;
    dpipe->input_buffers[0].batch_capacity_expo = 6; /* Default from original */
    dpipe->input_buffers[0].dtype = dpipe->dtype;
    dpipe->input_buffers[0].data_width = dpipe->data_width;
    
    /* Initialize buffer synchronization */
    if (pthread_mutex_init(&dpipe->input_buffers[0].mutex, NULL) != 0) {
        pthread_mutex_destroy(&dpipe->filter_mutex);
        PyErr_SetString(PyExc_OSError, "Failed to initialize buffer mutex");
        return -1;
    }
    if (pthread_cond_init(&dpipe->input_buffers[0].not_full, NULL) != 0) {
        pthread_mutex_destroy(&dpipe->filter_mutex);
        pthread_mutex_destroy(&dpipe->input_buffers[0].mutex);
        PyErr_SetString(PyExc_OSError, "Failed to initialize condition variable");
        return -1;
    }
    if (pthread_cond_init(&dpipe->input_buffers[0].not_empty, NULL) != 0) {
        pthread_mutex_destroy(&dpipe->filter_mutex);
        pthread_mutex_destroy(&dpipe->input_buffers[0].mutex);
        pthread_cond_destroy(&dpipe->input_buffers[0].not_full);
        PyErr_SetString(PyExc_OSError, "Failed to initialize condition variable");
        return -1;
    }
    
    /* Allocate buffers using original approach */
    Bp_EC result = Bp_allocate_buffers(dpipe, 0);
    if (result != Bp_EC_OK) {
        /* Cleanup on failure */
        pthread_mutex_destroy(&dpipe->filter_mutex);
        pthread_mutex_destroy(&dpipe->input_buffers[0].mutex);
        pthread_cond_destroy(&dpipe->input_buffers[0].not_full);
        pthread_cond_destroy(&dpipe->input_buffers[0].not_empty);
        PyErr_SetString(PyExc_RuntimeError, "Buffer allocation failed");
        return -1;
    }
    
    return 0;
}

/* Legacy wrapper for backward compatibility */
int Bp_init(PyObject* self, PyObject* args, PyObject* kwds)
{
    return BpFilterBase_init(self, args, kwds);
}

int BpFilterPy_init(PyObject* self, PyObject* args, PyObject* kwds)
{
    /* 1. Call base class init (which calls BpFilter_Init) */
    if (BpFilterBase_init(self, args, kwds) < 0)
        return -1;
    
    /* 2. Set Python-specific transform */
    BpFilterPy_t* filter = (BpFilterPy_t*) self;
    filter->base.transform = BpPyTransform;
    
    /* 3. Initialize Python-specific fields */
    /* filter->impl = NULL;  // Will be set by set_impl if needed */
    
    return 0;
}

PyObject* BpFilterPy_transform(PyObject* self, PyObject* args)
{
    long long ts;
    PyArrayObject* input;
    PyArrayObject* output;
    if (!PyArg_ParseTuple(args, "O!O!Li", &PyArray_Type, &output, &PyArray_Type,
                          &input, &ts))
        return NULL;
    memcpy(PyArray_DATA(output), PyArray_DATA(input), PyArray_NBYTES(input));
    Py_RETURN_NONE;
}

void BpPyTransform(Bp_Filter_t* filt, Bp_Batch_t** input_batches, int n_inputs,
                   Bp_Batch_t* const* output_batches, int n_outputs)
{
    PyGILState_STATE gstate = PyGILState_Ensure();

    // Create Python list of input arrays
    PyObject* input_list = PyList_New(n_inputs);
    for (int i = 0; i < n_inputs; i++) {
        if (input_batches[i] && input_batches[i]->data) {
            size_t input_len = input_batches[i]->head - input_batches[i]->tail;
            void* input_start = (char*) input_batches[i]->data +
                                input_batches[i]->tail * filt->data_width;
            npy_intp dims[1] = {input_len};
            PyObject* input_arr =
                PyArray_SimpleNewFromData(1, dims, NPY_FLOAT32, input_start);
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
            size_t output_len =
                output_batches[i]->capacity - output_batches[i]->head;
            void* output_start = (char*) output_batches[i]->data +
                                 output_batches[i]->head * filt->data_width;
            npy_intp dims[1] = {output_len};
            PyObject* output_arr =
                PyArray_SimpleNewFromData(1, dims, NPY_FLOAT32, output_start);
            PyList_SetItem(output_list, i, output_arr);
        } else {
            PyList_SetItem(output_list, i, Py_None);
            Py_INCREF(Py_None);
        }
    }

    // Find the Python object that contains this filter
    // This is a simplified approach - in practice we'd need to store a
    // reference
    BpFilterPy_t* py_filter =
        (BpFilterPy_t*) ((char*) filt - offsetof(BpFilterPy_t, base));
    PyObject* result = PyObject_CallMethod((PyObject*) py_filter, "transform",
                                           "OO", input_list, output_list);
    if (result) Py_DECREF(result);
    Py_DECREF(input_list);
    Py_DECREF(output_list);
    PyGILState_Release(gstate);
}

static PyMethodDef DPFilterPy_methods[] = {
    {"transform", BpFilterPy_transform, METH_VARARGS, "Transform data"},
    {NULL}};

PyTypeObject BpFilterBase = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "dpcore.BpFilterBase",
    .tp_basicsize = sizeof(BpFilterPy_t),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "Data Pipe Filter Base class",
    .tp_methods = BpFilterBase_methods,
    .tp_new = PyType_GenericNew,
    .tp_init = BpFilterBase_init,
    .tp_dealloc = Bp_dealoc,
};

PyTypeObject BpFilterPy = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "dpcore.BpFilterPy",
    .tp_basicsize = sizeof(BpFilterPy_t),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "Python transform filter",
    .tp_methods = DPFilterPy_methods,
    .tp_new = PyType_GenericNew,
    .tp_init = BpFilterPy_init,
    .tp_dealloc = Bp_dealoc,
    .tp_base = &BpFilterBase,
};

static struct PyModuleDef dpcore_module = {PyModuleDef_HEAD_INIT, "dpcore",
                                           NULL, -1, NULL};

PyMODINIT_FUNC PyInit_dpcore(void)
{
    import_array();
    if (PyType_Ready(&BpFilterBase) < 0) return NULL;
    if (PyType_Ready(&BpFilterPy) < 0) return NULL;
    if (PyType_Ready(&BpAggregatorPy) < 0) return NULL;
    PyObject* m = PyModule_Create(&dpcore_module);
    if (!m) return NULL;
    Py_INCREF(&BpFilterBase);
    Py_INCREF(&BpFilterPy);
    Py_INCREF(&BpAggregatorPy);
    PyModule_AddObject(m, "BpFilterBase", (PyObject*) &BpFilterBase);
    PyModule_AddObject(m, "BpFilterPy", (PyObject*) &BpFilterPy);
    PyModule_AddObject(m, "BpAggregatorPy", (PyObject*) &BpAggregatorPy);

    /* Add dtype constants */
    PyModule_AddIntConstant(m, "DTYPE_FLOAT", DTYPE_FLOAT);
    PyModule_AddIntConstant(m, "DTYPE_INT", DTYPE_INT);
    PyModule_AddIntConstant(m, "DTYPE_UNSIGNED", DTYPE_UNSIGNED);

    /* Add waveform constants */
    PyModule_AddIntConstant(m, "BP_WAVE_SQUARE", BP_WAVE_SQUARE);
    PyModule_AddIntConstant(m, "BP_WAVE_SINE", BP_WAVE_SINE);
    PyModule_AddIntConstant(m, "BP_WAVE_TRIANGLE", BP_WAVE_TRIANGLE);
    PyModule_AddIntConstant(m, "BP_WAVE_SAWTOOTH", BP_WAVE_SAWTOOTH);

    return m;
}

/* NumPy helper functions for aggregator */

/* Convert our dtype to NumPy type number */
int buffer_dtype_to_numpy(int dtype)
{
    switch (dtype) {
        case DTYPE_INT:
            return NPY_INT32;
        case DTYPE_UNSIGNED:
            return NPY_UINT32;
        case DTYPE_FLOAT:
            return NPY_FLOAT32;
        default:
            return -1;
    }
}

/* Create NumPy array for aggregator buffer */
PyObject* create_numpy_array_for_buffer(size_t size, int dtype, void* data,
                                        size_t element_size)
{
    int np_dtype = buffer_dtype_to_numpy(dtype);
    if (np_dtype < 0) {
        PyErr_SetString(PyExc_RuntimeError,
                        "Invalid dtype for NumPy conversion");
        return NULL;
    }

    npy_intp dims[1] = {size};
    PyObject* array;

    if (data && size > 0) {
        /* Create array as view of existing data */
        array = PyArray_SimpleNewFromData(1, dims, np_dtype, data);
        if (!array) {
            return NULL;
        }

        /* Make sure the array doesn't try to free our data */
        PyArrayObject* arr = (PyArrayObject*) array;
        PyArray_CLEARFLAGS(arr, NPY_ARRAY_OWNDATA);

    } else {
        /* Create empty array */
        array = PyArray_SimpleNew(1, dims, np_dtype);
        if (!array) {
            return NULL;
        }
    }

    /* Make array read-only */
    PyArrayObject* arr = (PyArrayObject*) array;
    PyArray_CLEARFLAGS(arr, NPY_ARRAY_WRITEABLE);

    return array;
}
