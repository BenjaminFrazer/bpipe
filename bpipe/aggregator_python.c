#include "aggregator.h"
#include "core_python.h"

/* Get arrays property - creates NumPy arrays on demand */
PyObject *BpAggregatorPy_get_arrays(PyObject *self, void *closure)
{
    if (!self) {
        PyErr_SetString(PyExc_RuntimeError, "self is NULL");
        return NULL;
    }

    BpAggregatorPy_t *agg = (BpAggregatorPy_t *) self;

    /* Return cached arrays if still valid */
    if (!agg->arrays_dirty && agg->arrays_cache) {
        Py_INCREF(agg->arrays_cache);
        return agg->arrays_cache;
    }

    /* Create new list */
    PyObject *arrays = PyList_New(agg->n_buffers);
    if (!arrays) {
        return NULL;
    }

    /* Create NumPy array for each buffer using helper functions */
    for (size_t i = 0; i < agg->n_buffers; i++) {
        AggregatorBuffer_t *buffer = &agg->buffers[i];

        /* Use NumPy helper function from core_python.c where import_array() was
         * called */
        PyObject *array = create_numpy_array_for_buffer(
            buffer->size, buffer->dtype, buffer->data, buffer->element_size);

        if (!array) {
            Py_DECREF(arrays);
            return NULL;
        }

        /* Add to list */
        PyList_SET_ITEM(arrays, i, array); /* Steals reference */
    }

    /* Update cache */
    Py_XDECREF(agg->arrays_cache);
    agg->arrays_cache = arrays;
    agg->arrays_dirty = false;
    Py_INCREF(arrays);

    return arrays;
}

/* Clear arrays - reset all buffers to empty */
static PyObject *BpAggregatorPy_clear(PyObject *self, PyObject *args)
{
    BpAggregatorPy_t *agg = (BpAggregatorPy_t *) self;

    /* Reset all buffer sizes to 0 */
    for (size_t i = 0; i < agg->n_buffers; i++) {
        agg->buffers[i].size = 0;
    }

    /* Mark arrays as dirty */
    agg->arrays_dirty = true;

    Py_RETURN_NONE;
}

/* Get current sizes of all buffers */
static PyObject *BpAggregatorPy_get_sizes(PyObject *self, PyObject *args)
{
    BpAggregatorPy_t *agg = (BpAggregatorPy_t *) self;

    PyObject *sizes = PyList_New(agg->n_buffers);
    if (!sizes) {
        return NULL;
    }

    for (size_t i = 0; i < agg->n_buffers; i++) {
        PyObject *size = PyLong_FromSize_t(agg->buffers[i].size);
        if (!size) {
            Py_DECREF(sizes);
            return NULL;
        }
        PyList_SET_ITEM(sizes, i, size);
    }

    return sizes;
}

/* Python methods for aggregator */
static PyMethodDef BpAggregatorPy_methods[] = {
    {"add_sink", Bp_set_sink, METH_VARARGS,
     "Add a sink filter (not supported for aggregator)"},
    {"remove_sink", Bp_remove_sink_py, METH_VARARGS,
     "Remove a sink filter (not supported for aggregator)"},
    {"run", Bp_start, METH_VARARGS, "Start the filter"},
    {"stop", Bp_stop, METH_VARARGS, "Stop the filter"},
    {"clear", BpAggregatorPy_clear, METH_NOARGS, "Clear all aggregated data"},
    {"get_sizes", BpAggregatorPy_get_sizes, METH_NOARGS,
     "Get current size of each buffer"},
    {NULL} /* Sentinel */
};

/* Python getters/setters */
static PyGetSetDef BpAggregatorPy_getsetters[] = {
    {"arrays", BpAggregatorPy_get_arrays, NULL,
     "List of NumPy arrays containing aggregated data", NULL},
    {NULL} /* Sentinel */
};

/* Python type object for BpAggregatorPy */
PyTypeObject BpAggregatorPy = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "dpcore.BpAggregatorPy",
    .tp_doc = "Buffer aggregator filter for collecting streaming data",
    .tp_basicsize = sizeof(BpAggregatorPy_t),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_init = BpAggregatorPy_init,
    .tp_dealloc = BpAggregatorPy_dealloc,
    .tp_methods = BpAggregatorPy_methods,
    .tp_getset = BpAggregatorPy_getsetters,
};