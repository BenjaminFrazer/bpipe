# Integration Test Specification - Core Filters Pipeline

## Overview

This specification defines a comprehensive integration test that combines multiple core filters into a realistic data processing pipeline. The test validates correct data flow, timing alignment, batch processing, and multi-output capabilities across the entire bpipe2 framework.

## Test Objectives

1. **End-to-end validation**: Verify data flows correctly from source to sinks through multiple transformations
2. **Timing preservation**: Ensure timestamp integrity and batch alignment throughout the pipeline
3. **Multi-output testing**: Validate tee filter correctly duplicates data to multiple sinks
4. **Batch matching**: Verify automatic batch size and phase alignment works correctly
5. **Data transformation**: Confirm map filter correctly scales values
6. **Debug observability**: Use debug filter to inspect intermediate data flow
7. **File I/O**: Test CSV reading and writing with multiple channels
8. **Error propagation**: Verify errors are correctly detected and reported
9. **Performance**: Measure throughput and latency through the full pipeline

## Pipeline Architecture

```
[CSV Source]
     ↓ (3 channels: sensor1, sensor2, sensor3)
[Batch Matcher] (per channel)
     ↓
[Debug Filter] (prints intermediate batches)
     ↓
[Tee Filter]
     ├→ [Map Filter (scale by 2.0)] → [CSV Sink 1]
     └→ [CSV Sink 2]
```

### Detailed Component Flow

1. **CSV Source**: Reads multi-channel sensor data from CSV file
   - Outputs separate channels for sensor1, sensor2, sensor3
   - Handles regular/irregular timing patterns
   - Configurable batch size

2. **Batch Matcher** (3 instances, one per channel):
   - Auto-detects output size from debug filter input
   - Aligns all batches to phase=0
   - Ensures consistent batch boundaries

3. **Debug Filter** (3 instances):
   - Prints batch metadata and sample values
   - Provides visibility into data flow
   - Passes data through unchanged

4. **Tee Filter** (3 instances):
   - Duplicates each channel to two outputs
   - Tests multi-output capability
   - No data transformation

5. **Map Filter** (3 instances):
   - Scales values by 2.0
   - Tests mathematical transformations
   - Preserves timing information

6. **CSV Sinks** (6 instances total):
   - 3 sinks receive scaled data
   - 3 sinks receive original data
   - Validates multiple file writes

## Test Data Specification

### Input CSV Format
```csv
ts_ns,sensor1,sensor2,sensor3
0,1.0,10.0,100.0
1000000,1.1,10.1,100.1
2000000,1.2,10.2,100.2
3000000,1.3,10.3,100.3
...
```

### Expected Output Files

**scaled_output_sensor1.csv**:
```csv
ts_ns,value
0,2.0
1000000,2.2
2000000,2.4
3000000,2.6
...
```

**original_output_sensor1.csv**:
```csv
ts_ns,value
0,1.0
1000000,1.1
2000000,1.2
3000000,1.3
...
```

## Configuration Details

### CSV Source Configuration
```c
CsvSource_config_t csv_config = {
    .name = "test_sensor_data",
    .file_path = "tests/data/integration_test_data.csv",
    .delimiter = ',',
    .has_header = true,
    .ts_column_name = "ts_ns",
    .data_column_names = {"sensor1", "sensor2", "sensor3", NULL},
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 6,    // 64 samples per batch
        .ring_capacity_expo = 8,     // 256 batches
        .overflow_behaviour = OVERFLOW_BLOCK
    },
    .loop = false,
    .skip_invalid = false,
    .timeout_us = 1000000  // 1 second
};
```

### Batch Matcher Configuration
```c
BatchMatcher_config_t matcher_config = {
    .name = "matcher_sensor1",
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 7,    // Auto-detected from sink
        .ring_capacity_expo = 8,
        .overflow_behaviour = OVERFLOW_BLOCK
    }
};
```

### Debug Filter Configuration
```c
DebugOutput_config_t debug_config = {
    .name = "debug_sensor1",
    .prefix = "[SENSOR1]",
    .print_samples = true,
    .max_samples_to_print = 5,
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 7,    // 128 samples
        .ring_capacity_expo = 8,
        .overflow_behaviour = OVERFLOW_BLOCK
    }
};
```

### Tee Filter Configuration
```c
Tee_config_t tee_config = {
    .name = "tee_sensor1",
    .n_outputs = 2,
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 7,
        .ring_capacity_expo = 8,
        .overflow_behaviour = OVERFLOW_BLOCK
    }
};
```

### Map Filter Configuration
```c
Map_config_t map_config = {
    .name = "scale_sensor1",
    .op = BP_MAP_OP_SCALE,
    .operand = 2.0,
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 7,
        .ring_capacity_expo = 8,
        .overflow_behaviour = OVERFLOW_BLOCK
    }
};
```

### CSV Sink Configuration
```c
CsvSink_config_t sink_config = {
    .name = "sink_scaled_sensor1",
    .file_path = "output/scaled_sensor1.csv",
    .value_column_name = "value",
    .overwrite = true,
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 7,
        .ring_capacity_expo = 8,
        .overflow_behaviour = OVERFLOW_BLOCK
    }
};
```

## Test Scenarios

### 1. Basic Data Flow Test
- **Setup**: 1000 samples at 1kHz regular rate
- **Verify**: 
  - All samples reach both sinks
  - Scaled values = original × 2.0
  - Timestamps preserved
  - Debug output shows correct batches

### 2. Irregular Data Test
- **Setup**: Variable timing between samples
- **Verify**:
  - Single-sample batches created
  - No data loss
  - Timing preserved exactly

