# Overflow Behavior Refactoring Log

## Objective
Remove redundant `overflow_behaviour` field from `Bp_Filter_t` struct to improve architecture and eliminate confusion about which overflow behavior field to use.

## Problem Analysis
The codebase had two separate `overflow_behaviour` fields:
1. `filter.overflow_behaviour` - stored in filter struct but never used at runtime
2. `buf->overflow_behaviour` - stored in buffer struct and actually used by `Bp_allocate`

This redundancy created:
- Potential confusion about which field to use
- Unnecessary memory usage in every filter
- Poor encapsulation (overflow behavior should belong to buffers)

## Changes Made

### 1. Removed Filter-Level Field
**File**: `bpipe/core.h`
- Removed `OverflowBehaviour_t overflow_behaviour;` from `Bp_Filter_t` struct
- Field was between `data_width` and `dtype` fields

### 2. Updated Initialization Logic  
**File**: `bpipe/core.c`
- Removed line that stored overflow behavior in filter: `filter->overflow_behaviour = working_config.overflow_behaviour;`
- Buffer initialization still correctly inherits overflow behavior from config via `BpBatchBuffer_InitFromConfig`

### 3. Fixed Test Verification
**File**: `tests/test_core_filter.c`
- Updated `test_Overflow_Behavior_Block_Default` to verify `filter.input_buffers[0].overflow_behaviour` instead of `filter.overflow_behaviour`
- This change makes the test verify the actual behavior that's used at runtime

## Results

### Test Results
- ✅ **30/30 tests pass** (100% success rate)
- ✅ **16 core filter tests** - all pass including overflow behavior tests
- ✅ **11 resampler tests** - all pass (resampler uses base filter correctly)
- ✅ **3 other tests** - signal gen, sentinel, multi-output all pass

### Architecture Benefits
1. **Better Encapsulation** - overflow behavior truly belongs to buffers that manage overflow
2. **Single Source of Truth** - only `buf->overflow_behaviour` is used, eliminating confusion
3. **Memory Efficiency** - removed unnecessary field from every filter instance
4. **Future Extensibility** - enables per-buffer overflow configuration
5. **Cleaner API** - maintains `BpFilterConfig.overflow_behaviour` as sensible default for all buffers

### Backward Compatibility
- ✅ **BpFilterConfig API unchanged** - users still set overflow_behaviour in config
- ✅ **Runtime behavior unchanged** - buffers still get correct overflow behavior during initialization  
- ✅ **Existing code unaffected** - all existing functionality works exactly the same

## Technical Details

### Buffer Configuration Flow
```c
// User sets config
BpFilterConfig config = {
    .overflow_behaviour = OVERFLOW_DROP,  // Set in config
    // ... other fields
};

// Filter initialization
BpFilter_Init(&filter, &config);
  ↓
// Buffer configuration inherits from filter config  
BpBufferConfig buffer_config = {
    .overflow_behaviour = working_config.overflow_behaviour,  // Copied to buffer
    // ... other fields
};
  ↓
// Buffer stores the actual field used at runtime
buffer->overflow_behaviour = config->overflow_behaviour;
```

### Runtime Usage
```c
// Bp_allocate checks ONLY the buffer field (never the filter field)
if (buf->overflow_behaviour == OVERFLOW_DROP && Bp_full(buf)) {
    batch.ec = Bp_EC_NOSPACE;
    return batch;
}
```

## Verification Steps
1. **Clean rebuild** - ensured no stale object files
2. **Core functionality** - verified basic filter operations work
3. **Overflow behavior** - tested both OVERFLOW_BLOCK and OVERFLOW_DROP modes  
4. **Resampler integration** - verified composite filters work correctly
5. **Full test suite** - all 30 tests pass consistently

## Commit Details
- **Branch**: `OVERFLOW_REFACTOR` 
- **Commit**: `9611502`
- **Files Changed**: 3 files, 2 insertions(+), 4 deletions(-)
- **Test Status**: All 30 tests passing
- **Merged to main**: ✅ Fast-forward merge successful

## Impact Assessment
- **Development**: ✅ **Improved** - cleaner architecture, less confusing API
- **Performance**: ✅ **Better** - reduced memory usage per filter
- **Maintainability**: ✅ **Enhanced** - single source of truth for overflow behavior
- **Future Development**: ✅ **Enabled** - can now implement per-buffer overflow policies

---
*Refactoring completed: 2025-06-29*  
*Architecture improvement: Remove redundant filter-level overflow_behaviour field*  
*Status: **MERGED TO MAIN** - All tests passing*