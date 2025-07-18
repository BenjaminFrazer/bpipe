# Tee Filter Design Specification

## Overview

The Tee filter (T-filter) implements a Single Input Multiple Output (SIMO) pattern that takes a single input stream and distributes it to multiple output buffers. This is essential for scenarios where the same data needs to be processed by different downstream filters or saved to multiple destinations.

## Use Cases

1. **Data Recording**: Send data to both processing pipeline and storage
2. **Parallel Processing**: Same data processed by different algorithms
3. **Monitoring**: Tap into data stream for real-time monitoring without affecting main pipeline
4. **Redundancy**: Multiple copies for fault tolerance

## A) Configuration Options

### Core Configuration Structure

```c
typedef struct _Tee_config_t {
    const char* name;                          // Filter name for debugging
    BatchBuffer_config buff_config;            // Input buffer configuration
    size_t n_outputs;                          // Number of output sinks (2-MAX_SINKS)
    BatchBuffer_config* output_configs;        // Array of output buffer configs
    long timeout_us;                           // Timeout for buffer operations
    bool copy_data;                            // true=deep copy, false=reference only
} Tee_config_t;

typedef struct _Tee_filt_t {
    Filter_t base;
    bool copy_data;
    size_t n_outputs;
    size_t successful_writes[MAX_SINKS];       // Track successful writes per output
} Tee_filt_t;
```

### Configuration Constraints

- `n_outputs`: Must be between 2 and MAX_SINKS (10)
- `output_configs`: Must all specify same dtype (no automatic casting)
- `timeout_us`: 0 means infinite wait (following bpipe convention)
- Output 0 is prioritized for minimum latency (hot path)
- Overflow behavior determined by each output buffer's configuration

## B) File/Directory Structure

```
bpipe/
├── tee.h              # Tee filter public interface
├── tee.c              # Tee filter implementation
│
tests/
├── test_tee.c         # Comprehensive Unity test suite
│
examples/
├── tee_examples.c     # Usage examples and patterns
│
docs/
└── tee_filter.md      # User documentation
```

## C) Challenges/Limitations/Impacts

### Technical Challenges

1. **Memory Management**
   - Deep copy: Simple but memory intensive (N × data_size)
   - Reference counting: Complex but memory efficient
   - Zero-copy: Requires immutable batch guarantees

2. **Backpressure Handling**
   - Handled by each output buffer's overflow configuration
   - OVERFLOW_BLOCK buffers will cause Tee to wait
   - OVERFLOW_DROP buffers will handle dropping internally
   - No additional complexity in Tee filter itself

3. **Mixed Batch Sizes**
   - Output buffers may have different batch configurations
   - Need to handle partial batch scenarios
   - Leverage learnings from map filter implementation

4. **Error Propagation**
   - Partial submission failures
   - Timeout on individual outputs
   - Recovery strategies for failed outputs

### Current Limitations

1. **Data Type Consistency**: All outputs must have same dtype (future: add casting)
2. **No Dynamic Outputs**: Number of outputs fixed at initialization
3. **Memory Overhead**: Deep copy implementation multiplies memory usage
4. **Synchronous Distribution**: All outputs processed in single thread

### Architectural Impact

1. **Aligns with FILT_T_SIMO_TEE**: Uses existing filter type
2. **Maintains Push Model**: Source pushes to tee, tee pushes to multiple sinks
3. **Preserves Unidirectional References**: No back-references needed
4. **Compatible with Filter Ops**: Can implement standard operations interface

## D) Implementation Options

### Option 1: Deep Copy Implementation (Recommended for v1)

