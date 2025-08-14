# Zero-Input Filter Patterns

This guide describes how to implement source filters (filters with zero inputs) correctly in bpipe2.

## Overview

Zero-input filters, also called source filters, generate data without any input connections. Common examples include:
- Signal generators
- File readers
- Network receivers
- Hardware interfaces
- Test data sources

## Key Characteristics

1. **No input connections**: `n_sources = 0` in filter_init
2. **Self-paced**: Filter determines when to produce data
3. **Timing responsibility**: Must set accurate timestamps
4. **Termination handling**: Must know when to stop

## Basic Pattern

```c
typedef struct {
    Filter_t base;
    // Configuration
    uint64_t period_ns;
    size_t max_samples;
    
    // State
    uint64_t next_timestamp_ns;
    size_t samples_generated;
} SourceFilter_t;

static void* source_worker(void* arg) {
    SourceFilter_t* src = (SourceFilter_t*)arg;
    BP_WORKER_ASSERT(src != NULL);
    
    while (atomic_load(&src->base.running)) {
        // Get output buffer
        Batch_t* output = bb_get_head(src->base.sinks[0]);
        if (!output) {
            usleep(1000);  // Avoid busy-waiting
            continue;
        }
        
        // Generate data
        size_t n_samples = output->capacity;
        generate_data(output->data, n_samples);
        
        // Set batch metadata
        output->t_ns = src->next_timestamp_ns;
        output->period_ns = src->period_ns;
        output->head = 0;
        output->tail = n_samples;
        output->ec = Bp_EC_OK;
        
        // Update state
        src->next_timestamp_ns += n_samples * src->period_ns;
        src->samples_generated += n_samples;
        
        // Submit batch
        Bp_EC err = bb_submit(src->base.sinks[0], 0);
        BP_WORKER_ASSERT(err == Bp_EC_OK);
        
        // Check termination
        if (src->max_samples && src->samples_generated >= src->max_samples) {
            atomic_store(&src->base.running, false);
            break;
        }
    }
    
    return NULL;
}
```

## Initialization Pattern

```c
Bp_EC source_filter_init(SourceFilter_t* src, SourceFilterCfg_t cfg) {
    if (!src) return Bp_EC_NULL_PTR;
    
    // Validate configuration
    if (cfg.period_ns == 0) return Bp_EC_INVALID_ARG;
    if (cfg.sample_rate_hz <= 0) return Bp_EC_INVALID_ARG;
    
    // Initialize base filter with 0 inputs
    Bp_EC err = filter_init(&src->base, "source_filter", 
                           FILT_T_MAP, 0, 1);  // 0 inputs!
    if (err != Bp_EC_OK) return err;
    
    // Store configuration
    src->period_ns = cfg.period_ns;
    src->max_samples = cfg.max_samples;
    
    // Initialize state
    src->next_timestamp_ns = 0;  // Or cfg.start_time_ns
    src->samples_generated = 0;
    
    // Set worker function
    src->base.worker = source_worker;
    
    return Bp_EC_OK;
}
```

## Common Patterns

### 1. File Reader Source

```c
typedef struct {
    Filter_t base;
    FILE* file;
    char* filename;
    uint64_t period_ns;
    uint64_t next_timestamp_ns;
    bool reached_eof;
} FileSource_t;

static void* file_source_worker(void* arg) {
    FileSource_t* src = (FileSource_t*)arg;
    BP_WORKER_ASSERT(src != NULL);
    BP_WORKER_ASSERT(src->file != NULL);
    
    while (atomic_load(&src->base.running) && !src->reached_eof) {
        Batch_t* output = bb_get_head(src->base.sinks[0]);
        if (!output) {
            usleep(1000);
            continue;
        }
        
        // Read data from file
        size_t n_read = fread(output->data, sizeof(float), 
                            output->capacity, src->file);
        
        if (n_read == 0) {
            // EOF or error
            if (feof(src->file)) {
                src->reached_eof = true;
                atomic_store(&src->base.running, false);
            }
            // Return unused buffer
            bb_return_head(src->base.sinks[0], 0);
            break;
        }
        
        // Set metadata
        output->t_ns = src->next_timestamp_ns;
        output->period_ns = src->period_ns;
        output->head = 0;
        output->tail = n_read;
        output->ec = Bp_EC_OK;
        
        src->next_timestamp_ns += n_read * src->period_ns;
        
        Bp_EC err = bb_submit(src->base.sinks[0], 0);
        BP_WORKER_ASSERT(err == Bp_EC_OK);
    }
    
    return NULL;
}
```

