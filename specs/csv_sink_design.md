# CSV Sink Filter - Design Specification

## A) Intent

### Primary Purpose
The CSV Sink is a single-input zero-output (SIZO) filter that writes incoming telemetry data to CSV (Comma-Separated Values) files. It acts as a terminal node in the processing pipeline, capturing data streams for offline analysis, archival, or integration with external tools.

### The Problem It Solves

Many telemetry processing pipelines need to export data for:

```
Use Cases:
1. Data archival and long-term storage
2. Offline analysis in Excel, MATLAB, Python
3. Sharing data with external systems
4. Debugging and validation of pipeline output
5. Creating datasets for machine learning

Example Pipeline:
Sensor → Processing → CSVSink → "output.csv"
                ↓
        (timestamped data)
```

Without CSV export:
```
Data → Processing → [No persistent storage]
```

With CSV export:
```
Data → Processing → CSVSink → Persistent CSV file
                           → Human-readable format
                           → Tool-compatible output
```

### Key Design Decisions

1. **Streaming Write**: Write data as it arrives, don't buffer entire dataset
2. **Simple Format**: Nanosecond timestamps with configurable precision
3. **Atomic File Operations**: Use temp file + rename for crash safety
4. **Batch-Optimized**: Write full batches efficiently
5. **Header Management**: Optional column headers with configurable names
6. **Error Handling**: Fail immediately and propagate useful errors

## B) Requirements

### Functional Requirements

#### 1. Input Support
- Accept any numeric data type (DTYPE_FLOAT, DTYPE_I32, DTYPE_U32)
- Support both regular (period_ns > 0) and irregular (period_ns = 0) data
- Handle partial batches correctly
- Process completion signals gracefully

#### 2. CSV Format Options (Initial Implementation)
```c
typedef enum {
    CSV_FORMAT_SIMPLE,      // timestamp,value
    CSV_FORMAT_MULTI_COL,   // timestamp,ch0,ch1,ch2...
} CSVFormat_e;
```

**Note**: Initial implementation will use nanosecond timestamps only. Other timestamp formats will be added as a future capability.

#### 3. File Management
- **Output Path**: Configurable file path
- **Overwrite Policy**: Append, overwrite, or error if exists
- **Max File Size**: Block when maximum size reached
- **Atomic Writes**: Write to temp file, rename on close

#### 4. Performance Requirements
- Write after each batch for data integrity
- Minimize syscalls with appropriate buffering
- Clear error propagation on write failures

### Non-Functional Requirements

1. **Reliability**
   - No data loss on normal shutdown
   - Recover from disk full errors
   - Handle filesystem errors gracefully

2. **Compatibility**
   - RFC 4180 compliant CSV output
   - Compatible with Excel, pandas, MATLAB
   - Proper escaping of special characters

3. **Performance**
   - Minimal impact on pipeline throughput
   - Efficient batch writing
   - Configurable I/O buffering

## C) Challenges/Considerations

### 1. Timestamp Format

**Decision**: Use nanosecond timestamps for initial implementation

```c
// Nanoseconds (precise, sortable)
1705315845123456789,42.5
1705315845123457789,42.6
```

**Future Capability**: Add support for other timestamp formats (ISO8601, relative seconds, etc.) in later versions.

### 2. Multi-Channel Data Handling

**Challenge**: How to represent vector data in CSV

**Decision**: Use wide format with configurable channel names

```csv
timestamp,channel_0,channel_1,channel_2,channel_3
1000000,1.0,2.0,3.0,4.0
2000000,1.1,2.1,3.1,4.1
```

Channel names can be configured via `column_names` array in configuration.

### 3. File Size Management

**Initial Implementation**: Support maximum file size with blocking behavior

```c
size_t max_file_size_bytes;  // Maximum file size (0 = unlimited)
// When limit reached: block and wait (no data loss)
```

**Future Capability**: Add file rotation support with time-based rotation, compression, and old file management.

### 4. Write Strategy

**Initial Implementation**: Write after each batch for data integrity

```c
// Simple approach: write and flush after processing each batch
// Ensures no data loss on unexpected shutdown
```

**Future Capability**: Add configurable buffering strategies for performance optimization.

### 5. Error Handling

**Decision**: Fail immediately and propagate useful errors

```c
// On any write error:
// 1. Stop the filter
// 2. Set worker_err_info with specific error code
// 3. Propagate error message with context
// Examples: "Disk full", "Permission denied", "Invalid path"
```

## D) Testing Strategy

### Unit Tests

