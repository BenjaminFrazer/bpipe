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
2. **Configurable Format**: Support different timestamp formats, delimiters
3. **Atomic File Operations**: Use temp file + rename for crash safety
4. **Batch-Optimized**: Write full batches efficiently
5. **Header Management**: Optional column headers with metadata
6. **File Rotation**: Support size/time-based file rotation

## B) Requirements

### Functional Requirements

#### 1. Input Support
- Accept any numeric data type (DTYPE_FLOAT, DTYPE_I32, DTYPE_U32)
- Support both regular (period_ns > 0) and irregular (period_ns = 0) data
- Handle partial batches correctly
- Process completion signals gracefully

#### 2. CSV Format Options
```c
typedef enum {
    CSV_FORMAT_SIMPLE,      // timestamp,value
    CSV_FORMAT_INDEXED,     // index,timestamp,value
    CSV_FORMAT_MULTI_COL,   // timestamp,ch0,ch1,ch2...
    CSV_FORMAT_RAW         // value only (no timestamp)
} CSVFormat_e;

typedef enum {
    TIMESTAMP_NS,          // Nanoseconds since epoch
    TIMESTAMP_US,          // Microseconds since epoch
    TIMESTAMP_MS,          // Milliseconds since epoch
    TIMESTAMP_S,           // Seconds since epoch
    TIMESTAMP_ISO8601,     // 2024-01-15T10:30:45.123Z
    TIMESTAMP_RELATIVE     // Seconds from start
} TimestampFormat_e;
```

#### 3. File Management
- **Output Path**: Configurable file path with variable substitution
- **Overwrite Policy**: Append, overwrite, or error if exists
- **File Rotation**: By size (MB) or time interval
- **Atomic Writes**: Write to temp file, rename on close

#### 4. Performance Requirements
- Minimize syscalls by buffering writes
- Support configurable write buffer size
- Flush on batch boundaries for data integrity
- Option to sync to disk periodically

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

### 1. Timestamp Precision vs Readability

**Challenge**: Nanosecond timestamps are precise but not human-readable

**Solution**: Configurable timestamp formats
```c
// Nanoseconds (precise, sortable)
1705315845123456789,42.5

// ISO 8601 (readable, standard)
2024-01-15T10:30:45.123456789Z,42.5

// Relative seconds (analysis-friendly)
0.000000,42.5
0.001000,42.6
```

### 2. Multi-Channel Data Handling

**Challenge**: How to represent vector data in CSV

**Option A: Wide Format (Recommended)**
```csv
timestamp,ch0,ch1,ch2,ch3
1000000,1.0,2.0,3.0,4.0
2000000,1.1,2.1,3.1,4.1
```

**Option B: Long Format**
```csv
timestamp,channel,value
1000000,0,1.0
1000000,1,2.0
1000000,2,3.0
```

**Decision**: Support wide format by default, long format optional

### 3. Large File Handling

**Challenge**: CSV files can grow very large

**Solution**: File rotation
```c
typedef struct {
    size_t max_file_size_mb;      // Rotate when size exceeded
    uint64_t rotation_interval_s;  // Rotate after time interval
    size_t max_files;             // Keep N most recent files
    bool compress_on_rotate;      // Gzip completed files
} FileRotation_config_t;
```

### 4. Buffering Strategy

**Challenge**: Balance between write performance and data integrity

**Solution**: Configurable buffering
```c
typedef enum {
    BUFFER_LINE,      // Flush after each line (safest)
    BUFFER_BATCH,     // Flush after each batch (default)
    BUFFER_SIZE,      // Flush when buffer full
    BUFFER_TIME       // Flush on time interval
} BufferStrategy_e;
```

### 5. Error Recovery

**Challenge**: How to handle write errors (disk full, permissions)

**Solution**: Error handling modes
```c
typedef enum {
    ON_ERROR_STOP,      // Stop filter and propagate error
    ON_ERROR_DROP,      // Drop data and continue
    ON_ERROR_RETRY,     // Retry with exponential backoff
    ON_ERROR_ROTATE     // Try rotating to new file
} ErrorMode_e;
```

## D) Testing Strategy

### Unit Tests

```c
void test_basic_csv_write(void) {
    // Write known data, verify CSV format
    // Check proper escaping of commas, quotes
}

void test_timestamp_formats(void) {
    // Test each timestamp format
    // Verify precision and readability
}

void test_file_rotation(void) {
    // Test size-based rotation
    // Test time-based rotation
    // Verify no data loss during rotation
}

void test_error_handling(void) {
    // Simulate disk full
    // Test permission errors
    // Verify error modes work correctly
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
    const char* output_path;          // File path (supports %d, %t variables)
    bool append;                      // Append vs overwrite
    mode_t file_mode;                 // Unix file permissions (0644)
    
    // CSV format
    CSVFormat_e format;               // Output format
    TimestampFormat_e timestamp_fmt;  // Timestamp format
    const char* delimiter;            // Field delimiter (default ",")
    const char* line_ending;          // Line ending (default "\n")
    bool write_header;                // Write column headers
    const char** column_names;        // Custom column names (optional)
    int precision;                    // Decimal places for floats
    
    // Buffering
    BufferStrategy_e buffer_strategy; // When to flush
    size_t buffer_size_kb;           // Write buffer size
    uint64_t flush_interval_ms;      // Time-based flush
    
    // File rotation
    FileRotation_config_t rotation;   // Rotation settings
    
    // Error handling
    ErrorMode_e error_mode;          // How to handle write errors
    size_t max_retries;              // For ON_ERROR_RETRY mode
    
    // Performance
    bool use_direct_io;              // O_DIRECT flag
    bool sync_on_flush;              // fsync() on flush
} CSVSink_config_t;
```

