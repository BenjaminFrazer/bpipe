# Map Filter Implementation Plan

## Overview

This document outlines the plan for bringing the map filter implementation up to production quality, addressing current issues and integrating with the new filter operations design.

## Current State Analysis

### A) Problems Identified

1. **Function pointer typedef syntax error** (map.h:4)
   - Current: `typedef Bp_EC(Map_fcn_t)(char*, char*, size_t);`
   - Should be: `typedef Bp_EC (*Map_fcn_t)(char* out, char* in, size_t n_samples);`

2. **Incorrect parameter order** (map.c:56-57)
   - Currently passes output first, then input to map function
   - Convention typically expects input first, then output

3. **Missing include guard** in map.h
   - No header protection against multiple inclusion

4. **Non-portable include** (map.c:2)
   - `#include <bits/types/struct_iovec.h>` is implementation-specific and unused

5. **Incomplete filter ops integration**
   - Partially implemented but not following the full pattern from filter_ops_design.md

### B) Improvements Needed

1. **Type safety**: Change from `char*` to `void*` for generic data handling
2. **Const correctness**: Input pointers should be `const void*`
3. **Timing preservation**: Copy `t_ns` and `period_ns` from input to output batches
4. **Metrics tracking**: Add samples_processed counter to Map_filt_t
5. **Error propagation**: Better handling of map function errors

### C) Outstanding Work

1. **Test suite**: No test file exists for the map filter
2. **Example functions**: No sample map transformations provided
3. **Documentation**: Missing usage examples and API documentation
4. **Python bindings**: No Python wrapper for map filter
5. **Performance optimization**: Could benefit from branch prediction hints

### D) Testing Strategy

#### Unit Tests (test_map.c)
- Initialization with valid/invalid configurations
- NULL pointer handling for all parameters
- Map function execution with various data sizes
- Error propagation from map function
- Buffer management (partial batches, full batches, empty batches)
- Graceful shutdown during processing
- Concurrent start/stop operations

#### Integration Tests
- Chain multiple map filters
- Test with different data types (float, int, complex)
- Various batch sizes and buffer configurations
- Performance benchmarks comparing to passthrough filter

#### Example Map Functions
- Identity/passthrough
- Scale by constant
- Add offset
- Type conversion (float to int with rounding)
- Complex number magnitude
- Byte swapping for endianness

## Implementation Plan

### Phase 1: Fix Critical Issues
1. Fix function pointer typedef syntax
2. Add include guards to map.h
3. Remove non-portable includes
4. Fix parameter order to (input, output, n_samples)

### Phase 2: Core Enhancements
1. Change to void* pointers for generic data
2. Add const correctness
3. Implement batch timing preservation
4. Add samples_processed tracking
5. Complete filter ops integration

### Phase 3: Testing Infrastructure
1. Create comprehensive test_map.c
2. Add example map functions library
3. Integration test with filter chains
4. Performance benchmarking

### Phase 4: Documentation & Examples
1. Header documentation with usage examples
2. Common transformation implementations
3. Python bindings
4. Add to filter categories documentation

## Code Structure

### Enhanced Map Filter Header (map.h)
```c
#ifndef MAP_H
#define MAP_H

#include "core.h"
#include "bperr.h"

/* Map function signature: process n_samples from in to out */
typedef Bp_EC (*Map_fcn_t)(const void* in, void* out, size_t n_samples);

typedef struct _Map_filt_t {
    Filter_t base;
    Map_fcn_t map_fcn;
    size_t samples_processed;  /* Total samples processed */
} Map_filt_t;

typedef struct _Map_config_t {
    BatchBuffer_config buff_config;
    Map_fcn_t map_fcn;
} Map_config_t;

/* Initialize a map filter */
Bp_EC map_init(Map_filt_t* f, Map_config_t config);

/* Example map functions */
Bp_EC map_identity(const void* in, void* out, size_t n);
Bp_EC map_scale_f32(const void* in, void* out, size_t n);

#endif /* MAP_H */
```

### Worker Implementation Pattern
- Preserve batch timing information
- Track samples processed
- Handle partial batches correctly
- Proper cleanup on error

## Success Criteria

1. All identified issues fixed
2. Full test coverage with Unity framework
3. Integration with filter ops design
4. Performance within 5% of passthrough filter
5. Clean linting with no warnings
6. Example transformations demonstrating usage

## Timeline Estimate

- Phase 1: 1 hour (critical fixes)
- Phase 2: 2 hours (enhancements)
- Phase 3: 3 hours (testing)
- Phase 4: 2 hours (documentation)

Total: ~8 hours of implementation work