```c
void test_basic_csv_write(void) {
    // Write known data, verify CSV format
    // Check proper escaping of commas, quotes
    // Verify nanosecond timestamps
}

void test_multi_column_output(void) {
    // Test multi-column format with custom names
    // Verify header generation
    // Check column alignment
}

void test_file_size_limit(void) {
    // Test max file size enforcement
    // Verify filter blocks when limit reached
    // Check error propagation
}

void test_error_handling(void) {
    // Simulate disk full
    // Test permission errors
    // Verify immediate failure and error messages
}

void test_completion_handling(void) {
    // Send completion signal
    // Verify file properly closed
    // Check all data flushed
}
```

### Integration Tests

```c
void test_with_signal_generator(void) {
    // SignalGenerator → CSVSink
    // Verify continuous data capture
    // Check timestamp accuracy
}

void test_high_throughput(void) {
    // Generate data at high rate
    // Verify no data loss
    // Monitor write performance
}

void test_pipeline_shutdown(void) {
    // Start pipeline, write data, stop
    // Verify graceful shutdown
    // Check file integrity
}
```

## E) Configuration

```c
typedef struct {
    const char* name;
    BatchBuffer_config buff_config;    // Input buffer config
    
    // File configuration
    const char* output_path;          // File path
    bool append;                      // Append vs overwrite
    mode_t file_mode;                 // Unix file permissions (0644)
    size_t max_file_size_bytes;       // Maximum file size (0 = unlimited)
    
    // CSV format
    CSVFormat_e format;               // Output format (SIMPLE or MULTI_COL)
    const char* delimiter;            // Field delimiter (default ",")
    const char* line_ending;          // Line ending (default "\n")
    bool write_header;                // Write column headers
    const char** column_names;        // Custom column names (optional)
    size_t n_columns;                 // Number of columns for MULTI_COL
    int precision;                    // Decimal places for floats
} CSVSink_config_t;
```

## F) Implementation Structure

### State
```c
typedef struct {
    Filter_t base;
    
    // Configuration (cached)
    CSVFormat_e format;
    char delimiter;
    const char* line_ending;
    int precision;
    size_t max_file_size_bytes;
    const char** column_names;
    size_t n_columns;
    
    // File management
    FILE* file;
    char* current_filename;
    size_t bytes_written;
    uint64_t lines_written;
    
    // State tracking
    uint64_t samples_written;
    uint64_t batches_processed;
} CSVSink_t;
```

### Core Algorithm
```c
void* csv_sink_worker(void* arg) {
    CSVSink_t* sink = (CSVSink_t*)arg;
    
    // Open output file
    WORKER_ASSERT(&sink->base, open_output_file(sink) == Bp_EC_OK,
                  Bp_EC_FILE_ERROR, "Failed to open output file");
    
    // Write header if configured
    if (sink->write_header) {
        write_csv_header(sink);
    }
    
    while (atomic_load(&sink->base.running)) {
        // Get input batch
        Batch_t* input = bb_get_tail(&sink->base.input_buffers[0],
                                     sink->base.timeout_us, &err);
        if (!input) {
            if (err == Bp_EC_TIMEOUT) continue;
            if (err == Bp_EC_STOPPED) break;
            break; // Real error
        }
        
        // Check for completion
        if (input->ec == Bp_EC_COMPLETE) {
            bb_del_tail(&sink->base.input_buffers[0]);
            break;
        }
        
        // Process batch data
        size_t samples = input->tail - input->head;
        for (size_t i = 0; i < samples; i++) {
            // Calculate timestamp for this sample
            uint64_t sample_time_ns = input->t_ns + i * input->period_ns;
            
            // Format and write the CSV line
            format_csv_line(sink, sample_time_ns, 
                          &input->data[input->head + i]);
        }
        
        // Update metrics
        sink->samples_written += samples;
        sink->batches_processed++;
        sink->base.metrics.samples_processed += samples;
        sink->base.metrics.n_batches++;
        
        // Release input batch
        bb_del_tail(&sink->base.input_buffers[0]);
        
        // Check file size limit
        if (sink->max_file_size_bytes > 0 && 
            sink->bytes_written >= sink->max_file_size_bytes) {
            // Block until space available or filter stopped
            WORKER_ASSERT(&sink->base, false, Bp_EC_FILE_FULL,
                          "Output file reached maximum size limit");
        }
    }
    
    // Close output file
    close_output_file(sink);
    
    return NULL;
}

static void format_csv_line(CSVSink_t* sink, uint64_t t_ns, void* data) {
    char line[MAX_LINE_LENGTH];
    size_t len = 0;
    
    // Format timestamp (nanoseconds)
    len += snprintf(line + len, sizeof(line) - len, 
                  "%llu", (unsigned long long)t_ns);
    
    // Add delimiter
    line[len++] = sink->delimiter;
    
    // Format data value(s)
    switch (sink->base.input_buffers[0].dtype) {
        case DTYPE_FLOAT:
            len += snprintf(line + len, sizeof(line) - len,
                          "%.*f", sink->precision, *(float*)data);
            break;
        case DTYPE_I32:
            len += snprintf(line + len, sizeof(line) - len,
                          "%d", *(int32_t*)data);
            break;
        // ... other types
    }
    
    // Add line ending
    strcpy(line + len, sink->line_ending);
    len += strlen(sink->line_ending);
    
    // Write the line directly
    fwrite(line, 1, len, sink->file);
    sink->bytes_written += len;
    sink->lines_written++;
}
```

