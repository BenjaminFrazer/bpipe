# Python Initialization Migration Specification

## Objective

Migrate Python filter initialization to wrap C core initialization functions, eliminating code duplication and ensuring complete field initialization.

## Background

Current Python initialization bypasses `BpFilter_Init`, leading to:
- Uninitialized `filter_mutex` causing worker thread failures
- Missing field initialization (running, n_sources, n_sinks, stopped)
- Maintenance burden from duplicated logic
- Drift between Python and C initialization paths

## Scope

### In Scope
- Refactor `Bp_init` in `core_python.c` to call `BpFilter_Init`
- Create parameter mapping layer between Python API and C config
- Ensure backward compatibility with existing Python code
- Add proper cleanup on initialization failure
- Update Python subclasses (e.g., `BpFilterPy_init`)

### Out of Scope
- Changing the Python API surface
- Modifying C core initialization logic
- Changing filter runtime behavior
- Updating Python buffer operations (already using C functions)

## Design

### 1. Parameter Mapping

Create translation between Python kwargs and `BpFilterConfig`:

```c
typedef struct {
    int capacity_exp;        // Python parameter
    SampleDtype_t dtype;     // Python parameter
    int batch_size;          // Optional, with default
    int buffer_size;         // Calculated from batch_size
    // ... other fields with defaults
} BpPythonInitParams;

static int parse_python_init_params(PyObject* args, PyObject* kwds, 
                                   BpPythonInitParams* params);
static void python_params_to_config(const BpPythonInitParams* params,
                                   BpFilterConfig* config);
```

### 2. Base Class Initialization

Rename current `Bp_init` to `BpFilterBase_init` and refactor:

```c
// Base initialization that wraps C core
int BpFilterBase_init(PyObject* self, PyObject* args, PyObject* kwds)
{
    BpFilterPy_t* pyfilter = (BpFilterPy_t*) self;
    BpPythonInitParams params = BP_PYTHON_INIT_PARAMS_DEFAULT;
    
    // 1. Parse Python arguments
    if (parse_python_init_params(args, kwds, &params) < 0)
        return -1;
    
    // 2. Convert to C config
    BpFilterConfig config = BP_FILTER_CONFIG_DEFAULT;
    python_params_to_config(&params, &config);
    
    // 3. Call C initializer
    Bp_EC result = BpFilter_Init(&pyfilter->base, &config);
    if (result != Bp_EC_OK) {
        PyErr_SetString(PyExc_RuntimeError, "Filter initialization failed");
        return -1;
    }
    
    // 4. Python-specific setup (none for base class)
    return 0;
}
```

### 3. Python Filter Subclass

Update `BpFilterPy_init` to use new pattern:

```c
int BpFilterPy_init(PyObject* self, PyObject* args, PyObject* kwds)
{
    // 1. Call base class init (which calls BpFilter_Init)
    if (BpFilterBase.tp_init(self, args, kwds) < 0)
        return -1;
    
    // 2. Set Python-specific transform
    BpFilterPy_t* filter = (BpFilterPy_t*) self;
    filter->base.transform = BpPyTransform;
    
    // 3. Initialize Python-specific fields
    filter->impl = NULL;  // Will be set by set_impl
    
    return 0;
}
```

### 4. Parameter Defaults

Define sensible defaults that match current behavior:

```c
#define BP_PYTHON_INIT_PARAMS_DEFAULT { \
    .capacity_exp = 10,                 \
    .dtype = DTYPE_FLOAT,               \
    .batch_size = 64,                   \
    .buffer_size = 128                  \
}
```

### 5. Error Handling

Ensure proper cleanup on failure:

```c
// In BpFilter_Init, ensure partial initialization is cleaned up
if (pthread_mutex_init(&filter->filter_mutex, NULL) != 0) {
    // Clean up any already-initialized resources
    BpFilter_cleanup_partial(filter);
    return Bp_EC_INIT_FAILED;
}
```

## Implementation Steps

### Phase 1: Create Infrastructure
1. Add `BpPythonInitParams` structure
2. Implement `parse_python_init_params` function
3. Implement `python_params_to_config` function
4. Add unit tests for parameter translation

### Phase 2: Refactor Base Class
1. Rename `Bp_init` to `BpFilterBase_init`
2. Implement wrapper pattern calling `BpFilter_Init`
3. Update `BpFilter_Type` to use new init function
4. Test basic filter creation

### Phase 3: Update Subclasses
1. Update `BpFilterPy_init` to call base init
2. Remove duplicate initialization code
3. Ensure transform function is set correctly
4. Test Python filter functionality

### Phase 4: Cleanup and Testing
1. Remove old initialization code
2. Add comprehensive tests for edge cases
3. Test thread safety with proper mutex initialization
4. Verify backward compatibility

## Testing Strategy

### Unit Tests
- Parameter parsing with various combinations
- Config translation accuracy
- Error handling for invalid parameters
- Initialization failure cleanup

### Integration Tests
- Create filters via Python API
- Verify all fields properly initialized
- Test multi-threaded operation
- Ensure mutexes work correctly

### Regression Tests
- Existing Python code continues to work
- Performance characteristics unchanged
- Memory usage patterns consistent

## Migration Checklist

- [x] Create parameter mapping infrastructure
- [x] Refactor base class initialization  
- [x] Update Python filter subclass
- [x] Add comprehensive error handling
- [x] Write unit tests for new code
- [x] Run integration tests
- [ ] Update documentation
- [x] Remove deprecated code
- [x] Performance validation
- [ ] Code review

## Success Criteria

1. All Python filters use `BpFilter_Init` for initialization
2. No duplicated initialization logic
3. All tests pass including multi-threaded scenarios
4. Existing Python API unchanged
5. Clear separation between Python and C layers

## Risks and Mitigations

### Risk: Breaking existing Python code
**Mitigation**: Careful parameter mapping to preserve API compatibility

### Risk: Performance regression
**Mitigation**: Benchmark before/after, optimize parameter translation

### Risk: Incomplete initialization
**Mitigation**: Comprehensive testing, especially threading scenarios

### Risk: Memory leaks on failure
**Mitigation**: Implement proper cleanup in error paths

## Timeline

- Infrastructure setup: 2 hours
- Base class refactoring: 3 hours
- Subclass updates: 2 hours
- Testing and validation: 3 hours
- Documentation: 1 hour

**Total estimate**: 11 hours

## Future Considerations

1. Consider exposing more `BpFilterConfig` options to Python
2. Add Python property accessors for filter configuration
3. Implement filter serialization/deserialization
4. Consider async/await patterns for Python filters