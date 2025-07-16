# Bpipe Core Data Model

## Overview

Bpipe is a real-time telemetry data processing framework built around a simple yet powerful data model. At its core, the framework uses a **push-based, filter-owned buffer** architecture where data flows through a directed acyclic graph (DAG) of processing components.

## Core Concepts

### 1. Filters (`Filter_t`)

Filters are the primary processing units in bpipe. Each filter:
- **Owns its input buffers** - ensuring clear lifecycle management
- **Runs in its own thread** - enabling parallel processing
- **Transforms data** - via a user-defined transform function
- **Pushes to sink buffers** - maintaining a push-based data flow

```c
// Filter initialization with configuration
BpFilterConfig config = {
    .transform = MyTransformFunction,
    .dtype = DTYPE_FLOAT,
    .buffer_size = 128,
    .batch_size = 64,
    .number_of_batches_exponent = 6,
    .number_of_input_buffers = 1
};
BpFilter_Init(&filter, &config);
```

### 2. Batch Buffers (`Bp_BatchBuffer_t`)

Buffers are self-contained ring buffers that:
- **Store batches of data** - not individual samples
- **Handle synchronization** - with built-in mutex and condition variables
- **Manage overflow** - via configurable block/drop behavior
- **Operate independently** - all operations work on buffer pointers

```c
// Direct buffer operations (proposed enhancement)
Bp_Batch_t batch = BpBatchBuffer_Allocate(&filter.input_buffers[0]);
// Process batch...
BpBatchBuffer_Submit(&filter.input_buffers[0], &batch);
```

### 3. Batches (`Bp_Batch_t`)

Batches are the unit of data transfer containing:
- **Data pointer** - to the actual samples
- **Metadata** - timestamps, sequence numbers, data type
- **Head/tail indices** - for partial batch processing
- **Error codes** - including completion signals

#### Fixed-Rate Batch Design

A critical architectural decision in bpipe is that **all samples within a batch are assumed to have fixed, regular timing**. This is specified by:
- `t_ns` - timestamp of the first sample in the batch
- `period_ns` - fixed interval between samples (0 for irregular data)

This design optimizes for high-performance processing of regularly-sampled data (audio, video, radar, SDR) while still supporting irregular data through reduced batch sizes.

**Regular Data (Optimal Performance):**
```c
// 1000 Hz audio: 64 samples per batch
// Overhead: 1 batch header per 64 samples = ~1.6%
Batch {
    t_ns = 1000000,      // Start time
    period_ns = 1000,    // 1ms between samples
    data[64]             // 64 contiguous samples
}
```

**Irregular Data (Reduced Performance):**
```c
// Irregular sensor: 1 sample per batch
// Higher overhead but correct timing preserved
Batch { t_ns = 1000000, period_ns = 0, data[1] }
Batch { t_ns = 1002300, period_ns = 0, data[1] }  // 2.3ms later
Batch { t_ns = 1003100, period_ns = 0, data[1] }  // 0.8ms later
```

This trade-off ensures:
- **Maximum performance** for common high-rate, fixed-rate signals
- **Correct handling** of irregular data with individual timestamps
- **Simple processing** - no complex per-sample timestamp arrays
- **Clear performance model** - users can predict and optimize

## Data Flow Model

### Connection Architecture

```
┌─────────────┐         ┌─────────────┐         ┌─────────────┐
│   Source    │         │  Transform  │         │    Sink     │
│             │ push to │             │ push to │             │
│  [output]---┼────────>│ [input][out]┼────────>│  [input]    │
│             │         │             │         │             │
└─────────────┘         └─────────────┘         └─────────────┘
```

#### Unidirectional Reference Design

A key architectural decision in bpipe is the **unidirectional reference model** for filter connections:

- **Sources maintain sink references** - to know where to push data
- **Sinks maintain input buffer count** - to know how many inputs to process
- **No bidirectional references** - eliminates redundant state and complexity

**Rationale:**
```c
// Simple connection model
Bp_add_sink(&source, &sink);        // Source stores reference to sink
Bp_add_source(&sink, &source);      // Sink only increments n_input_buffers count

// Worker thread only needs buffer count, not source references
for (int i = 0; i < filter->n_input_buffers; i++) {
    input_batches[i] = Bp_head(filter, &filter->input_buffers[i]);
}
```

This design provides:
- **Reduced configuration complexity** - single connection call instead of dual calls
- **Eliminated inconsistent state** - cannot have mismatched source/sink references  
- **Clearer ownership model** - sources own sink references, sinks own buffer management
- **Simplified API** - fewer opportunities for user errors

