# CSV Source Filter Specification

## Overview

The CSV Source Filter is a source filter that reads CSV files and emits data samples to connected sink filters. It expects CSV files to contain a timestamp column (in nanoseconds) and converts rows into batches according to the bpipe data model, automatically detecting whether the data is regularly or irregularly sampled.

## Design Goals

1. **File-based input**: Read CSV files from disk specified in configuration
2. **Flexible parsing**: Support configurable column selection and data types
3. **Timing control**: Generate appropriate timing metadata for samples
4. **Memory efficient**: Stream data without loading entire file
5. **Error handling**: Graceful handling of malformed data

## Constants

```c
#define BP_CSV_MAX_COLUMNS 64  // Maximum number of data columns supported
```

## Filter Configuration

```c
typedef struct _CsvSource_config_t {
    const char* name;                    // Filter name
    const char* file_path;               // Path to CSV file
    
    // CSV parsing options
    char delimiter;                      // Column delimiter (default: ',')
    bool has_header;                     // First row contains column names (required for name matching)
    const char* ts_column_name;          // Name of timestamp column (e.g., "ts_ns", "timestamp")
    const char* data_column_names[BP_CSV_MAX_COLUMNS]; // NULL-terminated array of column names
    
    // Timing configuration
    bool detect_regular_timing;          // Auto-detect if data is regularly sampled
    uint64_t regular_threshold_ns;       // Max deviation to consider "regular" (default: 1000ns)
    
    // Output configuration
    Bp_dtype_t output_dtype;            // Output data type (DTYPE_FLOAT, DTYPE_I32, etc.)
    size_t batch_size;                  // Max samples per batch (power of 2)
    size_t ring_capacity;               // Number of batches in ring buffer
    
    // Processing options
    bool loop;                          // Loop file when EOF reached
    bool skip_invalid;                  // Skip rows that fail parsing
    long timeout_us;                    // Output timeout (0 = infinite)
} CsvSource_config_t;
```

## Filter Structure

```c
typedef struct _CsvSource_t {
    Filter_t base;                      // MUST be first member
    
    // File handling
    FILE* file;                         // Open file handle
    char* line_buffer;                  // Buffer for reading lines
    size_t line_buffer_size;            // Size of line buffer
    
    // Column mapping
    int ts_column_index;                // Resolved index of timestamp column
    int data_column_indices[BP_CSV_MAX_COLUMNS]; // Resolved indices of data columns
    size_t n_data_columns;              // Number of data columns (computed from NULL terminator)
    char** header_names;                // Parsed header column names
    size_t n_header_columns;            // Total columns in header
    
    // Parsing state
    double parse_buffer[BP_CSV_MAX_COLUMNS]; // Fixed buffer for parsing
    size_t current_line;                // Current line number (for errors)
    
    // Timing detection
    bool is_regular;                    // Whether data has regular timing
    uint64_t detected_period_ns;        // Detected period if regular
    uint64_t last_timestamp_ns;         // Previous row's timestamp
    
    // Batch accumulation
    uint64_t* timestamp_buffer;         // Buffer for accumulating timestamps
    size_t timestamps_in_buffer;        // Current number of timestamps
    
    // Configuration (copied)
    char delimiter;
    bool has_header;
    const char* ts_column_name;
    const char* data_column_names[BP_CSV_MAX_COLUMNS];
    bool detect_regular_timing;
    uint64_t regular_threshold_ns;
    bool loop;
    bool skip_invalid;
    
} CsvSource_t;
```

## Processing Logic

### Initialization
1. Open CSV file for reading
2. If `has_header`:
   - Read first line and parse column names
   - Map column names to indices:
     - Find `ts_column_name` → `ts_column_index`
     - Find each `data_column_names[i]` → `data_column_indices[i]`
   - Error if any requested column not found
3. Allocate line buffer, parse buffer, and timestamp buffer
4. Initialize timing detection state
5. Create base filter with no inputs, configurable outputs

### Worker Thread Logic

