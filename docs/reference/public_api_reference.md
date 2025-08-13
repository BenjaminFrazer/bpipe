# Public API Reference

This document provides a comprehensive reference for the public APIs in the bpipe2 framework, focusing on batch buffer operations and filter lifecycle management.

## Breaking Changes

### Removal of tail field from Batch_t (v2.0)
The `tail` field has been removed from the `Batch_t` structure to simplify the architecture:
- **Before**: Batches had both `head` and `tail` indices for partial consumption tracking
- **After**: Batches only have a `head` index; data always starts at index 0
- **Migration**: Filters that need partial consumption tracking must maintain internal state

## Table of Contents
- [Breaking Changes](#breaking-changes)
- [Batch Buffer API](#batch-buffer-api)
- [Filter API](#filter-api)
- [Common Pitfalls](#common-pitfalls)
- [Thread Safety](#thread-safety)

## Batch Buffer API

### Core Operations

#### `bb_get_head(Batch_buff_t *buff)`
**Returns**: `Batch_t*` - Always returns a valid pointer to the current head batch  
**Never returns NULL**: This function returns a pointer to an element in the pre-allocated ring buffer

```c
// CORRECT - No NULL check needed
Batch_t* output = bb_get_head(f->sinks[0]);
output->batch_size = input->batch_size;

// INCORRECT - Unnecessary NULL check
Batch_t* output = bb_get_head(f->sinks[0]);
if (!output) {  // This check is unnecessary
    // Will never execute
}
```

#### `bb_get_tail(Batch_buff_t *buff, unsigned long timeout_us, Bp_EC *err)`
**Returns**: `Batch_t*` - Pointer to tail batch or NULL on timeout/error  
**Error parameter**: `err` is set to the error code (Bp_EC_OK, Bp_EC_TIMEOUT, Bp_EC_STOPPED, etc.)  
**Can return NULL**: Returns NULL if no batch is available within the timeout period or on error

```c
// CORRECT - Must check for NULL and handle error codes
Bp_EC err;
Batch_t* input = bb_get_tail(f->sources[0], TIMEOUT_US, &err);
if (!input) {
    if (err == Bp_EC_STOPPED) {
        break;  // Buffer was stopped
    }
    // Handle timeout or other error - this is normal behavior
    continue;
}

// INCORRECT - Missing NULL check
Bp_EC err;
Batch_t* input = bb_get_tail(f->sources[0], TIMEOUT_US, &err);
input->batch_size = 100;  // Potential segfault if timeout occurred
```

#### `bb_submit(Batch_buff_t *buff)`
**Returns**: `Bp_EC` - Error code (Bp_EC_OK on success)  
**Purpose**: Advances the head pointer after filling a batch

```c
Batch_t* output = bb_get_head(sink_buffer);
// Fill output batch...
output->batch_size = processed_samples;
output->t_ns = timestamp;

Bp_EC err = bb_submit(sink_buffer);
if (err != Bp_EC_OK) {
    // Handle error - buffer might be full
}
```

#### `bb_del_tail(Batch_buff_t *buff)`
**Returns**: `Bp_EC` - Error code  
**Purpose**: Advances the tail pointer after consuming a batch

```c
Batch_t* input = bb_get_tail(source_buffer, TIMEOUT_US);
if (input) {
    // Process input...
    bb_del_tail(source_buffer);  // Mark batch as consumed
}
```

### Buffer State Queries

#### `bb_occupancy(Batch_buff_t *buff)`
**Returns**: `size_t` - Number of batches currently in the buffer

```c
size_t batches_waiting = bb_occupancy(buffer);
if (batches_waiting > threshold) {
    // Buffer is getting full, maybe log a warning
}
```

#### `bb_batch_size(Batch_buff_t *buff)`
**Returns**: `size_t` - Maximum number of samples per batch

```c
size_t max_samples = bb_batch_size(buffer);
// Use this to validate batch_size fields
```

#### `bb_getdatawidth(SampleDtype_t stype)`
**Returns**: `size_t` - Size in bytes of the sample type

```c
size_t sample_size = bb_getdatawidth(DTYPE_FLOAT);  // Returns 4
size_t buffer_bytes = sample_size * batch_size;
```

### Buffer Lifecycle

#### Starting and Stopping Buffers
Buffers MUST be started before use and stopped before destruction:

```c
// Initialization
Batch_buff_t* buffer = f->sources[0];

// Start buffer (required before any operations)
bb_start(buffer);

// ... use buffer ...

// Stop buffer (required before deinit)
bb_stop(buffer);
```

#### Force Return Mechanism
The force return mechanism allows filters to unblock threads waiting on buffer operations during shutdown:

#### `bb_force_return_head(Batch_buff_t *buff, Bp_EC error_code)`
**Purpose**: Forces any thread blocked on `bb_get_head()` to return with specified error  
**When**: Called during filter shutdown to unblock producers

```c
// In filt_stop() to unblock upstream filters
bb_force_return_head(input_buffer, Bp_EC_FILTER_STOPPING);
```

#### `bb_force_return_tail(Batch_buff_t *buff, Bp_EC error_code)`
**Purpose**: Forces any thread blocked on `bb_get_tail()` to return with specified error  
**When**: Called during filter shutdown to unblock consumers

```c
// In filt_stop() to unblock this filter if waiting for data
bb_force_return_tail(input_buffer, Bp_EC_FILTER_STOPPING);
```

## Filter API

### Filter Lifecycle

#### `filt_init(Filter_t* f, void* config)`
**Purpose**: Initialize filter with configuration  
**When**: Called once before filter is started

```c
Map_config_t config = {
    .map_fcn = my_processing_function
};

Filter_t* filter = (Filter_t*)map_filter;
Bp_EC err = filter->ops->init(filter, &config);
if (err != Bp_EC_OK) {
    // Handle initialization error
}
```

#### `filt_start(Filter_t* f)`
**Purpose**: Start filter worker thread  
**Prerequisite**: Filter must be initialized

```c
err = filter->ops->start(filter);
// Filter worker thread is now running
```

#### `filt_stop(Filter_t* f)`
**Purpose**: Signal worker thread to stop  
**Note**: Does not wait for thread to finish

```c
filter->ops->stop(filter);
// Worker thread will exit at next check point
```

#### `filt_deinit(Filter_t* f)`
**Purpose**: Clean up filter resources  
**Prerequisite**: Filter must be stopped

```c
filter->ops->deinit(filter);
// Filter resources are freed
```

### Correct Filter Lifecycle Sequence

```c
// 1. Create and initialize
Filter_t* filter = create_my_filter();
filter->ops->init(filter, &config);

// 2. Connect buffers
filter->sources[0] = input_buffer;
filter->sinks[0] = output_buffer;

// 3. Start buffers
bb_start(input_buffer);
bb_start(output_buffer);

// 4. Start filter
filter->ops->start(filter);

// ... filter runs ...

// 5. Stop filter
filter->ops->stop(filter);

// 6. Stop buffers
bb_stop(input_buffer);
bb_stop(output_buffer);

// 7. Clean up
filter->ops->deinit(filter);
```

## Common Pitfalls

### 1. Unnecessary NULL checks on bb_get_head
```c
// ANTI-PATTERN - Don't do this
Batch_t* out = bb_get_head(buffer);
if (!out) {  // This will never be true
    return;
}

// CORRECT PATTERN
Batch_t* out = bb_get_head(buffer);
// out is always valid, proceed directly
```

### 2. Missing NULL checks on bb_get_tail
```c
// BUG - Missing NULL check
Batch_t* in = bb_get_tail(buffer, 1000);
process_batch(in);  // Crash if timeout!

// CORRECT
Batch_t* in = bb_get_tail(buffer, 1000);
if (in) {
    process_batch(in);
}
```

### 3. Not starting buffers before use
```c
// BUG - Buffer not started
Filter_t* f = create_filter();
f->sources[0] = create_buffer();
f->ops->start(f);  // Worker will hang or crash!

// CORRECT
Filter_t* f = create_filter();
f->sources[0] = create_buffer();
bb_start(f->sources[0]);  // Start buffer first!
f->ops->start(f);
```

### 4. Wrong teardown order
```c
// BUG - Deinit before stop
filter->ops->deinit(filter);  // Worker still running!
filter->ops->stop(filter);

// CORRECT
filter->ops->stop(filter);    // Stop first
filter->ops->deinit(filter);  // Then clean up
```

## Thread Safety

### Buffer Operations
- `bb_get_tail()` and `bb_del_tail()` - Safe from consumer thread only
- `bb_get_head()` and `bb_submit()` - Safe from producer thread only
- `bb_occupancy()` - Safe from any thread (atomic read)
- `bb_start()`/`bb_stop()` - NOT thread safe, call before/after worker threads

### Filter Operations
- Worker function runs in separate thread
- Use `BP_WORKER_ASSERT()` for error handling in workers
- Filter state should not be modified from outside worker thread
- Configuration is read-only after init

### Example Worker Pattern
```c
void my_worker(Filter_t* f) {
    while (f->running) {
        Batch_t* input = bb_get_tail(f->sources[0], TIMEOUT_US);
        if (!input) continue;
        
        Batch_t* output = bb_get_head(f->sinks[0]);
        // Process from input to output
        
        bb_submit(f->sinks[0]);
        bb_del_tail(f->sources[0]);
    }
}
```

## Error Handling

### Common Error Codes
- `Bp_EC_OK` - Success
- `Bp_EC_TIMEOUT` - Operation timed out (normal for bb_get_tail)
- `Bp_EC_BUFFER_EMPTY` - No data available
- `Bp_EC_NO_SPACE` - Buffer full
- `Bp_EC_STOPPED` - Operation on stopped buffer
- `Bp_EC_FILTER_STOPPING` - Filter is stopping (from bb_force_return)

### Worker Thread Assertions
Use `BP_WORKER_ASSERT` for unrecoverable errors in worker threads:

```c
void my_worker(Filter_t* f) {
    MyState* state = (MyState*)f;
    BP_WORKER_ASSERT(f, state->initialized, Bp_EC_NOT_INITIALIZED);
    
    // Assertion failure will:
    // 1. Log error with file:line
    // 2. Set filter error state
    // 3. Exit worker thread
}
```

## Available Filters

For a comprehensive list of all available filters including source filters (CSV Source, Signal Generator), processing filters (Map, Sample Aligner, Batch Matcher), sink filters (CSV Sink), and utility filters (Tee), see the [Available Filters Reference](available_filters.md).