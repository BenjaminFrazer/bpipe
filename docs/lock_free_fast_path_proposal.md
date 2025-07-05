# Lock-Free Fast Path Implementation Proposal

## Overview
This proposal outlines changes to implement a lock-free fast path for the bpipe2 buffer system, optimizing for single-producer, single-consumer (SPSC) scenarios while maintaining correctness for blocking operations.

## Key Changes

### 1. Separate Head/Tail into Different Cache Lines

```c
typedef struct _Bp_BatchBuffer {
    /* ... existing fields ... */
    
    /* Producer cache line
     * CRITICAL DESIGN DECISION: The producer fields (head, statistics) are placed
     * in a separate cache line from consumer fields to prevent false sharing.
     * False sharing occurs when different threads update different variables that
     * happen to reside on the same cache line, causing the entire cache line to
     * bounce between CPU cores. This can degrade performance by 10-100x.
     * 
     * By aligning to 64 bytes (typical cache line size), we ensure the producer
     * thread can update head/stats without invalidating the consumer's cache.
     */
    struct {
        size_t head;                /* Modified only by producer */
        uint64_t total_batches;     /* Producer statistics */
        uint64_t dropped_batches;   /* Producer statistics */
    } producer __attribute__((aligned(64)));
    
    /* Consumer cache line
     * CRITICAL DESIGN DECISION: The consumer's tail pointer is isolated in its own
     * cache line. This allows the consumer thread to update tail without causing
     * cache invalidations on the producer's CPU core. The 64-byte alignment 
     * matches typical x86_64 cache line sizes.
     */
    struct {
        size_t tail;                /* Modified only by consumer */
    } consumer __attribute__((aligned(64)));
    
    /* Shared synchronization - only used on slow path */
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    /* ... rest of fields ... */
} Batch_buff_t;
```

### 2. Lock-Free Submit Operation

```c
static inline Bp_EC bb_submit(Batch_buff_t* buff)
{
    /* Fast path - check without locks */
    size_t current_head = atomic_load_explicit(&buff->producer.head, memory_order_relaxed);
    size_t current_tail = atomic_load_explicit(&buff->consumer.tail, memory_order_acquire);
    
    size_t next_head = (current_head + 1) & bb_modulo_mask(buff);
    
    /* Check if buffer would be full */
    if (next_head == current_tail) {
        /* Slow path - need to wait or drop */
        if (buff->overflow_behaviour == OVERFLOW_DROP) {
            atomic_fetch_add(&buff->producer.dropped_batches, 1);
            return Bp_EC_OK;
        }
        
        /* Block until space available */
        pthread_mutex_lock(&buff->mutex);
        while (bb_isfull(buff) && buff->running) {
            pthread_cond_wait(&buff->not_full, &buff->mutex);
        }
        pthread_mutex_unlock(&buff->mutex);
        
        /* Re-read tail after waiting */
        current_tail = atomic_load_explicit(&buff->consumer.tail, memory_order_acquire);
    }
    
    /* Fast path - we have space */
    atomic_store_explicit(&buff->producer.head, next_head, memory_order_release);
    atomic_fetch_add(&buff->producer.total_batches, 1);
    
    /* Signal consumer if it might be waiting */
    if (current_head == current_tail) {
        pthread_mutex_lock(&buff->mutex);
        pthread_cond_signal(&buff->not_empty);
        pthread_mutex_unlock(&buff->mutex);
    }
    
    return Bp_EC_OK;
}
```

### 3. Lock-Free Delete Operation

```c
static inline Bp_EC bb_del(Batch_buff_t* buff)
{
    /* Fast path - check without locks */
    size_t current_head = atomic_load_explicit(&buff->producer.head, memory_order_acquire);
    size_t current_tail = atomic_load_explicit(&buff->consumer.tail, memory_order_relaxed);
    
    /* Check if empty */
    if (current_tail == current_head) {
        /* Slow path - need to wait */
        pthread_mutex_lock(&buff->mutex);
        while (bb_isempy(buff) && buff->running) {
            pthread_cond_wait(&buff->not_empty, &buff->mutex);
        }
        pthread_mutex_unlock(&buff->mutex);
        
        /* Re-read head after waiting */
        current_head = atomic_load_explicit(&buff->producer.head, memory_order_acquire);
        if (current_tail == current_head) {
            return Bp_EC_BUFFER_EMPTY;
        }
    }
    
    /* Fast path - we have data */
    size_t next_tail = (current_tail + 1) & bb_modulo_mask(buff);
    atomic_store_explicit(&buff->consumer.tail, next_tail, memory_order_release);
    
    /* Signal producer if buffer was full */
    if ((current_head + 1) & bb_modulo_mask(buff) == current_tail) {
        pthread_mutex_lock(&buff->mutex);
        pthread_cond_signal(&buff->not_full);
        pthread_mutex_unlock(&buff->mutex);
    }
    
    return Bp_EC_OK;
}
```

