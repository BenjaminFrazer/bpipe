# Map Filter Mixed Batch Size Test Specification

## Overview

This specification defines tests for cascaded map filters with different batch sizes. These tests are critical because they expose assumptions about data flow and buffer management in the current architecture.

## Architectural Considerations

The map filter currently assumes:
- 1:1 input to output sample mapping
- Same batch size for input and output buffers when connected

Mixed batch sizes challenge these assumptions and require careful handling at buffer boundaries.

## Test Implementations

### Test 1: `test_large_to_small_batch_cascade`

**Purpose**: Verify data integrity when flowing from large to small batches

**Configuration**:
```c
// Stage 1: Large batches (256 samples)
BatchBuffer_config large_config = {
    .dtype = DTYPE_FLOAT,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .ring_capacity_expo = 4,   // 15 batches
    .batch_capacity_expo = 8,  // 256 samples
};

// Stage 2: Small batches (64 samples)
BatchBuffer_config small_config = {
    .dtype = DTYPE_FLOAT,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .ring_capacity_expo = 5,   // 31 batches
    .batch_capacity_expo = 6,  // 64 samples
};
```

**Expected Behavior**:
- One large batch (256 samples) should produce exactly 4 small batches (64 samples each)
- Data continuity must be preserved across batch boundaries
- Timing from large batch should be applied to first small batch

**Test Scenarios**:
1. Full large batches (256 samples) → Verify 4 complete small batches
2. Partial large batches (128 samples) → Verify 2 complete small batches
3. Continuous stream → Verify no data loss at boundaries

### Test 2: `test_small_to_large_batch_cascade`

**Purpose**: Test data accumulation from small to large batches

**Note**: This test will likely expose limitations in the current map filter design, as it expects to process complete batches.

**Configuration**:
```c
// Stage 1: Small batches (64 samples)
BatchBuffer_config small_config = {
    .dtype = DTYPE_FLOAT,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .ring_capacity_expo = 5,   // 31 batches
    .batch_capacity_expo = 6,  // 64 samples
};

// Stage 2: Large batches (256 samples)
BatchBuffer_config large_config = {
    .dtype = DTYPE_FLOAT,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .ring_capacity_expo = 4,   // 15 batches
    .batch_capacity_expo = 8,  // 256 samples
};
```

**Expected Behavior**:
- Current implementation may not handle this correctly
- Would need 4 small batches to fill 1 large batch
- Test will document actual behavior

### Test 3: `test_cascade_batch_size_progression`

**Purpose**: Test a cascade with progressively different batch sizes

**Configuration**:
```
Filter1: 256 → 256 samples (scale ×2)
Filter2: 256 → 128 samples (different output config)
Filter3: 128 → 32 samples (different output config)
```

**Verification Points**:
1. Data integrity through entire cascade
2. Sample count preservation (considering 1:1 mapping)
3. Timing information handling
4. Backpressure behavior

### Test 4: `test_mismatched_ring_capacity`

**Purpose**: Test behavior with same batch sizes but different ring capacities

**Configuration**:
```c
// Stage 1: Few buffers
BatchBuffer_config few_buffers = {
    .dtype = DTYPE_FLOAT,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .ring_capacity_expo = 3,   // 7 batches
    .batch_capacity_expo = 7,  // 128 samples
};

// Stage 2: Many buffers
BatchBuffer_config many_buffers = {
    .dtype = DTYPE_FLOAT,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .ring_capacity_expo = 6,   // 63 batches
    .batch_capacity_expo = 7,  // 128 samples (same size)
};
```

**Test Scenarios**:
1. Burst production → Verify buffering behavior
2. Slow consumption → Test backpressure propagation

## Implementation Approach

### Helper Functions

```c
// Verify continuous sequence across multiple batches
static void verify_sequence_continuity(Batch_buff_t* buffer, 
                                      uint32_t start_value,
                                      size_t total_samples) {
    uint32_t expected = start_value;
    Bp_EC err;
    
    while (expected < start_value + total_samples) {
        Batch_t* batch = bb_get_tail(buffer, 10000, &err);
        if (!batch) break;
        
        float* data = (float*)batch->data;
        for (size_t i = 0; i < batch->head; i++) {
            TEST_ASSERT_EQUAL_FLOAT((float)expected, data[i]);
            expected++;
        }
        
        bb_del_tail(buffer);
    }
    
    TEST_ASSERT_EQUAL(start_value + total_samples, expected);
}

// Fill buffer with sequential data
static void fill_sequential_batches(Batch_buff_t* buffer,
                                   uint32_t* counter,
                                   size_t n_batches,
                                   size_t samples_per_batch) {
    for (size_t b = 0; b < n_batches; b++) {
        Batch_t* batch = bb_get_head(buffer);
        TEST_ASSERT_NOT_NULL(batch);
        
        float* data = (float*)batch->data;
        for (size_t i = 0; i < samples_per_batch; i++) {
            data[i] = (float)(*counter)++;
        }
        batch->head = samples_per_batch;
        batch->t_ns = 1000000 * b;
        
        bb_submit(buffer, 10000);
    }
}
```

### Test Template

```c
void test_large_to_small_batch_cascade(void) {
    // Create filters with different configs
    Map_filt_t filter1, filter2;
    Map_config_t config1 = {
        .buff_config = large_config,
        .map_fcn = test_identity_map
    };
    Map_config_t config2 = {
        .buff_config = small_config,
        .map_fcn = test_scale_map
    };
    
    // Initialize cascade
    CHECK_ERR(map_init(&filter1, config1));
    CHECK_ERR(map_init(&filter2, config2));
    
    // Create output buffer with small config
    Batch_buff_t output;
    CHECK_ERR(bb_init(&output, "output", small_config));
    
    // Connect: filter1 → filter2 → output
    // NOTE: This connection may fail or behave unexpectedly
    // due to batch size mismatch
    CHECK_ERR(filt_sink_connect(&filter1.base, 0, 
                                &filter2.base.input_buffers[0]));
    CHECK_ERR(filt_sink_connect(&filter2.base, 0, &output));
    
    // Start processing
    CHECK_ERR(filt_start(&filter1.base));
    CHECK_ERR(filt_start(&filter2.base));
    CHECK_ERR(bb_start(&output));
    
    // Submit test data
    uint32_t counter = 0;
    fill_sequential_batches(&filter1.base.input_buffers[0],
                           &counter, 5, 256);
    
    // Verify output
    verify_sequence_continuity(&output, 0, counter);
    
    // Cleanup
    CHECK_ERR(filt_stop(&filter1.base));
    CHECK_ERR(filt_stop(&filter2.base));
    CHECK_ERR(bb_stop(&output));
    CHECK_ERR(filt_deinit(&filter1.base));
    CHECK_ERR(filt_deinit(&filter2.base));
    CHECK_ERR(bb_deinit(&output));
}
```

## Expected Outcomes

1. **Current Implementation Limitations**
   - Tests will likely reveal that mixed batch sizes are not properly supported
   - Document the specific failure modes

2. **Architectural Insights**
   - Identify what changes would be needed to support mixed batch sizes
   - Consider if this is a map filter limitation or broader architectural issue

3. **Workaround Strategies**
   - Document how to achieve similar functionality with current constraints
   - Identify if intermediate buffering filters are needed

## Success Criteria

1. **Documentation**: Clearly document current behavior with mixed batch sizes
2. **Failure Modes**: Identify and categorize all failure scenarios
3. **Future Work**: Provide recommendations for architectural improvements