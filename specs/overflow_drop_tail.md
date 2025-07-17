# OVERFLOW_DROP_TAIL Implementation Specification

## Overview

This document analyzes the implementation of a new overflow behavior `OVERFLOW_DROP_TAIL` for the batch buffer system. This mode would drop the oldest batch in the buffer to make room for new data when the buffer is full, effectively implementing a circular buffer with automatic overwrite of old data.

## Current Overflow Behaviors

Currently, the system supports:
- **OVERFLOW_BLOCK**: Producer blocks when buffer is full (default)
- **OVERFLOW_DROP**: New data is dropped when buffer is full

## Proposed Behavior: OVERFLOW_DROP_TAIL

When the buffer is full and new data arrives:
1. Drop the oldest batch (at tail position)
2. Advance the tail pointer
3. Write the new batch at the head position
4. Advance the head pointer

## Implementation Challenges

### 1. SPSC Model Violation

**Challenge**: The current design assumes Single Producer Single Consumer (SPSC), where:
- Only the producer modifies `head`
- Only the consumer modifies `tail`

With OVERFLOW_DROP_TAIL, the producer would need to modify `tail`, breaking this fundamental assumption.

**Impact**: 
- Requires additional synchronization
- Loses lock-free fast path optimizations
- Increases complexity and potential for race conditions

### 2. Consumer State Corruption

**Challenge**: The consumer might be:
- Currently reading the batch at tail position
- Blocked waiting for data (though this is less likely if buffer is full)
- Holding a pointer to the tail batch from `bb_get_tail()`

**Scenarios**:
```c
// Consumer thread
Batch_t* batch = bb_get_tail(&buffer, timeout, &err);
// Producer drops this batch here!
process_batch(batch); // Undefined behavior - batch was overwritten
```

### 3. Synchronization Complexity

**Challenge**: Need to coordinate between producer and consumer when dropping tail.

**Options**:
1. **Mutex on tail operations**: Significant performance penalty
2. **Try-lock approach**: Producer attempts to acquire consumer lock
3. **Atomic CAS operations**: Complex to implement correctly

### 4. Consumer Notification

**Challenge**: Consumer needs to know data was dropped.

**Solutions**:
- Add a `dropped_by_producer` counter to the buffer
- Include sequence numbers in batches
- Add a special "data dropped" marker batch

### 5. Partial Batch Processing

**Challenge**: Consumer might have partially processed a batch (tail < head within batch).

**Example**:
```c
// Consumer processed 10 of 64 samples in a batch
batch->tail = 10;
batch->head = 64;
// Producer drops this batch - consumer loses track of position
```

## Proposed Implementation Approach

### Option 1: Full Synchronization (Safest)

```c
Bp_EC bb_submit_drop_tail(Batch_buff_t *buff, unsigned long timeout_us)
{
    pthread_mutex_lock(&buff->mutex);
    
    if (bb_isfull(buff)) {
        // Forcibly advance tail, dropping oldest batch
        size_t old_tail = atomic_load(&buff->consumer.tail);
        size_t new_tail = (old_tail + 1) & bb_modulo_mask(buff);
        
        // Update dropped counter
        atomic_fetch_add(&buff->consumer.dropped_by_producer, 1);
        
        // Advance tail
        atomic_store(&buff->consumer.tail, new_tail);
        
        // Wake consumer if waiting (they'll see dropped counter)
        pthread_cond_signal(&buff->not_empty);
    }
    
    // Now proceed with normal submit
    size_t next_head = (atomic_load(&buff->producer.head) + 1) & bb_modulo_mask(buff);
    atomic_store(&buff->producer.head, next_head);
    atomic_fetch_add(&buff->producer.total_batches, 1);
    
    pthread_cond_signal(&buff->not_empty);
    pthread_mutex_unlock(&buff->mutex);
    
    return Bp_EC_OK;
}
```

### Option 2: Consumer Resilience (Better Performance)

Instead of producer modifying tail, make consumer resilient to overwritten data:

1. Add sequence numbers to each batch
2. Consumer validates sequence before processing
3. Producer can overwrite old data, consumer detects gaps

```c
typedef struct _Batch {
    size_t head;
    size_t tail;
    uint64_t sequence;  // New field
    // ... other fields
} Batch_t;
```

### Option 3: Separate Drop Queue

Maintain a separate "drop candidate" queue that producer can safely access without interfering with consumer's tail.

## Critical Issue with Option 2

**Fatal Flaw**: If the consumer holds a pointer to the tail batch and is actively reading it when the producer overwrites that memory, we have undefined behavior and data corruption:

```c
// Consumer thread
Batch_t* batch = bb_get_tail(&buffer, timeout, &err);
// Producer overwrites this memory location while consumer is reading!
for (int i = batch->tail; i < batch->head; i++) {
    // Reading corrupted/changing data - UNDEFINED BEHAVIOR
    uint32_t sample = ((uint32_t*)batch->data)[i];
    // Sample value could change mid-read!
}
```

This makes Option 2 **fundamentally unsafe** without additional protection.

## Revised Recommendations

### Recommended Approach: Two-Phase Commit with Batch State

A safer approach that maintains some SPSC benefits while protecting against corruption:

