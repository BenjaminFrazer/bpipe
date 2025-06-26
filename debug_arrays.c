/* Debug version of get_arrays function */
PyObject* BpAggregatorPy_get_arrays_debug(PyObject *self, void *closure) {
    printf("Debug: Entering get_arrays\n");
    
    if (!self) {
        printf("Debug: self is NULL!\n");
        PyErr_SetString(PyExc_RuntimeError, "self is NULL");
        return NULL;
    }
    
    printf("Debug: self pointer: %p\n", self);
    
    BpAggregatorPy_t *agg = (BpAggregatorPy_t *)self;
    printf("Debug: agg pointer: %p\n", agg);
    
    printf("Debug: About to check n_buffers\n");
    printf("Debug: n_buffers = %zu\n", agg->n_buffers);
    
    printf("Debug: About to check arrays_dirty\n");
    printf("Debug: arrays_dirty = %d\n", agg->arrays_dirty);
    
    printf("Debug: Creating simple list\n");
    PyObject* arrays = PyList_New(agg->n_buffers);
    if (!arrays) {
        printf("Debug: Failed to create list\n");
        return NULL;
    }
    
    printf("Debug: Created list with %zu items\n", agg->n_buffers);
    
    /* For now, just add None objects to avoid NumPy complexity */
    for (size_t i = 0; i < agg->n_buffers; i++) {
        Py_INCREF(Py_None);
        PyList_SET_ITEM(arrays, i, Py_None);
    }
    
    printf("Debug: Returning list\n");
    return arrays;
}