### 3. Batch Boundary Test
- **Setup**: Data that spans multiple batch boundaries
- **Verify**:
  - Batch matcher creates aligned 128-sample batches
  - No samples duplicated at boundaries
  - Phase aligned to t=0

### 4. Backpressure Test
- **Setup**: Slow CSV sink (simulated with delays)
- **Verify**:
  - Pipeline handles backpressure correctly
  - No buffer overflows
  - Data integrity maintained

### 5. Error Propagation Test
- **Setup**: Invalid CSV data mid-file
- **Verify**:
  - Error detected and reported
  - Pipeline shuts down cleanly
  - Partial output files are valid

### 6. Multi-Channel Synchronization
- **Setup**: All three channels processing simultaneously
- **Verify**:
  - Channels remain synchronized
  - No cross-channel interference
  - Independent batch processing

### 7. Pipeline Shutdown Test
- **Setup**: Stop pipeline mid-processing
- **Verify**:
  - All filters stop cleanly
  - No hanging threads
  - Output files contain valid partial data

## Expected Debug Output

```
[SENSOR1] Batch received: t_ns=0, period_ns=1000000, samples=128
  [0]: 1.000000
  [1]: 1.100000
  [2]: 1.200000
  [3]: 1.300000
  [4]: 1.400000
  ... (123 more samples)

[SENSOR2] Batch received: t_ns=0, period_ns=1000000, samples=128
  [0]: 10.000000
  [1]: 10.100000
  [2]: 10.200000
  [3]: 10.300000
  [4]: 10.400000
  ... (123 more samples)
```

## Error Conditions

### 1. File Not Found
- CSV source file doesn't exist
- Expected: Clean error message and Bp_EC_FILE_NOT_FOUND

### 2. Column Mismatch
- CSV missing expected columns
- Expected: Bp_EC_COLUMN_NOT_FOUND with column name

### 3. Type Conversion Error
- Non-numeric data in CSV
- Expected: Skip row if skip_invalid=true, else Bp_EC_PARSE_ERROR

### 4. Disk Full
- CSV sink cannot write
- Expected: Bp_EC_IO_ERROR with clear message

### 5. Connection Errors
- Filters not properly connected
- Expected: Bp_EC_NO_SINK or Bp_EC_NOT_CONNECTED

## Performance Metrics

### Target Performance
- **Throughput**: > 1M samples/second per channel
- **Latency**: < 10ms from source to sink
- **Memory**: < 50MB for full pipeline
- **CPU**: < 80% single core utilization

### Measurement Points
1. **Source read rate**: Samples/second from CSV
2. **Pipeline latency**: Time from read to write
3. **Batch processing time**: Per-filter processing duration
4. **Memory usage**: Peak RSS during test
5. **Thread efficiency**: Context switches and CPU usage

## Implementation Checklist

### Setup Phase
- [ ] Create test data CSV file with 3 channels
- [ ] Initialize all filter structures
- [ ] Configure each filter with appropriate settings
- [ ] Create output directory for CSV sinks

### Connection Phase
- [ ] Connect CSV source outputs to batch matchers
- [ ] Connect batch matchers to debug filters
- [ ] Connect debug filters to tee filters
- [ ] Connect tee output 0 to map filters
- [ ] Connect tee output 1 to CSV sinks (original)
- [ ] Connect map filters to CSV sinks (scaled)

### Execution Phase
- [ ] Start all buffers in correct order
- [ ] Start all filters (sources last)
- [ ] Monitor debug output
- [ ] Wait for completion or timeout
- [ ] Stop all filters (sources first)
- [ ] Stop all buffers

### Validation Phase
- [ ] Verify output files exist
- [ ] Check sample counts match input
- [ ] Validate scaled values = original × 2.0
- [ ] Confirm timestamps preserved
- [ ] Check no data corruption
- [ ] Verify clean shutdown

### Cleanup Phase
- [ ] Deinitialize all filters
- [ ] Free all allocated memory
- [ ] Close all file handles
- [ ] Report test results

## Test Code Structure

```c
void test_integration_core_filters(void) {
    // Declare all filters
    CsvSource_t csv_source;
    BatchMatcher_t matchers[3];
    DebugOutput_t debug_filters[3];
    Tee_t tee_filters[3];
    Map_t map_filters[3];
    CsvSink_t csv_sinks_scaled[3];
    CsvSink_t csv_sinks_original[3];
    
    // Initialize phase
    // ... initialization code ...
    
    // Connection phase
    // ... connection code ...
    
    // Execution phase
    // ... start and run ...
    
    // Validation phase
    // ... check results ...
    
    // Cleanup phase
    // ... cleanup code ...
}
```

## Success Criteria

1. **Functional Success**:
   - All test scenarios pass
   - No data loss or corruption
   - Correct transformations applied
   - Timing preserved throughout

2. **Performance Success**:
   - Meets throughput targets
   - Acceptable memory usage
   - No resource leaks
   - Efficient CPU utilization

3. **Robustness Success**:
   - Handles errors gracefully
   - Clean shutdown in all cases
   - No hanging threads
   - Clear error messages

## Future Extensions

1. **Additional Filters**: Add resampler, window functions
2. **Stress Testing**: Increase data rates and volumes
3. **Fault Injection**: Test filter failures mid-pipeline
4. **Dynamic Reconfiguration**: Change connections at runtime
5. **Performance Profiling**: Detailed per-filter metrics
6. **Automated Validation**: Script to verify output correctness