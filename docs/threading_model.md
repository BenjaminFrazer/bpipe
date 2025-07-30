# Threading Model Documentation

This document describes the threading model, synchronization patterns, and best practices for multi-threaded filters in bpipe2.

## Overview

Each filter in bpipe2 can optionally have a worker thread that processes data asynchronously. The threading model is designed for:
- High-throughput data processing
- Low-latency operation
- Safe concurrent access to shared resources
- Clean shutdown semantics

## Worker Thread Lifecycle

### 1. Creation

Worker threads are created in `filter_start()`:

```c
Bp_EC filter_start(Filter_t* filter) {
    if (!filter->worker) return Bp_EC_OK;  // No worker thread
    
    atomic_store(&filter->running, true);
    
    int ret = pthread_create(&filter->worker_thread, NULL, filter->worker, filter);
    if (ret != 0) {
        atomic_store(&filter->running, false);
        return Bp_EC_PTHREAD;
    }
    
    return Bp_EC_OK;
}
```

### 2. Execution

Worker threads follow this pattern:

```c
static void* filter_worker(void* arg) {
    Filter_t* filter = (Filter_t*)arg;
    BP_WORKER_ASSERT(filter != NULL);
    
    // Main processing loop
    while (atomic_load(&filter->running)) {
        // Process data...
        
        // Check for shutdown periodically
        if (!atomic_load(&filter->running)) break;
    }
    
    return NULL;
}
```

### 3. Shutdown

Clean shutdown happens in `filter_stop()`:

```c
Bp_EC filter_stop(Filter_t* filter) {
    // Signal worker to stop
    atomic_store(&filter->running, false);
    
    // Force return on buffers to unblock any waiting operations
    // This ensures the worker thread can exit cleanly
    for (int i = 0; i < filter->n_input_buffers; i++) {
        // Unblock upstream filters writing to our input
        bb_force_return_head(filter->input_buffers[i], Bp_EC_FILTER_STOPPING);
        // Unblock our worker if reading from input
        bb_force_return_tail(filter->input_buffers[i], Bp_EC_FILTER_STOPPING);
    }
    
    // Unblock our worker if writing to output buffers
    for (int i = 0; i < filter->n_sinks; i++) {
        if (filter->sinks[i] != NULL) {
            bb_force_return_head(filter->sinks[i], Bp_EC_FILTER_STOPPING);
        }
    }
    
    // Wait for worker to finish
    if (filter->worker) {
        int ret = pthread_join(filter->worker_thread, NULL);
        if (ret != 0) return Bp_EC_PTHREAD;
    }
    
    return Bp_EC_OK;
}
```

## Synchronization Primitives

### Atomic Operations

Use C11 atomics for lock-free synchronization:

```c
#include <stdatomic.h>

typedef struct {
    Filter_t base;
    atomic_bool running;         // Use atomic_load/atomic_store
    atomic_size_t counter;       // Thread-safe counter
    // ...
} MyFilter_t;

// Reading
if (atomic_load(&filter->running)) {
    // ...
}

// Writing
atomic_store(&filter->running, false);

// Increment
atomic_fetch_add(&filter->counter, 1);
```

### Mutexes

For protecting critical sections:

```c
typedef struct {
    Filter_t base;
    pthread_mutex_t mutex;
    // Protected data...
} MyFilter_t;

// In init
pthread_mutex_init(&filter->mutex, NULL);

// In worker
pthread_mutex_lock(&filter->mutex);
// Critical section...
pthread_mutex_unlock(&filter->mutex);

// In deinit
pthread_mutex_destroy(&filter->mutex);
```

### Condition Variables

For efficient waiting:

```c
typedef struct {
    Filter_t base;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool data_ready;
} MyFilter_t;

// Producer
pthread_mutex_lock(&filter->mutex);
filter->data_ready = true;
pthread_cond_signal(&filter->cond);
pthread_mutex_unlock(&filter->mutex);

// Consumer
pthread_mutex_lock(&filter->mutex);
while (!filter->data_ready) {
    pthread_cond_wait(&filter->cond, &filter->mutex);
}
// Process data...
filter->data_ready = false;
pthread_mutex_unlock(&filter->mutex);
```

## Common Patterns

### 1. Zero-Input Source Filter

```c
static void* source_worker(void* arg) {
    SourceFilter_t* src = (SourceFilter_t*)arg;
    BP_WORKER_ASSERT(src != NULL);
    
    while (atomic_load(&src->base.running)) {
        // Get output buffer
        Batch_t* output = bb_get_head(src->base.sinks[0]);
        if (!output) {
            usleep(1000);  // Brief sleep to avoid busy-waiting
            continue;
        }
        
        // Generate data
        generate_data(output);
        
        // Submit batch
        Bp_EC err = bb_submit(src->base.sinks[0], 0);
        BP_WORKER_ASSERT(err == Bp_EC_OK);
        
        // Check termination condition
        if (should_terminate(src)) {
            atomic_store(&src->base.running, false);
            break;
        }
    }
    
    return NULL;
}
```

### 2. Transform Filter