```
while (running):
    1. Check for available output buffer space
    2. Read and accumulate samples:
        a. Parse line, extract timestamp from ts_column
        b. If detect_regular_timing, check timing consistency
        c. Accumulate data until:
           - Batch full (batch_size reached)
           - Irregular data detected (for regular batches)
           - Timing gap detected (for regular batches)
    3. Create output batch:
        - If regular timing: set period_ns, use first timestamp
        - If irregular timing: create single-sample batches
    4. Submit batch
    5. Handle EOF:
        - If loop=true: seek to beginning, skip header
        - If loop=false: send completion batch, exit
    6. Update metrics
```

### CSV Parsing

- Split line by delimiter
- Extract timestamp from ts_column_index (parse as uint64_t)
- Extract data from data_column_indices
- Parse data as doubles, then convert to output_dtype
- Handle parsing errors based on skip_invalid flag

### Column Name Resolution

During initialization with `has_header = true`:
```c
// Parse header line
char* header_copy = strdup(header_line);
char* token = strtok(header_copy, &delimiter);
int col_idx = 0;

while (token != NULL) {
    header_names[col_idx] = strdup(token);
    
    // Check if this is the timestamp column
    if (strcmp(token, ts_column_name) == 0) {
        ts_column_index = col_idx;
    }
    
    // Check if this is a data column
    for (int i = 0; data_column_names[i] != NULL && i < BP_CSV_MAX_COLUMNS; i++) {
        if (strcmp(token, data_column_names[i]) == 0) {
            data_column_indices[i] = col_idx;
        }
    }
    
    token = strtok(NULL, &delimiter);
    col_idx++;
}
```

### Timing Detection and Batch Formation

The filter implements smart batching based on timing patterns:

#### Regular Timing Mode
If `detect_regular_timing = true`:
1. Calculate period between first two samples
2. For each subsequent sample, check if: `abs(expected_ts - actual_ts) < regular_threshold_ns`
3. If regular:
   - Accumulate multiple samples per batch
   - Set `batch->period_ns = detected_period`
   - Set `batch->t_ns` = first sample timestamp
4. If timing deviation detected:
   - Submit current batch
   - Start new batch with new timing

#### Irregular Timing Mode
If timing is irregular or detection disabled:
- Create single-sample batches
- Set `batch->period_ns = 0`
- Set `batch->t_ns` = sample timestamp
- Preserves exact timing for each sample

### Handling Timing Gaps

For regular data with gaps:
```
Example: 1kHz data with gap
Timestamps: 1000, 2000, 3000, [gap], 8000, 9000
→ Batch 1: t_ns=1000, period_ns=1000, samples=3
→ Batch 2: t_ns=8000, period_ns=1000, samples=2
```

## Error Handling

### File Errors
- File not found → Bp_EC_FILE_NOT_FOUND
- Read error → Bp_EC_IO_ERROR
- Permissions → Bp_EC_PERMISSION_DENIED

### Parse Errors
- Invalid number format → if skip_invalid, skip row; else Bp_EC_PARSE_ERROR
- Wrong column count → if skip_invalid, skip row; else Bp_EC_FORMAT_ERROR
- Include line number in error message

### Configuration Errors
- Invalid file_path → Bp_EC_INVALID_CONFIG
- batch_size not power of 2 → Bp_EC_INVALID_CONFIG
- has_header = false with column names → Bp_EC_INVALID_CONFIG
- Column name not found in header → Bp_EC_COLUMN_NOT_FOUND

## Example Usage

### Basic CSV Reading with Column Names
```c
// CSV header: ts_ns,sensor1,sensor2,sensor3
CsvSource_config_t config = {
    .name = "sensor_data",
    .file_path = "/data/sensors.csv",
    .delimiter = ',',
    .has_header = true,
    .ts_column_name = "ts_ns",      // Match timestamp column by name
    .data_column_names = {           // NULL-terminated array
        "sensor1", 
        "sensor2", 
        "sensor3",
        NULL                         // Marks end of array
    },
    .detect_regular_timing = true,   // Auto-detect if regular
    .regular_threshold_ns = 1000,    // 1μs tolerance
    .output_dtype = DTYPE_FLOAT,
    .batch_size = 64,
    .ring_capacity = 256,
    .timeout_us = 1000000           // 1s timeout
};

CsvSource_t source;
csvsource_init(&source, config);
filt_sink_connect(&source.base, 0, &downstream.input_buffers[0]);
filt_start(&source.base);
```

