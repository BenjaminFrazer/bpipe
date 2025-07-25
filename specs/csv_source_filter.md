# CSV Source Filter Specification

## Overview

The CSV Source Filter is a source filter that reads CSV files and emits data samples to connected sink filters. It expects CSV files to contain a timestamp column (in nanoseconds) and outputs each data column to a separate sink, enabling clean composition with downstream filters. The filter uses a simple state machine to automatically detect whether data is regularly or irregularly sampled.

## Design Goals

1. **File-based input**: Read CSV files from disk specified in configuration
2. **Multi-output design**: Each data column outputs to a separate sink for composability
3. **Simple timing logic**: State machine automatically handles regular/irregular data
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
    
    // Timing configuration (simplified - no threshold needed)
    
    // Output configuration
    BatchBuffer_config buff_config;     // Standard buffer configuration (includes dtype, capacities)
    
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
    
    // State machine for batching
    uint64_t batch_start_time;          // First timestamp in current batch
    uint64_t expected_delta;            // Time delta established by first two samples
    bool delta_established;             // Whether we have a delta from two samples
    size_t samples_in_batch;            // Current batch size
    
    // Batch accumulation buffers (one per column)
    double** column_buffers;            // Array of buffers, one per data column
    uint64_t* timestamp_buffer;         // Buffer for accumulating timestamps
    
    // Configuration (copied)
    char delimiter;
    bool has_header;
    const char* ts_column_name;
    const char* data_column_names[BP_CSV_MAX_COLUMNS];
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
3. Allocate buffers:
   - Line buffer for reading
   - Parse buffer for parsing
   - Timestamp buffer (size = 1 << buff_config.batch_capacity_expo)
   - Column buffers array (n_data_columns buffers, each size = 1 << buff_config.batch_capacity_expo)
4. Initialize state machine (all zeros)
5. Create base filter with:
   - No inputs (source filter)
   - Number of outputs = n_data_columns (one sink per column)

### Worker Thread Logic

```
BatchState state = {0};

while (running):
    1. Read next line from CSV
    2. Parse timestamp and data values
    3. Apply state machine logic:
       
       if (state.samples_in_batch == 0):
           // Starting new batch
           state.batch_start_time = timestamp
           state.delta_established = false
           add_to_batch(timestamp, values)
           state.samples_in_batch = 1
           
       else if (state.samples_in_batch == 1):
           // Second sample establishes delta
           state.expected_delta = timestamp - state.batch_start_time
           state.delta_established = true
           add_to_batch(timestamp, values)
           state.samples_in_batch = 2
           
       else:
           // Check if sample fits pattern
           expected_time = state.batch_start_time + 
                          (state.samples_in_batch * state.expected_delta)
           
           if (timestamp == expected_time):
               // Fits pattern - add to batch
               add_to_batch(timestamp, values)
               state.samples_in_batch++
           else:
               // Doesn't fit - submit current batch to all outputs
               submit_batches_to_all_columns(state.expected_delta)
               
               // Start new batch
               state.batch_start_time = timestamp
               state.delta_established = false
               add_to_batch(timestamp, values)
               state.samples_in_batch = 1
    
    4. Check if batch full:
       batch_capacity = 1 << buff_config.batch_capacity_expo
       if (state.samples_in_batch >= batch_capacity):
           submit_batches_to_all_columns(state.delta_established ? state.expected_delta : 0)
           state.samples_in_batch = 0
           
    5. Handle EOF:
       - Submit any remaining samples in batch
       - If loop=true: seek to beginning, skip header
       - If loop=false: send completion batch, exit
       
    6. Update metrics

// Helper function: submit_batches_to_all_columns
submit_batches_to_all_columns(period_ns):
    for (i = 0; i < n_data_columns; i++):
        batch = bb_get_head(sinks[i])
        batch->t_ns = timestamp_buffer[0]
        batch->period_ns = period_ns
        batch->head = 0
        batch->tail = samples_in_batch
        
        // Copy column data to batch
        for (j = 0; j < samples_in_batch; j++):
            ((float*)batch->data)[j] = column_buffers[i][j]
        
        bb_submit(sinks[i], timeout_us)
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

The filter uses a simple state machine for batching that naturally handles both regular and irregular data:

#### State Machine Design
The filter maintains minimal state:
```c
typedef struct {
    uint64_t batch_start_time;    // First timestamp in current batch
    uint64_t expected_delta;      // Time delta established by first two samples
    bool delta_established;       // Whether we have a delta from two samples
    size_t samples_in_batch;      // Current batch size
} BatchState;
```

#### Batching Algorithm
1. **First sample**: Start new batch, record start time
2. **Second sample**: Calculate delta (`timestamp - batch_start_time`), establish pattern
3. **Subsequent samples**: 
   - If `timestamp == batch_start_time + (n * expected_delta)`: Add to batch
   - Otherwise: Submit current batch, start new batch with this sample

#### Behavioral Model
```
Regular data (1kHz):
  t=0, t=1000, t=2000, t=3000
  → Single batch: period_ns=1000, 4 samples

