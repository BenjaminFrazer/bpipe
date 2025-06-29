# Buffer-Centric API Design Specification

## Overview

This specification proposes enhancing `Bp_BatchBuffer_t` to be a self-contained entity while maintaining filter ownership. Filters continue to own their input buffers, but buffer operations become more modular and testable through a cleaner API that operates directly on buffer pointers.

## Motivation

Current analysis reveals that most buffer operations only minimally depend on filter state:
- `Bp_submit_batch` and `Bp_delete_tail` don't use filter data at all
- `Bp_allocate` only needs: `overflow_behaviour`, `timeout`, `data_width`, `dtype`
- `Bp_head` only needs: `timeout`

By moving these configuration fields into the buffer structure, we can:
1. Simplify the API (no need to pass both filter and buffer pointers)
2. Make buffers self-contained for testing
3. Maintain performance (no extra indirection)
4. Keep the intuitive filter-owns-buffers model

## Proposed Architecture

### 1. Enhanced Buffer Structure

```c
typedef struct _Bp_BatchBuffer {
    /* Existing synchronization and storage */
    void* data_ring;
    Bp_Batch_t* batch_ring;
    size_t head;
    size_t tail;
    size_t ring_capacity_expo;
    size_t batch_capacity_expo;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    bool stopped;
    
    /* Configuration moved from filter */
    size_t data_width;
    SampleDtype_t dtype;
    OverflowBehaviour_t overflow_behaviour;
    unsigned long timeout_us;
    
    /* Optional: back-reference for debugging */
    char name[32];  // e.g., "filter1.input[0]"
    
    /* Optional: statistics */
    uint64_t total_batches;
    uint64_t dropped_batches;
    uint64_t blocked_time_ns;
} Bp_BatchBuffer_t;
```

### 2. Buffer-Centric API

#### Initialization
```c
/* Full initialization with all parameters */
Bp_EC BpBatchBuffer_Init(
    Bp_BatchBuffer_t* buffer,
    const char* name,              // Optional debug name
    size_t batch_size,
    size_t number_of_batches,
    size_t data_width,
    SampleDtype_t dtype,
    OverflowBehaviour_t overflow,
    unsigned long timeout_us
);

/* Configuration structure for cleaner initialization */
typedef struct {
    size_t batch_size;
    size_t number_of_batches;
    size_t data_width;
    SampleDtype_t dtype;
    OverflowBehaviour_t overflow_behaviour;
    unsigned long timeout_us;
    const char* name;
} BpBufferConfig_t;

Bp_EC BpBatchBuffer_InitFromConfig(
    Bp_BatchBuffer_t* buffer,
    const BpBufferConfig_t* config
);

/* Allocate and initialize in one step */
Bp_BatchBuffer_t* BpBatchBuffer_Create(const BpBufferConfig_t* config);
void BpBatchBuffer_Destroy(Bp_BatchBuffer_t* buffer);
```

#### Core Operations
```c
/* All operations work directly on buffers */
Bp_Batch_t BpBatchBuffer_Allocate(Bp_BatchBuffer_t* buf);
Bp_EC BpBatchBuffer_Submit(Bp_BatchBuffer_t* buf, const Bp_Batch_t* batch);
Bp_Batch_t BpBatchBuffer_Head(Bp_BatchBuffer_t* buf);
Bp_EC BpBatchBuffer_DeleteTail(Bp_BatchBuffer_t* buf);

/* Additional utility operations */
bool BpBatchBuffer_IsEmpty(const Bp_BatchBuffer_t* buf);
bool BpBatchBuffer_IsFull(const Bp_BatchBuffer_t* buf);
size_t BpBatchBuffer_Available(const Bp_BatchBuffer_t* buf);
size_t BpBatchBuffer_Capacity(const Bp_BatchBuffer_t* buf);

/* Control operations */
void BpBatchBuffer_Stop(Bp_BatchBuffer_t* buf);
void BpBatchBuffer_Reset(Bp_BatchBuffer_t* buf);

/* Configuration updates (thread-safe) */
Bp_EC BpBatchBuffer_SetTimeout(Bp_BatchBuffer_t* buf, unsigned long timeout_us);
Bp_EC BpBatchBuffer_SetOverflowBehaviour(Bp_BatchBuffer_t* buf, OverflowBehaviour_t behaviour);
```

### 3. Filter Integration

Filters continue to own their input buffers, but now work with self-contained buffer structures:

```c
/* Filter structure remains similar, buffers are owned */
typedef struct _DataPipe {
    bool running;
    TransformFcn_t* transform;
    Err_info worker_err_info;
    
    /* Buffers are owned by the filter */
    Bp_BatchBuffer_t input_buffers[MAX_SOURCES];  // Enhanced self-contained buffers
    
    /* Sources push to our input buffers via references */
    struct _DataPipe* sources[MAX_SOURCES];
    struct _DataPipe* sinks[MAX_SINKS];
    int n_sources;
    int n_sinks;
    
    pthread_t worker_thread;
    pthread_mutex_t filter_mutex;
} Bp_Filter_t;
```

### 4. Connection Model

The connection model remains simple - sources hold references to sink input buffers:

