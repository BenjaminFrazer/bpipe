# Plan: Remove `tail` Index from Batch_t Structure

## Overview
This plan outlines the refactoring to remove the `tail` index from the `Batch_t` structure, simplifying the bpipe2 architecture by enforcing that all batches start at index 0.

## Rationale

### Current State Analysis
1. **Most filters already set `tail = 0`** when creating output batches
2. **Only the map filter** uses tail for internal partial consumption tracking
3. **Passthrough filters** preserve tail, but this is rarely needed
4. **Batch_matcher has a bug** where it incorrectly sets head/tail values

### Benefits of Removal
1. **Simpler mental model**: Batches are arrays from 0 to head (head = sample count)
2. **Eliminates common bugs**: No risk of tail > head or forgetting to handle partial batches
3. **Cleaner code**: Remove all `head - tail` calculations
4. **Better performance**: Smaller batch structure, simpler calculations

### Trade-offs
- **Loss of partial batch representation**: Filters cannot emit partially consumed batches
- **Map filter needs refactoring**: Must track partial consumption internally

## Implementation Tasks

### Phase 1: Preparation
1. **Fix batch_matcher bug**
   - Fix incorrect head/tail assignment (currently sets head=0, tail=accumulated)
   - Update to use proper convention before removal

2. **Audit all tail usage**
   - Document each file using tail
   - Identify any unexpected dependencies

### Phase 2: Core Changes
3. **Update Batch_t structure**
   - Remove `tail` field from `bpipe/batch_buffer.h`
   - Update batch initialization to only set head=0

4. **Update batch buffer operations**
   - Modify `bb_init()` to not initialize tail
   - Remove tail from any batch buffer internals

5. **Refactor map filter**
   - Add internal state to track partial input consumption
   - Change from `input->tail += n` to local tracking
   - Ensure output batches still start at 0

### Phase 3: Update All Filters
6. **Simple filters** (csv_source, signal_generator, sample_aligner, tee)
   - Remove `tail = 0` assignments
   - Change `head - tail` to just `head`
   - Update data offset calculations

7. **Passthrough filters** (matched_passthrough, debug_output_filter)
   - Remove tail preservation
   - Update to only copy head value

8. **Complex filters** (csv_sink, batch_matcher)
   - Update sample counting logic
   - Fix offset calculations

### Phase 4: Utilities and Macros
9. **Update utils.h**
   - Change `NEEDS_NEW_BATCH` macro: `(!batch || batch->head == 0)`
   - Update any other tail-dependent macros

10. **Update test files**
    - Remove tail checks/assertions
    - Update head-tail calculations to just head
    - Fix test data setup

### Phase 5: Documentation
11. **Update core_data_model.md**
    - Remove tail from head/tail convention section
    - Update diagram to show batches as 0 to head
    - Clarify that batches always start at index 0

12. **Update filter_implementation_guide.md**
    - Remove "Partial Batch Handling" section
    - Simplify sample counting examples
    - Update code patterns to not use tail

13. **Update public_api_reference.md**
    - Document the breaking change
    - Update all examples

## Migration Guide for External Filters

### Before:
```c
// Calculate samples
size_t samples = batch->head - batch->tail;

// Process from tail
for (size_t i = batch->tail; i < batch->head; i++) {
    process(batch->data[i]);
}

// Set output batch
output->tail = 0;
output->head = count;
```

### After:
```c
// Calculate samples  
size_t samples = batch->head;

// Process from start
for (size_t i = 0; i < batch->head; i++) {
    process(batch->data[i]);
}

// Set output batch
output->head = count;
```

## Testing Strategy
1. Run all existing tests after each phase
2. Add specific tests for map filter's new internal state
3. Verify batch_matcher fix resolves the head/tail bug
4. Performance benchmarks before/after

## Rollback Plan
- Tag current version before changes
- Keep tail field commented out initially
- Can revert individual filter changes if issues found

## Success Criteria
- All tests pass
- No performance regression
- Simplified filter implementations
- Clear documentation of changes