**Trade-offs:**
- **Lost source identity verification** - cannot verify specific source during disconnection
- **Reduced debugging capability** - cannot enumerate connected sources from sink side
- **Simplified connection management** - but loses some validation features

The benefits significantly outweigh the costs for a push-based architecture where data flow direction is clear and sources are responsible for delivery.

Key principles:
1. **Filters own their input buffers** - no shared ownership complexity
2. **Sources push to sink buffers** - via references obtained during connection
3. **Data flows in batches** - amortizing synchronization overhead
4. **Backpressure is automatic** - buffers block or drop based on configuration

### Connection Example

```c
// Create pipeline: source -> transform -> sink
BpFilter_Init(&source, &source_config);
BpFilter_Init(&transform, &transform_config);  
BpFilter_Init(&sink, &sink_config);

// Connect: sources get references to sink input buffers
Bp_add_sink(&source, &transform);      // source pushes to transform.input_buffers[0]
Bp_add_sink(&transform, &sink);        // transform pushes to sink.input_buffers[0]

// Start processing
BpFilter_Start(&source);
BpFilter_Start(&transform);
BpFilter_Start(&sink);
```

## Buffer Management

### Self-Contained Design

Each buffer contains:
- **Ring buffer storage** - for both data and batch metadata
- **Synchronization primitives** - mutex, not_empty, not_full conditions
- **Configuration** - capacity, data type, overflow behavior
- **State tracking** - head/tail indices, stopped flag

This self-contained design enables:
- **Direct operations** - no need to pass filter context
- **Independent testing** - buffers can be tested in isolation
- **Clear interfaces** - operations work on typed buffer pointers

### Overflow Handling

Buffers support two overflow behaviors:
- **OVERFLOW_BLOCK** - Producer waits when buffer full (default)
- **OVERFLOW_DROP** - New data dropped when buffer full

## Threading Model

Each filter runs a worker thread that:
1. **Reads from input buffers** - with timeout support
2. **Calls transform function** - user-defined processing
3. **Writes to output buffers** - of connected sinks
4. **Handles completion** - propagates termination signals

Thread safety is ensured by:
- **Buffer-level locking** - each buffer has its own mutex
- **Filter mutex** - protects connection changes
- **Condition variables** - efficient waiting for data/space

## Type System

Strong typing throughout:
- **Data types** - DTYPE_FLOAT, DTYPE_INT, DTYPE_UNSIGNED
- **Type checking** - at connection time with detailed errors
- **Consistent sizing** - data_width derived from type

## Performance Considerations

The architecture optimizes for:
- **Cache locality** - filters and their buffers in contiguous memory
- **Batch processing** - amortizes synchronization costs
- **Zero-copy potential** - batches reference buffer memory directly
- **Inline operations** - critical path functions are header-inlined
- **Lock-free reads** - where possible in hot paths

## Usage Patterns

### Simple Pipeline
```c
// Most common: linear processing chain
source -> filter1 -> filter2 -> sink
```

### Fan-Out
```c
// One source, multiple sinks (automatic distribution)
source -> [sink1, sink2, sink3]
```

### Fan-In
```c
// Multiple sources, one sink (via multiple input buffers)
[source1, source2] -> combiner -> sink
```

### Complex DAG
```c
// Arbitrary directed acyclic graphs supported
source1 -> filter1 -> [filter3, filter4] -> sink
source2 -> filter2 -> filter3
```

## Key Design Decisions

1. **Filter-owned buffers** - Simplifies lifecycle, improves cache locality
2. **Push-based model** - Natural for streaming, good backpressure handling
3. **Batch-oriented** - Reduces overhead, enables vectorization
4. **Self-contained buffers** - Clean APIs while maintaining performance
5. **Configuration structs** - Extensible initialization without API breaks
6. **Fixed-rate batches** - Optimizes for regular data, handles irregular data correctly
7. **Unidirectional connections** - Sources track sinks, sinks track input count only

### Impact on Filter Design

The fixed-rate batch design influences how filters should be implemented:

**For Transform Filters:**
- Can assume all samples in a batch have regular spacing
- Use `period_ns` for time-dependent calculations
- Process entire batches with vectorized operations

**For Multi-Input Filters:**
- Cannot assume input timing alignment
- Should be preceded by synchronization filters when needed
- Focus on computation, not timing reconciliation

**For Sources with Irregular Data:**
- Configure with `batch_size = 1` for optimal latency
- Set `period_ns = 0` to indicate irregular timing
- Pre-allocate many small batches to reduce allocation overhead

This architecture balances simplicity, performance, and flexibility for real-time telemetry processing applications.