### 2. Real-Time Hardware Source

```c
typedef struct {
    Filter_t base;
    int device_fd;
    uint64_t period_ns;
    struct timespec next_time;
} HardwareSource_t;

static void* hardware_source_worker(void* arg) {
    HardwareSource_t* src = (HardwareSource_t*)arg;
    BP_WORKER_ASSERT(src != NULL);
    
    // Get initial time
    clock_gettime(CLOCK_MONOTONIC, &src->next_time);
    
    while (atomic_load(&src->base.running)) {
        Batch_t* output = bb_get_head(src->base.sinks[0]);
        if (!output) {
            usleep(100);
            continue;
        }
        
        // Wait until next period
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, 
                       &src->next_time, NULL);
        
        // Read from hardware (blocking read)
        ssize_t n_read = read(src->device_fd, output->data, 
                            output->capacity * sizeof(float));
        
        if (n_read <= 0) {
            // Handle error
            bb_return_head(src->base.sinks[0], 0);
            continue;
        }
        
        size_t n_samples = n_read / sizeof(float);
        
        // Set batch metadata with hardware timestamp
        output->t_ns = timespec_to_ns(&src->next_time);
        output->period_ns = src->period_ns;
        output->head = 0;
        output->tail = n_samples;
        output->ec = Bp_EC_OK;
        
        // Calculate next time
        add_ns_to_timespec(&src->next_time, 
                          n_samples * src->period_ns);
        
        Bp_EC err = bb_submit(src->base.sinks[0], 0);
        BP_WORKER_ASSERT(err == Bp_EC_OK);
    }
    
    return NULL;
}
```

### 3. Network Source

```c
typedef struct {
    Filter_t base;
    int socket_fd;
    uint64_t period_ns;
    uint64_t base_timestamp_ns;
    uint64_t samples_received;
} NetworkSource_t;

static void* network_source_worker(void* arg) {
    NetworkSource_t* src = (NetworkSource_t*)arg;
    BP_WORKER_ASSERT(src != NULL);
    
    // Set socket to non-blocking
    fcntl(src->socket_fd, F_SETFL, O_NONBLOCK);
    
    while (atomic_load(&src->base.running)) {
        Batch_t* output = bb_get_head(src->base.sinks[0]);
        if (!output) {
            usleep(1000);
            continue;
        }
        
        // Try to receive data
        ssize_t n_recv = recv(src->socket_fd, output->data,
                            output->capacity * sizeof(float), 0);
        
        if (n_recv < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available
                bb_return_head(src->base.sinks[0], 0);
                usleep(1000);
                continue;
            }
            // Real error
            BP_WORKER_ASSERT(false);
        }
        
        if (n_recv == 0) {
            // Connection closed
            atomic_store(&src->base.running, false);
            bb_return_head(src->base.sinks[0], 0);
            break;
        }
        
        size_t n_samples = n_recv / sizeof(float);
        
        // Calculate timestamp based on sample count
        output->t_ns = src->base_timestamp_ns + 
                      src->samples_received * src->period_ns;
        output->period_ns = src->period_ns;
        output->head = 0;
        output->tail = n_samples;
        output->ec = Bp_EC_OK;
        
        src->samples_received += n_samples;
        
        Bp_EC err = bb_submit(src->base.sinks[0], 0);
        BP_WORKER_ASSERT(err == Bp_EC_OK);
    }
    
    return NULL;
}
```

## Timing Strategies

### 1. Free-Running (As Fast As Possible)

```c
// Generate data as fast as downstream can consume
while (atomic_load(&running)) {
    Batch_t* output = bb_get_head(sink);
    if (!output) {
        usleep(1000);
        continue;
    }
    
    generate_data(output);
    output->t_ns = get_current_time_ns();  // Wall clock time
    bb_submit(sink, 0);
}
```

### 2. Fixed Rate

```c
// Generate at specific rate regardless of consumption
uint64_t period_ns = 1000000;  // 1ms
uint64_t next_time_ns = get_current_time_ns();

while (atomic_load(&running)) {
    // Wait until next period
    sleep_until_ns(next_time_ns);
    
    Batch_t* output = bb_get_head_timeout(sink, period_ns);
    if (!output) {
        // Skip this period if buffer full
        next_time_ns += period_ns;
        continue;
    }
    
    generate_data(output);
    output->t_ns = next_time_ns;
    bb_submit(sink, 0);
    
    next_time_ns += period_ns;
}
```

### 3. Adaptive Rate