Irregular data:
  t=0, t=1001, t=2003, t=3000
  → Four batches: each with period_ns=0, 1 sample each

Mixed data:
  t=0, t=1000, t=2000, [gap], t=5000, t=6000
  → Batch 1: period_ns=1000, 3 samples
  → Batch 2: period_ns=1000, 2 samples
```

#### Design Rationale
This approach embraces bpipe2's fundamental design:
- **Regular data is efficient**: Multi-sample batches with period_ns set
- **Irregular data is correct**: Single-sample batches preserve exact timing
- **Simple and predictable**: No complex heuristics or threshold tuning
- **Natural batch boundaries**: Timing changes automatically create new batches

The implementation accepts that irregular data will have higher overhead (more batches), which aligns with bpipe2's optimization for regular, high-rate telemetry data. This is a deliberate trade-off for simplicity and correctness.

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
- has_header = false with column names → Bp_EC_INVALID_CONFIG
- Column name not found in header → Bp_EC_COLUMN_NOT_FOUND
- Invalid buffer configuration → Bp_EC_INVALID_CONFIG

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
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 6,    // 64 samples per batch
        .ring_capacity_expo = 8,     // 256 batches in ring
        .overflow_behaviour = OVERFLOW_BLOCK
    },
    .timeout_us = 1000000           // 1s timeout
};

CsvSource_t source;
csvsource_init(&source, config);

// Connect each column to its own downstream filter
filt_sink_connect(&source.base, 0, &sensor1_filter.input_buffers[0]);  // sensor1 data
filt_sink_connect(&source.base, 1, &sensor2_filter.input_buffers[0]);  // sensor2 data
filt_sink_connect(&source.base, 2, &sensor3_filter.input_buffers[0]);  // sensor3 data

filt_start(&source.base);
```

### Combining Multiple Columns
```c
// If you need all columns together, use a MISO filter
CsvSource_t source;
MisoElementwise_t combiner;  // Combines multiple inputs

// Connect CSV columns to combiner
filt_sink_connect(&source.base, 0, &combiner.input_buffers[0]);
filt_sink_connect(&source.base, 1, &combiner.input_buffers[1]);
filt_sink_connect(&source.base, 2, &combiner.input_buffers[2]);

// Combiner output has all columns interleaved
filt_sink_connect(&combiner.base, 0, &downstream.input_buffers[0]);
```

### High-Rate Regular Data
```c
// 10kHz sensor data - state machine will automatically batch efficiently
config.buff_config.batch_capacity_expo = 10;  // 1024 samples per batch
// Regular timing is detected automatically - no configuration needed
```

## Performance Considerations

1. **Multi-output efficiency**: Each column gets its own buffer, reducing copying
2. **Batch size trade-offs**: 
   - Regular data: Larger batches (256-1024) for efficiency
   - Irregular data: Will create single-sample batches regardless
3. **Memory allocation**: All buffers pre-allocated, no per-row allocations
4. **Type conversion**: Parse as double, convert once when copying to output
5. **Simple state machine**: Minimal overhead, predictable behavior

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
