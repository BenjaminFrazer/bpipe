# Bpipe2 Filter Implementation Guide

## Core Architecture

Every filter in bpipe2 follows a consistent pattern:
1. **Inheritance**: All filters inherit from `Filter_t` base struct
2. **Configuration**: Two-stage config (filter-specific → core config)
3. **Worker Thread**: Each filter runs processing in a dedicated thread
4. **Operations Interface**: Filters can override default operations
5. **Common Utilities**: Shared macros and functions in `bpipe/utils.h`

## Implementation Steps

### 1. Define Filter Structures (in `yourfilter.h`)

```c
#include "core.h"
#include "utils.h"  // Common utilities

// Filter-specific config
typedef struct _YourFilter_config_t {
    const char* name;               // Always expose
    BatchBuffer_config buff_config; // For input buffer
    long timeout_us;               // Always expose
    // Your filter-specific fields
} YourFilter_config_t;

// Filter struct (inherits Filter_t)
typedef struct _YourFilter_t {
    Filter_t base;  // MUST be first member
    // Your filter-specific state
} YourFilter_t;
```

### 2. Implement Worker Function

```c
void* yourfilter_worker(void* arg) {
    YourFilter_t* f = (YourFilter_t*)arg;
    Bp_EC err = Bp_EC_OK;
    
    // 1. Validate configuration
    WORKER_ASSERT(&f->base, f->config_field >= 0, Bp_EC_INVALID_CONFIG,
                  "Configuration field must be non-negative");
    WORKER_ASSERT(&f->base, f->base.sinks[0] != NULL, Bp_EC_NO_SINK,
                  "Filter requires connected sink");
    
    // 2. Main processing loop
    while (atomic_load(&f->base.running)) {
        // Get input batch
        Batch_t* input = bb_get_tail(&f->base.input_buffers[0], 
                                     f->base.timeout_us, &err);
        if (!input) {
            if (err == Bp_EC_TIMEOUT) continue;
            if (err == Bp_EC_STOPPED) break;
            break; // Real error
        }
        
        // Check for completion
        if (input->ec == Bp_EC_COMPLETE) {
            bb_del_tail(&f->base.input_buffers[0]);
            break;
        }
        
        // Validate input
        WORKER_ASSERT(&f->base, input->ec == Bp_EC_OK, input->ec,
                      "Input batch has error");
        
        // Process data...
        
        // Update metrics
        f->base.metrics.samples_processed += n_samples;
        f->base.metrics.n_batches++;
    }
    
    // 3. Error handling
    if (err != Bp_EC_OK && err != Bp_EC_STOPPED) {
        f->base.worker_err_info.ec = err;
        atomic_store(&f->base.running, false);
    }
    
    // 4. Cleanup (flush pending data)
    
    return NULL;
}
```

### 3. Implement Operations (Optional)

```c
static Bp_EC yourfilter_flush(Filter_t* self) {
    // Submit any pending output batches
    return Bp_EC_OK;
}

static Bp_EC yourfilter_describe(Filter_t* self, char* buffer, size_t size) {
    YourFilter_t* filter = (YourFilter_t*)self;
    snprintf(buffer, size, "YourFilter: %s\n...", self->name);
    return Bp_EC_OK;
}
```

### 4. Implement Init Function

```c
Bp_EC yourfilter_init(YourFilter_t* f, YourFilter_config_t config) {
    if (f == NULL) return Bp_EC_INVALID_CONFIG;
    
    // Build core config
    Core_filt_config_t core_config = {
        .name = config.name,
        .filt_type = FILT_T_YOUR_TYPE,
        .size = sizeof(YourFilter_t),
        .n_inputs = 1,  // Hardcode based on filter design
        .max_supported_sinks = 1,  // Hardcode based on filter design
        .buff_config = config.buff_config,
        .timeout_us = config.timeout_us,
        .worker = yourfilter_worker
    };
    
    // Initialize base filter
    Bp_EC err = filt_init(&f->base, core_config);
    if (err != Bp_EC_OK) return err;
    
    // Initialize filter-specific state
    
    // Override operations
    f->base.ops.flush = yourfilter_flush;
    f->base.ops.describe = yourfilter_describe;
    // ... other ops as needed
    
    return Bp_EC_OK;
}
```