```c
void* tee_worker(void* arg) {
    Tee_filt_t* tee = (Tee_filt_t*)arg;
    Filter_t* f = &tee->base;
    
    while (f->running) {
        // Get input batch
        Bp_EC err;
        Batch_t* input = bb_get_tail(&f->input_buffers[0], f->timeout_us, &err);
        if (!input) {
            if (err == Bp_EC_TIMEOUT) continue;
            // Handle completion or error
            break;
        }
        
        // Priority copy to output 0 first (hot path)
        for (size_t i = 0; i < tee->n_outputs && i < f->n_sinks; i++) {
            if (!f->sinks[i]) continue;
            
            // Get output buffer (respects buffer's own timeout/overflow settings)
            Batch_t* output = bb_get_head(f->sinks[i]);
            if (!output) {
                // Buffer's overflow behavior determines what happens
                // OVERFLOW_BLOCK: we'll wait
                // OVERFLOW_DROP: buffer will handle dropping
                continue;
            }
            
            // Deep copy data
            size_t data_size = input->head * bb_get_data_width(&f->input_buffers[0]);
            memcpy(output->data, input->data, data_size);
            
            // Copy metadata
            output->head = input->head;
            output->tail = input->tail;
            output->t_ns = input->t_ns;
            output->period_ns = input->period_ns;
            output->batch_id = input->batch_id;
            
            err = bb_submit(f->sinks[i], f->timeout_us);
            if (err == Bp_EC_OK) {
                tee->successful_writes[i]++;
            }
        }
        
        // Remove input batch after distribution
        bb_del_tail(&f->input_buffers[0]);
        
        // Update metrics
        f->metrics.n_batches++;
        f->metrics.samples_processed += input->head;
    }
    
    // Shutdown: wait for all outputs to flush
    for (size_t i = 0; i < f->n_sinks; i++) {
        if (f->sinks[i]) {
            bb_flush(f->sinks[i], 0);  // Wait indefinitely for flush
        }
    }
    
    return NULL;
}

Bp_EC tee_init(Tee_filt_t* tee, Tee_config_t config) {
    // Validate all outputs have same dtype
    DTYPE_T expected_dtype = config.output_configs[0].dtype;
    for (size_t i = 1; i < config.n_outputs; i++) {
        if (config.output_configs[i].dtype != expected_dtype) {
            return Bp_EC_INCOMPATIBLE_TYPES;
        }
    }
    
    // Initialize base filter
    Core_filt_config_t core_config = {
        .name = config.name,
        .filt_type = FILT_T_SIMO_TEE,
        .size = sizeof(Tee_filt_t),
        .n_inputs = 1,
        .max_supported_sinks = config.n_outputs,
        .buff_config = config.buff_config,  // Use provided input buffer config
        .timeout_us = config.timeout_us,
        .worker = tee_worker
    };
    
    return filt_init(&tee->base, core_config);
}
```

### Option 2: Reference Counting (Future Enhancement)

```c
// Add to Batch_t structure
typedef struct _Batch_t {
    // ... existing fields ...
    atomic_int ref_count;      // Reference counter
    bool is_shared;            // Flag for shared batches
} Batch_t;

// Tee would increment ref_count instead of copying
// Consumers would decrement and free when count reaches 0
```

### Option 3: Ring Buffer Sharing (Future Enhancement)

- Multiple consumers read from same ring buffer
- Requires read cursors per consumer
- Complex synchronization but zero memory overhead

## E) Testing Strategy

### 1. Basic Functionality Tests

```c
void test_tee_dual_output(void) {
    // Test: 1 input → 2 identical outputs
    // Verify: Data integrity, timing preservation
}

void test_tee_max_outputs(void) {
    // Test: 1 input → MAX_SINKS outputs
    // Verify: All outputs receive data
}
```

### 2. Mixed Batch Size Tests

```c
void test_tee_different_batch_sizes(void) {
    // Input: 256 samples/batch
    // Output1: 256 samples/batch (1:1)
    // Output2: 64 samples/batch (1:4)
    // Output3: 1024 samples/batch (4:1)
    // Verify: Correct data distribution
}
```

### 3. Backpressure Tests

```c
void test_tee_blocking_output(void) {
    // Setup: Output 0 with OVERFLOW_BLOCK, Output 1 with OVERFLOW_DROP
    // Verify: Tee respects each buffer's configuration
    // Output 0 gets priority (written first)
}

void test_tee_mixed_overflow_behavior(void) {
    // Setup: Mix of blocking and dropping outputs
    // Verify: Each output behaves according to its buffer config
    // No data corruption across outputs
}
```

### 4. Performance Tests

