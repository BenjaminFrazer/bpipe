# Integration Test: Core Filters Pipeline

## Overview
This specification defines a comprehensive integration test that exercises multiple core filters in a realistic pipeline configuration. The test focuses on correctness validation rather than performance.

## Test Pipeline Architecture

```
[CSV Source] → [Batch Matcher] → [Debug Filter] → [Tee] → [Map (scale by 2)] → [CSV Sink 1]
                                                         └→ [CSV Sink 2]
```

## Test Configuration

### Input Data
- **Single CSV file**: `test_data/integration_input.csv`
- **Format**: `timestamp_ns,channel,value`
- **Sampling pattern**:
  - Batches 1-3: Regular sampling at 1000 Hz (1ms period)
  - Batches 4-6: Irregular sampling with varying periods (0.5ms, 1.5ms, 2ms)
- **Total samples**: ~200 samples (to keep CSV file small)

### Ring Buffer Configuration
- **Ring size**: 4 batches (small to ensure wrap-around)
- **Batch size**: 16 samples (minimize test data requirements)

### CSV Source Filter
```c
{
    .type = FILT_T_MAP,
    .name = "csv_source",
    .map = {
        .channels = {"sensor1"},
        .n_channels = 1,
        .n_in = 0,
        .csv_path = "test_data/integration_input.csv",
        .batch_size = 16,
        .ring_size = 4
    }
}
```

### Batch Matcher Filter
```c
{
    .type = FILT_T_MAP,
    .name = "batch_matcher",
    .map = {
        .channels = {"sensor1"},
        .n_channels = 1,
        .n_in = 1,
        .inputs = {&csv_source},
        .batch_size = 24,  // Different size to test alignment
        .ring_size = 4,
        .phase_offset = 8  // Test phase alignment
    }
}
```

### Debug Filter
```c
{
    .type = FILT_T_MAP,
    .name = "debug_monitor",
    .map = {
        .channels = {"sensor1"},
        .n_channels = 1,
        .n_in = 1,
        .inputs = {&batch_matcher},
        .log_level = DEBUG_LEVEL_INFO,
        .log_batches = true,
        .batch_size = 24,
        .ring_size = 4
    }
}
```

### Tee Filter
```c
{
    .type = FILT_T_MAP,
    .name = "tee_splitter",
    .map = {
        .channels = {"sensor1"},
        .n_channels = 1,
        .n_in = 1,
        .inputs = {&debug_monitor},
        .n_outputs = 2,
        .batch_size = 24,
        .ring_size = 4
    }
}
```

### Map Filter (Scale by 2)
```c
{
    .type = FILT_T_MAP,
    .name = "scale_by_2",
    .map = {
        .channels = {"sensor1"},
        .n_channels = 1,
        .n_in = 1,
        .inputs = {&tee_splitter.outputs[0]},
        .map_fn = scale_by_2_fn,
        .batch_size = 24,
        .ring_size = 4
    }
}

// Map function
Bp_EC scale_by_2_fn(Bp_Sample* out, const Bp_Sample* in) {
    out->val = in->val * 2.0;
    return BP_EC_OK;
}
```

### CSV Sink Filters
```c
// Sink 1: Scaled output
{
    .type = FILT_T_MAP,
    .name = "csv_sink_scaled",
    .map = {
        .channels = {"sensor1"},
        .n_channels = 1,
        .n_in = 1,
        .inputs = {&scale_by_2},
        .csv_path = "test_data/output_scaled.csv",
        .batch_size = 24,
        .ring_size = 4
    }
}

// Sink 2: Original output
{
    .type = FILT_T_MAP,
    .name = "csv_sink_original",
    .map = {
        .channels = {"sensor1"},
        .n_channels = 1,
        .n_in = 1,
        .inputs = {&tee_splitter.outputs[1]},
        .csv_path = "test_data/output_original.csv",
        .batch_size = 24,
        .ring_size = 4
    }
}
```

