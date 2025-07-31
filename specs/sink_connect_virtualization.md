# Sink Connect Virtualization Specification

## Overview

This specification documents the virtualization of the `filt_sink_connect` function to enable complete API interchangeability for container filters like Pipeline. This change allows specialized filters to intercept and customize connection behavior while maintaining backward compatibility.

## Problem Statement

The current Pipeline filter implementation achieves partial API interchangeability but fails to properly handle output connections. When external filters connect to a Pipeline's output:

```c
filt_sink_connect(&pipeline, 0, &next_filter.input_buffers[0]);
```

The connection is stored in the Pipeline's sink array, but the Pipeline's internal output filter (which actually produces the data) doesn't know about this connection. This breaks the data flow.

## Design Goals

1. **Complete Interchangeability**: Pipeline filters must be indistinguishable from regular filters in all API operations
2. **Zero Performance Impact**: No runtime overhead for filters that don't need custom connection behavior
3. **Backward Compatibility**: Existing filters continue to work without modification
4. **Consistency**: Follow established patterns from `filter_ops_design.md`
5. **Simplicity**: Minimal code changes, clear semantics

## Solution: Virtual sink_connect Operation

### Core Design

Add `sink_connect` to the FilterOps structure, following the established pattern for other operations:

```c
typedef struct _FilterOps {
    /* === Lifecycle === */
    Bp_EC (*start)(Filter_t* self);
    Bp_EC (*stop)(Filter_t* self);
    Bp_EC (*deinit)(Filter_t* self);
    
    /* === Connection Management === */
    Bp_EC (*sink_connect)(Filter_t* self, size_t output_port, Batch_buff_t* sink);
    
    /* ... other operations ... */
} FilterOps;
```

### Implementation Pattern

#### 1. Default Implementation

Move current `filt_sink_connect` logic to default implementation:

```c
static Bp_EC default_sink_connect(Filter_t* self, size_t output_port, Batch_buff_t* sink) {
    if (!self || !sink) return Bp_EC_NULL_PTR;
    if (output_port >= self->max_sinks) return Bp_EC_INVALID_ARG;
    if (self->n_sinks >= self->max_sinks) return Bp_EC_EXCEEDS_MAX_SINKS;
    
    pthread_mutex_lock(&self->sink_mutex);
    
    // Check if already connected
    for (size_t i = 0; i < self->n_sinks; i++) {
        if (self->sinks[i] == sink) {
            pthread_mutex_unlock(&self->sink_mutex);
            return Bp_EC_ALREADY_CONNECTED;
        }
    }
    
    // Add sink
    self->sinks[self->n_sinks++] = sink;
    
    pthread_mutex_unlock(&self->sink_mutex);
    
    return Bp_EC_OK;
}
```

#### 2. Public API Wrapper

```c
Bp_EC filt_sink_connect(Filter_t* self, size_t output_port, Batch_buff_t* sink) {
    return self->ops.sink_connect(self, output_port, sink);
}
```

#### 3. Pipeline Override

```c
static Bp_EC pipeline_sink_connect(Filter_t* self, size_t output_port, Batch_buff_t* sink) {
    Pipeline_t* pipe = (Pipeline_t*)self;
    
    // Validate port
    if (output_port != 0) return Bp_EC_INVALID_ARG;  // Pipeline has single output
    
    // Forward connection to the designated output filter
    return filt_sink_connect(pipe->output_filter, pipe->output_port, sink);
}
```

#### 4. Filter Initialization

In `filt_init`:
```c
// Set default sink_connect operation
f->ops.sink_connect = default_sink_connect;
```

In `pipeline_init`:
```c
// Override sink_connect to forward connections
pipe->base.ops.sink_connect = pipeline_sink_connect;
```

## Benefits

### 1. Complete API Interchangeability

Pipeline filters become truly interchangeable:
```c
// These work identically whether 'filter' is a Pipeline or regular filter
filt_sink_connect(&filter, 0, &sink);
filt_start(&filter);
filt_stop(&filter);
filt_deinit(&filter);
```

### 2. Enables Advanced Filter Types

- **Pipeline**: Forward connections to internal output filter
- **Splitter**: Validate sink types before accepting connections
- **Multiplexer**: Route connections based on configuration
- **LoadBalancer**: Distribute connections across workers

### 3. Clean Implementation

- No hacks or workarounds
- Follows established ops pattern
- Clear semantics
- Maintainable code

### 4. Performance Characteristics