1. **Add batch state field** to track if batch is being consumed:
```c
typedef enum {
    BATCH_EMPTY = 0,
    BATCH_READY,
    BATCH_CONSUMING,  // Consumer is actively reading
    BATCH_CONSUMED    // Consumer finished, safe to overwrite
} BatchState_t;

typedef struct _Batch {
    _Atomic BatchState_t state;  // New field
    size_t head;
    size_t tail;
    // ... other fields
} Batch_t;
```

2. **Consumer marks batch as CONSUMING** before processing:
```c
Batch_t* bb_get_tail_safe(Batch_buff_t *buff, unsigned long timeout_us, Bp_EC *err) {
    Batch_t* batch = bb_get_tail(buff, timeout_us, err);
    if (batch) {
        atomic_store(&batch->state, BATCH_CONSUMING);
    }
    return batch;
}
```

3. **Producer checks state before overwriting**:
```c
Bp_EC bb_submit_drop_tail(Batch_buff_t *buff, unsigned long timeout_us) {
    if (bb_isfull(buff)) {
        size_t tail_idx = bb_get_tail_idx(buff);
        Batch_t* tail_batch = &buff->batch_ring[tail_idx];
        
        BatchState_t state = atomic_load(&tail_batch->state);
        if (state == BATCH_CONSUMING) {
            // Consumer is actively reading - must wait or drop new data
            if (buff->overflow_behaviour == OVERFLOW_DROP_TAIL_SAFE) {
                return Bp_EC_CONSUMER_ACTIVE;  // Drop new data instead
            }
        }
        
        // Safe to drop tail
        size_t new_tail = (tail_idx + 1) & bb_modulo_mask(buff);
        atomic_store(&buff->consumer.tail, new_tail);
    }
    // Continue with normal submit...
}
```

### Alternative: Reference Counting

Another approach using reference counting:

1. **Add reference count to batches**:
```c
typedef struct _Batch {
    _Atomic int ref_count;  // 0 = free, >0 = in use
    // ... other fields
} Batch_t;
```

2. **Consumer increments ref_count when getting batch**
3. **Producer only overwrites if ref_count == 0**
4. **Consumer decrements ref_count when done**

### Recommended: Graceful Degradation

Given the complexity, the safest approach might be:

1. **OVERFLOW_DROP_TAIL only works with mutex protection** (Option 1)
2. **For lock-free operation, only support OVERFLOW_BLOCK and OVERFLOW_DROP**
3. **Document that DROP_TAIL trades performance for functionality**

## Final Recommendation: Mutex-Protected DROP_TAIL

After analyzing all options, the safest and most practical approach is:

1. **Use Option 1 (Full Synchronization)** for OVERFLOW_DROP_TAIL
2. **Accept the performance trade-off** as inherent to this mode
3. **Keep lock-free fast path** for BLOCK and DROP modes only

### Rationale

- **Safety**: Prevents all race conditions and data corruption
- **Simplicity**: Easier to implement correctly and maintain
- **Correctness**: Consumer never sees partially overwritten data
- **Predictability**: Behavior is well-defined in all scenarios

### Implementation Summary

```c
// In bb_submit() when overflow_behaviour == OVERFLOW_DROP_TAIL
if (bb_isfull_lockfree(buff)) {
    pthread_mutex_lock(&buff->mutex);
    
    // Re-check under lock
    if (bb_isfull(buff)) {
        // Force tail advance
        size_t new_tail = (atomic_load(&buff->consumer.tail) + 1) & bb_modulo_mask(buff);
        atomic_store(&buff->consumer.tail, new_tail);
        atomic_fetch_add(&buff->consumer.dropped_by_producer, 1);
        
        // Wake consumer if blocked
        pthread_cond_signal(&buff->not_empty);
    }
    
    // Now there's space for our batch
    pthread_mutex_unlock(&buff->mutex);
}
// Continue with normal submit
```

This approach is safe because:
- Consumer operations already use the mutex when waiting
- Producer taking the mutex ensures no concurrent access
- Simple to reason about and verify correctness
### API Changes

```c
typedef enum _OverflowBehaviour {
  OVERFLOW_BLOCK = 0,
  OVERFLOW_DROP = 1,
  OVERFLOW_DROP_TAIL = 2,  // New mode
  OVERFLOW_MAX
} OverflowBehaviour_t;

// New error code
#define Bp_EC_DATA_DROPPED -20  // Consumer detected dropped batches
```

## Performance Considerations

- **OVERFLOW_BLOCK**: Best performance (lock-free fast path maintained)
- **OVERFLOW_DROP**: Same performance as BLOCK
- **OVERFLOW_DROP_TAIL**: Slight overhead for sequence checking, but maintains lock-free operation

## Testing Strategy

1. **Unit tests**:
   - Producer fills buffer, continues writing
   - Consumer validates no gaps when reading slowly
   - Measure dropped batch counts

2. **Stress tests**:
   - Fast producer, slow consumer
   - Verify data integrity with sequence numbers
   - Ensure no crashes or undefined behavior

3. **Edge cases**:
   - Consumer holding batch pointer during overwrite
   - Rapid fill/drain cycles
   - Mixed overflow behaviors in pipeline

## Conclusion

OVERFLOW_DROP_TAIL is implementable but requires careful design to maintain SPSC benefits. The recommended approach using sequence numbers preserves lock-free operation while providing robust drop-tail semantics. This is preferable to having the producer manipulate consumer state, which would require significant synchronization overhead.