## Test Data Generation

### Input CSV Example
```csv
timestamp_ns,channel,value
1000000,sensor1,1.0
2000000,sensor1,2.0
3000000,sensor1,3.0
...
# Batch 4 starts with irregular sampling
48000000,sensor1,48.0
48500000,sensor1,48.5
50000000,sensor1,50.0
51500000,sensor1,51.5
...
```

## Verification Strategy

### Unity Test Approach
1. **Template-based verification**:
   - Pre-generate expected output files
   - Use Unity file comparison utilities
   - Validate line-by-line match

2. **Checksum verification**:
   ```c
   // Calculate MD5 checksum of output files
   char actual_checksum[33];
   char expected_checksum[33] = "pre-calculated-checksum";
   
   calculate_file_md5("test_data/output_scaled.csv", actual_checksum);
   TEST_ASSERT_EQUAL_STRING(expected_checksum, actual_checksum);
   ```

3. **Content validation**:
   ```c
   // Parse and validate specific values
   validate_csv_content("test_data/output_scaled.csv", expected_scaled_values);
   validate_csv_content("test_data/output_original.csv", expected_original_values);
   ```

## Test Scenarios

### 1. Basic Data Flow
- Verify all samples flow through the pipeline
- Check scaled values are exactly 2x original
- Confirm both outputs contain same timestamps

### 2. Buffer Wrap-Around
- With ring_size=4 and ~200 samples, ensure multiple wrap-arounds
- Verify no data corruption during wrap-around
- Check buffer state consistency

### 3. Batch Alignment
- Verify batch matcher correctly aligns different batch sizes
- Test phase offset behavior
- Validate handling of irregular sampling periods

### 4. Error Propagation
- Test filter shutdown on upstream error
- Verify clean resource cleanup
- Check error codes propagate correctly

### 5. Edge Cases
- Empty batches (if source runs out of data)
- Partial batches at end of stream
- Timestamp discontinuities

## Test Implementation

```c
void test_integration_core_filters(void) {
    // Setup
    Bp_EC ec;
    
    // Create test input file
    create_test_input_csv();
    
    // Initialize filters
    Bp_Filter csv_source, batch_matcher, debug_monitor, 
              tee_splitter, scale_by_2, csv_sink_scaled, csv_sink_original;
    
    // Configure filters (as specified above)
    // ...
    
    // Connect pipeline
    ec = bp_connect(&batch_matcher, &csv_source);
    CHECK_ERR(ec);
    ec = bp_connect(&debug_monitor, &batch_matcher);
    CHECK_ERR(ec);
    // ... continue connections
    
    // Start pipeline
    ec = bp_start(&csv_sink_scaled);
    CHECK_ERR(ec);
    ec = bp_start(&csv_sink_original);
    CHECK_ERR(ec);
    
    // Wait for completion
    ec = bp_wait(&csv_source);
    CHECK_ERR(ec);
    
    // Verify outputs
    verify_output_files();
    
    // Cleanup
    bp_destroy(&csv_source);
    // ... destroy all filters
}
```

## Success Criteria

1. **Data Integrity**:
   - All input samples appear in both output files
   - Scaled output values are exactly 2x input values
   - Timestamps are preserved accurately

2. **Buffer Management**:
   - No memory leaks detected
   - Buffer wrap-around handled correctly
   - No data races or corruption

3. **Error Handling**:
   - Graceful shutdown on errors
   - Proper error code propagation
   - Resource cleanup verified

4. **Batch Processing**:
   - Correct handling of different batch sizes
   - Phase offset applied correctly
   - Irregular sampling periods handled

## Implementation Checklist

- [ ] Create test input CSV generator
- [ ] Implement Unity test harness
- [ ] Add file comparison utilities
- [ ] Implement checksum calculation
- [ ] Create expected output templates
- [ ] Add debug output validation
- [ ] Implement error injection tests
- [ ] Add resource leak detection
- [ ] Document test execution steps