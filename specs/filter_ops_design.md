# Filter Operations Design Specification

## Overview

This document specifies the operations (ops) interface for bpipe filters, enabling polymorphic behavior while maintaining simplicity and performance.

## Design Philosophy

1. **Self-contained filters** - Each filter type populates its own ops during initialization
2. **No NULL pointers** - All ops populated with sensible defaults or no-ops to avoid branching
3. **Reuse existing code** - Current filt_* functions become default implementations
4. **No central registry** - Avoids unnecessary abstraction layers
5. **Per-instance customization** - Ops can vary based on configuration

## Core Operations Structure

```c
typedef struct _FilterOps {
    /* === Lifecycle === */
    Bp_EC (*start)(Filter_t* self);      /* Pre-start initialization */
    Bp_EC (*stop)(Filter_t* self);       /* Graceful shutdown */
    Bp_EC (*deinit)(Filter_t* self);     /* Cleanup resources */
    
    /* === Data Flow Control === */
    Bp_EC (*flush)(Filter_t* self);      /* Process partial batches */
    Bp_EC (*drain)(Filter_t* self);      /* Wait for all outputs to complete */
    
    /* === State Management === */
    Bp_EC (*reset)(Filter_t* self);      /* Reset to initial state */
    Bp_EC (*save_state)(const Filter_t* self, void** buffer, size_t* size);
    Bp_EC (*load_state)(Filter_t* self, const void* buffer, size_t size);
    
    /* === Diagnostics === */
    void (*get_stats)(const Filter_t* self, Filt_metrics* stats);
    Bp_EC (*get_health)(const Filter_t* self, FilterHealth_t* health);
    size_t (*get_backlog)(const Filter_t* self);  /* Samples queued */
    
    /* === Configuration === */
    Bp_EC (*reconfigure)(Filter_t* self, const void* new_config);
    Bp_EC (*validate_connection)(const Filter_t* self, const Filter_t* other, size_t port);
    
    /* === Debugging === */
    const char* (*describe)(const Filter_t* self);
    void (*dump_state)(const Filter_t* self, FILE* out);
} FilterOps;
```

## Operation Categories

### Essential Operations (90% of filters need these)

#### `flush`
- **Purpose**: Force processing of accumulated partial data
- **Use case**: Filters that buffer data (FFT, block processors)
- **Return**: `Bp_EC_OK` on success, error code on failure

#### `reset`
- **Purpose**: Return filter to initial state without full teardown
- **Use case**: Restarting processing, clearing history
- **Return**: `Bp_EC_OK` on success

#### `get_stats`
- **Purpose**: Retrieve performance and processing metrics
- **Use case**: Monitoring, debugging, optimization
- **Parameters**: Populates provided `Filt_metrics` structure

#### `describe`
- **Purpose**: Human-readable description of filter configuration
- **Use case**: Logging, debugging, UI display
- **Return**: Static string (filter owns memory)

### Important Operations (50% of filters benefit)

#### `drain`
- **Purpose**: Block until all queued data is processed
- **Use case**: Graceful shutdown, synchronization points
- **Return**: `Bp_EC_OK` when drained

#### `get_backlog`
- **Purpose**: Report number of samples queued for processing
- **Use case**: Flow control, load balancing
- **Return**: Sample count

#### `save_state` / `load_state`
- **Purpose**: Checkpoint/restore filter state
- **Use case**: Fault tolerance, migration
- **Memory**: Filter allocates buffer for save, caller frees

### Specialized Operations (10% need these)

#### `reconfigure`
- **Purpose**: Change filter parameters at runtime
- **Use case**: Adaptive filtering, parameter tuning
- **Thread safety**: Must be safe to call while running

#### `validate_connection`
- **Purpose**: Check if connection to another filter is valid
- **Use case**: Type checking beyond simple dtype match
- **Return**: `Bp_EC_OK` if valid

#### `dump_state`
- **Purpose**: Detailed state dump for debugging
- **Use case**: Post-mortem analysis, deep debugging
- **Output**: Human-readable format to FILE*

## Implementation Pattern

### Default Implementations

The current `filt_start`, `filt_stop`, and `filt_deinit` functions become default operations:

```c
/* Current implementations become defaults */
static Bp_EC default_start(Filter_t* f)
{
    if (!f) return Bp_EC_NULL_FILTER;
    if (f->running) return Bp_EC_ALREADY_RUNNING;
    
    f->running = true;
    if (pthread_create(&f->worker_thread, NULL, f->worker, (void*)f) != 0) {
        f->running = false;
        return Bp_EC_THREAD_CREATE_FAIL;
    }
    return Bp_EC_OK;
}

static Bp_EC default_stop(Filter_t* f)
{
    if (!f) return Bp_EC_NULL_FILTER;
    if (!f->running) return Bp_EC_OK;
    
    f->running = false;
    
    /* Stop all input buffers */
    for (int i = 0; i < f->n_input_buffers; i++) {
        if (f->input_buffers[i].data_ring != NULL) {
            bb_stop(&f->input_buffers[i]);
        }
    }
    
    if (pthread_join(f->worker_thread, NULL) != 0) {
        return Bp_EC_THREAD_JOIN_FAIL;
    }
    return Bp_EC_OK;
}

/* No-op implementations for operations most filters don't need */
static Bp_EC noop_lifecycle(Filter_t* self) { return Bp_EC_OK; }
static Bp_EC noop_flush(Filter_t* self) { return Bp_EC_OK; }

/* In filt_init - populate ALL ops */
Bp_EC filt_init(Filter_t *f, Core_filt_config_t config)
{
    /* ... existing initialization ... */
    
    /* Set all ops to defaults/no-ops */
    f->ops.start = default_start;      /* Current filt_start logic */
    f->ops.stop = default_stop;        /* Current filt_stop logic */
    f->ops.deinit = default_deinit;    /* Current filt_deinit logic */
    f->ops.flush = noop_flush;         /* Most filters don't buffer */
    f->ops.drain = default_drain;      /* Generic wait for outputs */
    f->ops.reset = default_reset;      /* Clear metrics */
    f->ops.save_state = noop_save_state;
    f->ops.load_state = noop_load_state;
    f->ops.get_stats = default_get_stats;
    f->ops.describe = default_describe;
    /* ... etc ... */
    
    return Bp_EC_OK;
}
```

