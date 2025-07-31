# Input Buffer Pointer Refactoring Specification

## Overview

This specification describes the refactoring of `Filter_t` to use an array of pointers to `Batch_buff_t` instead of directly embedding the buffer structures. This change enables zero-copy composite filters and more flexible buffer management while maintaining API compatibility.

## Motivation

1. **Enable Zero-Copy Composite Filters**: Composite filters can directly share internal filter buffers without data copying
2. **Consistency**: Match the existing pattern used for sink buffers (`Batch_buff_t *sinks[MAX_SINKS]`)
3. **Flexibility**: Allow filters to share input buffers or use externally allocated buffers
4. **Memory Efficiency**: Allocate only the buffers actually needed

## Current vs Proposed Structure

### Current Structure
```c
typedef struct _Filter_t {
    // ... other fields ...
    Batch_buff_t input_buffers[MAX_INPUTS];  // Embedded array
    Batch_buff_t *sinks[MAX_SINKS];         // Pointer array
    // ...
} Filter_t;
```

### Proposed Structure
```c
typedef struct _Filter_t {
    // ... other fields ...
    Batch_buff_t *input_buffers[MAX_INPUTS];  // Pointer array (matching sinks)
    Batch_buff_t *sinks[MAX_SINKS];          // Unchanged
    // ...
} Filter_t;
```

## Implementation Plan

### Phase 1: Core Infrastructure Changes

#### 1.1 Update Filter_t Structure (bpipe/core.h)
```c
// Line 136: Change from embedded array to pointer array
Batch_buff_t *input_buffers[MAX_INPUTS];
```

#### 1.2 Update filt_init (bpipe/core.c)
```c
Bp_EC filt_init(Filter_t *filter, Core_filt_config_t config) {
    // ... existing initialization ...
    
    // Allocate and initialize input buffers
    for (size_t i = 0; i < config.n_inputs; i++) {
        filter->input_buffers[i] = malloc(sizeof(Batch_buff_t));
        if (!filter->input_buffers[i]) {
            // Cleanup already allocated buffers
            for (size_t j = 0; j < i; j++) {
                bb_deinit(filter->input_buffers[j]);
                free(filter->input_buffers[j]);
            }
            return Bp_EC_ALLOC;
        }
        
        Bp_EC err = bb_init(filter->input_buffers[i], config.buff_config);
        if (err != Bp_EC_OK) {
            // Cleanup
            free(filter->input_buffers[i]);
            for (size_t j = 0; j < i; j++) {
                bb_deinit(filter->input_buffers[j]);
                free(filter->input_buffers[j]);
            }
            return err;
        }
    }
    
    // Initialize remaining pointers to NULL
    for (size_t i = config.n_inputs; i < MAX_INPUTS; i++) {
        filter->input_buffers[i] = NULL;
    }
    
    // ... rest of initialization ...
}
```

#### 1.3 Update filt_deinit (bpipe/core.c)
```c
Bp_EC filt_deinit(Filter_t *filter) {
    // ... existing cleanup ...
    
    // Free allocated input buffers
    for (size_t i = 0; i < filter->n_input_buffers; i++) {
        if (filter->input_buffers[i]) {
            bb_deinit(filter->input_buffers[i]);
            free(filter->input_buffers[i]);
            filter->input_buffers[i] = NULL;
        }
    }
    
    // ... rest of cleanup ...
}
```

### Phase 2: Update Buffer Access Patterns

#### 2.1 Search and Replace Pattern
All occurrences of `&filter->input_buffers[i]` need to change to `filter->input_buffers[i]`.

**Files to update:**
- All filter implementations in `bpipe/filters/`
- All test files in `tests/`
- All examples in `examples/`
- Core files: `bpipe/core.c`

#### 2.2 Common Access Patterns to Update

**Getting buffer pointer:**
```c
// OLD
Batch_buff_t* buff = &filter->input_buffers[0];

// NEW
Batch_buff_t* buff = filter->input_buffers[0];
```

**Passing to functions:**
```c
// OLD
bb_get_tail(&filter->input_buffers[0], timeout, &err);

// NEW
bb_get_tail(filter->input_buffers[0], timeout, &err);
```

**In connections:**
```c
// OLD
filt_sink_connect(&source, 0, &sink.input_buffers[0]);

// NEW  
filt_sink_connect(&source, 0, sink.input_buffers[0]);
```

