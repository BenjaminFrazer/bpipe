# NumPy Segfault Investigation

## Issue
The PYFILT_AGGREGATOR implementation segfaults when trying to create NumPy arrays in the `BpAggregatorPy_get_arrays` function.

## Core Infrastructure Status
✅ **Working Components:**
- C aggregator structure and buffer management
- Python type object integration 
- Basic Python method calls (get_sizes, clear)
- Aggregator creation and initialization
- Buffer access and struct field access

## Segfault Location
The crash occurs specifically in **NumPy array creation**:
```c
PyObject* array = PyArray_SimpleNew(1, dims, NPY_FLOAT32);  // ← SEGFAULT HERE
```

## Investigation Results

### Working Code Patterns:
- `PyList_New()` - ✅ Works fine
- `PyList_SET_ITEM()` - ✅ Works fine
- Basic struct member access (`agg->n_buffers`) - ✅ Works fine
- Return Python objects - ✅ Works fine

### Failing Code Patterns:
- `PyArray_SimpleNew()` - ❌ Segfaults
- `PyArray_SimpleNewFromData()` - ❌ Segfaults  
- `PyArray_CLEARFLAGS()` - ❌ Segfaults

## Possible Causes

### 1. NumPy Import Issue
The `import_array()` call in module initialization might not be sufficient for NumPy usage in the aggregator_python.c file. NumPy macros might require per-file initialization.

### 2. Linker/Symbol Issue
NumPy symbols might not be properly linked when called from the aggregator_python.c compilation unit.

### 3. Module Structure Issue
The aggregator might need to be in the same compilation unit as the main module init to access NumPy properly.

## Workaround
Currently using Python lists as placeholders:
```c
PyObject* array = PyList_New(0);  // Works fine
```

## Next Steps to Fix

### Option 1: Move NumPy code to core_python.c
Create a helper function in core_python.c that handles NumPy array creation and call it from aggregator.

### Option 2: Proper NumPy initialization
Research if additional NumPy initialization is needed in aggregator_python.c.

### Option 3: Different NumPy API
Try using different NumPy C API functions that might be more stable.

## Test Status
- Basic aggregator functionality: ✅ Working
- Buffer management: ✅ Working  
- Python integration: ✅ Working
- NumPy arrays: ❌ Segfaulting (temporary workaround in place)

## Architecture Impact
The two-stage design remains sound. This is purely a NumPy C API integration issue, not a fundamental design problem.