# Map Filter Extended Test Specifications

## 1. Performance Metrics Test Suite

### Test: `test_map_throughput_benchmark`

**Purpose**: Measure maximum sustainable throughput under various conditions

**Test Cases**:
1. **Identity transform throughput**
   - Buffer config: 1024 samples/batch, 127 batch ring
   - Run for 10,000 batches
   - Measure: batches/sec, samples/sec, CPU usage
   - Expected: >1M samples/sec on modern hardware

2. **Complex transform throughput** (e.g., sqrt + scale + offset)
   - Same buffer config
   - Compare overhead vs identity
   - Expected: 70-90% of identity throughput

3. **Small batch overhead**
   - Buffer config: 16 samples/batch
   - Measure per-batch overhead
   - Expected: Linear scaling with batch count

**Implementation**:
```c
void test_map_throughput_benchmark(void) {
    // Setup high-resolution timer
    struct timespec start, end;
    
    // Configure for maximum throughput
    BatchBuffer_config perf_config = {
        .dtype = DTYPE_FLOAT,
        .overflow_behaviour = OVERFLOW_DROP_TAIL,  // Don't block
        .ring_capacity_expo = 7,   // 127 batches
        .batch_capacity_expo = 10, // 1024 samples
    };
    
    // Run test
    clock_gettime(CLOCK_MONOTONIC, &start);
    // Process N batches...
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Calculate metrics
    double elapsed = timespec_diff(&end, &start);
    double batches_per_sec = n_batches / elapsed;
    double samples_per_sec = n_samples / elapsed;
    
    // Report results
    TEST_PERFORMANCE("Identity throughput", samples_per_sec, 1000000.0);
}
```

### Test: `test_map_latency_measurement`

**Purpose**: Measure end-to-end latency through filter pipeline

**Test Cases**:
1. **Single filter latency**
   - Measure time from submit to output availability
   - Use timestamps in batch headers
   - Expected: <1ms for identity transform

2. **Cascade latency**
   - Measure through 5-stage cascade
   - Track cumulative latency
   - Expected: Linear scaling with stages

**Metrics to Track**:
- Min/max/average latency
- 99th percentile latency
- Latency variance/jitter

## 2. Concurrent Producers Test Suite

### Test: `test_multiple_producers_single_map`

**Purpose**: Verify correct operation with multiple producers feeding one filter

**Architecture**:
```
Producer1 ─┐
Producer2 ─┼─→ Mux → Map Filter → Consumer
Producer3 ─┘
```

**Test Cases**:
1. **Round-robin producers**
   - 3 producer threads, each submitting tagged data
   - Verify all data processed in correct order
   - Test backpressure handling

2. **Competing producers**
   - Producers racing to submit batches
   - Verify no data loss or corruption
   - Test fairness of access

**Implementation**:
```c
typedef struct {
    int producer_id;
    Batch_buff_t* target;
    size_t batches_to_produce;
    volatile bool* running;
} producer_info_t;

void* tagged_producer(void* arg) {
    producer_info_t* info = (producer_info_t*)arg;
    
    for (size_t i = 0; i < info->batches_to_produce; i++) {
        Batch_t* batch = bb_get_head(info->target);
        // Fill with tagged data: [producer_id, sequence, data...]
        batch->data[0] = (float)info->producer_id;
        batch->data[1] = (float)i;
        // ... fill rest
        bb_submit(info->target, timeout);
    }
    return NULL;
}

void test_multiple_producers_single_map(void) {
    // Create mux buffer to collect from multiple producers
    Batch_buff_t mux_buffer;
    
    // Start 3 producer threads
    pthread_t producers[3];
    producer_info_t producer_info[3];
    
    // Create map filter consuming from mux
    Map_filt_t filter;
    filt_sink_connect(&filter.base, 0, &mux_buffer);
    
    // Verify all tagged data arrives in output
}
```

### Test: `test_concurrent_producer_consumer_chains`

**Purpose**: Test multiple independent filter chains running concurrently

**Architecture**:
```
Chain1: Producer1 → Map1 → Consumer1
Chain2: Producer2 → Map2 → Consumer2  
Chain3: Producer3 → Map3 → Consumer3
```

**Verification**:
- Each chain processes independently
- No cross-contamination
- System resources shared fairly
- Aggregate throughput scales appropriately

## 3. Mixed Batch Size Cascade Tests

### Test: `test_large_to_small_batch_cascade`

**Purpose**: Test data flow from large batches to small batches