### Phase 3: Add Safety Checks

#### 3.1 NULL Checks in Critical Functions
```c
// In worker functions
Batch_t* input = bb_get_tail(filter->input_buffers[0], timeout, &err);
// Should become:
if (filter->input_buffers[0]) {
    Batch_t* input = bb_get_tail(filter->input_buffers[0], timeout, &err);
} else {
    WORKER_ASSERT(&filter->base, false, Bp_EC_NULL_PTR, 
                  "Input buffer 0 is NULL");
}
```

#### 3.2 Connection Validation
```c
Bp_EC filt_sink_connect(Filter_t *f, size_t sink_idx, 
                       Batch_buff_t *dest_buffer) {
    // Add validation
    if (!dest_buffer) {
        return Bp_EC_NULL_PTR;
    }
    // ... existing code ...
}
```

### Phase 4: Enable Buffer Sharing

#### 4.1 Add Buffer Ownership Tracking (Optional)
```c
typedef struct _Filter_t {
    // ... existing fields ...
    bool input_buffer_owned[MAX_INPUTS];  // Track which buffers we allocated
} Filter_t;
```

#### 4.2 Support External Buffer Assignment
```c
// New API to assign external buffer (for composite filters)
Bp_EC filt_set_input_buffer(Filter_t *filter, size_t idx, 
                           Batch_buff_t *buffer, bool owned) {
    if (idx >= filter->n_input_buffers) {
        return Bp_EC_INVALID_ARG;
    }
    
    // Free existing buffer if we own it
    if (filter->input_buffers[idx] && filter->input_buffer_owned[idx]) {
        bb_deinit(filter->input_buffers[idx]);
        free(filter->input_buffers[idx]);
    }
    
    filter->input_buffers[idx] = buffer;
    filter->input_buffer_owned[idx] = owned;
    
    return Bp_EC_OK;
}
```

## Testing Strategy

### 1. Unit Tests
- Test filter initialization with various input counts
- Test buffer allocation failures
- Test cleanup on partial initialization failure
- Test NULL buffer handling

### 2. Integration Tests
- Run all existing tests to ensure compatibility
- Add tests for buffer sharing scenarios
- Test composite filter with shared buffers

### 3. Performance Tests
- Verify no performance regression
- Measure improvement in composite filter overhead

## Migration Checklist

- [ ] Update Filter_t structure in core.h
- [ ] Update filt_init to allocate buffers
- [ ] Update filt_deinit to free buffers
- [ ] Update all filter implementations
- [ ] Update all test files
- [ ] Update all examples
- [ ] Add NULL checks where appropriate
- [ ] Run full test suite
- [ ] Update documentation
- [ ] Performance validation

## Risks and Mitigations

1. **Risk**: Memory leaks if cleanup not handled properly
   - **Mitigation**: Careful error handling in init/deinit, valgrind testing

2. **Risk**: NULL pointer dereferences
   - **Mitigation**: Add NULL checks, use static analysis tools

3. **Risk**: Breaking existing code
   - **Mitigation**: Systematic search/replace, comprehensive testing

## Future Enhancements

1. **Lazy Buffer Allocation**: Allocate buffers only when first connected
2. **Buffer Pooling**: Reuse buffers across filter restarts
3. **Dynamic Buffer Count**: Allow changing input count at runtime
4. **Zero-Copy Tee**: Multiple filters sharing same input buffer

## Appendix: Affected Files

Based on grep analysis, the following files need updates:

### Core Files
- bpipe/core.h
- bpipe/core.c

### Filter Implementations
- bpipe/filters/map.c
- bpipe/filters/tee.c
- bpipe/filters/csv_source.c
- bpipe/filters/csv_sink.c
- bpipe/filters/signal_generator.c
- bpipe/filters/debug_output.c
- bpipe/filters/batch_matcher.c
- bpipe/filters/sample_aligner.c

### Test Files
- tests/test_core_filt.c
- tests/test_map.c
- tests/test_tee.c
- tests/test_csv_source.c
- tests/test_csv_sink.c
- tests/test_signal_generator.c
- tests/test_debug_output_filter.c
- tests/test_batch_matcher.c
- tests/test_sample_aligner.c

### Example Files
- examples/csv_to_csv_scale.c
- examples/csv_to_csv_direct.c
- examples/csv_debug_simple.c
- examples/csv_scale_simple.c
- examples/csv_to_debug_auto.c