### Public API as Wrappers

```c
/* Public API becomes thin wrappers */
Bp_EC filt_start(Filter_t* filter) {
    return filter->ops.start(filter);
}

Bp_EC filt_stop(Filter_t* filter) {
    return filter->ops.stop(filter);
}

Bp_EC filt_deinit(Filter_t* filter) {
    return filter->ops.deinit(filter);
}

/* New operations use same pattern */
Bp_EC filt_flush(Filter_t* filter) {
    return filter->ops.flush(filter);
}
```

### Filter Initialization
```c
Bp_EC my_filter_init(MyFilter_t* f, MyFilterConfig* config)
{
    /* Initialize base filter - gets all default ops */
    Core_filt_config_t core_config = {
        .name = config->name,
        .size = sizeof(MyFilter_t),
        .worker = my_filter_worker,
        /* ... */
    };
    
    Bp_EC rc = filt_init(&f->base, core_config);
    if (rc != Bp_EC_OK) return rc;
    
    /* Override only ops that need custom behavior */
    f->base.ops.flush = my_filter_flush;      /* Custom flush */
    f->base.ops.describe = my_filter_describe; /* Custom description */
    /* All other ops remain as defaults */
    
    /* Initialize filter-specific fields */
    f->window_size = config->window_size;
    f->samples_processed = 0;
    
    return Bp_EC_OK;
}
```

### Operation Implementation
```c
static Bp_EC my_filter_flush(Filter_t* self)
{
    MyFilter_t* f = (MyFilter_t*)self;
    
    /* Process any partial window */
    if (f->partial_samples > 0) {
        process_partial_window(f);
        f->partial_samples = 0;
    }
    
    return Bp_EC_OK;
}

static const char* my_filter_describe(const Filter_t* self)
{
    MyFilter_t* f = (MyFilter_t*)self;
    static char desc[256];
    snprintf(desc, sizeof(desc), 
             "MyFilter: window=%zu, processed=%zu samples", 
             f->window_size, f->samples_processed);
    return desc;
}
```

## Framework Integration

### No Branching Required
```c
/* Direct calls - no NULL checks needed */
Bp_EC rc = filter->ops.flush(filter);
if (rc != Bp_EC_OK) {
    /* Handle error */
}

/* Chain operations without checks */
Bp_EC shutdown_filter(Filter_t* f)
{
    Bp_EC rc;
    
    rc = f->ops.flush(f);         /* Always safe to call */
    if (rc != Bp_EC_OK) return rc;
    
    rc = f->ops.drain(f);         /* Always safe to call */
    if (rc != Bp_EC_OK) return rc;
    
    rc = f->ops.stop(f);          /* Always safe to call */
    if (rc != Bp_EC_OK) return rc;
    
    return Bp_EC_OK;
}
```

### Extending Default Behavior
```c
/* Filter can call default then add custom logic */
static Bp_EC fft_start(Filter_t* self)
{
    /* First do standard startup */
    Bp_EC rc = default_start(self);
    if (rc != Bp_EC_OK) return rc;
    
    /* Then do FFT-specific setup */
    FFTFilter* f = (FFTFilter*)self;
    f->fft_plan = fftw_plan_dft_1d(...);
    
    return Bp_EC_OK;
}
```

## Benefits

1. **No branching** - All ops always callable, eliminating NULL checks
2. **Code reuse** - Existing filt_* functions become defaults
3. **Performance** - No branch misprediction from NULL checks
4. **Backward compatibility** - Existing API continues to work
5. **Flexibility** - Per-instance behavior based on config
6. **Extensibility** - Easy to add new ops as needed

## Guidelines

1. **Always populate all ops** - Use defaults/no-ops, never NULL
2. **Override selectively** - Only implement ops that differ from default
3. **Reuse defaults** - Call default implementation then add custom logic
4. **Document behavior** - Especially for state-changing ops
5. **Thread safety** - Document which ops are thread-safe
6. **Error handling** - Return meaningful error codes

## Example Filter Types

### Simple Map Filter
- Implements: `describe`, `get_stats`
- Skips: `save_state`, `reconfigure` (stateless)

### FFT Filter  
- Implements: `flush`, `reset`, `describe`, `get_stats`
- Skips: `reconfigure` (FFT size is fixed)

### Adaptive Filter
- Implements: All ops including `reconfigure`, `save_state`
- Custom: Parameter adjustment interface

### Source Filter
- Implements: `describe`, `get_stats`, `reconfigure`
- Skips: `flush`, `drain` (no input to process)

## Future Considerations

1. **Async ops** - Some ops might benefit from async versions
2. **Batch ops** - Operating on multiple filters at once
3. **Introspection** - Discovering which ops are implemented
4. **Versioning** - Handling ops structure evolution

This design provides the benefits of polymorphism while maintaining bpipe's philosophy of simplicity and directness.