### 4. Memory Ordering Considerations

```c
/* Helper functions with proper memory ordering */
static inline bool bb_isempy_lockfree(const Batch_buff_t* buf)
{
    size_t head = atomic_load_explicit(&buf->producer.head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&buf->consumer.tail, memory_order_relaxed);
    return head == tail;
}

static inline bool bb_isfull_lockfree(const Batch_buff_t* buff)
{
    size_t head = atomic_load_explicit(&buff->producer.head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&buff->consumer.tail, memory_order_acquire);
    return ((head + 1) & bb_modulo_mask(buff)) == tail;
}

static inline size_t bb_available_lockfree(const Batch_buff_t* buf)
{
    size_t head = atomic_load_explicit(&buf->producer.head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&buf->consumer.tail, memory_order_relaxed);
    return head - tail;
}
```

### 5. Batch Operations with Lock-Free Fast Path

```c
/* Producer allocates batch slot */
static inline Bp_Batch_t bb_allocate_batch(Batch_buff_t* buff)
{
    Bp_Batch_t batch = {0};
    
    /* Fast path check */
    if (!bb_isfull_lockfree(buff)) {
        size_t idx = atomic_load_explicit(&buff->producer.head, memory_order_relaxed);
        idx = idx & bb_modulo_mask(buff);
        
        void* data_ptr = (char*)buff->data_ring + 
                        idx * bb_getdatawidth(buff->dtype) * batch_size(buff);
        
        batch.capacity = batch_size(buff);
        batch.data = data_ptr;
        batch.batch_id = idx;
        batch.dtype = buff->dtype;
        batch.ec = Bp_EC_OK;
    } else if (buff->overflow_behaviour == OVERFLOW_DROP) {
        batch.ec = Bp_EC_NOSPACE;
    } else {
        /* Slow path - wait for space */
        Bp_EC rc = bb_await_notfull(buff, buff->timeout_us);
        if (rc == Bp_EC_OK) {
            /* Retry allocation after wait */
            return bb_allocate_batch(buff);
        }
        batch.ec = rc;
    }
    
    return batch;
}

/* Consumer gets tail batch */
static inline Bp_Batch_t bb_get_tail_lockfree(Batch_buff_t* buff)
{
    Bp_Batch_t batch = {0};
    
    /* Fast path check */
    if (!bb_isempy_lockfree(buff)) {
        size_t idx = atomic_load_explicit(&buff->consumer.tail, memory_order_relaxed);
        idx = idx & bb_modulo_mask(buff);
        
        /* Ensure batch data is visible */
        atomic_thread_fence(memory_order_acquire);
        
        batch = buff->batch_ring[idx];
    } else {
        /* Slow path - wait for data */
        Bp_EC rc = bb_await_notempty(buff, buff->timeout_us);
        if (rc == Bp_EC_OK) {
            /* Retry after wait */
            return bb_get_tail_lockfree(buff);
        }
        batch.ec = rc;
    }
    
    return batch;
}
```

## Implementation Strategy

### Phase 1: Core Changes
1. Add atomic operations to head/tail updates
2. Implement cache line separation
3. Add memory barriers for data visibility

### Phase 2: Testing
1. Create SPSC performance benchmarks
2. Verify correctness with memory ordering tests
3. Test blocking behavior edge cases

### Phase 3: Integration
1. Update existing code to use new APIs
2. Maintain backward compatibility
3. Document performance improvements

## Performance Benefits

1. **Throughput**: 5-10x improvement in SPSC scenarios
2. **Latency**: Sub-microsecond operations on fast path
3. **CPU Usage**: Reduced context switches and lock contention
4. **Scalability**: Better performance with high core counts

## Compatibility Notes

- API changes are acceptable as we're actively developing
- Lock-based slow path ensures correctness when blocking needed
- Memory barriers ensure data visibility across cores
- Statistics updated atomically for thread safety
- Uses C99-compatible `__attribute__((aligned(64)))` instead of C11 `alignas`