```c
static void* transform_worker(void* arg) {
    TransformFilter_t* tf = (TransformFilter_t*)arg;
    BP_WORKER_ASSERT(tf != NULL);
    
    while (atomic_load(&tf->base.running)) {
        // Get input
        Bp_EC err;
        Batch_t* input = bb_get_tail(tf->base.sources[0], timeout_us, &err);
        if (!input) {
            // Handle force return during shutdown
            if (err == Bp_EC_FILTER_STOPPING) {
                break;
            }
            continue;  // Normal timeout
        }
        
        // Get output  
        Batch_t* output = bb_get_head(tf->base.sinks[0]);
        BP_WORKER_ASSERT(&tf->base, output != NULL, Bp_EC_GET_HEAD_NULL);
        
        // Process
        transform_batch(input, output);
        
        // Return input and submit output
        Bp_EC err = bb_return(tf->base.sources[0], 0);
        BP_WORKER_ASSERT(err == Bp_EC_OK);
        
        err = bb_submit(tf->base.sinks[0], 0);
        BP_WORKER_ASSERT(err == Bp_EC_OK);
    }
    
    return NULL;
}
```

### 3. Multi-Input Synchronization

```c
static void* sync_worker(void* arg) {
    SyncFilter_t* sf = (SyncFilter_t*)arg;
    BP_WORKER_ASSERT(sf != NULL);
    
    while (atomic_load(&sf->base.running)) {
        // Try to get aligned batches from all inputs
        Batch_t* inputs[MAX_INPUTS];
        bool all_available = true;
        
        for (int i = 0; i < sf->base.n_sources; i++) {
            inputs[i] = bb_peek_tail(sf->base.sources[i]);
            if (!inputs[i]) {
                all_available = false;
                break;
            }
        }
        
        if (!all_available) {
            usleep(1000);
            continue;
        }
        
        // Check time alignment
        if (!are_time_aligned(inputs, sf->base.n_sources)) {
            handle_misalignment(sf, inputs);
            continue;
        }
        
        // Process synchronized batches...
    }
    
    return NULL;
}
```

## Deadlock Prevention

### 1. Lock Ordering

Always acquire locks in the same order:

```c
// BAD - can deadlock
Thread1: lock(A); lock(B);
Thread2: lock(B); lock(A);

// GOOD - consistent ordering
Thread1: lock(A); lock(B);
Thread2: lock(A); lock(B);
```

### 2. Avoid Holding Locks During I/O

```c
// BAD
pthread_mutex_lock(&mutex);
Batch_t* batch = bb_get_tail(source);  // May block!
pthread_mutex_unlock(&mutex);

// GOOD
Batch_t* batch = bb_get_tail(source);
pthread_mutex_lock(&mutex);
// Process batch...
pthread_mutex_unlock(&mutex);
```

### 3. Use Timeouts

```c
// Instead of potentially blocking forever
Batch_t* batch = bb_get_tail(source);

// Use timeout version
Batch_t* batch = NULL;
Bp_EC err = bb_get_tail_timeout(source, &batch, 1000000000);  // 1 second
if (err == Bp_EC_TIMEOUT) {
    // Handle timeout...
}
```

## Best Practices

1. **Use Atomic Flags for Shutdown**: Always use `atomic_bool running` for clean shutdown.

2. **Avoid Busy Waiting**: Use `usleep()` or condition variables instead of tight loops.

3. **Check Running Flag Frequently**: Check `atomic_load(&running)` in loops to ensure responsive shutdown.

4. **Handle All Error Cases**: Use `BP_WORKER_ASSERT` for unrecoverable errors in workers.

5. **Initialize Synchronization Primitives**: Always initialize mutexes, condition variables, etc.

6. **Clean Up Resources**: Destroy mutexes and condition variables in deinit functions.

7. **Document Thread Safety**: Clearly document which fields are accessed by which threads.

## Common Pitfalls

1. **Forgetting to Signal Shutdown**: Not setting `running = false` causes `pthread_join` to hang forever.

2. **Not Checking Running Flag**: Worker threads that don't check the flag won't shut down cleanly.

3. **Race Conditions on Initialization**: Ensure all fields are initialized before starting worker thread.

4. **Blocking in Destructors**: Don't hold locks or block in cleanup code.

5. **Missing Memory Barriers**: Use atomic operations for flags shared between threads.

## Example: Complete Thread-Safe Filter

```c
typedef struct {
    Filter_t base;
    atomic_size_t processed_count;
    pthread_mutex_t stats_mutex;
    FilterStats_t stats;
} SafeFilter_t;

Bp_EC safe_filter_init(SafeFilter_t* sf) {
    Bp_EC err = filter_init(&sf->base, "safe_filter", FILT_T_MAP, 1, 1);
    if (err != Bp_EC_OK) return err;
    
    atomic_store(&sf->processed_count, 0);
    pthread_mutex_init(&sf->stats_mutex, NULL);
    memset(&sf->stats, 0, sizeof(FilterStats_t));
    
    sf->base.worker = safe_filter_worker;
    return Bp_EC_OK;
}

static void* safe_filter_worker(void* arg) {
    SafeFilter_t* sf = (SafeFilter_t*)arg;
    BP_WORKER_ASSERT(sf != NULL);
    
    while (atomic_load(&sf->base.running)) {
        // Process batches...
        
        // Update stats thread-safely
        pthread_mutex_lock(&sf->stats_mutex);
        sf->stats.total_processed++;
        pthread_mutex_unlock(&sf->stats_mutex);
        
        // Update atomic counter
        atomic_fetch_add(&sf->processed_count, 1);
    }
    
    return NULL;
}

void safe_filter_deinit(SafeFilter_t* sf) {
    pthread_mutex_destroy(&sf->stats_mutex);
    filter_deinit(&sf->base);
}
```