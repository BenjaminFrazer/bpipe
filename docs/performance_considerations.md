# Performance Considerations

This guide covers performance optimization strategies for bpipe2 filters and pipelines.

## Batch Size Selection

### Key Principles

1. **Larger batches**: Better throughput, higher latency
2. **Smaller batches**: Lower latency, more overhead
3. **Sweet spot**: Usually 64-512 samples for real-time processing

### Guidelines

```c
// Low-latency audio (< 10ms)
size_t batch_size = 64;    // ~1.5ms at 44.1kHz

// Balanced performance
size_t batch_size = 256;   // ~6ms at 44.1kHz

// High-throughput processing
size_t batch_size = 1024;  // ~23ms at 44.1kHz

// Offline/batch processing
size_t batch_size = 4096;  // ~93ms at 44.1kHz
```

### Calculating Optimal Batch Size

```c
// Consider cache line size (typically 64 bytes)
size_t samples_per_cache_line = 64 / sizeof(float);  // 16 for float

// Align to cache lines
size_t batch_size = samples_per_cache_line * n_cache_lines;

// Consider processing time
double processing_time_ms = measure_processing_time();
double target_latency_ms = 10.0;
size_t max_batch_size = (target_latency_ms / processing_time_ms) * current_batch_size;
```

## Buffer Configuration

### BatchBuffer Sizing

```c
// Number of batches in circular buffer
// Too small: producer/consumer contention
// Too large: wasted memory
size_t n_batches = 4;  // Good default

// For high-throughput
size_t n_batches = 8;

// For bursty data
size_t n_batches = 16;
```

### Memory Layout

```c
// Good: Contiguous memory for cache efficiency
typedef struct {
    float samples[256];     // Data in array
    uint64_t timestamp_ns;
    uint32_t period_ns;
} GoodBatch_t;

// Bad: Pointer indirection hurts cache
typedef struct {
    float* samples;        // Pointer requires extra memory access
    uint64_t timestamp_ns;
    uint32_t period_ns;
} BadBatch_t;
```

## CPU and Cache Optimization

### Cache-Friendly Access Patterns

```c
// Good: Sequential access
for (size_t i = 0; i < n_samples; i++) {
    output[i] = input[i] * gain;
}

// Bad: Strided access misses cache
for (size_t ch = 0; ch < n_channels; ch++) {
    for (size_t i = 0; i < n_samples; i++) {
        output[ch][i] = input[ch][i] * gain;
    }
}

// Better: Process in blocks
for (size_t i = 0; i < n_samples; i++) {
    for (size_t ch = 0; ch < n_channels; ch++) {
        output[ch][i] = input[ch][i] * gain;
    }
}
```

### SIMD Optimization

```c
// Enable vectorization
void process_samples(float* restrict output, 
                    const float* restrict input,
                    float gain, size_t n_samples) {
    // Compiler can vectorize this
    for (size_t i = 0; i < n_samples; i++) {
        output[i] = input[i] * gain;
    }
}

// Explicit SIMD (x86 SSE example)
#include <xmmintrin.h>
void process_samples_sse(float* output, const float* input, 
                        float gain, size_t n_samples) {
    __m128 gain_vec = _mm_set1_ps(gain);
    
    for (size_t i = 0; i < n_samples; i += 4) {
        __m128 in = _mm_load_ps(&input[i]);
        __m128 out = _mm_mul_ps(in, gain_vec);
        _mm_store_ps(&output[i], out);
    }
}
```

### CPU Affinity

```c
// Pin worker thread to specific CPU
void set_thread_affinity(pthread_t thread, int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    
    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
}

// In filter init
filter->cpu_affinity = 2;  // Use CPU 2

// In worker thread
if (filter->cpu_affinity >= 0) {
    set_thread_affinity(pthread_self(), filter->cpu_affinity);
}
```

## Real-Time Considerations

### Priority and Scheduling

```c
// Set real-time priority
void set_realtime_priority(pthread_t thread) {
    struct sched_param param;
    param.sched_priority = 80;  // 1-99, higher is more priority
    
    pthread_setschedparam(thread, SCHED_FIFO, &param);
}

// Memory locking to prevent page faults
void lock_memory(void) {
    mlockall(MCL_CURRENT | MCL_FUTURE);
}
```

### Avoiding System Calls

```c
// Bad: System calls in hot path
void* worker(void* arg) {
    while (running) {
        gettimeofday(&tv, NULL);  // System call!
        process_batch();
    }
}

// Good: Use TSC for timing
uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void* worker(void* arg) {
    uint64_t tsc_per_ns = calibrate_tsc();
    while (running) {
        uint64_t tsc = rdtsc();
        uint64_t ns = tsc / tsc_per_ns;
        process_batch();
    }
}
```

### Memory Allocation

```c
// Bad: Allocation in hot path
void process(Batch_t* batch) {
    float* temp = malloc(batch->n_samples * sizeof(float));  // Bad!
    // ...
    free(temp);
}

// Good: Pre-allocate
typedef struct {
    Filter_t base;
    float* temp_buffer;
    size_t temp_size;
} MyFilter_t;

void process(MyFilter_t* filter, Batch_t* batch) {
    // Use pre-allocated buffer
    memset(filter->temp_buffer, 0, batch->n_samples * sizeof(float));
    // ...
}
```

## Profiling and Measurement

### Built-in Timing

