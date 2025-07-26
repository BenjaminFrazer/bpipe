# Debug Output Filter Design Specification

## Overview

The debug output filter is a passthrough filter that prints batch contents to stdout or a file for debugging and monitoring purposes. It preserves all batch data and metadata while providing configurable output formatting.

## Filter Type

- **Type**: `FILT_T_MAP` (transforms input to output)
- **Inputs**: 1 (any data type)
- **Outputs**: 1 (matches input type)
- **Threading**: Dedicated worker thread

## Configuration

```c
typedef struct {
    const char* prefix;           // Optional prefix for each line (default: "DEBUG: ")
    bool show_metadata;          // Print batch metadata (default: true)
    bool show_samples;           // Print sample data (default: true)
    int max_samples_per_batch;   // Max samples to print per batch (-1 for all, default: 10)
    enum {
        FMT_DECIMAL,             // Default for numeric types
        FMT_HEX,                 // Hexadecimal format
        FMT_SCIENTIFIC,          // Scientific notation for floats
        FMT_BINARY               // Binary representation
    } format;
    bool flush_after_print;      // Call fflush after each batch (default: true)
    const char* filename;        // Output filename (NULL for stdout)
    bool append_mode;            // Append to file if it exists (default: false)
} DebugOutputConfig_t;
```

## Implementation Details

### Structure

```c
typedef struct {
    Filter_t base;               // Must be first member
    DebugOutputConfig_t config;
    char* formatted_prefix;      // Heap-allocated formatted prefix
    FILE* output_file;           // Output file handle (stdout or file)
    pthread_mutex_t file_mutex;  // Protect file operations
} DebugOutputFilter_t;
```

### Worker Function Behavior

1. **Main Loop**:
   - Get input batch using `bb_get_tail()`
   - Handle timeout gracefully (continue loop)
   - Check for stop signal via `atomic_load(&running)`

2. **Batch Processing**:
   - Print batch metadata if enabled:
     - Timestamp (t_ns)
     - Period (period_ns) 
     - Sample count (tail - head)
     - Data type
     - Error code
   - Print sample data if enabled:
     - Respect max_samples_per_batch limit
     - Format according to configured format
     - Handle all data types (DTYPE_FLOAT, DTYPE_I32, DTYPE_U32)

3. **Passthrough**:
   - Get output buffer using `bb_get_head()`
   - Copy entire input batch to output:
     - All metadata (t_ns, period_ns, head, tail, ec)
     - All sample data (memcpy)
   - Submit output batch using `bb_submit()`
   - Delete input batch using `bb_del_tail()`

4. **Completion Handling**:
   - If batch has `Bp_EC_COMPLETE`, propagate to output
   - Print completion message if metadata enabled
   - Continue processing until stopped

### Output Format Examples

#### With Metadata (default):
```
DEBUG: [Batch t=1234567890ns, period=1000000ns, samples=5, type=FLOAT]
DEBUG:   [0] 1.234000
DEBUG:   [1] 2.345000
DEBUG:   [2] 3.456000
DEBUG:   [3] 4.567000
DEBUG:   [4] 5.678000
```

#### Without Metadata:
```
DEBUG: 1.234000
DEBUG: 2.345000
DEBUG: 3.456000
DEBUG: 4.567000
DEBUG: 5.678000
```

#### Hex Format (for integers):
```
DEBUG: [Batch t=1234567890ns, period=1000000ns, samples=3, type=U32]
DEBUG:   [0] 0x00000064
DEBUG:   [1] 0x000000C8
DEBUG:   [2] 0x0000012C
```

## Error Handling

- **Timeout on Input**: Normal operation, continue loop
- **Stop Signal**: Clean shutdown, exit worker thread
- **Buffer Allocation Failure**: Use `WORKER_ASSERT` to fail fast
- **Print Errors**: Log to stderr, continue processing (non-fatal)
- **File Open Failure**: Fail during initialization with appropriate error code
- **File Write Errors**: Log to stderr, continue processing (non-fatal)

## Performance Considerations

- **Non-blocking**: Printf operations should not block the pipeline
- **Buffered Output**: Use stdout buffering, flush only if configured
- **Sample Limiting**: Default max_samples_per_batch prevents flooding
- **Zero-copy Passthrough**: Direct memcpy from input to output buffer

## Thread Safety

- No shared state beyond atomic running flag
- All printing done from worker thread only
- File mutex protects file operations (future-proofing for potential flush operations)
- File opened once during init, closed during cleanup

## Testing Requirements

1. **Basic Functionality**:
   - Verify exact passthrough of data
   - Verify metadata preservation
   - Test all data types

2. **Configuration Options**:
   - Test all format options
   - Test sample limiting
   - Test prefix customization
   - Test stdout output (filename = NULL)
   - Test file output with new file
   - Test file output with append mode

3. **Edge Cases**:
   - Empty batches (head == tail)
   - Partial batches
   - Completion signal propagation
   - Very large batches
   - Invalid filename handling
   - File permission errors

4. **Performance**:
   - Ensure minimal latency added
   - Verify no data loss under load

## Use Cases

1. **Pipeline Debugging**: Insert between filters to observe data flow
2. **Data Validation**: Verify filter outputs during development
3. **System Monitoring**: Log telemetry data to stdout for analysis
4. **Integration Testing**: Capture filter outputs for comparison

## Example Usage

```c
// Example 1: Debug to stdout
DebugOutputConfig_t stdout_config = {
    .prefix = "SENSOR_DATA: ",
    .show_metadata = true,
    .show_samples = true,
    .max_samples_per_batch = 5,
    .format = FMT_SCIENTIFIC,
    .flush_after_print = true,
    .filename = NULL,  // Output to stdout
    .append_mode = false
};

DebugOutputFilter_t* debug = debug_output_filter_init(&stdout_config);

// Example 2: Debug to file
DebugOutputConfig_t file_config = {
    .prefix = "LOG: ",
    .show_metadata = true,
    .show_samples = true,
    .max_samples_per_batch = -1,  // Log all samples
    .format = FMT_DECIMAL,
    .flush_after_print = false,
    .filename = "/tmp/pipeline_debug.log",
    .append_mode = true  // Append to existing log
};

DebugOutputFilter_t* file_debug = debug_output_filter_init(&file_config);

// Connect in pipeline
filter_connect(sensor_source, 0, debug, 0);
filter_connect(debug, 0, data_processor, 0);
```

## Implementation Notes

- Use `fprintf(filter->output_file, ...)` for all output
- Allocate formatted prefix during init to avoid repeated formatting
- Free formatted prefix and close file (if not stdout) in destructor
- File is opened during init with error checking
- Consider adding timestamp formatting options in future versions
- Consider adding log rotation support for file output in future