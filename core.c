#include "dpcore.h"
#include "object.h"
#include <stdio.h>

PyObject* DP_set_sink(PyObject *self, PyObject *args){
	DP_Filter_t* consumer;
	DP_Filter_t* obj = (DP_Filter_t*)self;
	if (!PyArg_ParseTuple(args, "O!", &consumer))
		return NULL;
	assert(consumer);
	assert(obj->dtype != DTYPE_NDEF);
	if (consumer->dtype == DTYPE_NDEF){
		consumer->dtype = obj->dtype;
	}
	obj->sink = consumer;
	Py_RETURN_NONE;
}

PyObject* DP_start(PyObject* self, PyObject *args){
	DP_Filter_t* obj = (DP_Filter_t*)self;

	if (obj->running){
    PyErr_SetString(PyExc_ValueError, "Already running.");
		return NULL;
	}

	obj->running = true;
	pthread_t * thread;

	// Create the thread
	if (pthread_create(thread, NULL, &DataPipe_Worker, (void*)obj) != 0) {
		perror("pthread_create worker");
		return PyErr_SetFromErrno(PyExc_OSError);
	}
	Py_RETURN_NONE;
}

PyObject* DP_stop(PyObject* self, PyObject *args){
	DP_Filter_t* obj = (DP_Filter_t*)self;
	obj->running = false;
	if(pthread_join(obj->worker_thread, NULL)<0)
		return PyErr_SetFromErrno(PyExc_OSError);
	Py_RETURN_NONE;
}

static PyMethodDef DPFilterBase_methods[] = {
		{"set_sink", 		DP_set_sink, 		METH_VARARGS, "Send data over UDP"},
		{"run", 		DP_start, 		METH_VARARGS, "Start worker thread"},
		{"stop", 		DP_stop, 		METH_VARARGS, "Start worker thread"},
    //{"purge", UdpCom_purge, METH_NOARGS, "Clear buffers."},
    {NULL}  // Sentinel
};

static void DP_dealoc(PyObject *self) {
	Py_TYPE(self)->tp_free(self);
}

static PyTypeObject DpFilterBase = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "dpcore.DpFilterBase",
    .tp_basicsize = sizeof(DP_Filter_t),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "Data Pipe Filter Base class.",
    .tp_methods = DPFilterBase_methods,
    .tp_new = PyType_GenericNew,
		.tp_init = DP_init,
		.tp_dealloc = DP_dealoc,
    //.tp_hash = UdpCom_hash,  // <-- add this line
		//.tp_repr = UdpCom_repr,
};

PyObject* c_array_to_numpy(float* c_buffer, int size) {
    npy_intp dims[1] = { size };

    // Create NumPy array wrapping C buffer
    PyObject* arr = PyArray_SimpleNewFromData(1, dims, NPY_FLOAT32, (void*)c_buffer);

    // Optional: set writeable flag, or track ownership yourself
    if (!arr) return NULL;

    // Optionally prevent Python from freeing your buffer
    PyArray_CLEARFLAGS((PyArrayObject*)arr, NPY_ARRAY_OWNDATA);
		//PyArray_CLEARFLAGS((PyArrayObject*)arr, NPY_ARRAY_WRITEABLE);

    return arr;  // Returns a new reference
}

/* Dummy filter which does nothing other than copy input to output */
PyObject* DpFilterPy_transform(PyObject *self, PyObject *args){
	//DP_Filter_t* obj = (DP_Filter_t*)self;
	long long ts;
	PyArrayObject *input;
	PyArrayObject *output;
	size_t len_in;
	size_t len_out;
	int period;
	if (!PyArg_ParseTuple(args, "O!O!Li", &output, &input, &ts, &period))
		return NULL;

	import_array();

	len_in = PyArray_SIZE(input);
	len_out = PyArray_SIZE(output);
	size_t filter_range = len_in > len_out ? len_in : len_out;

	memcpy(PyArray_DATA(output), PyArray_DATA(input), filter_range*PyArray_ITEMSIZE(input));

	Py_RETURN_NONE; // None in this API means no change to timestamp or sample rate.
}