- **Setup time**: One additional indirect function call (negligible)
- **Runtime**: Zero impact - connections happen during setup only
- **Memory**: No additional memory overhead

## Implementation Steps

### Phase 1: Core Virtualization
1. Add `sink_connect` to FilterOps structure
2. Implement `default_sink_connect`
3. Update `filt_init` to set default
4. Convert `filt_sink_connect` to wrapper
5. Update tests to verify behavior unchanged

### Phase 2: Pipeline Integration
1. Implement `pipeline_sink_connect`
2. Update `pipeline_init` to override operation
3. Remove pipeline worker thread (set to NULL)
4. Add tests for pipeline connection forwarding

### Phase 3: Documentation
1. Update API documentation
2. Add examples of custom sink_connect implementations
3. Document in filter implementation guide

## Test Cases

### 1. Backward Compatibility
```c
void test_default_sink_connect_unchanged(void) {
    // Verify existing filters work exactly as before
    Map_t filter;
    CHECK_ERR(map_init(&filter, config));
    
    Batch_buff_t sink_buffer;
    CHECK_ERR(filt_sink_connect(&filter.base, 0, &sink_buffer));
    
    TEST_ASSERT_EQUAL(1, filter.base.n_sinks);
    TEST_ASSERT_EQUAL_PTR(&sink_buffer, filter.base.sinks[0]);
}
```

### 2. Pipeline Connection Forwarding
```c
void test_pipeline_forwards_connections(void) {
    // Setup pipeline with internal filters
    Pipeline_t pipeline;
    Map_t internal_filter;
    
    // Configure pipeline to use internal_filter as output
    Pipeline_config_t config = {
        .output_filter = &internal_filter.base,
        .output_port = 0,
        // ...
    };
    CHECK_ERR(pipeline_init(&pipeline, config));
    
    // Connect external sink to pipeline
    Batch_buff_t external_sink;
    CHECK_ERR(filt_sink_connect(&pipeline.base, 0, &external_sink));
    
    // Verify connection was forwarded to internal filter
    TEST_ASSERT_EQUAL(1, internal_filter.base.n_sinks);
    TEST_ASSERT_EQUAL_PTR(&external_sink, internal_filter.base.sinks[0]);
    
    // Pipeline itself should have no sinks
    TEST_ASSERT_EQUAL(0, pipeline.base.n_sinks);
}
```

### 3. Error Handling
```c
void test_pipeline_sink_connect_errors(void) {
    Pipeline_t pipeline;
    // ... initialize ...
    
    // Invalid port
    Batch_buff_t sink;
    TEST_ASSERT_EQUAL(Bp_EC_INVALID_ARG, 
                      filt_sink_connect(&pipeline.base, 1, &sink));
    
    // NULL sink
    TEST_ASSERT_EQUAL(Bp_EC_NULL_PTR,
                      filt_sink_connect(&pipeline.base, 0, NULL));
}
```

## Migration Guide

### For Filter Developers

Most filters require no changes. To implement custom connection behavior:

```c
// In your filter's init function:
static Bp_EC my_filter_sink_connect(Filter_t* self, size_t port, Batch_buff_t* sink) {
    MyFilter_t* filter = (MyFilter_t*)self;
    
    // Custom validation
    if (!is_compatible_sink(sink)) {
        return Bp_EC_INCOMPATIBLE_SINK;
    }
    
    // Call default implementation
    return default_sink_connect(self, port, sink);
}

// In init:
filter->base.ops.sink_connect = my_filter_sink_connect;
```

### For Framework Users

No changes required. Continue using `filt_sink_connect` as before.

## Alternatives Considered

### 1. Proxy Pattern for Sink Arrays
- **Approach**: Have Pipeline share sink array memory with output filter
- **Rejected**: Breaks encapsulation, complex memory management

### 2. Pipeline Worker Thread
- **Approach**: Use worker to forward data from internal to external
- **Rejected**: Unnecessary overhead, wastes thread resource

### 3. Double Connection
- **Approach**: Connect to both Pipeline and internal filter
- **Rejected**: Requires API changes, error-prone

### 4. Connection Interception
- **Approach**: Hook into existing filt_sink_connect
- **Rejected**: No clean way without virtualization

## Conclusion

Virtualizing `filt_sink_connect` is a clean, minimal change that:
- Enables complete API interchangeability for Pipeline filters
- Follows established patterns in the codebase
- Maintains backward compatibility
- Has negligible performance impact
- Opens possibilities for other advanced filter types

This design aligns with bpipe2's philosophy of simplicity and directness while providing the flexibility needed for container filters.