## F) Implementation Structure

### State
```c
typedef struct {
    Filter_t base;
    
    // Configuration (cached)
    CSVFormat_e format;
    TimestampFormat_e timestamp_fmt;
    char delimiter;
    const char* line_ending;
    int precision;
    BufferStrategy_e buffer_strategy;
    ErrorMode_e error_mode;
    
    // File management
    FILE* file;
    char* current_filename;
    size_t bytes_written;
    uint64_t lines_written;
    time_t file_opened_time;
    
    // Write buffering
    char* write_buffer;
    size_t buffer_size;
    size_t buffer_used;
    uint64_t last_flush_ns;
    
    // File rotation
    FileRotation_config_t rotation;
    size_t rotation_index;
    
    // State tracking
    uint64_t start_time_ns;      // For relative timestamps
    uint64_t samples_written;
    uint64_t batches_processed;
    size_t write_errors;
    
    // Error handling
    size_t consecutive_errors;
    uint64_t last_error_ns;
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
    
    // Record start time for relative timestamps
    sink->start_time_ns = get_current_time_ns();
    
    while (atomic_load(&sink->base.running)) {
        // Get input batch
        Batch_t* input = bb_get_tail(&sink->base.input_buffers[0],
                                     sink->base.timeout_us, &err);
        if (!input) {
            if (err == Bp_EC_TIMEOUT) {
                // Check if time-based flush needed
                maybe_flush_buffer(sink);
                continue;
            }
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
            
            // Format and buffer the CSV line
            format_csv_line(sink, sample_time_ns, 
                          &input->data[input->head + i]);
            
            // Check if buffer flush needed
            if (should_flush_buffer(sink)) {
                flush_buffer(sink);
            }
        }
        
        // Update metrics
        sink->samples_written += samples;
        sink->batches_processed++;
        sink->base.metrics.samples_processed += samples;
        sink->base.metrics.n_batches++;
        
        // Release input batch
        bb_del_tail(&sink->base.input_buffers[0]);
        
        // Check file rotation
        if (should_rotate_file(sink)) {
            rotate_output_file(sink);
        }
    }
    
    // Final flush and close
    flush_buffer(sink);
    close_output_file(sink);
    
    return NULL;
}

static void format_csv_line(CSVSink_t* sink, uint64_t t_ns, void* data) {
    char line[MAX_LINE_LENGTH];
    size_t len = 0;
    
    // Format timestamp
    switch (sink->timestamp_fmt) {
        case TIMESTAMP_NS:
            len += snprintf(line + len, sizeof(line) - len, 
                          "%llu", (unsigned long long)t_ns);
            break;
        case TIMESTAMP_ISO8601:
            len += format_iso8601(line + len, sizeof(line) - len, t_ns);
            break;
        case TIMESTAMP_RELATIVE:
            double rel_s = (t_ns - sink->start_time_ns) * 1e-9;
            len += snprintf(line + len, sizeof(line) - len,
                          "%.*f", sink->precision, rel_s);
            break;
        // ... other formats
    }
    
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
    
    // Buffer the line
    buffer_write(sink, line, len);
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
    .timestamp_fmt = TIMESTAMP_ISO8601,
    .write_header = true,
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 10,  // 1024 samples
        .ring_capacity_expo = 6     // 64 batches
    }
};

Sensor → Processing → CSVSink("sensor_data.csv")
```

### High-Performance Logging
```c
// Optimized for throughput
config.buffer_strategy = BUFFER_SIZE;
config.buffer_size_kb = 1024;  // 1MB buffer
config.use_direct_io = true;
config.sync_on_flush = false;

HighRateSource → CSVSink("high_rate.csv")
```

### Rotating Log Files
```c
// Daily rotation with compression
config.output_path = "telemetry_%Y%m%d_%H%M%S.csv";
config.rotation = {
    .rotation_interval_s = 86400,  // 24 hours
    .max_files = 7,                // Keep 1 week
    .compress_on_rotate = true     // Gzip old files
};

ContinuousSource → CSVSink(rotating)
```

### Debug Pipeline Output
```c
// Detailed debugging with all metadata
config.format = CSV_FORMAT_INDEXED;
config.timestamp_fmt = TIMESTAMP_NS;
config.precision = 9;  // Full float precision
config.buffer_strategy = BUFFER_LINE;  // Immediate write

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
3. **Reliability**: No data loss on shutdown or rotation
4. **Compatibility**: Output readable by standard tools
5. **Usability**: Simple config for common cases

## Implementation Notes

- Use buffered I/O (fwrite) for efficiency
- Pre-allocate format strings to avoid repeated parsing
- Consider memory mapping for very high throughput
- Implement proper CSV escaping (quotes, commas)
- Test with various locales (decimal separators)
- Handle timezone correctly for ISO8601 format
- Document performance characteristics clearly