```c
// Adjust rate based on buffer fullness
while (atomic_load(&running)) {
    size_t queue_depth = bb_get_queue_depth(sink);
    size_t max_depth = bb_get_capacity(sink);
    
    // Slow down if buffer is getting full
    if (queue_depth > max_depth * 0.75) {
        usleep(10000);  // 10ms pause
    }
    
    Batch_t* output = bb_get_head(sink);
    if (!output) {
        usleep(1000);
        continue;
    }
    
    generate_data(output);
    bb_submit(sink, 0);
}
```

## Termination Handling

### Finite Sources

```c
// Example: Generate exactly N samples
if (src->samples_generated >= src->max_samples) {
    // Signal completion
    atomic_store(&src->base.running, false);
    
    // Optionally send EOF marker
    if (output) {
        output->ec = Bp_EC_EOF;
        bb_submit(src->base.sinks[0], 0);
    }
    
    break;
}
```

### Infinite Sources

```c
// Run until explicitly stopped
while (atomic_load(&src->base.running)) {
    // Generate data forever
    // Shutdown only via filter_stop()
}
```

### Graceful Shutdown

```c
void source_filter_stop(SourceFilter_t* src) {
    // Set flag
    atomic_store(&src->base.running, false);
    
    // Wake up blocked operations
    if (src->device_fd >= 0) {
        // Send signal to interrupt blocking read
        pthread_kill(src->base.worker_thread, SIGUSR1);
    }
    
    // Join thread
    pthread_join(src->base.worker_thread, NULL);
}
```

## Error Handling

### Transient Errors

```c
// Retry on temporary failures
int retry_count = 0;
while (retry_count < MAX_RETRIES) {
    ssize_t n = read(fd, buffer, size);
    if (n > 0) {
        // Success
        break;
    }
    if (errno == EINTR || errno == EAGAIN) {
        // Temporary error, retry
        retry_count++;
        usleep(1000);
        continue;
    }
    // Permanent error
    BP_WORKER_ASSERT(false);
}
```

### Resource Errors

```c
// Handle resource exhaustion
Batch_t* output = bb_get_head_timeout(sink, 1000000000);
if (!output) {
    // Log warning but continue
    atomic_fetch_add(&src->dropped_batches, 1);
    continue;
}
```

## Testing Zero-Input Filters

```c
void test_source_filter(void) {
    // Create source
    SourceFilter_t source;
    SourceFilterCfg_t cfg = {
        .sample_rate_hz = 1000,
        .max_samples = 1000
    };
    CHECK_ERR(source_filter_init(&source, cfg));
    
    // Create sink to capture output
    TestSink_t sink;
    CHECK_ERR(test_sink_init(&sink, 2000));
    
    // Connect source -> sink
    CHECK_ERR(bp_connect(&source.base, 0, &sink.base, 0));
    
    // Start both
    CHECK_ERR(filter_start(&source.base));
    CHECK_ERR(filter_start(&sink.base));
    
    // Wait for completion
    while (atomic_load(&source.base.running)) {
        usleep(1000);
    }
    
    // Verify output
    TEST_ASSERT_EQUAL(1000, sink.captured_samples);
    
    // Check timestamps are monotonic
    for (size_t i = 1; i < sink.n_batches; i++) {
        TEST_ASSERT_TRUE(sink.batches[i].t_ns > 
                        sink.batches[i-1].t_ns);
    }
    
    // Cleanup
    CHECK_ERR(filter_stop(&source.base));
    CHECK_ERR(filter_stop(&sink.base));
    
    CHECK_ERR(source.base.worker_err_info.ec);
    CHECK_ERR(sink.base.worker_err_info.ec);
}
```

## Common Pitfalls

1. **Forgetting bb_submit()**: Always submit after filling buffer
2. **Wrong timestamp calculation**: Ensure monotonic timestamps
3. **Busy waiting**: Use sleep when no buffers available
4. **Not checking running flag**: Check frequently for responsive shutdown
5. **Blocking indefinitely**: Use timeouts or non-blocking I/O
6. **Not handling EOF/completion**: Set running=false when done
7. **Integer overflow**: Be careful with timestamp arithmetic

## Best Practices

1. **Set accurate timestamps**: Critical for downstream synchronization
2. **Handle backpressure**: Don't generate faster than consumed
3. **Provide clean shutdown**: Respond quickly to stop requests
4. **Document timing behavior**: Specify if real-time or batch
5. **Test edge cases**: Empty files, network disconnects, etc.
6. **Monitor performance**: Track dropped samples, delays
7. **Use appropriate buffer sizes**: Balance latency vs efficiency