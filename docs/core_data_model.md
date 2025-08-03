# Bpipe Core Data Model

## Overview

Bpipe is a real-time telemetry data processing framework built around a simple yet powerful data model. At its core, the framework uses a **push-based, filter-owned buffer** architecture where data flows through a directed acyclic graph (DAG) of processing components.

## Core Concepts

### Filter Types

The framework supports various filter types defined in `CORE_FILT_T`:
- **FILT_T_MAP** - Maps a function across all input samples to output samples
- **FILT_T_MATCHED_PASSTHROUGH** - Passes data through unchanged
- **FILT_T_CAST** - Converts between data types
- **FILT_T_MAP_STATE** - Map with state scratchpad
- **FILT_T_MAP_MP** - Parallel batch processing
- **FILT_T_SIMO_TEE** - Single input to multiple outputs
- **FILT_T_MIMO_SYNCRONISER** - Aligns batches to same sample times
- **FILT_T_MISO_ELEMENTWISE** - Multiple inputs to single output
- **FILT_T_OVERLAP_BATCHES** - Creates overlapping regions for convolution

### 1. Filters (`Filter_t`)

Filters are the primary processing units in bpipe. Each filter:
- **Owns its input buffers** - ensuring clear lifecycle management
- **Runs in its own thread** - enabling parallel processing
- **Transforms data** - via a user-defined worker function
- **Pushes to sink buffers** - maintaining a push-based data flow

```c
// Filter initialization with configuration
Core_filt_config_t config = {
    .name = "my_filter",
    .filt_type = FILT_T_MAP,
    .size = sizeof(Filter_t),
    .n_inputs = 1,
    .max_supported_sinks = MAX_SINKS,
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 6,    // 64 samples per batch
        .ring_capacity_expo = 8,     // 256 batches in ring
        .overflow_behaviour = OVERFLOW_BLOCK
    },
    .timeout_us = 1000000,  // 1 second timeout
    .worker = MyWorkerFunction
};
filt_init(&filter, config);
```

### 2. Batch Buffers (`Batch_buff_t`)

Buffers are self-contained ring buffers that:
- **Store batches of data** - not individual samples
- **Handle synchronization** - with built-in mutex and condition variables
- **Manage overflow** - via configurable block/drop behavior
- **Operate independently** - all operations work on buffer pointers
- **Optimize for cache performance** - producer/consumer fields in separate cache lines
- **Timeouts** - operations which add or removed from the buffer can block and take a timeout. Note that a timeout of 0 actually means infinite. 

```c
// Direct buffer operations
Batch_t *batch = bb_get_head(filter.input_buffers[0]);
// Process batch...
bb_submit(filter.input_buffers[0], timeout_us);
```

### 3. Batches (`Batch_t`)

Batches are the unit of data transfer containing:
- **Data pointer** - to the actual samples
- **Metadata** - timestamps (t_ns), period between samples (period_ns)
- **Head index** - number of valid samples in the batch
- **Error codes** - including completion signals
- **Batch ID** - for tracking and debugging
- **Optional metadata pointer** - for additional context

**IMPORTANT: Simplified Batch Model**
In bpipe2, batches now use a simplified model with only a `head` index:
- **`head`** = number of valid samples in the batch
- **Valid samples** = samples from index 0 to head-1
- **Empty batch** = `head == 0`
- **Full batch** = `head == batch_capacity`

```
Batch data array:
                         head
                          |
                          v
   [x][x][x][x][x][x][x][ ][ ][ ]
    ^------ used ------^
```

This simplification:
- Eliminates the tail index entirely
- Always assumes data starts at index 0
- Makes partial batch handling the responsibility of individual filters
- Reduces complexity and potential for off-by-one errors

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
filt_sink_connect(&source, 0, sink.input_buffers[0]);  // Source stores reference to sink's input buffer

// Worker thread only needs buffer count, not source references
for (int i = 0; i < filter->n_input_buffers; i++) {
    input_batches[i] = bb_get_tail(filter->input_buffers[i], timeout_us, &err);
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
filt_init(&source, source_config);
filt_init(&transform, transform_config);  
filt_init(&sink, sink_config);

// Connect: sources get references to sink input buffers
filt_sink_connect(&source, 0, transform.input_buffers[0]);      // source pushes to transform.input_buffers[0]
filt_sink_connect(&transform, 0, sink.input_buffers[0]);        // transform pushes to sink.input_buffers[0]

// Start processing
filt_start(&source);
filt_start(&transform);
filt_start(&sink);
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
- **Data types** - DTYPE_FLOAT, DTYPE_I32, DTYPE_U32
- **Type checking** - at connection time with detailed errors
- **Consistent sizing** - data_width derived from type via bb_getdatawidth()
- **Clear type enumeration** - with DTYPE_NDEF for uninitialized and DTYPE_MAX for bounds checking

## Performance Considerations

The architecture optimizes for:
- **Cache locality** - filters and their buffers in contiguous memory
- **Batch processing** - amortizes synchronization costs
- **Zero-copy potential** - batches reference buffer memory directly
- **Inline operations** - critical path functions are header-inlined
- **Lock-free operations** - fast path checks (bb_isempy_lockfree, bb_isfull_lockfree)
- **False sharing prevention** - producer/consumer fields in separate cache lines (64-byte aligned)
- **Atomic operations** - for head/tail indices with appropriate memory ordering

## Connection Requriements 

- Depending on the application filters may be more or less flexible about the data and batch configurations which they can be connected to.
- This is driven primerily by the simplicity & performance gain by not catering for corner cases.
- Filters may or may not tollerate the following:
    - **Incomplete input batches** - head != 2^batch_capacity_expo-1
    - **Input output capacity missmatch** - Output batch capacity != input batch capacity.
    - **Irregular Data** - Data does not hava a consistent sample rate.
    - **Different sink batch sizes** - 
    - **Un-aligned inputs** - Input timestamps & number of samples do not align.
Some filters may be more or less flexible about what out

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
8. **Worker-based architecture** - Each filter runs a Worker_t function in its own thread
9. **Exponential sizing** - Buffer capacities use power-of-2 sizes for efficient modulo operations

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