## Key Design Principles

1. **Hardcode What's Fixed**: Filter type, I/O counts, size - set in init, not config
2. **Expose What Varies**: name, timeout_us, buffer configs - user configurable
3. **Batch Processing**: Handle partial batches, preserve timing metadata
4. **Error Handling**: Use WORKER_ASSERT in workers, proper cleanup
5. **Thread Safety**: Filter runs in single worker thread, mutex protects sink array

## Batch Processing

### Head Index Semantics
Filters work with a simplified batch model:
- **Input batches**: Data always starts at index 0
- **Output batches**: Must set `head` to number of valid samples
- **Sample count**: Always use `head` for number of samples

### Filter Obligations
```c
// CORRECT: Process valid samples from 0 to head
for (size_t i = 0; i < input->head; i++) {
    output->data[i] = process_sample(input->data[i]);
}

// When submitting output:
output->head = samples_written;  // Number of samples written
```

### Common Scenarios
1. **Full batch passthrough**: Copy data and preserve head
2. **Partial consumption**: Filters must track their own state internally
3. **Accumulation**: May need multiple input batches for one output
4. **Decimation**: Output fewer samples than input

## Common Patterns

### Input/Output Management
```c
// Use macros from utils.h
if (NEEDS_NEW_BATCH(batch)) {
    // Get new batch
}

// Get available samples to process
size_t n = MIN(input->head, output_space);

// Check if batch is full
if (BATCH_FULL(output, batch_size)) {
    // Submit batch
}

// Preserve timing on first samples
if (output->head == n) {
    output->t_ns = input->t_ns;
    output->period_ns = input->period_ns;
}
```

### Metrics Tracking
- `samples_processed`: Increment by actual samples processed
- `n_batches`: Increment only when submitting output batch

### Timeout Handling
- `timeout_us = 0`: Wait indefinitely
- `Bp_EC_TIMEOUT`: Normal, continue loop
- `Bp_EC_STOPPED`: Graceful shutdown
- Other errors: Stop filter

## Common Utilities (bpipe/utils.h)

The framework provides common utilities that should be used across all filters:

### Available Macros
- `MIN(a, b)` - Returns minimum of two values
- `MAX(a, b)` - Returns maximum of two values
- `NEEDS_NEW_BATCH(batch)` - Checks if batch needs replacement
- `BATCH_FULL(batch, size)` - Checks if batch is at capacity
- `WORKER_ASSERT(filter, condition, error_code, message)` - Worker thread assertion with error tracking

### WORKER_ASSERT Usage

The `WORKER_ASSERT` macro is crucial for proper error handling in worker threads:

```c
WORKER_ASSERT(filter_ptr, condition, error_code, error_message);
```

When the condition is false:
1. Sets `filter->worker_err_info.ec` to the error code
2. Captures the error message, file, function, and line number
3. Sets `filter->running = false` to stop the worker
4. Returns NULL from the worker function

Common usage patterns:
```c
// Configuration validation
WORKER_ASSERT(&f->base, f->batch_size > 0, Bp_EC_INVALID_CONFIG,
              "Batch size must be positive");

// Runtime requirements
WORKER_ASSERT(&f->base, f->base.sinks[0] != NULL, Bp_EC_NO_SINK,
              "Filter requires connected sink");

// State validation
WORKER_ASSERT(&f->base, f->accumulator != NULL, Bp_EC_ALLOC,
              "Failed to allocate accumulator buffer");
```

### Guidelines for utils.h
- **Add common patterns**: If a macro/function is used by 2+ filters, consider adding it
- **Keep it focused**: Only truly generic utilities belong here
- **Document well**: Each utility should have clear usage comments
- **Avoid duplication**: Check utils.h before defining local macros

Example usage:
```c
#include "utils.h"

// In worker function
WORKER_ASSERT(&f->base, config->timeout_us >= 0, Bp_EC_INVALID_CONFIG,
              "Timeout must be non-negative");

// In processing loop
if (NEEDS_NEW_BATCH(input)) {
    input = bb_get_tail(&filter->input_buffers[0], timeout, &err);
}

size_t samples_to_process = MIN(available_input, available_output);
```

This pattern ensures consistent behavior across all filters while allowing flexibility through the operations interface.