### Irregular Event Data
```c
// CSV header: timestamp,event_value,event_type
CsvSource_config_t config = {
    .name = "events",
    .file_path = "/data/events.csv",
    .detect_regular_timing = false,  // Force single-sample batches
    .ts_column_name = "timestamp",
    .data_column_names = {
        "event_value", 
        "event_type",
        NULL                         // End of array
    },
    // ... other fields ...
};
```

### High-Rate Regular Data
```c
// 10kHz sensor data with strict timing
config.detect_regular_timing = true;
config.regular_threshold_ns = 100;  // 100ns tolerance (strict)
config.batch_size = 1024;           // Large batches for efficiency
```

## Performance Considerations

1. **Buffering**: Use reasonably sized line buffer (e.g., 4KB)
2. **Batch size**: Larger batches amortize parsing overhead
3. **Memory allocation**: Reuse buffers, avoid per-row allocations
4. **Type conversion**: Parse as double, then convert once
5. **I/O pattern**: Sequential reads, consider mmap for large files

## Limitations

1. **Memory**: Entire row must fit in line buffer
2. **Data types**: Timestamp must be uint64_t nanoseconds, data columns numeric only
3. **Format**: Simple CSV only (no quoted fields, escapes)
4. **Timing**: Requires monotonic timestamps (no time travel)

## Future Extensions

1. **Timestamp formats**: Support different timestamp formats (ISO8601, Unix epoch, etc.)
2. **String support**: Handle non-numeric data columns
3. **Compression**: Support gzipped CSV files
4. **Multiple files**: Read directory of CSV files in time order
5. **Schema validation**: Validate column types/names from header
6. **Parallel parsing**: Multi-threaded for large files
7. **Mixed column referencing**: Support both names and indices in same config

## Testing Strategy

1. **Unit tests**:
   - Parse various CSV formats
   - Handle malformed data
   - Verify timing generation
   - Test EOF behavior (loop vs stop)

2. **Integration tests**:
   - Connect to downstream filters
   - Verify data flow and timing
   - Test backpressure handling

3. **Performance tests**:
   - Measure throughput vs file size
   - Profile CPU usage
   - Memory usage validation

4. **Edge cases**:
   - Empty file
   - Single row file
   - Very wide CSV (many columns)
   - Very long lines
   - Non-existent file
   - Permission denied

## Implementation Status

### Current Implementation
The CSV source filter has been implemented in `trees/csv-source-feature/` with the following status:

#### ✅ Completed Features
- Full initialization with configuration validation
- Header parsing and column name resolution
- Line parsing with type conversion
- Worker thread with regular/irregular timing detection
- Smart batching based on timing patterns
- EOF handling with loop mode support
- Skip invalid rows functionality
- Multi-channel CSV data support
- Comprehensive test suite with real CSV files

#### ⚠️ Implementation Differences
1. **Data Type**: Uses `SampleDtype_t` instead of `Bp_dtype_t` (minor naming difference)
2. **Error Codes**: Uses generic `Bp_EC_INVALID_CONFIG` instead of specific codes like `Bp_EC_FILE_NOT_FOUND`
3. **Data Accumulation Bug**: The implementation has a bug in the worker thread where it incorrectly reuses `parse_buffer` for accumulating multiple samples. The code needs a separate accumulation buffer:
   ```c
   // Current (incorrect) - line 343:
   self->parse_buffer[self->timestamps_in_buffer * self->n_data_columns + ch] = value_buffer[ch];
   
   // Should have a separate accumulation buffer
   ```

#### 📝 Recommended Fixes
1. **Add data accumulation buffer** to the structure:
   ```c
   double* data_accumulation_buffer;  // Size: batch_size * n_data_columns
   ```
2. **Update error codes** to use specific codes from the specification
3. **Add line numbers** to error messages for better debugging
4. **Add validation** for maximum line length to prevent buffer overflow

### Test Coverage
The implementation includes comprehensive tests covering:
- Configuration validation
- Header parsing and column resolution  
- Regular and irregular data processing
- Timing gap detection
- Loop mode functionality
- Invalid row handling
- Multi-channel data support

Test data files are provided in `tests/data/` including regular, irregular, gapped, invalid, and multi-channel CSV files.