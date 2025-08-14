# Buffer Architecture: Lock-Free Fast Path Design

## Overview

The bpipe2 buffer system implements a high-performance ring buffer designed for single-producer, single-consumer (SPSC) scenarios with an optimized lock-free fast path for maximum throughput.

## Core Design Principles

### Ring Buffer Structure
The buffer uses a circular array with head and tail pointers that wrap around using power-of-2 sizes for efficient modulo operations via bit masking.

![Buffer Indexing](batch_buffer_indexing.drawio.png)

### Cache Line Separation
The buffer structure separates producer and consumer fields into different 64-byte aligned cache lines to prevent false sharing:
- **Producer cache line**: Contains head pointer, total_batches, dropped_batches, and blocked_time_ns
- **Consumer cache line**: Contains only the tail pointer
- This prevents cache line bouncing between CPU cores when producer and consumer run on different cores

### Lock-Free Fast Path
When space is available (producer) or data is available (consumer), operations proceed without locks:
- **Producer**: Writes to head position and increments atomically using `memory_order_release`
- **Consumer**: Reads from tail position and increments atomically using `memory_order_release`
- Memory barriers ensure proper ordering between threads

### Blocking Slow Path
Locks are only acquired when waiting is required:
- **Buffer Full**: Producer blocks until consumer frees space
- **Buffer Empty**: Consumer blocks until producer adds data
- Condition variables enable efficient thread sleeping/waking

## Key Operations

### Submit (Producer) - `bb_submit()`
```
1. Check if full using atomic reads with memory_order_acquire on tail
2. If space available (fast path):
   - Increment head with memory_order_release
   - Update statistics atomically
   - Signal not_empty only if buffer was previously empty
3. If full:
   - For OVERFLOW_DROP: Update dropped_batches and return
   - For OVERFLOW_BLOCK: Acquire mutex, wait on not_full condition
```

### Delete (Consumer) - `bb_del()`
```
1. Check if empty using atomic reads with memory_order_acquire on head
2. If data available (fast path):
   - Increment tail with memory_order_release
   - Signal not_full only if buffer was previously full
3. If empty:
   - Return error if timeout is 0
   - Otherwise: Acquire mutex, wait on not_empty condition
```

## Performance Benefits

1. **Zero contention on fast path** - No locks when buffer has space/data
2. **Cache-friendly** - Producer and consumer work on different cache lines
3. **Predictable latency** - Lock-free path has consistent timing
4. **Scalable throughput** - Approaches memory bandwidth limits

## Implementation Details

### Structure Layout
```c
typedef struct _Bp_BatchBuffer {
    /* ... other fields ... */
    
    /* Producer-only fields - 64-byte aligned */
    struct {
        _Atomic size_t head;
        _Atomic uint64_t total_batches;
        _Atomic uint64_t dropped_batches;
        uint64_t blocked_time_ns;
    } producer __attribute__((aligned(64)));
    
    /* Consumer-only fields - 64-byte aligned */
    struct {
        _Atomic size_t tail;
    } consumer __attribute__((aligned(64)));
    
    /* Shared fields for slow path */
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    /* ... */
} Batch_buff_t;
```

### Key Implementation Points
- Head/tail pointers use C11 `_Atomic` types with explicit memory ordering
- Power-of-2 sizes enable efficient wraparound: `index = position & (size - 1)`
- Condition variables are signaled only on state transitions (empty→non-empty, full→non-full)
- Statistics (dropped_batches, total_batches) updated with `atomic_fetch_add()`
- Uses `__attribute__((aligned(64)))` for C99 compatibility instead of C11 `alignas`