```c
typedef struct {
    Filter_t base;
    atomic_uint_fast64_t total_processing_ns;
    atomic_uint_fast64_t batch_count;
} TimedFilter_t;

void* timed_worker(void* arg) {
    TimedFilter_t* filter = (TimedFilter_t*)arg;
    
    while (atomic_load(&filter->base.running)) {
        uint64_t start = get_time_ns();
        
        process_batch();
        
        uint64_t elapsed = get_time_ns() - start;
        atomic_fetch_add(&filter->total_processing_ns, elapsed);
        atomic_fetch_add(&filter->batch_count, 1);
    }
}

double get_average_processing_time_us(TimedFilter_t* filter) {
    uint64_t total_ns = atomic_load(&filter->total_processing_ns);
    uint64_t count = atomic_load(&filter->batch_count);
    return count > 0 ? (total_ns / count) / 1000.0 : 0.0;
}
```

### Performance Counters

```c
// Use perf events for detailed analysis
#include <linux/perf_event.h>

// Measure cache misses, branch mispredictions, etc.
```

## Pipeline Optimization

### Minimize Data Copies

```c
// Bad: Unnecessary copy
void process(float* output, const float* input, size_t n) {
    float temp[1024];
    memcpy(temp, input, n * sizeof(float));  // Unnecessary!
    for (size_t i = 0; i < n; i++) {
        temp[i] *= 2.0f;
    }
    memcpy(output, temp, n * sizeof(float));
}

// Good: Process in-place when possible
void process(float* data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        data[i] *= 2.0f;
    }
}
```

### Pipeline Parallelism

```c
// Configure buffer depths for parallelism
// Deeper buffers allow more parallel execution

Source -> [####] -> Filter1 -> [####] -> Filter2 -> [####] -> Sink
           4 deep             4 deep              4 deep

// All stages can run concurrently
```

### Load Balancing

```c
// Monitor queue depths
size_t get_queue_depth(BatchBuffer_t* bb) {
    return (bb->tail - bb->head) & bb->mask;
}

// Adjust processing based on load
if (get_queue_depth(output_bb) > threshold) {
    // Output is backing up, slow down
    usleep(1000);
}
```

## Optimization Checklist

### Algorithm Level
- [ ] Use efficient algorithms (O(n) vs O(n²))
- [ ] Minimize operations per sample
- [ ] Combine multiple passes when possible
- [ ] Use lookup tables for expensive operations

### Memory Level
- [ ] Ensure cache-friendly access patterns
- [ ] Align data to cache lines
- [ ] Minimize pointer chasing
- [ ] Pre-allocate all memory
- [ ] Use memory pools for temporary buffers

### CPU Level
- [ ] Enable compiler optimizations (-O3)
- [ ] Use `restrict` keyword for pointers
- [ ] Ensure loop vectorization
- [ ] Consider SIMD intrinsics for hot paths
- [ ] Profile with `perf` to find bottlenecks

### System Level
- [ ] Set appropriate thread priorities
- [ ] Use CPU affinity for stable performance
- [ ] Disable frequency scaling for real-time
- [ ] Consider NUMA effects on multi-socket systems

### Pipeline Level
- [ ] Choose optimal batch sizes
- [ ] Configure appropriate buffer depths
- [ ] Minimize synchronization points
- [ ] Balance load across pipeline stages

## Performance Anti-Patterns

### 1. False Sharing

```c
// Bad: Multiple threads write to adjacent memory
struct {
    atomic_int counter1;  // Thread 1 writes
    atomic_int counter2;  // Thread 2 writes - same cache line!
} stats;

// Good: Pad to cache line
struct {
    atomic_int counter1;
    char pad1[60];  // Padding to 64 bytes
    atomic_int counter2;
    char pad2[60];
} stats;
```

### 2. Lock Contention

```c
// Bad: Global lock
pthread_mutex_t global_lock;
void process() {
    pthread_mutex_lock(&global_lock);
    // Long operation...
    pthread_mutex_unlock(&global_lock);
}

// Good: Fine-grained locking or lock-free
void process() {
    // Use atomic operations or per-object locks
}
```

### 3. Excessive Branching

```c
// Bad: Unpredictable branches
for (i = 0; i < n; i++) {
    if (data[i] > threshold) {  // Data-dependent branch
        output[i] = process_high(data[i]);
    } else {
        output[i] = process_low(data[i]);
    }
}

// Good: Branchless
for (i = 0; i < n; i++) {
    float high = process_high(data[i]);
    float low = process_low(data[i]);
    int mask = data[i] > threshold;
    output[i] = mask * high + (1 - mask) * low;
}
```

## Benchmarking Guidelines

1. **Warm up** before measuring (fill caches, JIT, etc.)
2. **Measure multiple runs** and report statistics
3. **Control for external factors** (CPU frequency, other processes)
4. **Test with realistic data** and batch sizes
5. **Profile before optimizing** to find actual bottlenecks
6. **Measure latency AND throughput**
7. **Test on target hardware** (results vary by CPU)

```c
// Example benchmark
void benchmark_filter(Filter_t* filter, size_t iterations) {
    // Warmup
    for (size_t i = 0; i < 100; i++) {
        run_filter(filter);
    }
    
    // Measure
    uint64_t times[iterations];
    for (size_t i = 0; i < iterations; i++) {
        uint64_t start = get_time_ns();
        run_filter(filter);
        times[i] = get_time_ns() - start;
    }
    
    // Analyze
    qsort(times, iterations, sizeof(uint64_t), compare_uint64);
    printf("Median: %.2f us\n", times[iterations/2] / 1000.0);
    printf("P99: %.2f us\n", times[iterations*99/100] / 1000.0);
}
```