**Configuration**:
```c
// Stage 1: Large batches
BatchBuffer_config large_config = {
    .dtype = DTYPE_FLOAT,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .ring_capacity_expo = 5,   // 31 batches
    .batch_capacity_expo = 10, // 1024 samples
};

// Stage 2: Small batches  
BatchBuffer_config small_config = {
    .dtype = DTYPE_FLOAT,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .ring_capacity_expo = 7,  // 127 batches (more buffers for same memory)
    .batch_capacity_expo = 6, // 64 samples
};
```

**Test Scenarios**:
1. **Perfect divisibility** (1024/64 = 16 small batches)
   - Verify timing distribution across small batches
   - Check data continuity

2. **Partial batch handling**
   - Submit partially filled large batches
   - Verify correct handling at boundaries

### Test: `test_small_to_large_batch_cascade`

**Purpose**: Test data accumulation from small to large batches

**Challenge**: Map filter expects 1:1 mapping, but buffer sizes differ

**Test Approach**:
```c
void test_small_to_large_batch_cascade(void) {
    // Small → Large requires accumulation logic
    // This exposes architectural assumption in map filter
    
    // Test 16 small batches (64 samples each) → 1 large batch (1024 samples)
    // Verify:
    // - Data ordering preserved
    // - Timing of first small batch applied to large batch
    // - Backpressure when large buffer full
}
```

### Test: `test_mixed_ring_sizes_backpressure`

**Purpose**: Test backpressure propagation with mismatched ring sizes

**Configuration**:
```c
// Stage 1: Few large batches
BatchBuffer_config few_large = {
    .ring_capacity_expo = 3,   // 7 batches only
    .batch_capacity_expo = 10, // 1024 samples each
};

// Stage 2: Many small batches
BatchBuffer_config many_small = {
    .ring_capacity_expo = 7,   // 127 batches  
    .batch_capacity_expo = 6,  // 64 samples each
};
```

**Test Scenarios**:
1. **Fast producer, slow consumer**
   - Producer fills large buffers quickly
   - Small buffer stage can buffer more batches
   - Test smooth flow despite mismatch

2. **Bursty producer**
   - Burst of 10 large batches
   - Verify system handles without loss
   - Test recovery after burst

### Test: `test_cascade_size_progression`

**Purpose**: Test cascade with progressively changing batch sizes

**Configuration**:
```
Stage1: 1024 samples → Stage2: 256 samples → Stage3: 64 samples → Output
```

**Verification**:
- Data integrity through all stages
- Timing preservation rules
- Performance characteristics
- Memory usage patterns

## Implementation Considerations

### Performance Test Helpers
```c
typedef struct {
    double min;
    double max;
    double sum;
    double sum_sq;
    size_t count;
} perf_stats_t;

void perf_stats_update(perf_stats_t* stats, double value);
double perf_stats_mean(const perf_stats_t* stats);
double perf_stats_stddev(const perf_stats_t* stats);

#define TEST_PERFORMANCE(name, actual, expected) \
    TEST_ASSERT_MESSAGE(actual > expected * 0.8, \
        "Performance below threshold: " name)
```

### Concurrent Test Helpers
```c
typedef struct {
    pthread_mutex_t mutex;
    size_t total_batches;
    size_t* producer_counts;
    int n_producers;
} producer_coordinator_t;

void coordinator_init(producer_coordinator_t* coord, int n_producers);
void coordinator_record_batch(producer_coordinator_t* coord, int producer_id);
void coordinator_verify_fairness(producer_coordinator_t* coord, float tolerance);
```

### Size Mismatch Helpers
```c
// Helper to verify data continuity across size boundaries
void verify_continuous_sequence(float* data, size_t start_value, size_t count);

// Helper to calculate timing distribution
void calculate_timing_distribution(Batch_t* small_batches, int count, 
                                  uint64_t original_timestamp);
```

## Test Priorities

1. **High Priority**: Mixed batch size cascades
   - Exposes architectural assumptions
   - Common in real applications
   - Currently untested

2. **Medium Priority**: Performance metrics
   - Important for optimization
   - Regression detection
   - Baseline establishment

3. **Lower Priority**: Concurrent producers
   - Less common use case for map filter
   - More relevant for mux/demux filters
   - Still valuable for stress testing

## Success Criteria

1. **Performance Tests**
   - Establish baseline metrics
   - Detect >10% performance regressions
   - Document hardware dependencies

2. **Concurrent Tests**
   - No data loss under contention
   - Fair resource allocation
   - Proper synchronization

3. **Size Mismatch Tests**
   - Data integrity maintained
   - Predictable timing behavior
   - Graceful backpressure handling