void DpPyTransform(DP_Filter_t* filt, DP_Batch_t *input_batch, DP_Batch_t *output_batch){
	PyGILState_STATE gstate = PyGILState_Ensure();

	/* Create input numpy array */
	size_t input_len = input_batch->head-input_batch->tail;
	size_t oputput_len = input_batch->capacity-input_batch->head;
	void* input_start = input_batch->data + input_batch->tail*filt->data_width;
	void* oputput_start = input_batch->data + input_batch->tail*filt->data_width;
	npy_intp dims_in[1] = {input_len};
	PyObject* input_arr = PyArray_SimpleNewFromData(1, dims_in, NPY_FLOAT32, input_start);
	DP_ASSERT(filt, input_arr, DP_EC_BAD_PYOBJECT, "Failed to create input numpy array");

	PyObject* output_arr= PyArray_SimpleNewFromData(1, dims_in, NPY_FLOAT32, input_start);
	DP_ASSERT(filt, input_arr, DP_EC_BAD_PYOBJECT, "Failed to create oputput numpy array");

	PyObject* py_bytearray = PyByteArray_FromStringAndSize((const char*)input_batch, sizeof(DP_Batch_t));
	DP_ASSERT(filt, py_bytearray, DP_EC_BAD_PYOBJECT, "Failed to create bytes array");

	/* Call python filter method */
	PyObject* result = PyObject_CallMethod((PyObject*)filt, "transform", "OOLi", input_arr, output_arr, input_batch->t_ns, input_batch->period_ns);


	/* Error handling */
	if (!result) {
			if (PyErr_Occurred()) {
					// Fetch the exception info
					PyObject *ptype, *pvalue, *ptraceback;
					PyErr_Fetch(&ptype, &pvalue, &ptraceback);

					// Optionally: format the exception into a string
					PyObject* str_exc = PyObject_Str(pvalue);
					const char* msg = str_exc ? PyUnicode_AsUTF8(str_exc) : "Unknown error";

					// Record the error into your filter
					SET_FILTER_ERROR(filt, EC_TYPE_MISMATCH, msg);  // Or other error code

					Py_XDECREF(str_exc);
					Py_XDECREF(ptype);
					Py_XDECREF(pvalue);
					Py_XDECREF(ptraceback);
			}
	}

	// If python funciton returns a typle this means it has modified the sample rate or time-stamp
	if (result!=Py_None){
		long long ts;
		int period;
		DP_ASSERT(filt, PyTuple_Check(result), EC_TYPE_MISMATCH, "Expected a tuple return value");
		DP_ASSERT(filt, PyArg_ParseTuple(result, "Li", &ts, &period), EC_TYPE_MISMATCH, "Expected a tuple return value");
		output_batch->t_ns = (long long)ts;
		output_batch->period_ns = (unsigned)period;
	}
	// Use f, i, s here
	Py_DECREF(result);

	PyGILState_Release(gstate);

	return;
}

static PyMethodDef DPFilterPy_methods[] = {
		{"transform", 		DpFilterPy_transform, 		METH_VARARGS, "Transform input data to output data."},
    //{"purge", UdpCom_purge, METH_NOARGS, "Clear buffers."},
    {NULL}  // Sentinel
};

static PyTypeObject DpFilterPy= {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "dpcore.DpFilterPy",
    .tp_basicsize = sizeof(DpFilterPy_t),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "Data Pipe Filter using Python transform method.",
    .tp_methods = DPFilterPy_methods,
    .tp_new = PyType_GenericNew,
		.tp_init = DpFilterPy_init, // TODO: custom init function needed.
		.tp_dealloc = DP_dealoc, // TODO: this will need to be modified.
		.tp_base = &DpFilterBase,
    //.tp_hash = UdpCom_hash,  // <-- add this line
		//.tp_repr = UdpCom_repr,
};

static struct PyModuleDef dpcore_module = {
	PyModuleDef_HEAD_INIT,
	"dpcore",
	NULL,
	-1,
	NULL
};

PyMODINIT_FUNC PyInit_dpcore(void) {
    PyObject *m;
    if (PyType_Ready(&DpFilterBase) < 0)
        return NULL;

    m = PyModule_Create(&dpcore_module);
    if (!m) return NULL;

    Py_INCREF(&DpFilterBase);
    Py_INCREF(&DpFilterPy);
    PyModule_AddObject(m, "DpFilterBase", (PyObject *)&DpFilterBase);
    PyModule_AddObject(m, "DpFilterPy", (PyObject *)&DpFilterPy);
    return m;
}