## G) Usage Examples

### Basic Data Logging
```c
// Log sensor data to CSV
CSVSink_config_t config = {
    .name = "sensor_log",
    .output_path = "sensor_data.csv",
    .format = CSV_FORMAT_SIMPLE,
    .write_header = true,
    .precision = 6,
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 10,  // 1024 samples
        .ring_capacity_expo = 6     // 64 batches
    }
};

Sensor → Processing → CSVSink("sensor_data.csv")
```

### Multi-Channel Logging
```c
// Log multi-channel data with custom names
const char* channels[] = {"temperature", "pressure", "humidity"};
config.format = CSV_FORMAT_MULTI_COL;
config.column_names = channels;
config.n_columns = 3;
config.write_header = true;

MultiChannelSource → CSVSink("multi_channel.csv")
```

### Size-Limited Logging
```c
// Stop when file reaches size limit
config.max_file_size_bytes = 100 * 1024 * 1024;  // 100MB
config.append = false;  // Start fresh

ContinuousSource → CSVSink("limited.csv")
// Filter will block and error when limit reached
```

### Debug Pipeline Output
```c
// Detailed debugging with high precision
config.format = CSV_FORMAT_SIMPLE;
config.precision = 9;  // Full float precision
config.write_header = true;

Pipeline → [NormalOutput, CSVSink("debug.csv")]
```

## H) Open Questions/Decision Points

### 1. Multi-Channel Data Layout
- **Current**: Single column of values
- **Alternative**: Support multi-column for vector data
- **Decision**: Start with single column, add multi-column in v2

### 2. Compression Support
- **Option A**: Compress completed files during rotation
- **Option B**: Streaming compression (gzip writer)
- **Option C**: No compression, rely on filesystem
- **Recommendation**: Option A for simplicity

### 3. Metadata Storage
- **Question**: Should we write batch metadata (period_ns, etc)?
- **Options**: Separate metadata file, CSV comments, ignore
- **Recommendation**: Optional CSV header comments

### 4. Binary CSV Alternative
- **Question**: Support binary formats (HDF5, Parquet)?
- **Answer**: No, keep filter focused on CSV only
- **Rationale**: Other formats deserve dedicated filters

## Success Metrics

1. **Correctness**: All data written, proper CSV escaping
2. **Performance**: < 5% overhead vs raw file write
3. **Reliability**: No data loss on shutdown
4. **Compatibility**: Output readable by standard tools
5. **Usability**: Simple config for common cases

## Future Capabilities

The following features are planned for future versions:

1. **Timestamp Formats**
   - ISO8601 timestamps for human readability
   - Relative timestamps from start
   - Configurable precision (ms, us, ns)

2. **File Rotation**
   - Size-based rotation with automatic file naming
   - Time-based rotation (hourly, daily)
   - Compression of rotated files
   - Retention policies

3. **Buffering Strategies**
   - Line-based buffering for debugging
   - Size-based buffering for performance
   - Time-based flushing

4. **Advanced Error Handling**
   - Retry with backoff
   - Alternative file on error
   - Partial write recovery

5. **Format Extensions**
   - Long format for multi-channel data
   - Metadata headers with comments
   - Variable substitution in filenames

## Implementation Notes

- Use buffered I/O (fwrite) for efficiency
- Write after each batch for data integrity
- Implement proper CSV escaping (quotes, commas)
- Test with various locales (decimal separators)
- Document performance characteristics clearly
- Clear error messages for common failures (disk full, permissions)