```c
void test_tee_throughput_scaling(void) {
    // Measure throughput with 2, 4, 8 outputs
    // Compare to passthrough baseline
    // Verify linear scaling
}

void test_tee_memory_usage(void) {
    // Monitor memory allocation patterns
    // Verify no memory leaks
    // Check peak memory usage
}
```

### 5. Error Handling Tests

```c
void test_tee_partial_failure(void) {
    // Simulate output buffer allocation failure
    // Verify graceful degradation
}

void test_tee_timeout_handling(void) {
    // Test various timeout scenarios
    // Verify no data loss
}
```

### 6. Integration Tests

```c
void test_tee_in_pipeline(void) {
    // Source → Map → Tee → [Map1, Map2] → Sinks
    // Verify end-to-end data flow
}
```

## Implementation Phases

### Phase 1: Basic Deep Copy Implementation
- Single data type support (fail on mismatch)
- Same batch size for all outputs
- Priority copying to output 0
- Basic metrics collection

### Phase 2: Mixed Batch Size Support
- Leverage map filter batch size handling
- Support different output batch configurations
- Maintain timing accuracy

### Phase 3: Advanced Features
- Per-output metrics and monitoring
- Dynamic enable/disable of outputs
- Graceful shutdown with flush

### Phase 4: Optimization
- Reference counting implementation
- Zero-copy for read-only pipelines
- Performance profiling and tuning

## Performance Targets

- **Throughput**: >90% of memcpy bandwidth for 2 outputs
- **Latency**: <1μs overhead vs passthrough for output 0 (priority path)
- **CPU Usage**: Linear scaling with output count
- **Memory**: N × input_size for N outputs (deep copy)
- **Priority Impact**: Output 0 latency unaffected by slower outputs when using OVERFLOW_DROP on secondary outputs

## API Examples

### Basic Usage

```c
// Create tee with 2 outputs
// Output 0: Hot path with blocking (critical processing)
// Output 1: Monitoring with dropping (best-effort logging)
BatchBuffer_config out_configs[2] = {
    {
        .dtype = DTYPE_FLOAT, 
        .batch_capacity_expo = 8, 
        .ring_capacity_expo = 5,
        .overflow_behaviour = OVERFLOW_BLOCK  // Critical path
    },
    {
        .dtype = DTYPE_FLOAT, 
        .batch_capacity_expo = 8, 
        .ring_capacity_expo = 5,
        .overflow_behaviour = OVERFLOW_DROP   // Best-effort monitoring
    }
};

Tee_config_t config = {
    .name = "data_splitter",
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 8,
        .ring_capacity_expo = 5,
        .overflow_behaviour = OVERFLOW_BLOCK
    },
    .n_outputs = 2,
    .output_configs = out_configs,
    .timeout_us = 1000,
    .copy_data = true
};

Tee_filt_t tee;
tee_init(&tee, config);
```

### Pipeline Integration

```c
// Source → Tee → [Processing, Storage]
filt_sink_connect(&source, 0, &tee.base.input_buffers[0]);
filt_sink_connect(&tee.base, 0, &processor.base.input_buffers[0]);
filt_sink_connect(&tee.base, 1, &storage.base.input_buffers[0]);
```

## Success Criteria

1. **Functional**: All tests pass, no data loss or corruption
2. **Performance**: Meets throughput and latency targets
3. **Reliable**: Handles edge cases gracefully
4. **Maintainable**: Clean code following project patterns
5. **Documented**: Clear examples and usage guide

## Design Decisions

Based on architecture review, the following decisions have been made:

1. **No automatic casting** - Single responsibility principle. Fail on data type mismatch.
2. **Output 0 is prioritized** - Copied first to minimize latency on hot path. Other outputs may be used for logging/monitoring.
3. **Graceful shutdown** - Wait for all outputs to flush before closing.
4. **No redundant modes** - Blocking/dropping behavior is entirely driven by each output buffer's configuration. The Tee filter respects these settings rather than implementing its own overflow strategies.

## Conclusion

The Tee filter fills a critical gap in the bpipe2 architecture, enabling data distribution patterns essential for monitoring, redundancy, and parallel processing. Starting with a simple deep-copy implementation provides a solid foundation while leaving room for future optimizations.