#include "core.h"
#include <stdio.h>

void* Bp_Worker(void* filter) {
	Bp_Filter_t* f = (Bp_Filter_t*)filter;
	Bp_Batch_t input_batch = f->has_input_buffer ? Bp_head(f)       : (Bp_Batch_t){ .data = NULL, .capacity = 0, .ec=Bp_EC_NOINPUT};
	Bp_Batch_t output_batch = f->sink ? Bp_allocate(f->sink) : (Bp_Batch_t){ .data = malloc(1024 * f->data_width), .capacity = 1024 };

	while (f->running) {
		f->transform(filter, &input_batch, &output_batch);

		if (f->has_input_buffer && (input_batch.head >= input_batch.capacity)) {
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

PyObject* Bp_set_sink(PyObject *self, PyObject *args){
	Bp_Filter_t* consumer;
	Bp_Filter_t* obj = (Bp_Filter_t*)self;
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

PyObject* Bp_start(PyObject* self, PyObject *args){
	Bp_Filter_t* obj = (Bp_Filter_t*)self;

	if (obj->running){
    PyErr_SetString(PyExc_ValueError, "Already running.");
		return NULL;
	}

	obj->running = true;

	// Create the thread
	if (pthread_create(&obj->worker_thread, NULL, &Bp_Worker, (void*)obj) != 0) {
		perror("pthread_create worker");
		return PyErr_SetFromErrno(PyExc_OSError);
	}
	Py_RETURN_NONE;
}

PyObject* Bp_stop(PyObject* self, PyObject *args){
	Bp_Filter_t* obj = (Bp_Filter_t*)self;
	obj->running = false;
	if(pthread_join(obj->worker_thread, NULL)<0)
		return PyErr_SetFromErrno(PyExc_OSError);
	Py_RETURN_NONE;
}

static PyMethodDef BpFilterBase_methods[] = {
		{"set_sink", 		Bp_set_sink, 		METH_VARARGS, "Send data over UDP"},
		{"run", 		Bp_start, 		METH_VARARGS, "Start worker thread"},
		{"stop", 		Bp_stop, 		METH_VARARGS, "Start worker thread"},
    //{"purge", UdpCom_purge, METH_NOARGS, "Clear buffers."},
    {NULL}  // Sentinel
};

static void Bp_dealoc(PyObject *self) {
	Py_TYPE(self)->tp_free(self);
}

static PyTypeObject BpFilterBase = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "dpcore.BpFilterBase",
    .tp_basicsize = sizeof(Bp_Filter_t),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "Data Pipe Filter Base class.",
    .tp_methods = BpFilterBase_methods,
    .tp_new = PyType_GenericNew,
		.tp_init = Bp_init,
		.tp_dealloc = Bp_dealoc,
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
PyObject* BpFilterPy_transform(PyObject *self, PyObject *args){
	//Bp_Filter_t* obj = (Bp_Filter_t*)self;
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

/* C-level transform that simply copies input batch to output batch */
void BpPassThroughTransform(Bp_Filter_t* filt, Bp_Batch_t *input_batch, Bp_Batch_t *output_batch){
        size_t available = input_batch->head - input_batch->tail;
        size_t space     = output_batch->capacity - output_batch->head;
        size_t ncopy     = available < space ? available : space;

        if(ncopy){
                void* src = (char*)input_batch->data + input_batch->tail * filt->data_width;
                void* dst = (char*)output_batch->data + output_batch->head * filt->data_width;
                memcpy(dst, src, ncopy * filt->data_width);
        }

        output_batch->t_ns      = input_batch->t_ns;
        output_batch->period_ns = input_batch->period_ns;
        output_batch->dtype     = input_batch->dtype;
        output_batch->meta      = input_batch->meta;
        output_batch->ec        = input_batch->ec;

        input_batch->tail  += ncopy;
        output_batch->head += ncopy;
}

void BpPyTransform(Bp_Filter_t* filt, Bp_Batch_t *input_batch, Bp_Batch_t *output_batch){
	PyGILState_STATE gstate = PyGILState_Ensure();

	/* Create input numpy array */
	size_t input_len = input_batch->head-input_batch->tail;
	size_t output_len = input_batch->capacity-input_batch->head;
	void* input_start = input_batch->data + input_batch->tail*filt->data_width;
	void* oputput_start = input_batch->data + input_batch->tail*filt->data_width;
	npy_intp dims_in[1] = {input_len};
	npy_intp dims_out[1] = {output_len};
	PyObject* input_arr = PyArray_SimpleNewFromData(1, dims_in, NPY_FLOAT32, input_start);
	Bp_ASSERT(filt, input_arr, Bp_EC_BAD_PYOBJECT, "Failed to create input numpy array");

	PyObject* output_arr= PyArray_SimpleNewFromData(1, dims_out, NPY_FLOAT32, oputput_start);
	Bp_ASSERT(filt, input_arr, Bp_EC_BAD_PYOBJECT, "Failed to create oputput numpy array");

	PyObject* py_bytearray = PyByteArray_FromStringAndSize((const char*)input_batch, sizeof(Bp_Batch_t));
	Bp_ASSERT(filt, py_bytearray, Bp_EC_BAD_PYOBJECT, "Failed to create bytes array");

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
		Bp_ASSERT(filt, PyTuple_Check(result), EC_TYPE_MISMATCH, "Expected a tuple return value");
		Bp_ASSERT(filt, PyArg_ParseTuple(result, "Li", &ts, &period), EC_TYPE_MISMATCH, "Expected a tuple return value");
		output_batch->t_ns = (long long)ts;
		output_batch->period_ns = (unsigned)period;
	}
	// Use f, i, s here
	Py_DECREF(result);

	PyGILState_Release(gstate);

	return;
}

static PyMethodDef DPFilterPy_methods[] = {
		{"transform", 		BpFilterPy_transform, 		METH_VARARGS, "Transform input data to output data."},
    //{"purge", UdpCom_purge, METH_NOARGS, "Clear buffers."},
    {NULL}  // Sentinel
};

static PyTypeObject BpFilterPy= {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "dpcore.BpFilterPy",
    .tp_basicsize = sizeof(BpFilterPy_t),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "Data Pipe Filter using Python transform method.",
    .tp_methods = DPFilterPy_methods,
    .tp_new = PyType_GenericNew,
		.tp_init = BpFilterPy_init, // TODO: custom init function needed.
		.tp_dealloc = Bp_dealoc, // TODO: this will need to be modified.
		.tp_base = &BpFilterBase,
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
    import_array();  // MUST be called to init NumPy
    PyObject *m;
    if (PyType_Ready(&BpFilterBase) < 0)
        return NULL;

    m = PyModule_Create(&dpcore_module);
    if (!m) return NULL;

    Py_INCREF(&BpFilterBase);
    Py_INCREF(&BpFilterPy);
    PyModule_AddObject(m, "BpFilterBase", (PyObject *)&BpFilterBase);
    PyModule_AddObject(m, "BpFilterPy", (PyObject *)&BpFilterPy);
    return m;
}