```c
// Initialize filters with enhanced buffers
BpFilterConfig config = {
    .transform = MyTransform,
    .dtype = DTYPE_FLOAT,
    .buffer_size = 128,
    .batch_size = 64,
    .number_of_batches_exponent = 6,
    .number_of_input_filters = 1,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .timeout_us = 1000000
};

BpFilter_Init(&sink_filter, &config);  // Creates self-contained input buffers

// Connect: source gets reference to sink's input buffer
Bp_add_sink(&source_filter, &sink_filter);

// Source operations use sink's buffer directly
BpBatchBuffer_Allocate(&sink_filter.input_buffers[0]);  // Clean API
```

## Benefits

### 1. **API Simplicity**
- Single buffer pointer parameter instead of filter + buffer
- Cleaner function signatures
- More intuitive operations

### 2. **Testability**
- Buffer operations can be unit tested in isolation
- No need to create filter structures for buffer tests
- Clear API boundaries

### 3. **Performance**
- No extra indirection (buffers still in filter struct)
- Direct inline operations maintained
- Better cache locality preserved

### 4. **Backward Compatibility**
- Filter ownership model unchanged
- Existing connection logic works
- Gradual migration possible

### 5. **Self-Contained Operations**
- Each buffer has its own configuration
- No need to access filter state for most operations
- Thread-safe by design

### 6. **Type Safety**
- Operations on typed buffer pointers
- Compile-time checking maintained
- Clear ownership model

## Implementation Challenges

### 1. **Configuration Duplication**
- Some config fields needed in both filter and buffer
- Need to keep them synchronized
- Initialization order matters

### 2. **Migration Effort**
- All buffer operations need updating
- Extensive testing required
- Documentation updates

### 3. **Size Increase**
- Each buffer now larger with config fields
- More memory usage with many buffers
- Cache line considerations

## Migration Strategy

### Phase 1: Parallel Implementation
1. Implement new buffer-centric API alongside existing API
2. Create adapters to use new buffers with old filters
3. Comprehensive testing of new implementation

### Phase 2: Incremental Migration
1. Update core components to use new API
2. Provide compatibility wrappers for existing code
3. Update documentation and examples

### Phase 3: Deprecation
1. Mark old API as deprecated
2. Update all remaining code
3. Remove old implementation

## Example Usage

### Simple Pipeline
```c
// Initialize filters - buffers created automatically
BpFilterConfig source_config = BP_CONFIG_FLOAT_STANDARD;
source_config.transform = SourceTransform;
BpFilter_Init(&source, &source_config);

BpFilterConfig sink_config = BP_CONFIG_FLOAT_STANDARD;
sink_config.transform = SinkTransform;
sink_config.overflow_behaviour = OVERFLOW_DROP;
sink_config.timeout_us = 100000;  // 100ms
BpFilter_Init(&sink, &sink_config);

// Connect pipeline
Bp_add_sink(&source, &sink);

// Clean buffer operations
Bp_Batch_t batch = BpBatchBuffer_Allocate(&sink.input_buffers[0]);
BpBatchBuffer_Submit(&sink.input_buffers[0], &batch);
```

### Direct Buffer Testing
```c
// Create standalone buffer for testing
BpBufferConfig_t config = {
    .batch_size = 64,
    .number_of_batches = 16,
    .data_width = sizeof(float),
    .dtype = DTYPE_FLOAT,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .timeout_us = 1000000,
    .name = "test_buffer"
};

Bp_BatchBuffer_t buffer;
BpBatchBuffer_InitFromConfig(&buffer, &config);

// Test buffer operations directly
Bp_Batch_t batch = BpBatchBuffer_Allocate(&buffer);
assert(batch.ec == Bp_EC_OK);
assert(batch.capacity == 64);

// Clean up
BpBatchBuffer_Deinit(&buffer);
```

### Worker Thread Usage
```c
void* MyWorker(void* arg) {
    Bp_Filter_t* filter = (Bp_Filter_t*)arg;
    
    while (filter->running) {
        // Clean API - just pass buffer pointer
        Bp_Batch_t input = BpBatchBuffer_Head(&filter->input_buffers[0]);
        if (input.ec != Bp_EC_OK) break;
        
        // Process data...
        
        BpBatchBuffer_DeleteTail(&filter->input_buffers[0]);
    }
    return NULL;
}
```

## Comparison with Current Architecture

| Aspect | Current | Proposed |
|--------|---------|----------|
| Ownership | Filters own buffers | Filters still own buffers |
| Buffer Config | Split between filter/buffer | Self-contained in buffer |
| API | `func(filter, buffer, ...)` | `func(buffer, ...)` |
| Performance | Direct inline access | Same - no extra indirection |
| Testing | Need filter for buffer ops | Can test buffers directly |
| Connection Model | Same | Same |

## Conclusion

This approach provides the best of both worlds:
- **Maintains filter ownership** for simplicity and performance
- **Makes buffers self-contained** for cleaner APIs and testing
- **Preserves inline performance** with no extra indirection
- **Enables gradual migration** without breaking changes

The key insight is that making buffers self-contained doesn't require making them independent. By moving configuration into the buffer structure, we get cleaner APIs while maintaining the proven filter-owns-buffers architecture.