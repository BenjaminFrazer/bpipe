# OVERFLOW_DROP_TAIL Design Decision

## Overview

This document captures the design decision for implementing OVERFLOW_DROP_TAIL behavior in the batch buffer system, specifically addressing the trade-off between function pointers and conditional branches.

## Decision: Use Conditional Branch with `unlikely()` Hint

After analysis, we chose to use a predictable conditional branch rather than function pointers for the following reasons:

### Performance Comparison

```c
// Option 1: Function Pointer (NOT CHOSEN)
buff->submit(buff, timeout);  // ~5ns, prevents inlining, indirect branch

// Option 2: Conditional Branch (CHOSEN)
if (unlikely(buff->overflow_behaviour == OVERFLOW_DROP_TAIL)) {
    return bb_submit_drop_tail_mutex(buff, timeout_us);
}
// Fast path continues inline...  // ~2.1ns, can be inlined
```

### Rationale

1. **DROP_TAIL is Rare**: This mode is exceptional, not the common case
   - Users explicitly choose performance (BLOCK/DROP) vs. functionality (DROP_TAIL)
   - Branch will be correctly predicted 99%+ of the time

2. **DROP_TAIL is Already Slow**: Uses mutex synchronization
   - One predictable branch adds ~0.1ns
   - Mutex operations add ~50-100ns
   - Branch cost is negligible in comparison

3. **Preserves Fast Path Optimizations**:
   - Compiler can inline the common path
   - Better instruction cache usage
   - Enables cross-function optimizations

4. **Simpler and Safer**:
   - No function pointer security concerns
   - Easier to debug and trace
   - Static analysis tools work better

### Implementation

```c
static inline Bp_EC bb_submit(Batch_buff_t *buff, unsigned long timeout_us) {
    // Exceptional case first with unlikely hint
    if (unlikely(buff->overflow_behaviour == OVERFLOW_DROP_TAIL)) {
        return bb_submit_drop_tail_mutex(buff, timeout_us);
    }
    
    // Fast path - inlinable, no function pointer overhead
    // ... existing BLOCK/DROP implementation ...
}
```

## Conclusion

The conditional branch approach provides the best balance of:
- Performance for the common case (BLOCK/DROP modes)
- Functionality for the exceptional case (DROP_TAIL mode)
- Code maintainability and security

Users who need maximum performance will use BLOCK/DROP modes and pay near-zero overhead. Users who need DROP_TAIL functionality have already accepted the performance trade-off of mutex synchronization.